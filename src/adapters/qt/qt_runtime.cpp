#include "qt_runtime.hpp"

#include "qt_surface_host.hpp"

#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/theme/theme.hpp"

#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <QtGui/QWindow>

namespace spectra::adapters::qt
{

QtRuntime::QtRuntime()  = default;
QtRuntime::~QtRuntime() { shutdown(); }

QtRuntime::WindowState* QtRuntime::find_window_state(QWindow* window)
{
    if (!window)
    {
        return nullptr;
    }
    auto it = window_states_.find(window);
    return it != window_states_.end() ? it->second.get() : nullptr;
}

const QtRuntime::WindowState* QtRuntime::find_window_state(QWindow* window) const
{
    if (!window)
    {
        return nullptr;
    }
    auto it = window_states_.find(window);
    return it != window_states_.end() ? it->second.get() : nullptr;
}

bool QtRuntime::adopt_vulkan_instance(VkInstance instance)
{
    if (instance == VK_NULL_HANDLE)
    {
        return false;
    }

    if (!vulkan_instance_)
    {
        vulkan_instance_ = std::make_unique<QVulkanInstance>();
    }

    if (vulkan_instance_->vkInstance() == VK_NULL_HANDLE)
    {
        vulkan_instance_->setVkInstance(instance);
    }

    return vulkan_instance_->vkInstance() == instance;
}

QVulkanInstance* QtRuntime::vulkan_instance() const
{
    return vulkan_instance_.get();
}

bool QtRuntime::init()
{
    if (initialized_)
    {
        return true;
    }

    // Create Qt surface host for Vulkan extension/surface queries
    surface_host_ = std::make_unique<QtSurfaceHost>();

    // Create Vulkan backend (non-headless — we have a real surface)
    backend_ = std::make_unique<VulkanBackend>();
    backend_->set_surface_host(surface_host_.get());

    if (!backend_->init(false /*headless*/))
    {
        SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize Vulkan backend");
        return false;
    }

    // Adopt the VkInstance into a QVulkanInstance so Qt can use it
    if (!vulkan_instance_)
    {
        vulkan_instance_ = std::make_unique<QVulkanInstance>();
    }
    if (vulkan_instance_->vkInstance() == VK_NULL_HANDLE)
    {
        vulkan_instance_->setVkInstance(backend_->instance());
    }

    // create() initializes the Qt-side Vulkan wrapper (XCB/Wayland bridge).
    // Must be called after setVkInstance() and before surfaceForWindow().
    if (!vulkan_instance_->isValid())
    {
        if (!vulkan_instance_->create())
        {
            SPECTRA_LOG_ERROR("qt_runtime",
                              "QVulkanInstance::create() failed (VkResult="
                                  + std::to_string(static_cast<int>(vulkan_instance_->errorCode()))
                                  + ")");
            return false;
        }
    }

    // Wire the QVulkanInstance into the surface host so create_surface works
    surface_host_->set_vulkan_instance(vulkan_instance_.get());

    // Apply dark theme by default
    ui::ThemeManager::instance().set_theme("dark");

    initialized_ = true;
    SPECTRA_LOG_INFO("qt_runtime", "Initialized (swapchain/pipelines deferred until attach_window)");
    return true;
}

void QtRuntime::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    if (backend_)
    {
        backend_->wait_idle();
    }

    // Destroy per-window contexts before renderer/backend teardown.
    if (backend_)
    {
        for (auto& [window, state] : window_states_)
        {
            (void)window;
            if (!state || !state->window_ctx)
            {
                continue;
            }
            backend_->set_active_window(state->window_ctx.get());
            backend_->destroy_window_context(*state->window_ctx);
        }
        backend_->set_active_window(nullptr);
    }

    window_states_.clear();
    primary_window_       = nullptr;
    current_frame_window_ = nullptr;
    next_window_id_       = 1;

    renderer_.reset();
    backend_.reset();

    surface_host_.reset();
    vulkan_instance_.reset();

    initialized_ = false;
    SPECTRA_LOG_INFO("qt_runtime", "Shutdown complete");
}

bool QtRuntime::attach_window(QWindow* window, uint32_t width, uint32_t height)
{
    if (!initialized_ || !window)
    {
        return false;
    }

    if (has_window(window))
    {
        if (!primary_window_)
        {
            primary_window_ = window;
        }
        return true;
    }

    // Ensure the QWindow has VulkanSurface type
    if (window->surfaceType() != QSurface::VulkanSurface)
    {
        window->setSurfaceType(QSurface::VulkanSurface);
    }

    // Bind QVulkanInstance to the window
    if (window->vulkanInstance() != vulkan_instance_.get())
    {
        window->setVulkanInstance(vulkan_instance_.get());
    }

    auto state            = std::make_unique<WindowState>();
    state->window_ctx     = VulkanBackend::create_window_context();
    state->window_ctx->id = next_window_id_++;
    state->window_ctx->native_window = static_cast<void*>(window);

    if (!backend_->init_window_context(*state->window_ctx, width, height))
    {
        SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize window context");
        return false;
    }

    // First attached window initializes shared renderer resources.
    if (!renderer_)
    {
        backend_->set_active_window(state->window_ctx.get());
        renderer_ = std::make_unique<Renderer>(*backend_);
        if (!renderer_->init())
        {
            SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize renderer");
            backend_->destroy_window_context(*state->window_ctx);
            return false;
        }
    }

    backend_->set_active_window(state->window_ctx.get());
    backend_->ensure_pipelines();

    window_states_.emplace(window, std::move(state));
    if (!primary_window_)
    {
        primary_window_ = window;
    }

    SPECTRA_LOG_INFO("qt_runtime",
                     "Window attached: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

void QtRuntime::detach_window(QWindow* window)
{
    if (!initialized_ || !window)
    {
        return;
    }

    auto it = window_states_.find(window);
    if (it == window_states_.end())
    {
        return;
    }

    auto* state = it->second.get();
    if (backend_ && state && state->window_ctx)
    {
        backend_->set_active_window(state->window_ctx.get());
        backend_->destroy_window_context(*state->window_ctx);
    }

    if (current_frame_window_ == window)
    {
        current_frame_window_ = nullptr;
    }

    window_states_.erase(it);

    if (primary_window_ == window)
    {
        primary_window_ = window_states_.empty() ? nullptr : window_states_.begin()->first;
    }

    if (backend_)
    {
        WindowContext* next_ctx = nullptr;
        if (auto* primary_state = find_window_state(primary_window_))
        {
            next_ctx = primary_state->window_ctx.get();
        }
        backend_->set_active_window(next_ctx);
    }
}

bool QtRuntime::has_window(QWindow* window) const
{
    return find_window_state(window) != nullptr;
}

bool QtRuntime::resize(QWindow* window, uint32_t width, uint32_t height)
{
    if (!initialized_ || !window || width == 0 || height == 0)
    {
        return false;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx || !backend_)
    {
        return false;
    }

    backend_->set_active_window(state->window_ctx.get());
    return backend_->recreate_swapchain_for(*state->window_ctx, width, height);
}

bool QtRuntime::resize(uint32_t width, uint32_t height)
{
    return resize(primary_window_, width, height);
}

void QtRuntime::mark_swapchain_dirty(QWindow* window)
{
    if (!initialized_ || !window)
    {
        return;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return;
    }

    state->resize_pending      = true;
    state->last_resize_request = std::chrono::steady_clock::now();
    state->window_ctx->swapchain_dirty = true;

    SPECTRA_LOG_DEBUG("qt_runtime",
                      "Swapchain marked dirty for window {} (debounce pending)",
                      state->window_ctx->id);
}

void QtRuntime::mark_swapchain_dirty()
{
    mark_swapchain_dirty(primary_window_);
}

bool QtRuntime::begin_frame(QWindow* window)
{
    if (!initialized_ || !window || !backend_ || !renderer_)
    {
        return false;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return false;
    }

    backend_->set_active_window(state->window_ctx.get());

    // Handle swapchain recreation if needed (dirty from resize or OUT_OF_DATE)
    if (state->window_ctx->swapchain_dirty || state->window_ctx->swapchain_invalidated)
    {
        // Debounce: wait until resize events settle before recreating.
        // OUT_OF_DATE from present is always immediate (must recreate).
        if (state->resize_pending && !state->window_ctx->swapchain_invalidated)
        {
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state->last_resize_request);
            if (elapsed < k_resize_debounce)
            {
                ++state->frame_skip_count;
                SPECTRA_LOG_DEBUG(
                    "qt_runtime",
                    "Resize debounce: {}ms < {}ms, skipping frame for window {} (skip #{})",
                    elapsed.count(), k_resize_debounce.count(), state->window_ctx->id,
                    state->frame_skip_count);
                return false;   // Wait for resize events to settle
            }
        }

        platform::SurfaceSize sz{};
        if (surface_host_ && surface_host_->framebuffer_size(static_cast<void*>(window), sz)
            && sz.width > 0 && sz.height > 0)
        {
            ++state->swapchain_recreate_count;
            SPECTRA_LOG_DEBUG("qt_runtime",
                              "Recreating swapchain for window {}: {}x{} (recreate #{}, dirty={}, "
                              "invalidated={})",
                              state->window_ctx->id, sz.width, sz.height,
                              state->swapchain_recreate_count, state->window_ctx->swapchain_dirty,
                              state->window_ctx->swapchain_invalidated);

            if (!backend_->recreate_swapchain_for(*state->window_ctx, sz.width, sz.height))
            {
                SPECTRA_LOG_WARN("qt_runtime",
                                 "Swapchain recreation failed for window {}",
                                 state->window_ctx->id);
                return false;
            }
            state->window_ctx->swapchain_dirty       = false;
            state->window_ctx->swapchain_invalidated = false;
            state->resize_pending                    = false;
        }
        else
        {
            ++state->frame_skip_count;
            SPECTRA_LOG_DEBUG(
                "qt_runtime",
                "Surface not ready (minimized?) for window {} — skipping frame (skip #{})",
                state->window_ctx->id, state->frame_skip_count);
            return false;   // Surface not ready (minimized, etc.)
        }
    }

    // Wait on all OTHER windows' in-flight fences before recording commands.
    // The Renderer shares text vertex buffers and overlay scratch buffers across
    // windows. Without this wait, this window's host-side uploads (text vertices,
    // tick geometry) overwrite memory that another window's submitted-but-not-yet-
    // executed command buffer is still reading on the GPU — causing tick label
    // flicker and geometry corruption on the other canvas.
    for (auto& [other_win, other_state] : window_states_)
    {
        if (other_win != window && other_state && other_state->window_ctx)
        {
            backend_->wait_window_fences(*other_state->window_ctx);
        }
    }

    if (!backend_->begin_frame())
    {
        return false;
    }

    renderer_->flush_pending_deletions();
    current_frame_window_ = window;
    return true;
}

bool QtRuntime::begin_frame()
{
    return begin_frame(primary_window_);
}

void QtRuntime::render_figure(QWindow* window, Figure& figure)
{
    if (!initialized_ || !window || !backend_ || !renderer_)
    {
        return;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return;
    }

    if (current_frame_window_ != window)
    {
        return;
    }

    backend_->set_active_window(state->window_ctx.get());

    auto sw_u = backend_->swapchain_width();
    auto sh_u = backend_->swapchain_height();
    float sw  = static_cast<float>(sw_u);
    float sh  = static_cast<float>(sh_u);

    // Only recompute layout when dimensions actually change — avoids per-frame
    // axis viewport oscillation during resize debounce (root cause of flicker
    // and squished-plot appearance on the left canvas).
    if (figure.width() != sw_u || figure.height() != sh_u)
    {
        figure.set_size(sw_u, sh_u);
        figure.compute_layout();
    }

    // Upload dirty series data
    for (auto& ax : figure.all_axes_mut())
    {
        if (!ax)
            continue;
        for (auto& s : ax->series_mut())
        {
            if (s)
                renderer_->upload_series_data(*s);
        }
    }

    // render_figure_content() already calls render_plot_text() and
    // render_plot_geometry() internally — do NOT call them again here.
    renderer_->begin_render_pass();
    renderer_->render_figure_content(figure);
    renderer_->render_text(sw, sh);
    renderer_->end_render_pass();
}

void QtRuntime::render_figure(Figure& figure)
{
    render_figure(primary_window_, figure);
}

void QtRuntime::end_frame(QWindow* window)
{
    if (!initialized_ || !window || !backend_)
    {
        return;
    }

    auto* state = find_window_state(window);
    if (!state || !state->window_ctx)
    {
        return;
    }

    if (current_frame_window_ != window)
    {
        return;
    }

    backend_->set_active_window(state->window_ctx.get());
    backend_->end_frame();
    current_frame_window_ = nullptr;
}

void QtRuntime::end_frame()
{
    end_frame(primary_window_);
}

WindowContext* QtRuntime::window_context(QWindow* window) const
{
    if (auto* state = find_window_state(window))
    {
        return state->window_ctx.get();
    }
    return nullptr;
}

WindowContext* QtRuntime::window_context() const
{
    return window_context(primary_window_);
}

}   // namespace spectra::adapters::qt

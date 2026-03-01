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

    // Destroy window context before renderer/backend.
    // IMPORTANT: The VkSurfaceKHR was created by QVulkanInstance::surfaceForWindow()
    // and is owned by Qt — it will be destroyed when vulkan_instance_ is reset below.
    // Zero the surface handle here so destroy_window_context does NOT call
    // vkDestroySurfaceKHR (which would be a double-free and trigger a validation crash).
    if (window_ctx_)
    {
        window_ctx_->surface = VK_NULL_HANDLE;
    }
    if (window_ctx_ && backend_)
    {
        backend_->destroy_window_context(*window_ctx_);
    }
    window_ctx_.reset();
    attached_window_ = nullptr;

    renderer_.reset();

    if (backend_)
    {
        backend_->set_surface_host(nullptr);
    }
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

    // Create a WindowContext via factory (avoids incomplete WindowUIContext type)
    window_ctx_                = VulkanBackend::create_window_context();
    window_ctx_->id            = 1;
    window_ctx_->native_window = static_cast<void*>(window);

    if (!backend_->init_window_context(*window_ctx_, width, height))
    {
        SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize window context");
        window_ctx_.reset();
        return false;
    }

    // Set this as the active window for rendering
    backend_->set_active_window(window_ctx_.get());
    attached_window_ = window;

    // Now that the swapchain + render pass exist, compile pipelines against them
    // and initialize the renderer.
    if (!renderer_)
    {
        renderer_ = std::make_unique<Renderer>(*backend_);
        if (!renderer_->init())
        {
            SPECTRA_LOG_ERROR("qt_runtime", "Failed to initialize renderer");
            backend_->destroy_window_context(*window_ctx_);
            window_ctx_.reset();
            attached_window_ = nullptr;
            return false;
        }
    }
    backend_->ensure_pipelines();

    SPECTRA_LOG_INFO("qt_runtime",
                     "Window attached: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool QtRuntime::resize(uint32_t width, uint32_t height)
{
    if (!initialized_ || !window_ctx_ || width == 0 || height == 0)
    {
        return false;
    }

    return backend_->recreate_swapchain_for(*window_ctx_, width, height);
}

void QtRuntime::mark_swapchain_dirty()
{
    if (!initialized_ || !window_ctx_)
    {
        return;
    }

    resize_pending_        = true;
    last_resize_request_   = std::chrono::steady_clock::now();
    window_ctx_->swapchain_dirty = true;

    SPECTRA_LOG_DEBUG("qt_runtime", "Swapchain marked dirty (debounce pending)");
}

bool QtRuntime::begin_frame()
{
    if (!initialized_ || !window_ctx_)
    {
        return false;
    }

    backend_->set_active_window(window_ctx_.get());

    // Handle swapchain recreation if needed (dirty from resize or OUT_OF_DATE)
    if (window_ctx_->swapchain_dirty || window_ctx_->swapchain_invalidated)
    {
        // Debounce: wait until resize events settle before recreating.
        // OUT_OF_DATE from present is always immediate (must recreate).
        if (resize_pending_ && !window_ctx_->swapchain_invalidated)
        {
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_resize_request_);
            if (elapsed < k_resize_debounce)
            {
                ++frame_skip_count_;
                SPECTRA_LOG_DEBUG("qt_runtime",
                                  "Resize debounce: {}ms < {}ms, skipping frame (skip #{})",
                                  elapsed.count(), k_resize_debounce.count(), frame_skip_count_);
                return false;   // Wait for resize events to settle
            }
        }

        platform::SurfaceSize sz{};
        if (surface_host_ && attached_window_
            && surface_host_->framebuffer_size(static_cast<void*>(attached_window_), sz)
            && sz.width > 0 && sz.height > 0)
        {
            ++swapchain_recreate_count_;
            SPECTRA_LOG_DEBUG("qt_runtime",
                              "Recreating swapchain: {}x{} (recreate #{}, dirty={}, invalidated={})",
                              sz.width, sz.height, swapchain_recreate_count_,
                              window_ctx_->swapchain_dirty, window_ctx_->swapchain_invalidated);

            if (!backend_->recreate_swapchain_for(*window_ctx_, sz.width, sz.height))
            {
                SPECTRA_LOG_WARN("qt_runtime", "Swapchain recreation failed");
                return false;
            }
            window_ctx_->swapchain_dirty       = false;
            window_ctx_->swapchain_invalidated = false;
            resize_pending_                    = false;
        }
        else
        {
            ++frame_skip_count_;
            SPECTRA_LOG_DEBUG("qt_runtime",
                              "Surface not ready (minimized?), skipping frame (skip #{})",
                              frame_skip_count_);
            return false;   // Surface not ready (minimized, etc.)
        }
    }

    if (!backend_->begin_frame())
    {
        return false;
    }

    renderer_->flush_pending_deletions();
    return true;
}

void QtRuntime::render_figure(Figure& figure)
{
    if (!initialized_ || !window_ctx_)
    {
        return;
    }

    float sw = static_cast<float>(backend_->swapchain_width());
    float sh = static_cast<float>(backend_->swapchain_height());

    // Update figure dimensions
    figure.set_size(static_cast<uint32_t>(sw), static_cast<uint32_t>(sh));

    // Compute subplot layout
    figure.compute_layout();

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

    renderer_->begin_render_pass();
    renderer_->render_figure_content(figure);
    renderer_->render_plot_text(figure);
    renderer_->render_plot_geometry(figure);
    renderer_->render_text(sw, sh);
    renderer_->end_render_pass();
}

void QtRuntime::end_frame()
{
    if (!initialized_ || !window_ctx_)
    {
        return;
    }

    backend_->end_frame();
}

}   // namespace spectra::adapters::qt

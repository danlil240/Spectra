#include <spectra/embed.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/figures/figure_registry.hpp"
#include "ui/input/input.hpp"

namespace spectra
{

// ─── EmbedSurface::Impl ────────────────────────────────────────────────────

struct EmbedSurface::Impl
{
    EmbedConfig config;

    std::unique_ptr<VulkanBackend> backend;
    std::unique_ptr<Renderer>      renderer;
    FigureRegistry                 registry;
    InputHandler                   input;

    Figure*  active_fig    = nullptr;
    FigureId active_fig_id = INVALID_FIGURE_ID;

    bool initialized = false;

    // Callbacks
    RedrawCallback       redraw_cb;
    CursorChangeCallback cursor_cb;
    TooltipCallback      tooltip_cb;

    bool init()
    {
        backend = std::make_unique<VulkanBackend>();

        if (!backend->init(true /*headless*/))
        {
            SPECTRA_LOG_ERROR("embed", "Failed to initialize Vulkan backend");
            return false;
        }

        if (!backend->create_offscreen_framebuffer(config.width, config.height))
        {
            SPECTRA_LOG_ERROR("embed", "Failed to create offscreen framebuffer");
            return false;
        }

        renderer = std::make_unique<Renderer>(*backend);
        if (!renderer->init())
        {
            SPECTRA_LOG_ERROR("embed", "Failed to initialize renderer");
            return false;
        }

        backend->ensure_pipelines();

        initialized = true;
        SPECTRA_LOG_INFO("embed",
                         "EmbedSurface initialized (" + std::to_string(config.width) + "x"
                             + std::to_string(config.height) + ")");
        return true;
    }

    void shutdown()
    {
        if (!initialized)
            return;

        if (backend)
        {
            backend->wait_idle();
        }

        // Clear registry before renderer/backend to ensure proper cleanup order
        registry.clear();

        renderer.reset();
        backend.reset();
        initialized = false;
    }

    void update_input_figure()
    {
        if (!active_fig)
            return;

        input.set_figure(active_fig);

        // Set active axes to first 2D axes if available
        if (!active_fig->axes().empty() && active_fig->axes()[0])
        {
            input.set_active_axes(active_fig->axes()[0].get());
        }
        else if (!active_fig->all_axes().empty() && active_fig->all_axes()[0])
        {
            input.set_active_axes_base(active_fig->all_axes()[0].get());
        }

        // Set viewport for the full surface
        input.set_viewport(0.0f, 0.0f, static_cast<float>(config.width),
                           static_cast<float>(config.height));
    }

    bool render_frame()
    {
        if (!initialized || !active_fig)
            return false;

        // Update figure dimensions to match surface
        active_fig->config_.width  = config.width;
        active_fig->config_.height = config.height;
        active_fig->compute_layout();

        // Upload any dirty series data
        for (auto& ax : active_fig->all_axes_mut())
        {
            if (!ax)
                continue;
            for (auto& s : ax->series_mut())
            {
                if (s)
                    renderer->upload_series_data(*s);
            }
        }

        if (!backend->begin_frame())
        {
            SPECTRA_LOG_ERROR("embed", "begin_frame() failed");
            return false;
        }

        renderer->flush_pending_deletions();
        renderer->begin_render_pass();
        renderer->render_figure_content(*active_fig);
        renderer->render_plot_text(*active_fig);
        renderer->render_plot_geometry(*active_fig);
        {
            float sw = static_cast<float>(config.width);
            float sh = static_cast<float>(config.height);
            renderer->render_text(sw, sh);
        }
        renderer->end_render_pass();
        backend->end_frame();

        return true;
    }
};

// ─── EmbedSurface public methods ────────────────────────────────────────────

EmbedSurface::EmbedSurface(const EmbedConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->config = config;
    impl_->init();
}

EmbedSurface::~EmbedSurface()
{
    if (impl_)
    {
        impl_->shutdown();
    }
}

EmbedSurface::EmbedSurface(EmbedSurface&&) noexcept            = default;
EmbedSurface& EmbedSurface::operator=(EmbedSurface&&) noexcept = default;

bool EmbedSurface::is_valid() const
{
    return impl_ && impl_->initialized;
}

// ── Figure management ───────────────────────────────────────────────────────

Figure& EmbedSurface::figure(const FigureConfig& cfg)
{
    auto id = impl_->registry.register_figure(std::make_unique<Figure>(cfg));
    auto* fig = impl_->registry.get(id);

    // Auto-activate first figure
    if (!impl_->active_fig)
    {
        impl_->active_fig    = fig;
        impl_->active_fig_id = id;
        impl_->update_input_figure();
    }

    return *fig;
}

Figure* EmbedSurface::active_figure()
{
    return impl_->active_fig;
}

const Figure* EmbedSurface::active_figure() const
{
    return impl_->active_fig;
}

void EmbedSurface::set_active_figure(Figure* fig)
{
    impl_->active_fig = fig;
    if (fig)
    {
        impl_->active_fig_id = impl_->registry.find_id(fig);
    }
    else
    {
        impl_->active_fig_id = INVALID_FIGURE_ID;
    }
    impl_->update_input_figure();
}

FigureRegistry& EmbedSurface::figure_registry()
{
    return impl_->registry;
}

// ── Rendering ───────────────────────────────────────────────────────────────

bool EmbedSurface::resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return false;

    if (!impl_ || !impl_->initialized)
        return false;

    if (width == impl_->config.width && height == impl_->config.height)
        return true;   // no-op

    impl_->backend->wait_idle();

    if (!impl_->backend->create_offscreen_framebuffer(width, height))
    {
        SPECTRA_LOG_ERROR("embed", "Failed to recreate offscreen framebuffer for resize");
        return false;
    }

    impl_->backend->ensure_pipelines();

    impl_->config.width  = width;
    impl_->config.height = height;

    // Update input viewport
    impl_->input.set_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

    SPECTRA_LOG_DEBUG("embed",
                      "Resized to " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool EmbedSurface::render_to_buffer(uint8_t* out_rgba)
{
    if (!out_rgba)
        return false;

    if (!impl_ || !impl_->initialized)
        return false;

    if (!impl_->render_frame())
        return false;

    return impl_->backend->readback_framebuffer(out_rgba, impl_->config.width, impl_->config.height);
}

bool EmbedSurface::render_to_image(const VulkanInteropInfo& /*target*/)
{
    if (!impl_ || !impl_->initialized)
        return false;

    if (!impl_->config.enable_vulkan_interop)
    {
        SPECTRA_LOG_ERROR("embed",
                          "render_to_image() called but enable_vulkan_interop is false");
        return false;
    }

    // TODO: Implement Vulkan interop (Step 3 in plan).
    // This requires:
    //   1. Import host's VkImage into a framebuffer
    //   2. Wait on ready_semaphore before rendering
    //   3. Signal finished_semaphore after rendering
    //   4. Handle image layout transitions
    SPECTRA_LOG_WARN("embed", "render_to_image() not yet implemented — use render_to_buffer()");
    return false;
}

// ── Input forwarding ────────────────────────────────────────────────────────

void EmbedSurface::inject_mouse_move(float x, float y)
{
    if (!impl_ || !impl_->initialized)
        return;
    impl_->input.on_mouse_move(static_cast<double>(x), static_cast<double>(y));
}

void EmbedSurface::inject_mouse_button(int button, int action, int mods, float x, float y)
{
    if (!impl_ || !impl_->initialized)
        return;
    impl_->input.on_mouse_button(button, action, mods, static_cast<double>(x),
                                 static_cast<double>(y));
}

void EmbedSurface::inject_scroll(float dx, float dy, float cursor_x, float cursor_y)
{
    if (!impl_ || !impl_->initialized)
        return;
    (void)dx;   // InputHandler only uses y_offset for zoom
    impl_->input.on_scroll(static_cast<double>(dx), static_cast<double>(dy),
                           static_cast<double>(cursor_x), static_cast<double>(cursor_y));
}

void EmbedSurface::inject_key(int key, int action, int mods)
{
    if (!impl_ || !impl_->initialized)
        return;
    impl_->input.on_key(key, action, mods);
}

void EmbedSurface::inject_char(unsigned int /*codepoint*/)
{
    // Currently no text input handling in plot interaction.
    // Reserved for future use (e.g., axis label editing).
}

void EmbedSurface::update(float dt)
{
    if (!impl_ || !impl_->initialized)
        return;
    impl_->input.update(dt);
}

// ── Properties ──────────────────────────────────────────────────────────────

uint32_t EmbedSurface::width() const
{
    return impl_ ? impl_->config.width : 0;
}

uint32_t EmbedSurface::height() const
{
    return impl_ ? impl_->config.height : 0;
}

float EmbedSurface::dpi_scale() const
{
    return impl_ ? impl_->config.dpi_scale : 1.0f;
}

void EmbedSurface::set_dpi_scale(float scale)
{
    if (impl_)
        impl_->config.dpi_scale = scale;
}

float EmbedSurface::background_alpha() const
{
    return impl_ ? impl_->config.background_alpha : 1.0f;
}

void EmbedSurface::set_background_alpha(float alpha)
{
    if (impl_)
        impl_->config.background_alpha = alpha;
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void EmbedSurface::set_redraw_callback(RedrawCallback cb)
{
    if (impl_)
        impl_->redraw_cb = std::move(cb);
}

void EmbedSurface::set_cursor_change_callback(CursorChangeCallback cb)
{
    if (impl_)
        impl_->cursor_cb = std::move(cb);
}

void EmbedSurface::set_tooltip_callback(TooltipCallback cb)
{
    if (impl_)
        impl_->tooltip_cb = std::move(cb);
}

// ── Advanced ────────────────────────────────────────────────────────────────

Backend* EmbedSurface::backend()
{
    return impl_ ? impl_->backend.get() : nullptr;
}

Renderer* EmbedSurface::renderer()
{
    return impl_ ? impl_->renderer.get() : nullptr;
}

}   // namespace spectra

#include <spectra/embed.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <cfloat>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/figures/figure_registry.hpp"
#include "ui/input/input.hpp"
#include "ui/theme/theme.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "core/layout.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include <imgui.h>
#endif

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

#ifdef SPECTRA_USE_IMGUI
    std::unique_ptr<ImGuiIntegration> imgui_ui;
    std::unique_ptr<DataInteraction>  data_interaction;

    // Mouse state tracked from inject_* calls, fed to ImGui each frame
    float mouse_x    = -FLT_MAX;
    float mouse_y    = -FLT_MAX;
    bool  mouse_down[5] = {};
    float mouse_wheel   = 0.0f;
    float mouse_wheel_h = 0.0f;
#endif

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

        // Force the configured theme BEFORE any rendering so that
        // bg_primary, grid_line, tick_label, series palette colors
        // are all correct from the first frame.  The renderer reads
        // colors from ThemeManager::instance().colors() at render time.
        ui::ThemeManager::instance().set_theme(config.theme);

#ifdef SPECTRA_USE_IMGUI
        // Only initialize ImGui when the user explicitly opts in to UI chrome.
        // Default embed mode is canvas-only — just the plot with Spectra colors.
        if (config.show_imgui_chrome)
        {
            imgui_ui = std::make_unique<ImGuiIntegration>();
            if (!imgui_ui->init_headless(*backend, config.width, config.height))
            {
                SPECTRA_LOG_WARN("embed",
                                 "ImGui headless init failed — falling back to canvas-only");
                imgui_ui.reset();
            }
            else
            {
                data_interaction = std::make_unique<DataInteraction>();
                imgui_ui->set_data_interaction(data_interaction.get());
                imgui_ui->set_input_handler(&input);

                // Apply UI chrome visibility from config
                auto& lm = imgui_ui->get_layout_manager();
                lm.set_inspector_visible(config.show_inspector);
                lm.set_tab_bar_visible(false);   // Always off for embed (single figure)
                if (!config.show_nav_rail)
                    lm.set_nav_rail_width(0.0f);
            }
        }
#endif

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

#ifdef SPECTRA_USE_IMGUI
        data_interaction.reset();
        imgui_ui.reset();
#endif

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

        float sw = static_cast<float>(config.width);
        float sh = static_cast<float>(config.height);

#ifdef SPECTRA_USE_IMGUI
        if (imgui_ui)
        {
            // Start ImGui frame with current input state
            ImGuiIntegration::HeadlessFrameInput fi{};
            fi.display_w  = sw;
            fi.display_h  = sh;
            fi.dt         = 1.0f / 60.0f;
            fi.mouse_x    = mouse_x;
            fi.mouse_y    = mouse_y;
            for (int i = 0; i < 5; ++i)
                fi.mouse_down[i] = mouse_down[i];
            fi.mouse_wheel   = mouse_wheel;
            fi.mouse_wheel_h = mouse_wheel_h;
            fi.dpi_scale     = config.dpi_scale;

            imgui_ui->new_frame_headless(fi);

            // Consume scroll (one-shot per frame, like GLFW)
            mouse_wheel   = 0.0f;
            mouse_wheel_h = 0.0f;

            // Build the full UI (command bar, canvas, status bar, overlays)
            imgui_ui->build_ui(*active_fig);

            // Always hide tab bar for embed (single-figure surface)
            imgui_ui->get_layout_manager().set_tab_bar_visible(false);

            // Compute subplot layout using LayoutManager canvas rect
            const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();
            const auto& af_style = active_fig->style();
            Margins fig_margins;
            fig_margins.left   = af_style.margin_left;
            fig_margins.right  = af_style.margin_right;
            fig_margins.top    = af_style.margin_top;
            fig_margins.bottom = af_style.margin_bottom;
            const auto rects = compute_subplot_layout(canvas.w,
                                                      canvas.h,
                                                      active_fig->grid_rows_,
                                                      active_fig->grid_cols_,
                                                      fig_margins,
                                                      canvas.x,
                                                      canvas.y);
            for (size_t i = 0; i < active_fig->axes_mut().size() && i < rects.size(); ++i)
            {
                if (active_fig->axes_mut()[i])
                    active_fig->axes_mut()[i]->set_viewport(rects[i]);
            }
            for (size_t i = 0; i < active_fig->all_axes_mut().size() && i < rects.size(); ++i)
            {
                if (active_fig->all_axes_mut()[i])
                    active_fig->all_axes_mut()[i]->set_viewport(rects[i]);
            }

            // Update data interaction (cursor readout, crosshair, tooltips)
            if (data_interaction)
            {
                auto readout = input.cursor_readout();
                imgui_ui->set_cursor_data(readout.data_x, readout.data_y);
                data_interaction->update(readout, *active_fig);
            }
        }
        else
#endif
        {
            // Fallback: direct layout without ImGui
            active_fig->compute_layout();
        }

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
#ifdef SPECTRA_USE_IMGUI
            if (imgui_ui)
                ImGui::EndFrame();
#endif
            return false;
        }

        renderer->flush_pending_deletions();

        renderer->begin_render_pass();
        renderer->render_figure_content(*active_fig);

        // Flush Vulkan plot text BEFORE ImGui so UI overlays render on top
        renderer->render_text(sw, sh);

#ifdef SPECTRA_USE_IMGUI
        if (imgui_ui)
        {
            imgui_ui->render(*backend);
        }
#endif

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
#ifdef SPECTRA_USE_IMGUI
    impl_->mouse_x = x;
    impl_->mouse_y = y;
#endif
    impl_->input.on_mouse_move(static_cast<double>(x), static_cast<double>(y));
}

void EmbedSurface::inject_mouse_button(int button, int action, int mods, float x, float y)
{
    if (!impl_ || !impl_->initialized)
        return;
#ifdef SPECTRA_USE_IMGUI
    impl_->mouse_x = x;
    impl_->mouse_y = y;
    if (button >= 0 && button < 5)
        impl_->mouse_down[button] = (action != 0);   // 0 = release
#endif
    impl_->input.on_mouse_button(button, action, mods, static_cast<double>(x),
                                 static_cast<double>(y));
}

void EmbedSurface::inject_scroll(float dx, float dy, float cursor_x, float cursor_y)
{
    if (!impl_ || !impl_->initialized)
        return;
#ifdef SPECTRA_USE_IMGUI
    impl_->mouse_wheel   += dy;
    impl_->mouse_wheel_h += dx;
#endif
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

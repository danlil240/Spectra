#include <spectra/embed.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include <cfloat>

#include "render/backend.hpp"
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include <spectra/figure_registry.hpp>
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

    ui::ThemeManager               theme_mgr;
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
    FrameCallback        frame_cb;
    float                frame_time = 0.0f;

    // Interactive event callbacks (Phase 3)
    PointSelectedCallback  point_selected_cb;
    SeriesSelectedCallback series_selected_cb;
    HoverCallback          hover_cb;
    ViewChangedCallback    view_changed_cb;
    const Series*          last_hover_series = nullptr;
    std::size_t            last_hover_index  = 0;
    bool                   has_last_view     = false;
    double                 last_view[4]      = {0.0, 0.0, 0.0, 0.0};

    // Locate the (axes_index, series_index) of a series within a figure.
    static void find_series_indices(Figure* fig, const Series* s, int& out_ai, int& out_si)
    {
        out_ai = -1;
        out_si = -1;
        if (!fig || !s)
            return;
        auto& axes = fig->all_axes_mut();
        for (std::size_t ai = 0; ai < axes.size(); ++ai)
        {
            if (!axes[ai])
                continue;
            auto& ser = axes[ai]->series_mut();
            for (std::size_t si = 0; si < ser.size(); ++si)
            {
                if (ser[si].get() == s)
                {
                    out_ai = static_cast<int>(ai);
                    out_si = static_cast<int>(si);
                    return;
                }
            }
        }
    }

    // Fire hover/view-changed callbacks based on current interaction state.
    void dispatch_interaction_events(Figure* active_fig)
    {
#ifdef SPECTRA_USE_IMGUI
        if (hover_cb && data_interaction)
        {
            const auto&   np = data_interaction->nearest_point();
            const Series* s  = np.found ? np.series : nullptr;
            if (s != last_hover_series || (s && np.point_index != last_hover_index))
            {
                last_hover_series = s;
                last_hover_index  = np.point_index;
                if (s)
                {
                    int ai = -1, si = -1;
                    find_series_indices(active_fig, s, ai, si);
                    hover_cb(ai, si, np.point_index, np.data_x, np.data_y);
                }
                else
                {
                    hover_cb(-1, -1, 0, 0.0, 0.0);
                }
            }
        }
#endif
        if (view_changed_cb && active_fig)
        {
            auto& axes2d = active_fig->axes_mut();
            if (!axes2d.empty() && axes2d[0])
            {
                const auto xl = axes2d[0]->x_limits();
                const auto yl = axes2d[0]->y_limits();
                if (!has_last_view || xl.min != last_view[0] || xl.max != last_view[1] ||
                    yl.min != last_view[2] || yl.max != last_view[3])
                {
                    has_last_view = true;
                    last_view[0]  = xl.min;
                    last_view[1]  = xl.max;
                    last_view[2]  = yl.min;
                    last_view[3]  = yl.max;
                    view_changed_cb(xl.min, xl.max, yl.min, yl.max);
                }
            }
        }
    }

#ifdef SPECTRA_USE_IMGUI
    std::unique_ptr<ImGuiIntegration> imgui_ui;
    std::unique_ptr<DataInteraction>  data_interaction;

    // Mouse state tracked from inject_* calls, fed to ImGui each frame
    float mouse_x       = -FLT_MAX;
    float mouse_y       = -FLT_MAX;
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

        // Register the embed-owned ThemeManager so all subsystems
        // (renderer, overlays, ui::theme()) use it instead of requiring
        // a global fallback singleton.
        ui::ThemeManager::set_current(&theme_mgr);

        renderer = std::make_unique<Renderer>(*backend, theme_mgr);
        if (!renderer->init())
        {
            SPECTRA_LOG_ERROR("embed", "Failed to initialize renderer");
            return false;
        }

        backend->ensure_pipelines();

        // Register all built-in themes and palettes so that set_theme()
        // can actually find the requested theme by name.  Without this
        // the themes_ map is empty and set_theme() silently no-ops,
        // leaving a default (all-black) fallback.
        theme_mgr.ensure_initialized();

        // Force the configured theme BEFORE any rendering so that
        // bg_primary, grid_line, tick_label, series palette colors
        // are all correct from the first frame.
        theme_mgr.set_theme(config.theme);

#ifdef SPECTRA_USE_IMGUI
        // Only initialize ImGui when the user explicitly opts in to UI chrome.
        // Default embed mode is canvas-only — just the plot with Spectra colors.
        if (config.show_imgui_chrome)
        {
            imgui_ui = std::make_unique<ImGuiIntegration>();
            imgui_ui->set_theme_manager(&theme_mgr);
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
                input.set_data_interaction(data_interaction.get());

                // Phase 3: route DataInteraction selection callbacks to the
                // EmbedSurface-level callbacks (read dynamically so the host can
                // (re)register them at any time).
                data_interaction->set_on_point_selected(
                    [this](Figure* fig, Axes*, int axes_index, Series*, int series_index,
                           size_t point_index)
                    {
                        (void)fig;
                        if (point_selected_cb)
                        {
                            const auto& np = data_interaction->nearest_point();
                            point_selected_cb(axes_index, series_index, point_index,
                                              np.data_x, np.data_y);
                        }
                    });
                data_interaction->set_on_series_selected(
                    [this](Figure*, Axes*, int axes_index, Series*, int series_index)
                    {
                        if (series_selected_cb)
                            series_selected_cb(axes_index, series_index);
                    });

                // Apply UI chrome visibility from config
                imgui_ui->get_layout_manager().set_tab_bar_visible(
                    false);   // Always off for embed (single figure)
                apply_chrome();
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

    // Apply UI chrome visibility from config to the live ImGui layer / overlays.
    void apply_chrome()
    {
#ifdef SPECTRA_USE_IMGUI
        if (imgui_ui)
        {
            imgui_ui->set_command_bar_visible(config.show_command_bar);
            imgui_ui->set_status_bar_visible(config.show_status_bar);
            imgui_ui->set_nav_rail_visible(config.show_nav_rail);

            auto& lm = imgui_ui->get_layout_manager();
            lm.set_inspector_visible(config.show_inspector);
            if (!config.show_nav_rail)
                lm.set_nav_rail_width(0.0f);
        }
        if (data_interaction)
        {
            data_interaction->set_crosshair(config.show_crosshair);
        }
#endif
        // Legend is rendered by the Vulkan path; apply to the active figure.
        if (active_fig)
            active_fig->legend().visible = config.show_legend;
    }

    void update_input_figure()
    {
        if (!active_fig)
            return;

        active_fig->legend().visible = config.show_legend;

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
        input.set_viewport(0.0f,
                           0.0f,
                           static_cast<float>(config.width),
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
            fi.display_w = sw;
            fi.display_h = sh;
            fi.dt        = 1.0f / 60.0f;
            fi.mouse_x   = mouse_x;
            fi.mouse_y   = mouse_y;
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
            const Rect  canvas   = imgui_ui->get_layout_manager().canvas_rect();
            const auto& af_style = active_fig->style();
            Margins     fig_margins;
            fig_margins.left   = af_style.margin_left;
            fig_margins.right  = af_style.margin_right;
            fig_margins.top    = af_style.margin_top;
            fig_margins.bottom = af_style.margin_bottom;
            const auto rects   = compute_subplot_layout(canvas.w,
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
                imgui_ui->set_cursor_data(readout.data_x, readout.data_y, readout.valid);
                data_interaction->update(readout, *active_fig);
            }
        }
        else
#endif
        {
            // Fallback: direct layout without ImGui
            active_fig->compute_layout();
        }

        // Phase 3: fire hover / view-changed callbacks for the active figure.
        dispatch_interaction_events(active_fig);

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

EmbedSurface::EmbedSurface(const EmbedConfig& config) : impl_(std::make_unique<Impl>())
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
    auto  id  = impl_->registry.register_figure(std::make_unique<Figure>(cfg));
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

    return impl_->backend->readback_framebuffer(out_rgba,
                                                impl_->config.width,
                                                impl_->config.height);
}

bool EmbedSurface::render_to_image(const VulkanInteropInfo& /*target*/)
{
    if (!impl_ || !impl_->initialized)
        return false;

    if (!impl_->config.enable_vulkan_interop)
    {
        SPECTRA_LOG_ERROR("embed", "render_to_image() called but enable_vulkan_interop is false");
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
    impl_->input.on_mouse_button(button,
                                 action,
                                 mods,
                                 static_cast<double>(x),
                                 static_cast<double>(y));
}

void EmbedSurface::inject_scroll(float dx, float dy, float cursor_x, float cursor_y)
{
    if (!impl_ || !impl_->initialized)
        return;
#ifdef SPECTRA_USE_IMGUI
    impl_->mouse_wheel += dy;
    impl_->mouse_wheel_h += dx;
#endif
    impl_->input.on_scroll(static_cast<double>(dx),
                           static_cast<double>(dy),
                           static_cast<double>(cursor_x),
                           static_cast<double>(cursor_y));
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

    // Phase 4: drive the per-frame callback for live data / animation.
    if (impl_->frame_cb)
    {
        impl_->frame_time += dt;
        impl_->frame_cb(impl_->frame_time, dt);
    }
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

// ── UI chrome visibility ──────────────────────────────────────────────────────

void EmbedSurface::set_show_command_bar(bool visible)
{
    if (!impl_)
        return;
    impl_->config.show_command_bar = visible;
    impl_->apply_chrome();
}

void EmbedSurface::set_show_status_bar(bool visible)
{
    if (!impl_)
        return;
    impl_->config.show_status_bar = visible;
    impl_->apply_chrome();
}

void EmbedSurface::set_show_nav_rail(bool visible)
{
    if (!impl_)
        return;
    impl_->config.show_nav_rail = visible;
    impl_->apply_chrome();
}

void EmbedSurface::set_show_inspector(bool visible)
{
    if (!impl_)
        return;
    impl_->config.show_inspector = visible;
    impl_->apply_chrome();
}

void EmbedSurface::set_show_legend(bool visible)
{
    if (!impl_)
        return;
    impl_->config.show_legend = visible;
    impl_->apply_chrome();
}

void EmbedSurface::set_show_crosshair(bool visible)
{
    if (!impl_)
        return;
    impl_->config.show_crosshair = visible;
    impl_->apply_chrome();
}

bool EmbedSurface::show_command_bar() const
{
    return impl_ && impl_->config.show_command_bar;
}

bool EmbedSurface::show_status_bar() const
{
    return impl_ && impl_->config.show_status_bar;
}

bool EmbedSurface::show_nav_rail() const
{
    return impl_ && impl_->config.show_nav_rail;
}

bool EmbedSurface::show_inspector() const
{
    return impl_ && impl_->config.show_inspector;
}

bool EmbedSurface::show_legend() const
{
    return impl_ && impl_->config.show_legend;
}

bool EmbedSurface::show_crosshair() const
{
    return impl_ && impl_->config.show_crosshair;
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

void EmbedSurface::set_frame_callback(FrameCallback cb)
{
    if (impl_)
        impl_->frame_cb = std::move(cb);
}

void EmbedSurface::clear_frame_callback()
{
    if (impl_)
        impl_->frame_cb = nullptr;
}

void EmbedSurface::reset_frame_clock()
{
    if (impl_)
        impl_->frame_time = 0.0f;
}

float EmbedSurface::frame_time() const
{
    return impl_ ? impl_->frame_time : 0.0f;
}

void EmbedSurface::set_on_point_selected(PointSelectedCallback cb)
{
    if (impl_)
    {
        impl_->point_selected_cb = std::move(cb);
        if (impl_->point_selected_cb)
            impl_->input.set_tool_mode(ToolMode::Select);
    }
}

void EmbedSurface::set_on_series_selected(SeriesSelectedCallback cb)
{
    if (impl_)
    {
        impl_->series_selected_cb = std::move(cb);
        if (impl_->series_selected_cb)
            impl_->input.set_tool_mode(ToolMode::Select);
    }
}

void EmbedSurface::set_on_hover(HoverCallback cb)
{
    if (impl_)
        impl_->hover_cb = std::move(cb);
}

void EmbedSurface::set_on_view_changed(ViewChangedCallback cb)
{
    if (impl_)
        impl_->view_changed_cb = std::move(cb);
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

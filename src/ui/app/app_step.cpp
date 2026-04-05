// app_step.cpp — Frame-by-frame control API for App.
// Extracts init / step / shutdown from app_inproc.cpp so external
// drivers (QA agent, test harnesses) can pump frames individually.

#include <spectra/animator.hpp>
#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

#include "anim/frame_scheduler.hpp"
#include "adapters/data_source_registry.hpp"
#include "io/export_registry.hpp"
#include "math/data_transform.hpp"
#include "render/renderer.hpp"
#include "render/series_type_registry.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "render/vulkan/window_context.hpp"
#ifdef SPECTRA_USE_WEBGPU
    #include "render/webgpu/wgpu_backend.hpp"
#endif
#include "ui/commands/command_queue.hpp"
#include "session_runtime.hpp"
#include "window_runtime.hpp"
#include "window_ui_context_builder.hpp"
#include "window_ui_context.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "ui/window/glfw_adapter.hpp"
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "ui/theme/icons.hpp"
    #include "register_commands.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/commands/undoable_property.hpp"
    #include "ui/workspace/workspace.hpp"
#endif

#include "ui/automation/automation_server.hpp"
#include "ui/automation/mcp_server.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace spectra
{

// ─── App ctor/dtor (must be here where AppRuntime is complete) ───────────────
App::App(const AppConfig& config) : config_(config)
{
    spectra::setup_dual_logging(spectra::LogLevel::Info, spectra::LogLevel::Trace);

    SPECTRA_LOG_INFO("app",
                     "Initializing Spectra application (headless: "
                         + std::string(config_.headless ? "true" : "false") + ")");

    bool multiproc = !config_.socket_path.empty();
    if (!multiproc)
    {
        const char* env = std::getenv("SPECTRA_SOCKET");
        multiproc       = (env && env[0] != '\0');
    }
    SPECTRA_LOG_INFO("app", "Runtime mode: " + std::string(multiproc ? "multiproc" : "inproc"));

    backend_ = std::make_unique<VulkanBackend>();
#ifdef SPECTRA_USE_WEBGPU
    if (config_.backend == RenderBackend::WebGPU)
        backend_ = std::make_unique<WebGPUBackend>();
#endif
    if (!backend_->init(config_.headless))
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize Vulkan backend");
        return;
    }

    // Create the App-owned ThemeManager and register it as the active instance
    // so ThemeManager::instance() returns this object instead of the fallback
    // singleton for the lifetime of this App.
    theme_mgr_ = std::make_unique<ui::ThemeManager>();
    ui::ThemeManager::set_current(theme_mgr_.get());
    theme_mgr_->ensure_initialized();

    renderer_ = std::make_unique<Renderer>(*backend_, *theme_mgr_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize renderer");
        return;
    }

    SPECTRA_LOG_INFO("app", "Spectra application initialized successfully");
}

App::~App()
{
    // Guard each destruction step with try-catch.  On CI with lavapipe
    // (software Vulkan), non-deterministic std::system_error("Invalid
    // argument") can be thrown from deep inside member destructors
    // (likely from pthread operations in the software driver).  Since
    // destructors are noexcept, an uncaught throw triggers std::terminate.
    try
    {
        shutdown_runtime();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown", std::string("Exception in shutdown_runtime: ") + e.what());
    }
    catch (...)
    {
    }

    try
    {
        runtime_.reset();
    }
    catch (...)
    {
    }

    try
    {
        renderer_.reset();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown", std::string("Exception in ~Renderer: ") + e.what());
    }
    catch (...)
    {
    }

    // Clear the singleton pointer before the ThemeManager member is destroyed
    // to prevent any remaining call sites from dereferencing a dangling pointer.
    ui::ThemeManager::set_current(nullptr);
    theme_mgr_.reset();
    if (backend_)
    {
        try
        {
            backend_->shutdown();
        }
        catch (...)
        {
        }
    }
}

// ─── AppRuntime: all state that lives across frames ──────────────────────────
struct App::AppRuntime
{
    CommandQueue   cmd_queue;
    FrameScheduler scheduler;
    Animator       animator;
    SessionRuntime session;

    PluginManager        plugin_manager;
    ExportFormatRegistry export_format_registry;
    DataSourceRegistry   data_source_registry;
    SeriesTypeRegistry   series_type_registry;

    FrameState frame_state;
    uint64_t   frame_number = 0;

    WindowUIContext*                 ui_ctx_ptr = nullptr;
    std::unique_ptr<WindowUIContext> headless_ui_ctx;

    Figure*  active_figure    = nullptr;
    FigureId active_figure_id = INVALID_FIGURE_ID;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<GlfwAdapter>   glfw;
    std::unique_ptr<WindowManager> window_mgr;
#endif

#ifdef SPECTRA_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t>           video_frame_pixels;
    bool                           is_recording = false;
#endif

    // Wall-clock for frame timing
    std::chrono::steady_clock::time_point last_step_time;

    std::unique_ptr<AutomationServer> auto_server;
    std::unique_ptr<McpServer>        mcp_server;

    // Pending PNG capture (pre-present path to avoid reading presented images)
    struct PendingPngCapture
    {
        std::vector<uint8_t> pixels;
        std::string          path;
        uint32_t             width  = 0;
        uint32_t             height = 0;
        bool                 active = false;
    } pending_png_capture;

    AppRuntime(float fps, Backend& backend, Renderer& renderer, FigureRegistry& registry)
        : scheduler(fps), session(backend, renderer, registry)
    {
    }

    // Explicit noexcept(false) destructor: the implicit default destructor
    // is noexcept, which means any exception thrown during member destruction
    // triggers std::terminate.  On CI with lavapipe (software Vulkan),
    // non-deterministic std::system_error("Invalid argument") can occur
    // during member cleanup.  Making the destructor noexcept(false) lets
    // the exception propagate to callers that have try-catch guards.
    ~AppRuntime() noexcept(false) = default;
};

// ─── init_runtime ────────────────────────────────────────────────────────────
void App::init_runtime()
{
    if (!backend_ || !renderer_)
    {
        std::cerr << "[spectra] Cannot run: backend or renderer not initialized\n";
        return;
    }

    auto       all_ids       = registry_.all_ids();
    auto       window_groups = compute_window_groups();
    const bool empty_start   = all_ids.empty();

    float    init_fps       = 60.0f;
    Figure*  init_active    = nullptr;
    FigureId init_active_id = INVALID_FIGURE_ID;

    if (!empty_start)
    {
        init_active_id = all_ids[0];
        init_active    = registry_.get(init_active_id);
        if (init_active && init_active->anim_.fps > 0.0f)
            init_fps = init_active->anim_.fps;
    }

    uint32_t init_w = init_active ? init_active->width() : 1280;
    uint32_t init_h = init_active ? init_active->height() : 720;

    runtime_ = std::make_unique<AppRuntime>(init_fps, *backend_, *renderer_, registry_);
    auto& rt = *runtime_;

    rt.frame_state.active_figure_id = init_active_id;
    rt.frame_state.active_figure    = init_active;
    rt.active_figure                = init_active;
    rt.active_figure_id             = init_active_id;

    rt.frame_state.has_animation =
        init_active ? static_cast<bool>(init_active->anim_.on_frame) : false;

    if (!config_.headless)
    {
        rt.scheduler.set_mode(FrameScheduler::Mode::VSync);
    }

#ifdef SPECTRA_USE_FFMPEG
    rt.is_recording = init_active && !init_active->export_req_.video_path.empty();
#else
    if (init_active && !init_active->export_req_.video_path.empty())
    {
        std::cerr << "[spectra] Video recording requested but SPECTRA_USE_FFMPEG is not enabled\n";
    }
#endif

#ifdef SPECTRA_USE_FFMPEG
    if (rt.is_recording)
    {
        VideoExporter::Config vcfg;
        vcfg.output_path  = init_active->export_req_.video_path;
        vcfg.width        = init_active->width();
        vcfg.height       = init_active->height();
        vcfg.fps          = init_active->anim_.fps;
        rt.video_exporter = std::make_unique<VideoExporter>(vcfg);
        if (!rt.video_exporter->is_open())
        {
            std::cerr << "[spectra] Failed to open video exporter for: "
                      << init_active->export_req_.video_path << "\n";
            rt.video_exporter.reset();
        }
        else
        {
            rt.video_frame_pixels.resize(static_cast<size_t>(init_active->width())
                                         * init_active->height() * 4);
        }
        if (!config_.headless)
        {
            config_.headless = true;
        }
    }
#endif

#ifdef SPECTRA_USE_GLFW
    if (!config_.headless)
    {
        rt.glfw = std::make_unique<GlfwAdapter>();
        if (!rt.glfw->init(init_w, init_h, "Spectra"))
        {
            std::cerr << "[spectra] Failed to create GLFW window\n";
            rt.glfw.reset();
        }
        else
        {
            backend_->create_surface(rt.glfw->native_window());
            backend_->create_swapchain(init_w, init_h);

            // WindowManager requires VulkanBackend — skip for other backends.
            if (config_.backend == RenderBackend::Vulkan)
            {
                rt.window_mgr = std::make_unique<WindowManager>();
                rt.window_mgr->init(static_cast<VulkanBackend*>(backend_.get()),
                                    &registry_,
                                    renderer_.get(),
                                    theme_mgr_.get());
                rt.window_mgr->set_redraw_request_handler(
                    [&session = rt.session](const char* reason)
                    { session.redraw_tracker().mark_dirty(reason); });
                rt.window_mgr->set_plugin_manager(&rt.plugin_manager);
                rt.window_mgr->set_export_format_registry(&rt.export_format_registry);
                rt.window_mgr->set_session_runtime(&rt.session);

                rt.window_mgr->set_tab_detach_handler(
                    [&session = rt.session](FigureId           fid,
                                            uint32_t           w,
                                            uint32_t           h,
                                            const std::string& title,
                                            int                sx,
                                            int                sy)
                    { session.queue_detach({fid, w, h, title, sx, sy}); });
                rt.window_mgr->set_tab_move_handler(
                    [&session = rt.session](FigureId fid,
                                            uint32_t target_wid,
                                            int      drop_zone,
                                            float    local_x,
                                            float    local_y,
                                            FigureId target_figure_id) {
                        session.queue_move(
                            {fid, target_wid, drop_zone, local_x, local_y, target_figure_id});
                    });

                std::vector<FigureId> first_group =
                    window_groups.empty() ? std::vector<FigureId>{} : window_groups[0];
                auto* initial_wctx =
                    rt.window_mgr->create_first_window_with_ui(rt.glfw->native_window(),
                                                               first_group);

                if (initial_wctx && initial_wctx->ui_ctx)
                {
                    rt.ui_ctx_ptr              = initial_wctx->ui_ctx.get();
                    rt.ui_ctx_ptr->glfw_window = initial_wctx->glfw_window;
                }

                // Pre-create a hidden preview window so tab tearoff is instant.
                rt.window_mgr->warmup_preview_window();

                for (size_t gi = 1; gi < window_groups.size(); ++gi)
                {
                    auto& group = window_groups[gi];
                    if (group.empty())
                        continue;

                    auto*    fig0 = registry_.get(group[0]);
                    uint32_t w    = fig0 ? fig0->width() : 800;
                    uint32_t h    = fig0 ? fig0->height() : 600;

                    auto* new_wctx =
                        rt.window_mgr->create_window_with_ui(w, h, "Spectra", group[0]);

                    if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                    {
                        for (size_t fi = 1; fi < group.size(); ++fi)
                        {
                            new_wctx->ui_ctx->fig_mgr->add_figure(group[fi], FigureState{});
                            new_wctx->assigned_figures.push_back(group[fi]);
                        }
                    }
                }
            }   // if (config_.backend == RenderBackend::Vulkan)
        }
    }
#endif

    if (!rt.ui_ctx_ptr)
    {
        // Headless mode: create a minimal WindowUIContext with only the
        // FigureManager needed for rendering.  The full builder
        // (build_window_ui_context) creates CommandRegistry, ShortcutManager,
        // InputHandler, AnimationController, and many other objects whose
        // destruction order within AppRuntime can trigger std::system_error
        // during rapid create/destroy cycles (e.g. golden tests running
        // multiple App instances in the same process).
        rt.headless_ui_ctx                = std::make_unique<WindowUIContext>();
        rt.headless_ui_ctx->theme_mgr     = theme_mgr_.get();
        rt.headless_ui_ctx->fig_mgr_owned = std::make_unique<FigureManager>(registry_);
        rt.headless_ui_ctx->fig_mgr       = rt.headless_ui_ctx->fig_mgr_owned.get();
        rt.ui_ctx_ptr                     = rt.headless_ui_ctx.get();
    }

    // Wire plugin host services after UI context creation.
    rt.plugin_manager.set_command_registry(&rt.ui_ctx_ptr->cmd_registry);
    rt.plugin_manager.set_shortcut_manager(&rt.ui_ctx_ptr->shortcut_mgr);
    rt.plugin_manager.set_undo_manager(&rt.ui_ctx_ptr->undo_mgr);
    rt.plugin_manager.set_transform_registry(&TransformRegistry::instance());
    rt.plugin_manager.set_export_format_registry(&rt.export_format_registry);
    rt.plugin_manager.set_data_source_registry(&rt.data_source_registry);
    rt.plugin_manager.set_series_type_registry(&rt.series_type_registry);
    rt.plugin_manager.set_backend(backend_.get());
    renderer_->set_series_type_registry(&rt.series_type_registry);
#ifdef SPECTRA_USE_GLFW
    if (rt.window_mgr)
        rt.plugin_manager.set_overlay_registry(&rt.window_mgr->overlay_registry());
#endif

    rt.ui_ctx_ptr->plugin_manager = &rt.plugin_manager;
    if (rt.ui_ctx_ptr->imgui_ui)
    {
        rt.ui_ctx_ptr->imgui_ui->set_plugin_manager(&rt.plugin_manager);
        rt.ui_ctx_ptr->imgui_ui->set_export_format_registry(&rt.export_format_registry);
    }

#ifdef SPECTRA_USE_IMGUI
    auto& imgui_ui              = rt.ui_ctx_ptr->imgui_ui;
    auto& figure_tabs           = rt.ui_ctx_ptr->figure_tabs;
    auto& dock_system           = rt.ui_ctx_ptr->dock_system;
    auto& timeline_editor       = rt.ui_ctx_ptr->timeline_editor;
    auto& keyframe_interpolator = rt.ui_ctx_ptr->keyframe_interpolator;
    // auto& curve_editor          = rt.ui_ctx_ptr->curve_editor;
    // auto& home_limits           = rt.ui_ctx_ptr->home_limits;
    // auto& cmd_registry        = rt.ui_ctx_ptr->cmd_registry;
    // auto& shortcut_mgr        = rt.ui_ctx_ptr->shortcut_mgr;
    auto& cmd_palette         = rt.ui_ctx_ptr->cmd_palette;
    auto& tab_drag_controller = rt.ui_ctx_ptr->tab_drag_controller;
    auto& fig_mgr             = *rt.ui_ctx_ptr->fig_mgr;
    auto& input_handler       = rt.ui_ctx_ptr->input_handler;

    if (knob_manager_ && !knob_manager_->empty() && imgui_ui)
    {
        imgui_ui->set_knob_manager(knob_manager_);
    }

    if (init_active)
    {
        if (init_active->anim_.duration > 0.0f)
        {
            timeline_editor.set_duration(init_active->anim_.duration);
        }
        else if (rt.frame_state.has_animation)
        {
            timeline_editor.set_duration(60.0f);
        }
        if (init_active->anim_.loop)
        {
            timeline_editor.set_loop_mode(LoopMode::Loop);
        }
        if (init_active->anim_.fps > 0.0f)
        {
            timeline_editor.set_fps(init_active->anim_.fps);
        }
        if (rt.frame_state.has_animation)
        {
            timeline_editor.play();
        }

        // ── Wire up animated channels for the curve editor ──────────────
        // Create keyframe channels for each series' opacity and size/width
        // so the curve editor and timeline have real, editable curves.
        float anim_dur = timeline_editor.duration();
        int   s_idx    = 0;
        for (auto& ax : init_active->axes())
        {
            if (!ax)
                continue;
            for (auto& s : ax->series_mut())
            {
                if (!s)
                    continue;
                std::string prefix =
                    s->label().empty() ? "Series " + std::to_string(s_idx) : s->label();

                // Opacity channel — ramp from 1.0 to 0.3 and back
                {
                    uint32_t ch_id = timeline_editor.add_animated_track(prefix + " Opacity", 1.0f);
                    timeline_editor.add_animated_keyframe(ch_id, 0.0f, 1.0f, 1);   // Linear
                    timeline_editor.add_animated_keyframe(ch_id,
                                                          anim_dur * 0.4f,
                                                          0.3f,
                                                          6);   // EaseInOut
                    timeline_editor.add_animated_keyframe(ch_id,
                                                          anim_dur * 0.7f,
                                                          0.8f,
                                                          4);                          // EaseIn
                    timeline_editor.add_animated_keyframe(ch_id, anim_dur, 1.0f, 5);   // EaseOut

                    Series* raw = s.get();
                    keyframe_interpolator.bind_callback(
                        ch_id,
                        prefix + " Opacity",
                        [raw](float v) { raw->opacity(std::clamp(v, 0.0f, 1.0f)); });
                }

                // Size channel — scatter point_size or line width
                if (auto* sc = dynamic_cast<ScatterSeries*>(s.get()))
                {
                    float    base  = sc->size();
                    uint32_t ch_id = timeline_editor.add_animated_track(prefix + " Size", base);
                    timeline_editor.add_animated_keyframe(ch_id, 0.0f, base, 1);
                    timeline_editor.add_animated_keyframe(ch_id,
                                                          anim_dur * 0.3f,
                                                          base * 2.5f,
                                                          3);   // Spring
                    timeline_editor.add_animated_keyframe(ch_id,
                                                          anim_dur * 0.6f,
                                                          base * 0.5f,
                                                          6);   // EaseInOut
                    timeline_editor.add_animated_keyframe(ch_id, anim_dur, base, 5);

                    keyframe_interpolator.bind_callback(ch_id,
                                                        prefix + " Size",
                                                        [sc](float v)
                                                        { sc->size(std::max(v, 1.0f)); });
                }
                else if (auto* ln = dynamic_cast<LineSeries*>(s.get()))
                {
                    float    base  = ln->width();
                    uint32_t ch_id = timeline_editor.add_animated_track(prefix + " Width", base);
                    timeline_editor.add_animated_keyframe(ch_id, 0.0f, base, 1);
                    timeline_editor.add_animated_keyframe(ch_id,
                                                          anim_dur * 0.3f,
                                                          base * 3.0f,
                                                          3);   // Spring
                    timeline_editor.add_animated_keyframe(ch_id,
                                                          anim_dur * 0.6f,
                                                          base * 0.5f,
                                                          6);   // EaseInOut
                    timeline_editor.add_animated_keyframe(ch_id, anim_dur, base, 5);

                    keyframe_interpolator.bind_callback(ch_id,
                                                        prefix + " Width",
                                                        [ln](float v)
                                                        { ln->width(std::max(v, 0.5f)); });
                }

                ++s_idx;
            }
        }
        keyframe_interpolator.compute_all_auto_tangents();
    }

    #ifdef SPECTRA_USE_GLFW
    if (rt.window_mgr)
    {
        tab_drag_controller.set_window_manager(rt.window_mgr.get());
        if (init_active)
        {
            input_handler.set_figure(init_active);
            if (!init_active->axes().empty() && init_active->axes()[0])
            {
                input_handler.set_active_axes(init_active->axes()[0].get());
                auto& vp = init_active->axes()[0]->viewport();
                input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
            }
        }
    }
    #endif
#endif

    if (config_.headless)
    {
        backend_->create_offscreen_framebuffer(init_w, init_h);
        if (config_.backend == RenderBackend::Vulkan)
            static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();
    }

#ifdef SPECTRA_USE_IMGUI
    if (figure_tabs && !config_.headless)
    {
        figure_tabs->set_tab_split_right_callback(
            [&dock_system, &fig_mgr](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                FigureId id   = fig_mgr.figure_ids()[pos];
                auto*    pane = dock_system.split_view().root()
                                    ? dock_system.split_view().root()->find_by_figure(id)
                                    : nullptr;
                if (!pane || pane->figure_count() < 2)
                    return;
                auto* new_pane = dock_system.split_figure_right(id, id);
                if (!new_pane)
                    return;
                auto* parent = new_pane->parent();
                if (parent && parent->first())
                    parent->first()->remove_figure(id);
                dock_system.set_active_figure_index(id);
            });

        figure_tabs->set_tab_split_down_callback(
            [&dock_system, &fig_mgr](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                FigureId id   = fig_mgr.figure_ids()[pos];
                auto*    pane = dock_system.split_view().root()
                                    ? dock_system.split_view().root()->find_by_figure(id)
                                    : nullptr;
                if (!pane || pane->figure_count() < 2)
                    return;
                auto* new_pane = dock_system.split_figure_down(id, id);
                if (!new_pane)
                    return;
                auto* parent = new_pane->parent();
                if (parent && parent->first())
                    parent->first()->remove_figure(id);
                dock_system.set_active_figure_index(id);
            });

        figure_tabs->set_tab_detach_callback(
            [&fig_mgr, &session = rt.session, this](size_t pos, float screen_x, float screen_y)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                FigureId id  = fig_mgr.figure_ids()[pos];
                auto*    fig = registry_.get(id);
                if (!fig)
                    return;

                if (fig_mgr.count() <= 1)
                    return;

                uint32_t    win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t    win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(id);

                session.queue_detach({id,
                                      win_w,
                                      win_h,
                                      title,
                                      static_cast<int>(screen_x),
                                      static_cast<int>(screen_y)});
            });
    }

    if (!config_.headless && rt.ui_ctx_ptr)
    {
        tab_drag_controller.set_on_drop_outside(
            [&fig_mgr, &session = rt.session, this](FigureId index, float screen_x, float screen_y)
            {
                auto* fig = registry_.get(index);
                if (!fig)
                    return;

                uint32_t    win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t    win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(index);

                session.queue_detach({index,
                                      win_w,
                                      win_h,
                                      title,
                                      static_cast<int>(screen_x),
                                      static_cast<int>(screen_y)});
            });

        if (imgui_ui)
        {
            imgui_ui->set_pane_tab_detach_cb(
                [&fig_mgr, &session = rt.session, this](FigureId index,
                                                        float    screen_x,
                                                        float    screen_y)
                {
                    auto* fig = registry_.get(index);
                    if (!fig)
                        return;

                    uint32_t    win_w = fig->width() > 0 ? fig->width() : 800;
                    uint32_t    win_h = fig->height() > 0 ? fig->height() : 600;
                    std::string title = fig_mgr.get_title(index);

                    session.queue_detach({index,
                                          win_w,
                                          win_h,
                                          title,
                                          static_cast<int>(screen_x),
                                          static_cast<int>(screen_y)});
                });
        }

        if (imgui_ui)
        {
            imgui_ui->set_csv_plot_callback(
                [&fig_mgr, this](const std::string& /*path*/,
                                 const std::vector<float>& x,
                                 const std::vector<float>& y,
                                 const std::string& /* x_label */,
                                 const std::string& y_label,
                                 const std::vector<float>* /*z*/,
                                 const std::string* /*z_label*/)
                {
                    FigureId active_id = fig_mgr.active_index();
                    Figure*  fig       = registry_.get(active_id);
                    if (!fig)
                    {
                        active_id = fig_mgr.create_figure(FigureConfig{});
                        fig       = registry_.get(active_id);
                        if (!fig)
                            return;
                    }

                    auto& ax   = fig->subplot(1, 1, 1);
                    auto& line = ax.line(x, y);
                    line.label(y_label);
                    ax.auto_fit();
                });
        }

        cmd_palette.set_body_font(nullptr);
        cmd_palette.set_heading_font(nullptr);
    }
#endif

    rt.scheduler.reset();

    // Capture initial axes limits for Home button — stored per-figure in ViewModel
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        auto& vm = rt.ui_ctx_ptr->fig_mgr->state(id);
        for (auto& ax : fig_ptr->axes_mut())
        {
            if (ax)
                vm.set_home_limit(ax.get(), {ax->x_limits(), ax->y_limits()});
        }
    }

    rt.last_step_time = std::chrono::steady_clock::now();

    // Start automation server for MCP-driven testing.
    // In headless mode, skip unless explicitly requested via environment variable
    // to avoid thread lifecycle issues during rapid create/destroy cycles in tests.
    {
        const char* auto_env = std::getenv("SPECTRA_AUTO_SOCKET");
        bool        want_automation =
            !config_.headless || (auto_env && auto_env[0] != '\0');

        if (want_automation)
        {
            std::string auto_sock;
            if (auto_env && auto_env[0] != '\0')
                auto_sock = auto_env;
            else
                auto_sock = AutomationServer::default_socket_path();

            rt.auto_server = std::make_unique<AutomationServer>();
            if (rt.auto_server->start(auto_sock))
            {
                SPECTRA_LOG_INFO("app", "Automation server started: " + auto_sock);

                std::string mcp_bind = "127.0.0.1";
                if (const char* mcp_bind_env = std::getenv("SPECTRA_MCP_BIND"))
                {
                    if (mcp_bind_env[0] != '\0')
                        mcp_bind = mcp_bind_env;
                }

                uint16_t mcp_port = 8765;
                if (const char* mcp_port_env = std::getenv("SPECTRA_MCP_PORT"))
                {
                    if (mcp_port_env[0] != '\0')
                    {
                        const long parsed_port = std::strtol(mcp_port_env, nullptr, 10);
                        if (parsed_port > 0 && parsed_port <= 65535)
                            mcp_port = static_cast<uint16_t>(parsed_port);
                    }
                }

                rt.mcp_server = std::make_unique<McpServer>();
                if (rt.mcp_server->start(*rt.auto_server, mcp_bind, mcp_port))
                    SPECTRA_LOG_INFO("app",
                                     "MCP server started: " + rt.mcp_server->endpoint());
                else
                    rt.mcp_server.reset();
            }
            else
            {
                SPECTRA_LOG_WARN("app", "Automation server failed to start");
                rt.auto_server.reset();
            }
        }
    }
}

// ─── step ────────────────────────────────────────────────────────────────────
App::StepResult App::step()
{
    StepResult result;
    if (!runtime_)
    {
        result.should_exit = true;
        return result;
    }

    auto& rt = *runtime_;

    auto step_start = std::chrono::steady_clock::now();

    // Poll automation server for pending remote commands
    if (rt.auto_server)
    {
        rt.auto_server->poll(*this, rt.ui_ctx_ptr);
        // Automation commands will have been drained into cmd_queue;
        // the tick() will mark dirty when it processes them.
    }

    rt.session.tick(rt.scheduler,
                    rt.animator,
                    rt.cmd_queue,
                    config_.headless,
                    rt.ui_ctx_ptr,
#ifdef SPECTRA_USE_GLFW
                    rt.window_mgr.get(),
#endif
                    rt.frame_state);
    rt.active_figure = rt.frame_state.active_figure;

#ifdef SPECTRA_USE_FFMPEG
    if (rt.video_exporter && rt.video_exporter->is_open() && rt.active_figure)
    {
        if (backend_->readback_framebuffer(rt.video_frame_pixels.data(),
                                           rt.active_figure->width(),
                                           rt.active_figure->height()))
        {
            rt.video_exporter->write_frame(rt.video_frame_pixels.data());
        }
    }
#endif

    // Phase 1: Write PNG from a previously completed pre-present capture.
    if (rt.pending_png_capture.active)
    {
        auto& cap = rt.pending_png_capture;
        if (ImageExporter::write_png(cap.path, cap.pixels.data(), cap.width, cap.height))
        {
            SPECTRA_LOG_INFO("export", "Saved PNG: " + cap.path);
        }
        else
        {
            SPECTRA_LOG_ERROR("export", "Failed to write PNG: " + cap.path);
        }
        cap.active = false;
        cap.pixels.clear();
        cap.path.clear();
    }

    // Phase 2: Schedule a pre-present capture for figures with pending export.
    // The capture will execute inside end_frame() (do_capture_before_present),
    // before vkQueuePresentKHR, so the swapchain image contents are valid.
    if (!config_.headless && rt.active_figure && !rt.active_figure->export_req_.png_path.empty())
    {
        uint32_t ew  = rt.active_figure->export_req_.png_width > 0
                           ? rt.active_figure->export_req_.png_width
                           : rt.active_figure->width();
        uint32_t eh  = rt.active_figure->export_req_.png_height > 0
                           ? rt.active_figure->export_req_.png_height
                           : rt.active_figure->height();
        auto&    cap = rt.pending_png_capture;
        cap.pixels.resize(static_cast<size_t>(ew) * eh * 4);
        cap.path   = rt.active_figure->export_req_.png_path;
        cap.width  = ew;
        cap.height = eh;
        cap.active = true;

        auto* vk = (config_.backend == RenderBackend::Vulkan)
                       ? static_cast<VulkanBackend*>(backend_.get())
                       : nullptr;
#ifdef SPECTRA_USE_GLFW
        // Target the window that owns this figure so multi-window captures
        // read the correct swapchain image.
        WindowContext* target_wctx = nullptr;
        if (rt.window_mgr)
        {
            for (auto* wctx : rt.window_mgr->windows())
            {
                if (wctx->active_figure_id == rt.frame_state.active_figure_id)
                {
                    target_wctx = wctx;
                    break;
                }
            }
        }
        if (target_wctx)
            vk->request_framebuffer_capture(cap.pixels.data(), ew, eh, target_wctx);
        else
#endif
            if (vk)
            vk->request_framebuffer_capture(cap.pixels.data(), ew, eh);

        rt.active_figure->export_req_.png_path.clear();
        rt.active_figure->export_req_.png_width  = 0;
        rt.active_figure->export_req_.png_height = 0;
    }

    // Check animation duration termination
    if (rt.active_figure && rt.active_figure->anim_.duration > 0.0f
        && rt.scheduler.elapsed_seconds() >= rt.active_figure->anim_.duration
        && !rt.active_figure->anim_.loop)
    {
        rt.session.request_exit();
    }

#ifdef SPECTRA_USE_GLFW
    if (!rt.window_mgr && rt.glfw)
    {
        rt.glfw->poll_events();
        if (rt.glfw->should_close())
        {
            SPECTRA_LOG_INFO("main_loop", "Window closed, exiting loop");
            rt.session.request_exit();
        }
    }
#endif

    rt.frame_number++;

    auto  step_end = std::chrono::steady_clock::now();
    float ms       = std::chrono::duration<float, std::milli>(step_end - step_start).count();

    result.should_exit   = rt.session.should_exit();
    result.frame_time_ms = ms;
    result.frame_number  = rt.frame_number;

    return result;
}

// ─── shutdown_runtime ────────────────────────────────────────────────────────
void App::shutdown_runtime()
{
    if (!runtime_)
        return;

    auto& rt = *runtime_;

    SPECTRA_LOG_INFO("main_loop", "Exited main render loop");

    if (rt.mcp_server)
    {
        rt.mcp_server->stop();
        rt.mcp_server.reset();
    }

    if (rt.auto_server)
    {
        rt.auto_server->stop();
        rt.auto_server.reset();
    }

#ifdef SPECTRA_USE_FFMPEG
    if (rt.video_exporter)
    {
        rt.video_exporter->finish();
        rt.video_exporter.reset();
    }
#endif

    // Process exports for all figures (headless batch mode)
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        auto& f = *fig_ptr;

        if (config_.headless && !f.export_req_.png_path.empty())
        {
            uint32_t export_w = f.export_req_.png_width > 0 ? f.export_req_.png_width : f.width();
            uint32_t export_h =
                f.export_req_.png_height > 0 ? f.export_req_.png_height : f.height();

            bool needs_render =
                (&f != rt.active_figure) || (export_w != f.width()) || (export_h != f.height());

            if (needs_render)
            {
                backend_->create_offscreen_framebuffer(export_w, export_h);
                if (config_.backend == RenderBackend::Vulkan)
                    static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

                uint32_t orig_w  = f.config_.width;
                uint32_t orig_h  = f.config_.height;
                f.config_.width  = export_w;
                f.config_.height = export_h;
                f.compute_layout();

                if (backend_->begin_frame())
                {
                    renderer_->render_figure(f);
                    backend_->end_frame();
                }

                f.config_.width  = orig_w;
                f.config_.height = orig_h;
                f.compute_layout();
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
            if (backend_->readback_framebuffer(pixels.data(), export_w, export_h))
            {
                if (!ImageExporter::write_png(f.export_req_.png_path,
                                              pixels.data(),
                                              export_w,
                                              export_h))
                {
                    std::cerr << "[spectra] Failed to write PNG: " << f.export_req_.png_path
                              << "\n";
                }
            }
            else
            {
                std::cerr << "[spectra] Failed to readback framebuffer\n";
            }
        }

        if (!f.export_req_.svg_path.empty())
        {
            f.compute_layout();
            if (!SvgExporter::write_svg(f.export_req_.svg_path, f))
            {
                std::cerr << "[spectra] Failed to write SVG: " << f.export_req_.svg_path << "\n";
            }
        }
    }

#ifdef SPECTRA_USE_GLFW
    if (rt.window_mgr)
    {
        if (rt.glfw)
        {
            rt.glfw->release_window();
        }
        rt.window_mgr->shutdown();
        rt.window_mgr.reset();
    }
#endif

    if (backend_)
    {
        backend_->wait_idle();
    }

    try
    {
        runtime_.reset();
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_ERROR("shutdown",
                          std::string("Exception during AppRuntime destruction: ") + e.what());
        // Force-null the pointer to avoid double-free in ~App()
        runtime_.release();
    }
    catch (...)
    {
        runtime_.release();
    }
}

// ─── Accessors ───────────────────────────────────────────────────────────────
WindowUIContext* App::ui_context()
{
    if (!runtime_)
        return nullptr;

    auto& rt = *runtime_;

#ifdef SPECTRA_USE_GLFW
    if (rt.window_mgr)
    {
        for (auto* wctx : rt.window_mgr->windows())
        {
            if (wctx && wctx->ui_ctx)
            {
                rt.ui_ctx_ptr = wctx->ui_ctx.get();
                return rt.ui_ctx_ptr;
            }
        }

        // All windows were closed/destroyed this frame.
        rt.ui_ctx_ptr = nullptr;
        return nullptr;
    }
#endif

    if (rt.headless_ui_ctx)
        rt.ui_ctx_ptr = rt.headless_ui_ctx.get();

    return rt.ui_ctx_ptr;
}

SessionRuntime* App::session()
{
    return runtime_ ? &runtime_->session : nullptr;
}

#ifdef SPECTRA_USE_GLFW
WindowManager* App::window_manager()
{
    return runtime_ ? runtime_->window_mgr.get() : nullptr;
}
#endif

}   // namespace spectra

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
#include "render/renderer.hpp"
#include "render/vulkan/vk_backend.hpp"
#include "ui/commands/command_queue.hpp"
#include "session_runtime.hpp"
#include "window_runtime.hpp"
#include "window_ui_context.hpp"

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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace spectra
{

// ─── App ctor/dtor (must be here where AppRuntime is complete) ───────────────
App::App(const AppConfig& config) : config_(config)
{
    auto& logger = spectra::Logger::instance();
    logger.set_level(spectra::LogLevel::Debug);

    logger.add_sink(spectra::sinks::console_sink());

    try
    {
        std::string log_path =
            (std::filesystem::temp_directory_path() / "spectra_app.log").string();
        logger.add_sink(spectra::sinks::file_sink(log_path));
        SPECTRA_LOG_INFO("app", "Log file: " + log_path);
    }
    catch (const std::exception& e)
    {
        SPECTRA_LOG_WARN("app", "Failed to create log file: " + std::string(e.what()));
    }

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
    if (!backend_->init(config_.headless))
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize Vulkan backend");
        return;
    }

    renderer_ = std::make_unique<Renderer>(*backend_);
    if (!renderer_->init())
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize renderer");
        return;
    }

    SPECTRA_LOG_INFO("app", "Spectra application initialized successfully");
}

App::~App()
{
    runtime_.reset();
    renderer_.reset();
    if (backend_)
    {
        backend_->shutdown();
    }
}

// ─── AppRuntime: all state that lives across frames ──────────────────────────
struct App::AppRuntime
{
    CommandQueue   cmd_queue;
    FrameScheduler scheduler;
    Animator       animator;
    SessionRuntime session;

    FrameState frame_state;
    uint64_t   frame_number = 0;

    WindowUIContext* ui_ctx_ptr = nullptr;
    std::unique_ptr<WindowUIContext> headless_ui_ctx;

    Figure*   active_figure    = nullptr;
    FigureId  active_figure_id = INVALID_FIGURE_ID;

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

    AppRuntime(float fps, Backend& backend, Renderer& renderer, FigureRegistry& registry)
        : scheduler(fps), session(backend, renderer, registry)
    {
    }
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

    float init_fps = 60.0f;
    Figure* init_active = nullptr;
    FigureId init_active_id = INVALID_FIGURE_ID;

    if (!empty_start)
    {
        init_active_id = all_ids[0];
        init_active    = registry_.get(init_active_id);
        if (init_active && init_active->anim_fps_ > 0.0f)
            init_fps = init_active->anim_fps_;
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
        init_active ? static_cast<bool>(init_active->anim_on_frame_) : false;

    if (!config_.headless)
    {
        rt.scheduler.set_mode(FrameScheduler::Mode::VSync);
    }

#ifdef SPECTRA_USE_FFMPEG
    rt.is_recording = init_active && !init_active->video_record_path_.empty();
#else
    if (init_active && !init_active->video_record_path_.empty())
    {
        std::cerr << "[spectra] Video recording requested but SPECTRA_USE_FFMPEG is not enabled\n";
    }
#endif

#ifdef SPECTRA_USE_FFMPEG
    if (rt.is_recording)
    {
        VideoExporter::Config vcfg;
        vcfg.output_path = init_active->video_record_path_;
        vcfg.width       = init_active->width();
        vcfg.height      = init_active->height();
        vcfg.fps         = init_active->anim_fps_;
        rt.video_exporter = std::make_unique<VideoExporter>(vcfg);
        if (!rt.video_exporter->is_open())
        {
            std::cerr << "[spectra] Failed to open video exporter for: "
                      << init_active->video_record_path_ << "\n";
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

            rt.window_mgr = std::make_unique<WindowManager>();
            rt.window_mgr->init(static_cast<VulkanBackend*>(backend_.get()),
                                &registry_,
                                renderer_.get());

            rt.window_mgr->set_tab_detach_handler(
                [&session = rt.session](FigureId           fid,
                                        uint32_t           w,
                                        uint32_t           h,
                                        const std::string& title,
                                        int                sx,
                                        int                sy) {
                    session.queue_detach({fid, w, h, title, sx, sy});
                });
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
                rt.window_mgr->create_first_window_with_ui(rt.glfw->native_window(), first_group);

            if (initial_wctx && initial_wctx->ui_ctx)
            {
                rt.ui_ctx_ptr = initial_wctx->ui_ctx.get();
            }

            for (size_t gi = 1; gi < window_groups.size(); ++gi)
            {
                auto& group = window_groups[gi];
                if (group.empty())
                    continue;

                auto*    fig0 = registry_.get(group[0]);
                uint32_t w    = fig0 ? fig0->width() : 800;
                uint32_t h    = fig0 ? fig0->height() : 600;

                auto* new_wctx = rt.window_mgr->create_window_with_ui(w, h, "Spectra", group[0]);

                if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                {
                    for (size_t fi = 1; fi < group.size(); ++fi)
                    {
                        new_wctx->ui_ctx->fig_mgr->add_figure(group[fi], FigureState{});
                        new_wctx->assigned_figures.push_back(group[fi]);
                    }
                }
            }
        }
    }
#endif

    if (!rt.ui_ctx_ptr)
    {
        rt.headless_ui_ctx                = std::make_unique<WindowUIContext>();
        rt.headless_ui_ctx->fig_mgr_owned = std::make_unique<FigureManager>(registry_);
        rt.headless_ui_ctx->fig_mgr       = rt.headless_ui_ctx->fig_mgr_owned.get();
        rt.ui_ctx_ptr                     = rt.headless_ui_ctx.get();
    }

#ifdef SPECTRA_USE_IMGUI
    auto& imgui_ui              = rt.ui_ctx_ptr->imgui_ui;
    auto& figure_tabs           = rt.ui_ctx_ptr->figure_tabs;
    auto& dock_system           = rt.ui_ctx_ptr->dock_system;
    auto& timeline_editor       = rt.ui_ctx_ptr->timeline_editor;
    auto& keyframe_interpolator = rt.ui_ctx_ptr->keyframe_interpolator;
    auto& curve_editor          = rt.ui_ctx_ptr->curve_editor;
    auto& home_limits           = rt.ui_ctx_ptr->home_limits;
    auto& cmd_registry          = rt.ui_ctx_ptr->cmd_registry;
    auto& shortcut_mgr          = rt.ui_ctx_ptr->shortcut_mgr;
    auto& cmd_palette           = rt.ui_ctx_ptr->cmd_palette;
    auto& tab_drag_controller   = rt.ui_ctx_ptr->tab_drag_controller;
    auto& fig_mgr               = *rt.ui_ctx_ptr->fig_mgr;
    auto& input_handler         = rt.ui_ctx_ptr->input_handler;

    if (knob_manager_ && !knob_manager_->empty() && imgui_ui)
    {
        imgui_ui->set_knob_manager(knob_manager_);
    }

    timeline_editor.set_interpolator(&keyframe_interpolator);
    curve_editor.set_interpolator(&keyframe_interpolator);
    if (init_active)
    {
        if (init_active->anim_duration_ > 0.0f)
        {
            timeline_editor.set_duration(init_active->anim_duration_);
        }
        else if (rt.frame_state.has_animation)
        {
            timeline_editor.set_duration(60.0f);
        }
        if (init_active->anim_loop_)
        {
            timeline_editor.set_loop_mode(LoopMode::Loop);
        }
        if (init_active->anim_fps_ > 0.0f)
        {
            timeline_editor.set_fps(init_active->anim_fps_);
        }
        if (rt.frame_state.has_animation)
        {
            timeline_editor.play();
        }
    }

    shortcut_mgr.set_command_registry(&cmd_registry);
    shortcut_mgr.register_defaults();
    cmd_palette.set_command_registry(&cmd_registry);
    cmd_palette.set_shortcut_manager(&shortcut_mgr);

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

        // Clear cached figure/axes pointers when a figure is closed,
        // preventing dangling pointer dereference in legend/crosshair/input/inspector rendering.
        {
            auto* di       = rt.ui_ctx_ptr->data_interaction.get();
            auto* ih       = &rt.ui_ctx_ptr->input_handler;
            auto* imgui_ui = rt.ui_ctx_ptr->imgui_ui.get();
            fig_mgr.set_on_figure_closed(
                [di, ih, imgui_ui, this](FigureId id)
                {
                    auto* fig = registry_.get(id);
                    if (fig)
                    {
                        if (di) di->clear_figure_cache(fig);
                        ih->clear_figure_cache(fig);
                        if (imgui_ui) imgui_ui->clear_figure_cache(fig);
                    }
                });
        }

        cmd_palette.set_body_font(nullptr);
        cmd_palette.set_heading_font(nullptr);

        {
            CommandBindings cb;
            cb.ui_ctx           = rt.ui_ctx_ptr;
            cb.registry         = &registry_;
            cb.active_figure    = &rt.active_figure;
            cb.active_figure_id = &rt.active_figure_id;
            cb.session          = &rt.session;
    #ifdef SPECTRA_USE_GLFW
            cb.window_mgr = rt.window_mgr.get();
    #endif
            register_standard_commands(cb);
        }
    }
#endif

    rt.scheduler.reset();

    // Capture initial axes limits for Home button
    auto& home_limits_ref = rt.ui_ctx_ptr->home_limits;
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        for (auto& ax : fig_ptr->axes_mut())
        {
            if (ax)
                home_limits_ref[ax.get()] = {ax->x_limits(), ax->y_limits()};
        }
    }

    rt.last_step_time = std::chrono::steady_clock::now();
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

    // Process pending PNG export for the active figure (interactive mode)
    if (!config_.headless && rt.active_figure && !rt.active_figure->png_export_path_.empty())
    {
        uint32_t ew = rt.active_figure->png_export_width_ > 0
                          ? rt.active_figure->png_export_width_
                          : rt.active_figure->width();
        uint32_t eh = rt.active_figure->png_export_height_ > 0
                          ? rt.active_figure->png_export_height_
                          : rt.active_figure->height();
        std::vector<uint8_t> px(static_cast<size_t>(ew) * eh * 4);
        if (backend_->readback_framebuffer(px.data(), ew, eh))
        {
            if (ImageExporter::write_png(rt.active_figure->png_export_path_, px.data(), ew, eh))
            {
                SPECTRA_LOG_INFO("export", "Saved PNG: " + rt.active_figure->png_export_path_);
            }
            else
            {
                SPECTRA_LOG_ERROR("export",
                                  "Failed to write PNG: " + rt.active_figure->png_export_path_);
            }
        }
        else
        {
            SPECTRA_LOG_ERROR("export", "Failed to readback framebuffer for PNG export");
        }
        rt.active_figure->png_export_path_.clear();
        rt.active_figure->png_export_width_  = 0;
        rt.active_figure->png_export_height_ = 0;
    }

    // Check animation duration termination
    if (rt.active_figure && rt.active_figure->anim_duration_ > 0.0f
        && rt.scheduler.elapsed_seconds() >= rt.active_figure->anim_duration_
        && !rt.active_figure->anim_loop_)
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

    auto step_end = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(step_end - step_start).count();

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

        if (config_.headless && !f.png_export_path_.empty())
        {
            uint32_t export_w = f.png_export_width_ > 0 ? f.png_export_width_ : f.width();
            uint32_t export_h = f.png_export_height_ > 0 ? f.png_export_height_ : f.height();

            bool needs_render =
                (&f != rt.active_figure) || (export_w != f.width()) || (export_h != f.height());

            if (needs_render)
            {
                backend_->create_offscreen_framebuffer(export_w, export_h);
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
                if (!ImageExporter::write_png(f.png_export_path_,
                                              pixels.data(),
                                              export_w,
                                              export_h))
                {
                    std::cerr << "[spectra] Failed to write PNG: " << f.png_export_path_ << "\n";
                }
            }
            else
            {
                std::cerr << "[spectra] Failed to readback framebuffer\n";
            }
        }

        if (!f.svg_export_path_.empty())
        {
            f.compute_layout();
            if (!SvgExporter::write_svg(f.svg_export_path_, f))
            {
                std::cerr << "[spectra] Failed to write SVG: " << f.svg_export_path_ << "\n";
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

    runtime_.reset();
}

// ─── Accessors ───────────────────────────────────────────────────────────────
WindowUIContext* App::ui_context()
{
    return runtime_ ? runtime_->ui_ctx_ptr : nullptr;
}

SessionRuntime* App::session()
{
    return runtime_ ? &runtime_->session : nullptr;
}

}   // namespace spectra

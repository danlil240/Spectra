// app_inproc.cpp — In-process (single-process) run implementation.
// All windows share one GPU context, one Vulkan device, one process.

#include <spectra/animator.hpp>
#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "command_queue.hpp"
#include "session_runtime.hpp"
#include "window_runtime.hpp"
#include "window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "glfw_adapter.hpp"
    #include "window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

    #include "icons.hpp"
    #include "register_commands.hpp"
    #include "theme.hpp"
    #include "undoable_property.hpp"
    #include "workspace.hpp"
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

void App::run_inproc()
{
    if (!backend_ || !renderer_)
    {
        std::cerr << "[spectra] Cannot run: backend or renderer not initialized\n";
        return;
    }

    if (registry_.count() == 0)
    {
        return;
    }

    // Multi-figure support - track active figure via FrameState
    auto all_ids = registry_.all_ids();
    auto window_groups = compute_window_groups();
    FrameState frame_state;
    frame_state.active_figure_id = all_ids[0];
    frame_state.active_figure = registry_.get(frame_state.active_figure_id);
    Figure* active_figure = frame_state.active_figure;
    FigureId& active_figure_id = frame_state.active_figure_id;

    CommandQueue cmd_queue;
    FrameScheduler scheduler(active_figure->anim_fps_);
    // Windowed mode uses VK_PRESENT_MODE_FIFO_KHR (VSync) which already
    // provides frame pacing via vkQueuePresentKHR blocking.  Adding
    // FrameScheduler sleep on top causes double-pacing and periodic stutters.
    // Only use TargetFPS sleep for headless mode where there's no swapchain.
    if (!config_.headless)
    {
        scheduler.set_mode(FrameScheduler::Mode::VSync);
    }
    Animator animator;
    SessionRuntime session(*backend_, *renderer_, registry_);

    frame_state.has_animation = static_cast<bool>(active_figure->anim_on_frame_);
    bool& has_animation = frame_state.has_animation;

#ifdef SPECTRA_USE_FFMPEG
    bool is_recording = !active_figure->video_record_path_.empty();
#else
    if (!active_figure->video_record_path_.empty())
    {
        std::cerr << "[spectra] Video recording requested but SPECTRA_USE_FFMPEG is not enabled\n";
    }
#endif

#ifdef SPECTRA_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t> video_frame_pixels;
    if (is_recording)
    {
        VideoExporter::Config vcfg;
        vcfg.output_path = active_figure->video_record_path_;
        vcfg.width = active_figure->width();
        vcfg.height = active_figure->height();
        vcfg.fps = active_figure->anim_fps_;
        video_exporter = std::make_unique<VideoExporter>(vcfg);
        if (!video_exporter->is_open())
        {
            std::cerr << "[spectra] Failed to open video exporter for: "
                      << active_figure->video_record_path_ << "\n";
            video_exporter.reset();
        }
        else
        {
            video_frame_pixels.resize(static_cast<size_t>(active_figure->width())
                                      * active_figure->height() * 4);
        }
        // Recording always runs headless
        if (!config_.headless)
        {
            config_.headless = true;
        }
    }
#endif

    // ── Per-window UI subsystem bundle ─────────────────────────────────
    // The UI context is created by WindowManager::init_window_ui() for
    // windowed mode, or manually for headless mode.
    // ui_ctx_ptr is set after window creation and used for app-level wiring.
    WindowUIContext* ui_ctx_ptr = nullptr;

    // Headless mode: create a standalone UI context (no GLFW window)
    std::unique_ptr<WindowUIContext> headless_ui_ctx;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<GlfwAdapter> glfw;
    std::unique_ptr<WindowManager> window_mgr;

    if (!config_.headless)
    {
        glfw = std::make_unique<GlfwAdapter>();
        if (!glfw->init(active_figure->width(), active_figure->height(), "Spectra"))
        {
            std::cerr << "[spectra] Failed to create GLFW window\n";
            glfw.reset();
        }
        else
        {
            // Create Vulkan surface from GLFW window
            backend_->create_surface(glfw->native_window());
            backend_->create_swapchain(active_figure->width(), active_figure->height());

            // Initialize WindowManager and create windows based on figure grouping.
            // The first group goes to the primary GLFW window; additional groups
            // each get their own OS window via create_window_with_ui().
            window_mgr = std::make_unique<WindowManager>();
            window_mgr->init(
                static_cast<VulkanBackend*>(backend_.get()), &registry_, renderer_.get());

            // Set tab drag handlers BEFORE creating windows so all windows get them
            window_mgr->set_tab_detach_handler(
                [&session](
                    FigureId fid, uint32_t w, uint32_t h, const std::string& title, int sx, int sy)
                {
                    session.queue_detach({fid, w, h, title, sx, sy});
                });
            window_mgr->set_tab_move_handler(
                [&session](FigureId fid, uint32_t target_wid, int drop_zone, float local_x, float local_y) {
                    session.queue_move({fid, target_wid, drop_zone, local_x, local_y});
                });

            // First group → primary window
            auto* initial_wctx =
                window_mgr->create_first_window_with_ui(glfw->native_window(), window_groups[0]);

            if (initial_wctx && initial_wctx->ui_ctx)
            {
                ui_ctx_ptr = initial_wctx->ui_ctx.get();
            }

            // Additional groups → new OS windows
            for (size_t gi = 1; gi < window_groups.size(); ++gi)
            {
                auto& group = window_groups[gi];
                if (group.empty())
                    continue;

                auto* fig0 = registry_.get(group[0]);
                uint32_t w = fig0 ? fig0->width() : 800;
                uint32_t h = fig0 ? fig0->height() : 600;

                auto* new_wctx = window_mgr->create_window_with_ui(w, h, "Spectra", group[0]);

                if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                {
                    // Add remaining figures in this group as tabs
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

    // Headless fallback: create a minimal UI context
    if (!ui_ctx_ptr)
    {
        headless_ui_ctx = std::make_unique<WindowUIContext>();
        headless_ui_ctx->fig_mgr_owned = std::make_unique<FigureManager>(registry_);
        headless_ui_ctx->fig_mgr = headless_ui_ctx->fig_mgr_owned.get();
        ui_ctx_ptr = headless_ui_ctx.get();
    }

#ifdef SPECTRA_USE_IMGUI
    // Convenience aliases — reference members of ui_ctx_ptr.
    auto& imgui_ui = ui_ctx_ptr->imgui_ui;
    auto& data_interaction = ui_ctx_ptr->data_interaction;
    auto& figure_tabs = ui_ctx_ptr->figure_tabs;
    auto& box_zoom_overlay = ui_ctx_ptr->box_zoom_overlay;
    auto& dock_system = ui_ctx_ptr->dock_system;
    auto& dock_tab_sync_guard = ui_ctx_ptr->dock_tab_sync_guard;
    auto& axis_link_mgr = ui_ctx_ptr->axis_link_mgr;
    auto& timeline_editor = ui_ctx_ptr->timeline_editor;
    auto& keyframe_interpolator = ui_ctx_ptr->keyframe_interpolator;
    auto& curve_editor = ui_ctx_ptr->curve_editor;
    auto& mode_transition = ui_ctx_ptr->mode_transition;
    auto& is_in_3d_mode = ui_ctx_ptr->is_in_3d_mode;
    auto& saved_3d_camera = ui_ctx_ptr->saved_3d_camera;
    auto& home_limits = ui_ctx_ptr->home_limits;
    auto& cmd_registry = ui_ctx_ptr->cmd_registry;
    auto& shortcut_mgr = ui_ctx_ptr->shortcut_mgr;
    auto& undo_mgr = ui_ctx_ptr->undo_mgr;
    auto& cmd_palette = ui_ctx_ptr->cmd_palette;
    auto& tab_drag_controller = ui_ctx_ptr->tab_drag_controller;
    auto& fig_mgr = *ui_ctx_ptr->fig_mgr;
    auto& input_handler = ui_ctx_ptr->input_handler;
    auto& anim_controller = ui_ctx_ptr->anim_controller;

    // Point ImGui at the external knob manager (if provided by easy API or user).
    // This lets ImGui directly modify the same Knob objects the user holds
    // references to, so knob.value updates are visible immediately.
    if (knob_manager_ && !knob_manager_->empty() && imgui_ui)
    {
        imgui_ui->set_knob_manager(knob_manager_);
    }

    // Sync timeline with figure animation settings
    timeline_editor.set_interpolator(&keyframe_interpolator);
    curve_editor.set_interpolator(&keyframe_interpolator);
    if (active_figure->anim_duration_ > 0.0f)
    {
        timeline_editor.set_duration(active_figure->anim_duration_);
    }
    else if (has_animation)
    {
        timeline_editor.set_duration(60.0f);
    }
    if (active_figure->anim_loop_)
    {
        timeline_editor.set_loop_mode(LoopMode::Loop);
    }
    if (active_figure->anim_fps_ > 0.0f)
    {
        timeline_editor.set_fps(active_figure->anim_fps_);
    }
    if (has_animation)
    {
        timeline_editor.play();
    }

    shortcut_mgr.set_command_registry(&cmd_registry);
    shortcut_mgr.register_defaults();
    cmd_palette.set_command_registry(&cmd_registry);
    cmd_palette.set_shortcut_manager(&shortcut_mgr);

    #ifdef SPECTRA_USE_GLFW
    if (window_mgr)
    {
        tab_drag_controller.set_window_manager(window_mgr.get());
        input_handler.set_figure(active_figure);
        if (!active_figure->axes().empty() && active_figure->axes()[0])
        {
            input_handler.set_active_axes(active_figure->axes()[0].get());
            auto& vp = active_figure->axes()[0]->viewport();
            input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
    }
    #endif
#endif

    if (config_.headless)
    {
        backend_->create_offscreen_framebuffer(active_figure->width(), active_figure->height());
        static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();
    }

#ifdef SPECTRA_USE_IMGUI
    // App-specific callback wiring on top of what init_window_ui() already set up.
    // init_window_ui() creates ImGui, FigureManager, TabBar, DockSystem,
    // InputHandler, DataInteraction, etc.  Here we wire app-level callbacks
    // that need access to SessionRuntime, registry_, and command registration.
    if (figure_tabs && !config_.headless)
    {
        // Tab context menu: Split Right / Split Down
        figure_tabs->set_tab_split_right_callback(
            [&dock_system, &fig_mgr, this](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                FigureId id = fig_mgr.figure_ids()[pos];
                if (!registry_.get(id))
                    return;
                FigureId new_fig = fig_mgr.duplicate_figure(id);
                if (new_fig == INVALID_FIGURE_ID)
                    return;
                dock_system.split_figure_right(id, new_fig);
                dock_system.set_active_figure_index(id);
            });

        figure_tabs->set_tab_split_down_callback(
            [&dock_system, &fig_mgr, this](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                FigureId id = fig_mgr.figure_ids()[pos];
                if (!registry_.get(id))
                    return;
                FigureId new_fig = fig_mgr.duplicate_figure(id);
                if (new_fig == INVALID_FIGURE_ID)
                    return;
                dock_system.split_figure_down(id, new_fig);
                dock_system.set_active_figure_index(id);
            });

        // Tab detach: drag tab outside window or context menu "Detach to Window"
        figure_tabs->set_tab_detach_callback(
            [&fig_mgr, &session, this](size_t pos, float screen_x, float screen_y)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                FigureId id = fig_mgr.figure_ids()[pos];
                auto* fig = registry_.get(id);
                if (!fig)
                    return;

                if (fig_mgr.count() <= 1)
                    return;

                uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(id);

                session.queue_detach({id,
                                      win_w,
                                      win_h,
                                      title,
                                      static_cast<int>(screen_x),
                                      static_cast<int>(screen_y)});
            });
    }

    if (!config_.headless && ui_ctx_ptr)
    {
        // App-specific wiring that init_window_ui() doesn't handle:
        // TabDragController drop-outside needs session.queue_detach(),
        // pane tab detach needs session.queue_detach(),
        // and command registrations need access to App members.

        // TabDragController drop-outside callback: detach to new window (deferred)
        tab_drag_controller.set_on_drop_outside(
            [&fig_mgr, &session, this](FigureId index, float screen_x, float screen_y)
            {
                auto* fig = registry_.get(index);
                if (!fig)
                    return;

                uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(index);

                session.queue_detach({index,
                                      win_w,
                                      win_h,
                                      title,
                                      static_cast<int>(screen_x),
                                      static_cast<int>(screen_y)});
            });

        // Pane tab detach callback (needs session.queue_detach)
        if (imgui_ui)
        {
            imgui_ui->set_pane_tab_detach_cb(
                [&fig_mgr, &session, this](FigureId index, float screen_x, float screen_y)
                {
                    auto* fig = registry_.get(index);
                    if (!fig)
                        return;

                    uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                    uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                    std::string title = fig_mgr.get_title(index);

                    session.queue_detach({index,
                                          win_w,
                                          win_h,
                                          title,
                                          static_cast<int>(screen_x),
                                          static_cast<int>(screen_y)});
                });
        }

        cmd_palette.set_body_font(nullptr);
        cmd_palette.set_heading_font(nullptr);

        // ─── Register standard commands (shared with spectra-window agent) ──
        {
            CommandBindings cb;
            cb.ui_ctx = ui_ctx_ptr;
            cb.registry = &registry_;
            cb.active_figure = &active_figure;
            cb.active_figure_id = &active_figure_id;
            cb.session = &session;
    #ifdef SPECTRA_USE_GLFW
            cb.window_mgr = window_mgr.get();
    #endif
            register_standard_commands(cb);
        }
    }
#endif

    scheduler.reset();

    // Capture initial axes limits for Home button (restore original view)
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        for (auto& ax : fig_ptr->axes_mut())
        {
            if (ax)
                home_limits[ax.get()] = {ax->x_limits(), ax->y_limits()};
        }
    }

    while (!session.should_exit())
    {
        // ── Session tick: scheduler, commands, animations, window loop, detach ──
        session.tick(scheduler,
                     animator,
                     cmd_queue,
                     config_.headless,
                     ui_ctx_ptr,
#ifdef SPECTRA_USE_GLFW
                     window_mgr.get(),
#endif
                     frame_state);
        active_figure = frame_state.active_figure;

#ifdef SPECTRA_USE_FFMPEG
        // Capture frame for video recording
        if (video_exporter && video_exporter->is_open() && active_figure)
        {
            if (backend_->readback_framebuffer(
                    video_frame_pixels.data(), active_figure->width(), active_figure->height()))
            {
                video_exporter->write_frame(video_frame_pixels.data());
            }
        }
#endif

        // Process pending PNG export for the active figure (interactive mode)
        if (!config_.headless && active_figure && !active_figure->png_export_path_.empty())
        {
            uint32_t ew = active_figure->png_export_width_ > 0 ? active_figure->png_export_width_
                                                               : active_figure->width();
            uint32_t eh = active_figure->png_export_height_ > 0 ? active_figure->png_export_height_
                                                                : active_figure->height();
            std::vector<uint8_t> px(static_cast<size_t>(ew) * eh * 4);
            if (backend_->readback_framebuffer(px.data(), ew, eh))
            {
                if (ImageExporter::write_png(active_figure->png_export_path_, px.data(), ew, eh))
                {
                    SPECTRA_LOG_INFO("export", "Saved PNG: " + active_figure->png_export_path_);
                }
                else
                {
                    SPECTRA_LOG_ERROR("export",
                                      "Failed to write PNG: " + active_figure->png_export_path_);
                }
            }
            else
            {
                SPECTRA_LOG_ERROR("export", "Failed to readback framebuffer for PNG export");
            }
            active_figure->png_export_path_.clear();
            active_figure->png_export_width_ = 0;
            active_figure->png_export_height_ = 0;
        }

        // Check animation duration termination
        if (active_figure && active_figure->anim_duration_ > 0.0f
            && scheduler.elapsed_seconds() >= active_figure->anim_duration_
            && !active_figure->anim_loop_)
        {
            session.request_exit();
        }

#ifdef SPECTRA_USE_GLFW
        // Fallback: GlfwAdapter without WindowManager (legacy path)
        if (!window_mgr && glfw)
        {
            glfw->poll_events();
            if (glfw->should_close())
            {
                SPECTRA_LOG_INFO("main_loop", "Window closed, exiting loop");
                session.request_exit();
            }
        }
#endif
    }

    SPECTRA_LOG_INFO("main_loop", "Exited main render loop");

#ifdef SPECTRA_USE_FFMPEG
    // Finalize video recording
    if (video_exporter)
    {
        video_exporter->finish();
        video_exporter.reset();
    }
#endif

    // Process exports for all figures (headless batch mode)
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr)
            continue;
        auto& f = *fig_ptr;

        // Export PNG if requested (headless mode)
        if (config_.headless && !f.png_export_path_.empty())
        {
            uint32_t export_w = f.png_export_width_ > 0 ? f.png_export_width_ : f.width();
            uint32_t export_h = f.png_export_height_ > 0 ? f.png_export_height_ : f.height();

            // Render this figure into an offscreen framebuffer at the target resolution
            bool needs_render =
                (&f != active_figure) || (export_w != f.width()) || (export_h != f.height());

            if (needs_render)
            {
                backend_->create_offscreen_framebuffer(export_w, export_h);
                static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

                // Temporarily override figure dimensions for layout
                uint32_t orig_w = f.config_.width;
                uint32_t orig_h = f.config_.height;
                f.config_.width = export_w;
                f.config_.height = export_h;
                f.compute_layout();

                if (backend_->begin_frame())
                {
                    renderer_->render_figure(f);
                    backend_->end_frame();
                }

                // Restore original dimensions
                f.config_.width = orig_w;
                f.config_.height = orig_h;
                f.compute_layout();
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
            if (backend_->readback_framebuffer(pixels.data(), export_w, export_h))
            {
                if (!ImageExporter::write_png(
                        f.png_export_path_, pixels.data(), export_w, export_h))
                {
                    std::cerr << "[spectra] Failed to write PNG: " << f.png_export_path_ << "\n";
                }
            }
            else
            {
                std::cerr << "[spectra] Failed to readback framebuffer\n";
            }
        }

        // Export SVG if requested (works for any figure, no GPU needed)
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
    if (window_mgr)
    {
        // Release GlfwAdapter's copy of the initial GLFW window handle.
        // WindowManager owns the actual glfwDestroyWindow call for all
        // windows (including the initial one).  Without this release,
        // GlfwAdapter::~GlfwAdapter() would double-destroy the same handle
        // → X11 BadWindow / segfault.
        if (glfw)
        {
            glfw->release_window();
        }
        window_mgr->shutdown();
        window_mgr.reset();
    }
    // GlfwAdapter destructor handles glfwTerminate().
#endif

    // Ensure all GPU work is complete before destructors clean up resources
    if (backend_)
    {
        backend_->wait_idle();
    }
}

// ─── render_secondary_window ──────────────────────────────────────────────────
// Render a secondary window (no ImGui, figure-only).
void App::render_secondary_window(WindowContext* wctx)
{
    if (!wctx || wctx->should_close)
        return;

    auto* fig = registry_.get(wctx->assigned_figure_index);
    if (!fig)
        return;

    auto* vk = static_cast<VulkanBackend*>(backend_.get());

    // Handle per-window resize with debounce
    static constexpr auto SECONDARY_RESIZE_DEBOUNCE = std::chrono::milliseconds(50);
    if (wctx->needs_resize)
    {
        auto elapsed = std::chrono::steady_clock::now() - wctx->resize_time;
        if (elapsed >= SECONDARY_RESIZE_DEBOUNCE && wctx->pending_width > 0
            && wctx->pending_height > 0)
        {
            // Use ImGui-aware swapchain recreation if this window has an ImGui context
            vk->recreate_swapchain_for_with_imgui(*wctx, wctx->pending_width, wctx->pending_height);
            fig->config_.width = wctx->pending_width;
            fig->config_.height = wctx->pending_height;
            wctx->needs_resize = false;
        }
    }

    // Switch active window to this secondary context
    vk->set_active_window(wctx);

    bool sec_ok = backend_->begin_frame();
    if (!sec_ok && wctx->pending_width > 0 && wctx->pending_height > 0)
    {
        // Swapchain out of date — recreate and retry
        vk->recreate_swapchain_for_with_imgui(*wctx, wctx->pending_width, wctx->pending_height);
        vk->clear_swapchain_dirty();
        sec_ok = backend_->begin_frame();
    }

    if (sec_ok)
    {
        renderer_->begin_render_pass();
        renderer_->render_figure_content(*fig);
        renderer_->end_render_pass();
        backend_->end_frame();

        // Post-present recovery: if present returned OUT_OF_DATE, recreate
        // immediately so the next frame's begin_frame() doesn't loop.
        if (wctx->swapchain_invalidated)
        {
            vk->recreate_swapchain_for_with_imgui(*wctx, wctx->pending_width, wctx->pending_height);
            vk->clear_swapchain_dirty();
        }
    }
}

}  // namespace spectra

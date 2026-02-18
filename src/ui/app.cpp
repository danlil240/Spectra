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
    #include "theme.hpp"
    #include "undoable_property.hpp"
    #include "workspace.hpp"
#endif

#ifdef SPECTRA_MULTIPROC
    #include "../ipc/codec.hpp"
    #include "../ipc/message.hpp"
    #include "../ipc/transport.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>

namespace spectra
{

// ─── App ─────────────────────────────────────────────────────────────────────

App::App(const AppConfig& config) : config_(config)
{
    // Initialize logger for debugging
    // Set to Trace for maximum debugging, Debug for normal debugging, Info for production
    auto& logger = spectra::Logger::instance();
    logger.set_level(spectra::LogLevel::Debug);  // Change to Trace to see all frame-by-frame logs

    // Add console sink with timestamps
    logger.add_sink(spectra::sinks::console_sink());

    // Add file sink in temp directory with error handling
    try
    {
        std::string log_path = std::filesystem::temp_directory_path() / "spectra_app.log";
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

    // Create Vulkan backend
    backend_ = std::make_unique<VulkanBackend>();
    if (!backend_->init(config_.headless))
    {
        SPECTRA_LOG_ERROR("app", "Failed to initialize Vulkan backend");
        return;
    }

    // Create renderer
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
    // Destroy renderer before backend (renderer holds backend reference)
    renderer_.reset();
    if (backend_)
    {
        backend_->shutdown();
    }
}

Figure& App::figure(const FigureConfig& config)
{
    auto id = registry_.register_figure(std::make_unique<Figure>(config));
    return *registry_.get(id);
}

void App::run()
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
    FrameState frame_state;
    frame_state.active_figure_id = all_ids[0];
    frame_state.active_figure = registry_.get(frame_state.active_figure_id);
    Figure* active_figure = frame_state.active_figure;
    FigureId& active_figure_id = frame_state.active_figure_id;

    CommandQueue cmd_queue;
    FrameScheduler scheduler(active_figure->anim_fps_);
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

            // Initialize WindowManager and create the first window with full UI.
            // create_first_window_with_ui() takes ownership of the backend's
            // initial WindowContext, creates ImGui + FigureManager + all UI
            // subsystems via init_window_ui(), and installs GLFW callbacks.
            // This is the SAME path secondary windows use.
            window_mgr = std::make_unique<WindowManager>();
            window_mgr->init(static_cast<VulkanBackend*>(backend_.get()),
                             &registry_, renderer_.get());
            auto* initial_wctx = window_mgr->create_first_window_with_ui(
                glfw->native_window(), all_ids);

            if (initial_wctx && initial_wctx->ui_ctx)
            {
                ui_ctx_ptr = initial_wctx->ui_ctx.get();
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
    auto& imgui_ui        = ui_ctx_ptr->imgui_ui;
    auto& data_interaction = ui_ctx_ptr->data_interaction;
    auto& figure_tabs     = ui_ctx_ptr->figure_tabs;
    auto& box_zoom_overlay = ui_ctx_ptr->box_zoom_overlay;
    auto& dock_system     = ui_ctx_ptr->dock_system;
    auto& dock_tab_sync_guard = ui_ctx_ptr->dock_tab_sync_guard;
    auto& axis_link_mgr   = ui_ctx_ptr->axis_link_mgr;
    auto& timeline_editor = ui_ctx_ptr->timeline_editor;
    auto& keyframe_interpolator = ui_ctx_ptr->keyframe_interpolator;
    auto& curve_editor    = ui_ctx_ptr->curve_editor;
    auto& mode_transition = ui_ctx_ptr->mode_transition;
    auto& is_in_3d_mode   = ui_ctx_ptr->is_in_3d_mode;
    auto& saved_3d_camera = ui_ctx_ptr->saved_3d_camera;
    auto& home_limits     = ui_ctx_ptr->home_limits;
    auto& cmd_registry    = ui_ctx_ptr->cmd_registry;
    auto& shortcut_mgr    = ui_ctx_ptr->shortcut_mgr;
    auto& undo_mgr        = ui_ctx_ptr->undo_mgr;
    auto& cmd_palette     = ui_ctx_ptr->cmd_palette;
    auto& tab_drag_controller = ui_ctx_ptr->tab_drag_controller;
    auto& fig_mgr = *ui_ctx_ptr->fig_mgr;
    auto& input_handler   = ui_ctx_ptr->input_handler;
    auto& anim_controller = ui_ctx_ptr->anim_controller;

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
                if (pos >= fig_mgr.figure_ids().size()) return;
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
                if (pos >= fig_mgr.figure_ids().size()) return;
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
                if (pos >= fig_mgr.figure_ids().size()) return;
                FigureId id = fig_mgr.figure_ids()[pos];
                auto* fig = registry_.get(id);
                if (!fig)
                    return;

                if (fig_mgr.count() <= 1)
                    return;

                uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(id);

                session.queue_detach({id, win_w, win_h, title,
                    static_cast<int>(screen_x), static_cast<int>(screen_y)});
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

                if (fig_mgr.count() <= 1)
                    return;

                uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(index);

                session.queue_detach({index, win_w, win_h, title,
                    static_cast<int>(screen_x), static_cast<int>(screen_y)});
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

                    if (fig_mgr.count() <= 1)
                        return;

                    uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                    uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                    std::string title = fig_mgr.get_title(index);

                    session.queue_detach({index, win_w, win_h, title,
                        static_cast<int>(screen_x), static_cast<int>(screen_y)});
                });
        }

        cmd_palette.set_body_font(nullptr);
        cmd_palette.set_heading_font(nullptr);

        // ─── Register 30+ commands ──────────────────────────────────────
        // View commands
        cmd_registry.register_command(
            "view.reset",
            "Reset View",
            [&]()
            {
                auto before = capture_figure_axes(*active_figure);
                for (auto& ax : active_figure->axes_mut())
                {
                    if (ax)
                    {
                        auto old_xlim = ax->x_limits();
                        auto old_ylim = ax->y_limits();
                        ax->auto_fit();
                        AxisLimits target_x = ax->x_limits();
                        AxisLimits target_y = ax->y_limits();
                        ax->xlim(old_xlim.min, old_xlim.max);
                        ax->ylim(old_ylim.min, old_ylim.max);
                        anim_controller.animate_axis_limits(
                            *ax, target_x, target_y, 0.25f, ease::ease_out);
                    }
                }
                auto after = capture_figure_axes(*active_figure);
                // Push undo with the target (post-animation) limits
                undo_mgr.push(UndoAction{"Reset view",
                                         [before]() { restore_figure_axes(before); },
                                         [after]() { restore_figure_axes(after); }});
            },
            "R",
            "View",
            static_cast<uint16_t>(ui::Icon::Home));

        cmd_registry.register_command(
            "view.autofit",
            "Auto-Fit Active Axes",
            [&]()
            {
                if (auto* ax = input_handler.active_axes())
                {
                    auto old_x = ax->x_limits();
                    auto old_y = ax->y_limits();
                    ax->auto_fit();
                    auto new_x = ax->x_limits();
                    auto new_y = ax->y_limits();
                    undo_mgr.push(UndoAction{"Auto-fit axes",
                                             [ax, old_x, old_y]()
                                             {
                                                 ax->xlim(old_x.min, old_x.max);
                                                 ax->ylim(old_y.min, old_y.max);
                                             },
                                             [ax, new_x, new_y]()
                                             {
                                                 ax->xlim(new_x.min, new_x.max);
                                                 ax->ylim(new_y.min, new_y.max);
                                             }});
                }
            },
            "A",
            "View");

        cmd_registry.register_command(
            "view.toggle_grid",
            "Toggle Grid",
            [&]() { undoable_toggle_grid_all(&undo_mgr, *active_figure); },
            "G",
            "View",
            static_cast<uint16_t>(ui::Icon::Grid));

        cmd_registry.register_command(
            "view.toggle_crosshair",
            "Toggle Crosshair",
            [&]()
            {
                if (data_interaction)
                {
                    bool old_val = data_interaction->crosshair_active();
                    data_interaction->toggle_crosshair();
                    bool new_val = data_interaction->crosshair_active();
                    undo_mgr.push(UndoAction{new_val ? "Show crosshair" : "Hide crosshair",
                                             [&data_interaction, old_val]()
                                             {
                                                 if (data_interaction)
                                                     data_interaction->set_crosshair(old_val);
                                             },
                                             [&data_interaction, new_val]()
                                             {
                                                 if (data_interaction)
                                                     data_interaction->set_crosshair(new_val);
                                             }});
                }
            },
            "C",
            "View",
            static_cast<uint16_t>(ui::Icon::Crosshair));

        cmd_registry.register_command(
            "view.toggle_legend",
            "Toggle Legend",
            [&]() { undoable_toggle_legend(&undo_mgr, *active_figure); },
            "L",
            "View",
            static_cast<uint16_t>(ui::Icon::Eye));

        cmd_registry.register_command(
            "view.toggle_border",
            "Toggle Border",
            [&]() { undoable_toggle_border_all(&undo_mgr, *active_figure); },
            "B",
            "View");

        cmd_registry.register_command(
            "view.fullscreen",
            "Toggle Fullscreen Canvas",
            [&]()
            {
                if (imgui_ui)
                {
                    auto& lm = imgui_ui->get_layout_manager();
                    bool old_inspector = lm.is_inspector_visible();
                    bool old_nav = lm.is_nav_rail_expanded();
                    bool all_hidden = !old_inspector && !old_nav;
                    bool new_inspector = all_hidden;
                    bool new_nav = all_hidden;
                    lm.set_inspector_visible(new_inspector);
                    lm.set_nav_rail_expanded(new_nav);
                    undo_mgr.push(UndoAction{
                        "Toggle fullscreen",
                        [&imgui_ui, old_inspector, old_nav]()
                        {
                            if (imgui_ui)
                            {
                                imgui_ui->get_layout_manager().set_inspector_visible(old_inspector);
                                imgui_ui->get_layout_manager().set_nav_rail_expanded(old_nav);
                            }
                        },
                        [&imgui_ui, new_inspector, new_nav]()
                        {
                            if (imgui_ui)
                            {
                                imgui_ui->get_layout_manager().set_inspector_visible(new_inspector);
                                imgui_ui->get_layout_manager().set_nav_rail_expanded(new_nav);
                            }
                        }});
                }
            },
            "F",
            "View",
            static_cast<uint16_t>(ui::Icon::Fullscreen));

        cmd_registry.register_command(
            "view.home",
            "Home (Restore Original View)",
            [&]()
            {
                auto before = capture_figure_axes(*active_figure);
                for (auto& ax : active_figure->axes_mut())
                {
                    if (!ax) continue;
                    auto it = home_limits.find(ax.get());
                    if (it != home_limits.end())
                    {
                        ax->xlim(it->second.x.min, it->second.x.max);
                        ax->ylim(it->second.y.min, it->second.y.max);
                    }
                    else
                    {
                        ax->auto_fit();
                    }
                }
                auto after = capture_figure_axes(*active_figure);
                undo_mgr.push(UndoAction{"Restore original view",
                                         [before]() { restore_figure_axes(before); },
                                         [after]() { restore_figure_axes(after); }});
            },
            "Home",
            "View",
            static_cast<uint16_t>(ui::Icon::Home));

        cmd_registry.register_command(
            "view.zoom_in",
            "Zoom In",
            [&]()
            {
                // Zoom in 25% on active axes
                if (auto* ax = input_handler.active_axes())
                {
                    auto old_x = ax->x_limits();
                    auto old_y = ax->y_limits();
                    float xc = (old_x.min + old_x.max) * 0.5f,
                          xr = (old_x.max - old_x.min) * 0.375f;
                    float yc = (old_y.min + old_y.max) * 0.5f,
                          yr = (old_y.max - old_y.min) * 0.375f;
                    AxisLimits new_x{xc - xr, xc + xr};
                    AxisLimits new_y{yc - yr, yc + yr};
                    undoable_set_limits(&undo_mgr, *ax, new_x, new_y);
                }
            },
            "",
            "View",
            static_cast<uint16_t>(ui::Icon::ZoomIn));

        cmd_registry.register_command(
            "view.zoom_out",
            "Zoom Out",
            [&]()
            {
                if (auto* ax = input_handler.active_axes())
                {
                    auto old_x = ax->x_limits();
                    auto old_y = ax->y_limits();
                    float xc = (old_x.min + old_x.max) * 0.5f,
                          xr = (old_x.max - old_x.min) * 0.625f;
                    float yc = (old_y.min + old_y.max) * 0.5f,
                          yr = (old_y.max - old_y.min) * 0.625f;
                    AxisLimits new_x{xc - xr, xc + xr};
                    AxisLimits new_y{yc - yr, yc + yr};
                    undoable_set_limits(&undo_mgr, *ax, new_x, new_y);
                }
            },
            "",
            "View");

        // Toggle 2D/3D view mode (Agent 6 Week 11)
        // Only animates the camera between orbit-3D and top-down-ortho.
        // Axis limits are NEVER modified — that would break the model matrix.
        cmd_registry.register_command(
            "view.toggle_3d",
            "Toggle 2D/3D View",
            [&]()
            {
                Axes3D* ax3d = nullptr;
                for (auto& ax_base : active_figure->all_axes())
                {
                    if (ax_base)
                    {
                        ax3d = dynamic_cast<Axes3D*>(ax_base.get());
                        if (ax3d)
                            break;
                    }
                }
                if (!ax3d || mode_transition.is_active())
                    return;

                if (is_in_3d_mode)
                {
                    // 3D → 2D: save current camera, animate to top-down ortho
                    saved_3d_camera = ax3d->camera();

                    ModeTransition3DState from;
                    from.camera = ax3d->camera();
                    from.xlim = ax3d->x_limits();
                    from.ylim = ax3d->y_limits();
                    from.zlim = ax3d->z_limits();
                    from.grid_planes = static_cast<int>(ax3d->grid_planes());

                    ModeTransition2DState to;
                    to.xlim = ax3d->x_limits();
                    to.ylim = ax3d->y_limits();

                    mode_transition.begin_to_2d(from, to);
                    is_in_3d_mode = false;
                    input_handler.set_orbit_locked(true);
                }
                else
                {
                    // 2D → 3D: animate back to saved 3D camera
                    ModeTransition2DState from;
                    from.xlim = ax3d->x_limits();
                    from.ylim = ax3d->y_limits();

                    ModeTransition3DState to;
                    to.camera = saved_3d_camera;
                    to.xlim = ax3d->x_limits();
                    to.ylim = ax3d->y_limits();
                    to.zlim = ax3d->z_limits();
                    to.grid_planes = static_cast<int>(ax3d->grid_planes());

                    mode_transition.begin_to_3d(from, to);
                    is_in_3d_mode = true;
                    input_handler.set_orbit_locked(false);
                }
            },
            "3",
            "View",
            static_cast<uint16_t>(ui::Icon::Axes));

        // Command palette
        cmd_registry.register_command(
            "app.command_palette",
            "Command Palette",
            [&]() { cmd_palette.toggle(); },
            "Ctrl+K",
            "App",
            static_cast<uint16_t>(ui::Icon::Search));

        cmd_registry.register_command(
            "app.cancel",
            "Cancel / Close",
            [&]()
            {
                if (cmd_palette.is_open())
                {
                    cmd_palette.close();
                }
            },
            "Escape",
            "App");

        // File operations
        cmd_registry.register_command(
            "file.export_png",
            "Export PNG",
            [&]() { active_figure->save_png("spectra_export.png"); },
            "Ctrl+S",
            "File",
            static_cast<uint16_t>(ui::Icon::Export));

        cmd_registry.register_command(
            "file.export_svg",
            "Export SVG",
            [&]() { active_figure->save_svg("spectra_export.svg"); },
            "Ctrl+Shift+S",
            "File",
            static_cast<uint16_t>(ui::Icon::Export));

        cmd_registry.register_command(
            "file.save_workspace",
            "Save Workspace",
            [&]()
            {
                std::vector<Figure*> figs;
                for (auto id : fig_mgr.figure_ids())
                {
                    Figure* f = registry_.get(id);
                    if (f) figs.push_back(f);
                }
                auto data =
                    Workspace::capture(figs,
                                       fig_mgr.active_index(),
                                       ui::ThemeManager::instance().current_theme_name(),
                                       imgui_ui->get_layout_manager().is_inspector_visible(),
                                       imgui_ui->get_layout_manager().inspector_width(),
                                       imgui_ui->get_layout_manager().is_nav_rail_expanded());
                // Capture interaction state
                if (data_interaction)
                {
                    data.interaction.crosshair_enabled = data_interaction->crosshair_active();
                    data.interaction.tooltip_enabled = data_interaction->tooltip_active();
                    for (const auto& m : data_interaction->markers())
                    {
                        WorkspaceData::InteractionState::MarkerEntry me;
                        me.data_x = m.data_x;
                        me.data_y = m.data_y;
                        me.series_label = m.series ? m.series->label() : "";
                        me.point_index = m.point_index;
                        data.interaction.markers.push_back(std::move(me));
                    }
                }
                // Capture tab titles from FigureManager
                for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
                {
                    data.figures[i].custom_tab_title = fig_mgr.get_title(i);
                    data.figures[i].is_modified = fig_mgr.is_modified(i);
                }
                // Capture undo metadata
                data.undo_count = undo_mgr.undo_count();
                data.redo_count = undo_mgr.redo_count();
                // Capture dock/split view state
                data.dock_state = dock_system.serialize();
                Workspace::save(Workspace::default_path(), data);
            },
            "",
            "File",
            static_cast<uint16_t>(ui::Icon::Save));

        cmd_registry.register_command(
            "file.load_workspace",
            "Load Workspace",
            [&]()
            {
                WorkspaceData data;
                if (Workspace::load(Workspace::default_path(), data))
                {
                    // Capture before-state for undo
                    auto before_snap = capture_figure_axes(*active_figure);
                    std::vector<Figure*> figs;
                    for (auto id : fig_mgr.figure_ids())
                    {
                        Figure* f = registry_.get(id);
                        if (f) figs.push_back(f);
                    }
                    Workspace::apply(data, figs);
                    auto after_snap = capture_figure_axes(*active_figure);
                    undo_mgr.push(UndoAction{"Load workspace",
                                             [before_snap]() { restore_figure_axes(before_snap); },
                                             [after_snap]() { restore_figure_axes(after_snap); }});
                    // Restore interaction state
                    if (data_interaction)
                    {
                        data_interaction->set_crosshair(data.interaction.crosshair_enabled);
                        data_interaction->set_tooltip(data.interaction.tooltip_enabled);
                    }
                    // Restore tab titles
                    for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i)
                    {
                        if (!data.figures[i].custom_tab_title.empty())
                        {
                            fig_mgr.set_title(i, data.figures[i].custom_tab_title);
                        }
                    }
                    // Switch to saved active figure
                    if (data.active_figure_index < fig_mgr.count())
                    {
                        fig_mgr.queue_switch(data.active_figure_index);
                    }
                    // Restore theme
                    if (!data.theme_name.empty())
                    {
                        ui::ThemeManager::instance().set_theme(data.theme_name);
                        ui::ThemeManager::instance().apply_to_imgui();
                    }
                    // Restore panel state
                    if (imgui_ui)
                    {
                        auto& lm = imgui_ui->get_layout_manager();
                        lm.set_inspector_visible(data.panels.inspector_visible);
                        lm.set_nav_rail_expanded(data.panels.nav_rail_expanded);
                    }
                    // Restore dock/split view state
                    if (!data.dock_state.empty())
                    {
                        dock_system.deserialize(data.dock_state);
                    }
                }
            },
            "",
            "File",
            static_cast<uint16_t>(ui::Icon::FolderOpen));

        // Edit commands (undo/redo)
        cmd_registry.register_command(
            "edit.undo",
            "Undo",
            [&]() { undo_mgr.undo(); },
            "Ctrl+Z",
            "Edit",
            static_cast<uint16_t>(ui::Icon::Undo));

        cmd_registry.register_command(
            "edit.redo",
            "Redo",
            [&]() { undo_mgr.redo(); },
            "Ctrl+Shift+Z",
            "Edit",
            static_cast<uint16_t>(ui::Icon::Redo));

        // Figure management
        cmd_registry.register_command(
            "figure.new",
            "New Figure",
            [&]() { fig_mgr.queue_create(); },
            "Ctrl+T",
            "Figure",
            static_cast<uint16_t>(ui::Icon::Plus));

        cmd_registry.register_command(
            "figure.close",
            "Close Figure",
            [&]()
            {
                if (fig_mgr.count() > 1)
                {
                    fig_mgr.queue_close(fig_mgr.active_index());
                }
            },
            "Ctrl+W",
            "Figure",
            static_cast<uint16_t>(ui::Icon::Close));

        // Tab switching (1-9)
        for (int i = 0; i < 9; ++i)
        {
            cmd_registry.register_command(
                "figure.tab_" + std::to_string(i + 1),
                "Switch to Figure " + std::to_string(i + 1),
                [&fig_mgr, i]() { fig_mgr.queue_switch(static_cast<size_t>(i)); },
                std::to_string(i + 1),
                "Figure");
        }

        // Ctrl+Tab / Ctrl+Shift+Tab for cycling figures
        cmd_registry.register_command(
            "figure.next_tab",
            "Next Figure Tab",
            [&fig_mgr]() { fig_mgr.switch_to_next(); },
            "Ctrl+Tab",
            "Figure");

        cmd_registry.register_command(
            "figure.prev_tab",
            "Previous Figure Tab",
            [&fig_mgr]() { fig_mgr.switch_to_previous(); },
            "Ctrl+Shift+Tab",
            "Figure");

        // Series commands
        cmd_registry.register_command(
            "series.cycle_selection",
            "Cycle Series Selection",
            [&]()
            {
                // Placeholder for series cycling
            },
            "Tab",
            "Series");

        // Animation commands — wired to TimelineEditor
        cmd_registry.register_command(
            "anim.toggle_play",
            "Toggle Play/Pause",
            [&]() { timeline_editor.toggle_play(); },
            "Space",
            "Animation",
            static_cast<uint16_t>(ui::Icon::Play));

        cmd_registry.register_command(
            "anim.step_back",
            "Step Frame Back",
            [&]() { timeline_editor.step_backward(); },
            "[",
            "Animation",
            static_cast<uint16_t>(ui::Icon::StepBackward));

        cmd_registry.register_command(
            "anim.step_forward",
            "Step Frame Forward",
            [&]() { timeline_editor.step_forward(); },
            "]",
            "Animation",
            static_cast<uint16_t>(ui::Icon::StepForward));

        cmd_registry.register_command(
            "anim.stop", "Stop Playback", [&]() { timeline_editor.stop(); }, "", "Animation");

        cmd_registry.register_command(
            "anim.go_to_start",
            "Go to Start",
            [&]() { timeline_editor.set_playhead(0.0f); },
            "",
            "Animation");

        cmd_registry.register_command(
            "anim.go_to_end",
            "Go to End",
            [&]() { timeline_editor.set_playhead(timeline_editor.duration()); },
            "",
            "Animation");

        // Panel toggle commands for timeline & curve editor
        cmd_registry.register_command(
            "panel.toggle_timeline",
            "Toggle Timeline Panel",
            [&]()
            {
                if (imgui_ui)
                {
                    imgui_ui->set_timeline_visible(!imgui_ui->is_timeline_visible());
                }
            },
            "T",
            "Panel",
            static_cast<uint16_t>(ui::Icon::Play));

        cmd_registry.register_command(
            "panel.toggle_curve_editor",
            "Toggle Curve Editor",
            [&]()
            {
                if (imgui_ui)
                {
                    imgui_ui->set_curve_editor_visible(!imgui_ui->is_curve_editor_visible());
                }
            },
            "",
            "Panel");

        // Theme commands (undoable)
        cmd_registry.register_command(
            "theme.dark",
            "Switch to Dark Theme",
            [&]()
            {
                auto& tm = ui::ThemeManager::instance();
                std::string old_theme = tm.current_theme_name();
                tm.set_theme("dark");
                tm.apply_to_imgui();
                undo_mgr.push(UndoAction{"Switch to dark theme",
                                         [old_theme]()
                                         {
                                             auto& t = ui::ThemeManager::instance();
                                             t.set_theme(old_theme);
                                             t.apply_to_imgui();
                                         },
                                         []()
                                         {
                                             auto& t = ui::ThemeManager::instance();
                                             t.set_theme("dark");
                                             t.apply_to_imgui();
                                         }});
            },
            "",
            "Theme",
            static_cast<uint16_t>(ui::Icon::Moon));

        cmd_registry.register_command(
            "theme.light",
            "Switch to Light Theme",
            [&]()
            {
                auto& tm = ui::ThemeManager::instance();
                std::string old_theme = tm.current_theme_name();
                tm.set_theme("light");
                tm.apply_to_imgui();
                undo_mgr.push(UndoAction{"Switch to light theme",
                                         [old_theme]()
                                         {
                                             auto& t = ui::ThemeManager::instance();
                                             t.set_theme(old_theme);
                                             t.apply_to_imgui();
                                         },
                                         []()
                                         {
                                             auto& t = ui::ThemeManager::instance();
                                             t.set_theme("light");
                                             t.apply_to_imgui();
                                         }});
            },
            "",
            "Theme",
            static_cast<uint16_t>(ui::Icon::Sun));

        cmd_registry.register_command(
            "theme.toggle",
            "Toggle Dark/Light Theme",
            [&]()
            {
                auto& tm = ui::ThemeManager::instance();
                std::string old_theme = tm.current_theme_name();
                std::string new_theme = (old_theme == "dark") ? "light" : "dark";
                tm.set_theme(new_theme);
                tm.apply_to_imgui();
                undo_mgr.push(UndoAction{"Toggle theme",
                                         [old_theme]()
                                         {
                                             auto& t = ui::ThemeManager::instance();
                                             t.set_theme(old_theme);
                                             t.apply_to_imgui();
                                         },
                                         [new_theme]()
                                         {
                                             auto& t = ui::ThemeManager::instance();
                                             t.set_theme(new_theme);
                                             t.apply_to_imgui();
                                         }});
            },
            "",
            "Theme",
            static_cast<uint16_t>(ui::Icon::Contrast));

        // Panel commands (undoable)
        cmd_registry.register_command(
            "panel.toggle_inspector",
            "Toggle Inspector Panel",
            [&]()
            {
                if (imgui_ui)
                {
                    auto& lm = imgui_ui->get_layout_manager();
                    bool old_val = lm.is_inspector_visible();
                    lm.set_inspector_visible(!old_val);
                    undo_mgr.push(UndoAction{
                        old_val ? "Hide inspector" : "Show inspector",
                        [&imgui_ui, old_val]()
                        {
                            if (imgui_ui)
                                imgui_ui->get_layout_manager().set_inspector_visible(old_val);
                        },
                        [&imgui_ui, old_val]()
                        {
                            if (imgui_ui)
                                imgui_ui->get_layout_manager().set_inspector_visible(!old_val);
                        }});
                }
            },
            "",
            "Panel");

        cmd_registry.register_command(
            "panel.toggle_nav_rail",
            "Toggle Navigation Rail",
            [&]()
            {
                if (imgui_ui)
                {
                    auto& lm = imgui_ui->get_layout_manager();
                    bool old_val = lm.is_nav_rail_expanded();
                    lm.set_nav_rail_expanded(!old_val);
                    undo_mgr.push(UndoAction{
                        old_val ? "Collapse nav rail" : "Expand nav rail",
                        [&imgui_ui, old_val]()
                        {
                            if (imgui_ui)
                                imgui_ui->get_layout_manager().set_nav_rail_expanded(old_val);
                        },
                        [&imgui_ui, old_val]()
                        {
                            if (imgui_ui)
                                imgui_ui->get_layout_manager().set_nav_rail_expanded(!old_val);
                        }});
                }
            },
            "",
            "Panel",
            static_cast<uint16_t>(ui::Icon::Menu));

        // Split view commands (Agent A Week 9)
        // Splitting never creates new empty figures. Instead it redistributes
        // existing figure tabs between panes:
        //   First split:  all tabs → first pane, one tab moves → second pane
        //   Further splits: take a tab from the active pane → new pane
        auto do_split = [&](SplitDirection dir)
        {
            if (dock_system.is_split())
            {
                // Already split — take a non-active tab from the active pane
                SplitPane* active_pane = dock_system.split_view().active_pane();
                if (!active_pane || active_pane->figure_count() < 2)
                    return;

                // Pick a figure to move: the one after the active, or the first non-active
                size_t active_local = active_pane->active_local_index();
                size_t move_local = (active_local + 1) % active_pane->figure_count();
                size_t move_fig = active_pane->figure_indices()[move_local];

                // Remove from current pane before splitting
                active_pane->remove_figure(move_fig);

                // Split the active pane, putting the moved figure in the new pane
                size_t active_fig = active_pane->figure_index();
                SplitPane* new_pane = nullptr;
                if (dir == SplitDirection::Horizontal)
                    new_pane = dock_system.split_figure_right(active_fig, move_fig);
                else
                    new_pane = dock_system.split_figure_down(active_fig, move_fig);

                (void)new_pane;
            }
            else
            {
                // Going from single view to split:
                // Need at least 2 figures to split without creating new ones
                if (fig_mgr.count() < 2)
                    return;

                FigureId orig_active = fig_mgr.active_index();

                // Pick a figure to move to the second pane (first non-active)
                FigureId move_fig = INVALID_FIGURE_ID;
                for (auto id : fig_mgr.figure_ids())
                {
                    if (id != orig_active)
                    {
                        move_fig = id;
                        break;
                    }
                }
                if (move_fig == INVALID_FIGURE_ID)
                    return;

                // Split: orig_active stays in first pane, move_fig goes to second
                SplitPane* new_pane = nullptr;
                if (dir == SplitDirection::Horizontal)
                    new_pane = dock_system.split_figure_right(orig_active, move_fig);
                else
                    new_pane = dock_system.split_figure_down(orig_active, move_fig);

                // Fix up pane contents after split:
                // split() copies ALL figure_indices_ to first child, so move_fig
                // ends up in both panes. Remove it from the first pane, and ensure
                // all remaining figures are in the first pane.
                if (new_pane)
                {
                    SplitPane* root = dock_system.split_view().root();
                    SplitPane* first_pane = root ? root->first() : nullptr;
                    if (first_pane && first_pane->is_leaf())
                    {
                        // Remove move_fig from first pane (it belongs in second only)
                        if (first_pane->has_figure(move_fig))
                        {
                            first_pane->remove_figure(move_fig);
                        }
                        // Add any remaining figures not yet in first pane
                        for (auto id : fig_mgr.figure_ids())
                        {
                            if (id == move_fig)
                                continue;
                            if (!first_pane->has_figure(id))
                            {
                                first_pane->add_figure(id);
                            }
                        }
                        // Restore the originally active figure as the active tab
                        for (size_t li = 0; li < first_pane->figure_indices().size(); ++li)
                        {
                            if (first_pane->figure_indices()[li] == orig_active)
                            {
                                first_pane->set_active_local_index(li);
                                break;
                            }
                        }
                    }
                }

                dock_system.set_active_figure_index(orig_active);
            }
        };

        cmd_registry.register_command(
            "view.split_right",
            "Split Right",
            [&, do_split]() { do_split(SplitDirection::Horizontal); },
            "Ctrl+\\",
            "View");

        cmd_registry.register_command(
            "view.split_down",
            "Split Down",
            [&, do_split]() { do_split(SplitDirection::Vertical); },
            "Ctrl+Shift+\\",
            "View");

        cmd_registry.register_command(
            "view.close_split",
            "Close Split Pane",
            [&]()
            {
                if (dock_system.is_split())
                {
                    dock_system.close_split(dock_system.active_figure_index());
                }
            },
            "",
            "View");

        cmd_registry.register_command(
            "view.reset_splits",
            "Reset All Splits",
            [&]() { dock_system.reset_splits(); },
            "",
            "View");

        // Tool mode commands
        cmd_registry.register_command(
            "tool.pan",
            "Pan Tool",
            [&]() { input_handler.set_tool_mode(ToolMode::Pan); },
            "",
            "Tools",
            static_cast<uint16_t>(ui::Icon::Hand));

        cmd_registry.register_command(
            "tool.box_zoom",
            "Box Zoom Tool",
            [&]() { input_handler.set_tool_mode(ToolMode::BoxZoom); },
            "",
            "Tools",
            static_cast<uint16_t>(ui::Icon::ZoomIn));

        // New window command (Ctrl+Shift+N) — spawns a secondary window with full UI
        cmd_registry.register_command(
            "app.new_window",
            "New Window",
            [&]()
            {
#ifdef SPECTRA_MULTIPROC
                // In multiproc mode, send REQ_CREATE_WINDOW to the backend daemon
                // which will spawn a new agent process.
                if (ipc_conn_ && ipc_conn_->is_open())
                {
                    ipc::ReqCreateWindowPayload req;
                    req.template_window_id = ipc::INVALID_WINDOW;
                    ipc::Message msg;
                    msg.header.type = ipc::MessageType::REQ_CREATE_WINDOW;
                    msg.header.session_id = ipc_session_id_;
                    msg.header.window_id = ipc_window_id_;
                    msg.payload = ipc::encode_req_create_window(req);
                    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
                    ipc_conn_->send(msg);
                    return;
                }
#endif
                if (!window_mgr)
                    return;
                // In-process mode: duplicate the active figure into a new window
                FigureId dup_id = fig_mgr.duplicate_figure(active_figure_id);
                if (dup_id == INVALID_FIGURE_ID)
                    return;
                Figure* dup_fig = registry_.get(dup_id);
                uint32_t w = dup_fig ? dup_fig->width() : 800;
                uint32_t h = dup_fig ? dup_fig->height() : 600;
                std::string win_title = fig_mgr.get_title(dup_id);
                window_mgr->create_window_with_ui(w, h, win_title, dup_id);
            },
            "Ctrl+Shift+N",
            "App",
            static_cast<uint16_t>(ui::Icon::Plus));

        // Move figure to another window (Ctrl+Shift+M) — moves active figure
        // from current window to the next available window, or spawns a new one.
        cmd_registry.register_command(
            "figure.move_to_window",
            "Move Figure to Window",
            [&]()
            {
                if (!window_mgr)
                    return;
                // Find the focused/current window as the source
                if (!window_mgr || window_mgr->windows().empty())
                    return;
                auto* src_wctx = window_mgr->focused_window();
                if (!src_wctx) src_wctx = window_mgr->windows()[0];

                FigureId fig_id = active_figure_id;
                if (fig_id == INVALID_FIGURE_ID)
                    return;

                // Don't move the last figure in the source window
                if (fig_mgr.count() <= 1)
                {
                    SPECTRA_LOG_WARN("window_manager",
                                     "Cannot move last figure from window");
                    return;
                }

                // Find another window to move to, or create one
                WindowContext* target = nullptr;
                for (auto* wctx : window_mgr->windows())
                {
                    if (wctx != src_wctx && wctx->ui_ctx)
                    {
                        target = wctx;
                        break;
                    }
                }

                if (target)
                {
                    window_mgr->move_figure(fig_id, src_wctx->id, target->id);
                }
                else
                {
                    // No other window — detach into a new one
                    Figure* fig = registry_.get(fig_id);
                    uint32_t w = fig ? fig->width() : 800;
                    uint32_t h = fig ? fig->height() : 600;
                    std::string title = fig_mgr.get_title(fig_id);

                    // Remove from source's FigureManager first
                    FigureState state = fig_mgr.remove_figure(fig_id);

                    // Remove from source's assigned_figures
                    auto& pf = src_wctx->assigned_figures;
                    pf.erase(std::remove(pf.begin(), pf.end(), fig_id), pf.end());
                    if (src_wctx->active_figure_id == fig_id)
                        src_wctx->active_figure_id = pf.empty() ? INVALID_FIGURE_ID : pf.front();

                    // Create new window with the figure
                    auto* new_wctx = window_mgr->create_window_with_ui(w, h, title, fig_id);
                    if (new_wctx && new_wctx->ui_ctx && new_wctx->ui_ctx->fig_mgr)
                    {
                        auto* new_fm = new_wctx->ui_ctx->fig_mgr;
                        new_fm->state(fig_id) = std::move(state);
                        std::string correct_title = new_fm->get_title(fig_id);
                        if (new_fm->tab_bar())
                            new_fm->tab_bar()->set_tab_title(0, correct_title);
                    }
                }
            },
            "Ctrl+Shift+M",
            "App",
            static_cast<uint16_t>(ui::Icon::Plus));

        // Register default shortcut bindings
        shortcut_mgr.register_defaults();

        SPECTRA_LOG_INFO("app",
                        "Registered " + std::to_string(cmd_registry.count()) + " commands, "
                            + std::to_string(shortcut_mgr.count()) + " shortcuts");
    }
#endif

    scheduler.reset();

    // Capture initial axes limits for Home button (restore original view)
    for (auto id : registry_.all_ids())
    {
        Figure* fig_ptr = registry_.get(id);
        if (!fig_ptr) continue;
        for (auto& ax : fig_ptr->axes_mut())
        {
            if (ax)
                home_limits[ax.get()] = {ax->x_limits(), ax->y_limits()};
        }
    }

    while (!session.should_exit())
    {
        // ── Session tick: scheduler, commands, animations, window loop, detach ──
        session.tick(scheduler, animator, cmd_queue,
                     config_.headless, ui_ctx_ptr,
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
                    SPECTRA_LOG_INFO("export",
                                    "Saved PNG: " + active_figure->png_export_path_);
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
        if (elapsed >= SECONDARY_RESIZE_DEBOUNCE
            && wctx->pending_width > 0 && wctx->pending_height > 0)
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
    }
}

}  // namespace spectra

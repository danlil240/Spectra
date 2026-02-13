#include <plotix/app.hpp>
#include <plotix/animator.hpp>
#include <plotix/export.hpp>
#include <plotix/figure.hpp>
#include <plotix/frame.hpp>
#include <plotix/logger.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../core/layout.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "animation_controller.hpp"
#include "command_queue.hpp"
#include "gesture_recognizer.hpp"
#include "input.hpp"

#ifdef PLOTIX_USE_GLFW
#include "glfw_adapter.hpp"
#endif

#ifdef PLOTIX_USE_IMGUI
#include "box_zoom_overlay.hpp"
#include "command_palette.hpp"
#include "command_registry.hpp"
#include "data_interaction.hpp"
#include "figure_manager.hpp"
#include "icons.hpp"
#include "imgui_integration.hpp"
#include "shortcut_manager.hpp"
#include "tab_bar.hpp"
#include "theme.hpp"
#include "undo_manager.hpp"
#include "undoable_property.hpp"
#include "workspace.hpp"
#include <imgui.h>
#endif

#include <algorithm>
#include <iostream>
#include <memory>
#include <filesystem>
#include <span>

namespace plotix {

// ─── App ─────────────────────────────────────────────────────────────────────

App::App(const AppConfig& config)
    : config_(config)
{
    // Initialize logger for debugging
    // Set to Trace for maximum debugging, Debug for normal debugging, Info for production
    auto& logger = plotix::Logger::instance();
    logger.set_level(plotix::LogLevel::Debug);  // Change to Trace to see all frame-by-frame logs
    
    // Add console sink with timestamps
    logger.add_sink(plotix::sinks::console_sink());
    
    // Add file sink in temp directory with error handling
    try {
        std::string log_path = std::filesystem::temp_directory_path() / "plotix_app.log";
        logger.add_sink(plotix::sinks::file_sink(log_path));
        PLOTIX_LOG_INFO("app", "Log file: " + log_path);
    } catch (const std::exception& e) {
        PLOTIX_LOG_WARN("app", "Failed to create log file: " + std::string(e.what()));
    }
    
    PLOTIX_LOG_INFO("app", "Initializing Plotix application (headless: " + std::string(config_.headless ? "true" : "false") + ")");
    
    // Create Vulkan backend
    backend_ = std::make_unique<VulkanBackend>();
    if (!backend_->init(config_.headless)) {
        PLOTIX_LOG_ERROR("app", "Failed to initialize Vulkan backend");
        return;
    }

    // Create renderer
    renderer_ = std::make_unique<Renderer>(*backend_);
    if (!renderer_->init()) {
        PLOTIX_LOG_ERROR("app", "Failed to initialize renderer");
        return;
    }
    
    PLOTIX_LOG_INFO("app", "Plotix application initialized successfully");
}

App::~App() {
    // Destroy renderer before backend (renderer holds backend reference)
    renderer_.reset();
    if (backend_) {
        backend_->shutdown();
    }
}

Figure& App::figure(const FigureConfig& config) {
    figures_.push_back(std::make_unique<Figure>(config));
    return *figures_.back();
}

void App::run() {
    if (!backend_ || !renderer_) {
        std::cerr << "[plotix] Cannot run: backend or renderer not initialized\n";
        return;
    }

    if (figures_.empty()) {
        return;
    }

    // Multi-figure support - track active figure
    size_t active_figure_index = 0;
    Figure* active_figure = figures_[active_figure_index].get();

    CommandQueue cmd_queue;
    FrameScheduler scheduler(active_figure->anim_fps_);
    Animator animator;

    bool has_animation = static_cast<bool>(active_figure->anim_on_frame_);
    bool running = true;

    auto switch_active_figure = [&](size_t new_index
#ifdef PLOTIX_USE_GLFW
                                    , InputHandler* input_handler
#endif
    ) {
        if (new_index >= figures_.size()) {
            return;
        }
        active_figure_index = new_index;
        active_figure = figures_[active_figure_index].get();
        scheduler.set_target_fps(active_figure->anim_fps_);
        has_animation = static_cast<bool>(active_figure->anim_on_frame_);
#ifdef PLOTIX_USE_GLFW
        if (input_handler) {
            input_handler->set_figure(active_figure);
            if (!active_figure->axes().empty() && active_figure->axes()[0]) {
                input_handler->set_active_axes(active_figure->axes()[0].get());
                const auto& vp = active_figure->axes()[0]->viewport();
                input_handler->set_viewport(vp.x, vp.y, vp.w, vp.h);
            }
        }
#endif
    };

#ifdef PLOTIX_USE_FFMPEG
    bool is_recording = !active_figure->video_record_path_.empty();
#else
    if (!active_figure->video_record_path_.empty()) {
        std::cerr << "[plotix] Video recording requested but PLOTIX_USE_FFMPEG is not enabled\n";
    }
#endif

#ifdef PLOTIX_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t> video_frame_pixels;
    if (is_recording) {
        VideoExporter::Config vcfg;
        vcfg.output_path = active_figure->video_record_path_;
        vcfg.width  = active_figure->width();
        vcfg.height = active_figure->height();
        vcfg.fps    = active_figure->anim_fps_;
        video_exporter = std::make_unique<VideoExporter>(vcfg);
        if (!video_exporter->is_open()) {
            std::cerr << "[plotix] Failed to open video exporter for: "
                      << active_figure->video_record_path_ << "\n";
            video_exporter.reset();
        } else {
            video_frame_pixels.resize(
                static_cast<size_t>(active_figure->width()) * active_figure->height() * 4);
        }
        // Recording always runs headless
        if (!config_.headless) {
            config_.headless = true;
        }
    }
#endif

#ifdef PLOTIX_USE_IMGUI
    std::unique_ptr<ImGuiIntegration> imgui_ui;
    std::unique_ptr<DataInteraction> data_interaction;
    std::unique_ptr<TabBar> figure_tabs;

    // Agent B Week 7: Box zoom overlay
    BoxZoomOverlay box_zoom_overlay;

    // Agent A Week 6: FigureManager for multi-figure lifecycle
    FigureManager fig_mgr(figures_);

    // Agent F: Command palette & productivity
    CommandRegistry cmd_registry;
    ShortcutManager shortcut_mgr;
    UndoManager undo_mgr;
    CommandPalette cmd_palette;
    shortcut_mgr.set_command_registry(&cmd_registry);
    cmd_palette.set_command_registry(&cmd_registry);
    cmd_palette.set_shortcut_manager(&shortcut_mgr);
#endif

#ifdef PLOTIX_USE_GLFW
    std::unique_ptr<GlfwAdapter> glfw;
    AnimationController anim_controller;
    GestureRecognizer gesture;
    InputHandler input_handler;
    input_handler.set_animation_controller(&anim_controller);
    input_handler.set_gesture_recognizer(&gesture);
    bool needs_resize = false;
    uint32_t new_width = active_figure->width();
    uint32_t new_height = active_figure->height();
    bool is_resizing = false;
    int resize_frame_counter = 0;
    static constexpr int RESIZE_SKIP_FRAMES = 1;  // Skip frames during rapid resize

    if (!config_.headless) {
        glfw = std::make_unique<GlfwAdapter>();
        if (!glfw->init(active_figure->width(), active_figure->height(), "Plotix")) {
            std::cerr << "[plotix] Failed to create GLFW window\n";
            glfw.reset();
        } else {
            // Create Vulkan surface from GLFW window
            backend_->create_surface(glfw->native_window());
            backend_->create_swapchain(active_figure->width(), active_figure->height());

            // Wire input handler — set active figure for multi-axes hit-testing
            input_handler.set_figure(active_figure);
            if (!active_figure->axes().empty() && active_figure->axes()[0]) {
                input_handler.set_active_axes(active_figure->axes()[0].get());
                auto& vp = active_figure->axes()[0]->viewport();
                input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
            }

            // Set GLFW callbacks for input
            InputCallbacks callbacks;
            callbacks.on_mouse_move = [&input_handler
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui
#endif
            ](double x, double y) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && imgui_ui->wants_capture_mouse()) {
                    PLOTIX_LOG_TRACE("input", "Mouse move ignored - ImGui wants capture");
                    return;
                }
#endif
                input_handler.on_mouse_move(x, y);
            };
            callbacks.on_mouse_button = [&input_handler
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui
#endif
            ](int button, int action, double x, double y) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && imgui_ui->wants_capture_mouse()) {
                    PLOTIX_LOG_DEBUG("input", "Mouse button ignored - ImGui wants capture");
                    return;
                }
#endif
                input_handler.on_mouse_button(button, action, x, y);
            };
            callbacks.on_scroll = [&input_handler, &glfw
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui
#endif
            ](double x_offset, double y_offset) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && imgui_ui->wants_capture_mouse()) {
                    PLOTIX_LOG_DEBUG("input", "Scroll ignored - ImGui wants capture");
                    return;
                }
#endif
                double cx = 0.0, cy = 0.0;
                if (glfw) {
                    glfw->mouse_position(cx, cy);
                }
                input_handler.on_scroll(x_offset, y_offset, cx, cy);
            };
            callbacks.on_key = [&input_handler
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui
#endif
            ](int key, int action, int mods) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && imgui_ui->wants_capture_keyboard()) return;
#endif
                input_handler.on_key(key, action, mods);
            };
            callbacks.on_resize = [&needs_resize, &new_width, &new_height, &is_resizing](int w, int h) {
                static int call_count = 0;
                static bool ignore_resizes = false;
                call_count++;
                auto now = std::chrono::steady_clock::now();
                static auto last_call = now;
                auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_call);
                
                // After 10 resize events, start ignoring them to test stability
                if (call_count > 10 && !ignore_resizes) {
                    PLOTIX_LOG_WARN("resize", "Too many resize events (" + std::to_string(call_count) + 
                                   "). Ignoring future resize events for testing.");
                    ignore_resizes = true;
                }
                
                if (ignore_resizes) {
                    PLOTIX_LOG_DEBUG("resize", "Ignoring resize callback #" + std::to_string(call_count) + 
                                    ": " + std::to_string(w) + "x" + std::to_string(h));
                    return;
                }
                
                PLOTIX_LOG_DEBUG("resize", "GLFW resize callback #" + std::to_string(call_count) + 
                                ": " + std::to_string(w) + "x" + std::to_string(h) + 
                                " (+" + std::to_string(time_since_last.count()) + "ms since last)");
                
                if (w > 0 && h > 0) {
                    needs_resize = true;
                    new_width = static_cast<uint32_t>(w);
                    new_height = static_cast<uint32_t>(h);
                    is_resizing = true;
                    PLOTIX_LOG_DEBUG("resize", "Set resize pending: " + std::to_string(new_width) + "x" + std::to_string(new_height));
                } else {
                    PLOTIX_LOG_WARN("resize", "Invalid resize dimensions: " + std::to_string(w) + "x" + std::to_string(h));
                }
                last_call = now;
            };
            glfw->set_callbacks(callbacks);
        }
    }
#endif

#ifdef PLOTIX_USE_IMGUI
    if (!config_.headless && glfw) {
        imgui_ui = std::make_unique<ImGuiIntegration>();
        figure_tabs = std::make_unique<TabBar>();

        // Wire FigureManager to TabBar
        fig_mgr.set_tab_bar(figure_tabs.get());

        // TabBar callbacks → FigureManager queued operations
        figure_tabs->set_tab_change_callback([&fig_mgr](size_t new_index) {
            fig_mgr.queue_switch(new_index);
        });
        figure_tabs->set_tab_close_callback([&fig_mgr](size_t index) {
            fig_mgr.queue_close(index);
        });
        figure_tabs->set_tab_add_callback([&fig_mgr]() {
            fig_mgr.queue_create();
        });
        figure_tabs->set_tab_duplicate_callback([&fig_mgr](size_t index) {
            fig_mgr.duplicate_figure(index);
        });
        figure_tabs->set_tab_close_all_except_callback([&fig_mgr](size_t index) {
            fig_mgr.close_all_except(index);
        });
        figure_tabs->set_tab_close_to_right_callback([&fig_mgr](size_t index) {
            fig_mgr.close_to_right(index);
        });
        figure_tabs->set_tab_rename_callback([&fig_mgr](size_t index, const std::string& title) {
            fig_mgr.set_title(index, title);
        });
    }
#endif

    if (config_.headless) {
        backend_->create_offscreen_framebuffer(active_figure->width(), active_figure->height());
    }

    // Now that render pass exists, create real Vulkan pipelines from SPIR-V
    static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

#ifdef PLOTIX_USE_IMGUI
    if (imgui_ui && glfw) {
        auto* vk = static_cast<VulkanBackend*>(backend_.get());
        auto* glfw_window = static_cast<GLFWwindow*>(glfw->native_window());
        imgui_ui->init(*vk, glfw_window);

        // Create and wire DataInteraction layer (Agent E)
        data_interaction = std::make_unique<DataInteraction>();
        imgui_ui->set_data_interaction(data_interaction.get());
        input_handler.set_data_interaction(data_interaction.get());
        input_handler.set_shortcut_manager(&shortcut_mgr);

        // Wire Agent B Week 7: Box zoom overlay
        box_zoom_overlay.set_input_handler(&input_handler);
        imgui_ui->set_box_zoom_overlay(&box_zoom_overlay);

        // Wire Agent F: command palette & productivity
        imgui_ui->set_command_palette(&cmd_palette);
        imgui_ui->set_command_registry(&cmd_registry);
        imgui_ui->set_shortcut_manager(&shortcut_mgr);
        imgui_ui->set_undo_manager(&undo_mgr);
        cmd_palette.set_body_font(nullptr);   // Will use ImGui default
        cmd_palette.set_heading_font(nullptr);

        // ─── Register 30+ commands ──────────────────────────────────────
        // View commands
        cmd_registry.register_command("view.reset", "Reset View", [&]() {
            auto before = capture_figure_axes(*active_figure);
            for (auto& ax : active_figure->axes_mut()) {
                if (ax) {
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
            undo_mgr.push(UndoAction{
                "Reset view",
                [before]() { restore_figure_axes(before); },
                [after]()  { restore_figure_axes(after); }
            });
        }, "R", "View", static_cast<uint16_t>(ui::Icon::Home));

        cmd_registry.register_command("view.autofit", "Auto-Fit Active Axes", [&]() {
            if (auto* ax = input_handler.active_axes()) {
                auto old_x = ax->x_limits();
                auto old_y = ax->y_limits();
                ax->auto_fit();
                auto new_x = ax->x_limits();
                auto new_y = ax->y_limits();
                undo_mgr.push(UndoAction{
                    "Auto-fit axes",
                    [ax, old_x, old_y]() { ax->xlim(old_x.min, old_x.max); ax->ylim(old_y.min, old_y.max); },
                    [ax, new_x, new_y]() { ax->xlim(new_x.min, new_x.max); ax->ylim(new_y.min, new_y.max); }
                });
            }
        }, "A", "View");

        cmd_registry.register_command("view.toggle_grid", "Toggle Grid", [&]() {
            undoable_toggle_grid_all(&undo_mgr, *active_figure);
        }, "G", "View", static_cast<uint16_t>(ui::Icon::Grid));

        cmd_registry.register_command("view.toggle_crosshair", "Toggle Crosshair", [&]() {
            if (data_interaction) {
                bool old_val = data_interaction->crosshair_active();
                data_interaction->toggle_crosshair();
                bool new_val = data_interaction->crosshair_active();
                undo_mgr.push(UndoAction{
                    new_val ? "Show crosshair" : "Hide crosshair",
                    [&data_interaction, old_val]() { if (data_interaction) data_interaction->set_crosshair(old_val); },
                    [&data_interaction, new_val]() { if (data_interaction) data_interaction->set_crosshair(new_val); }
                });
            }
        }, "C", "View", static_cast<uint16_t>(ui::Icon::Crosshair));

        cmd_registry.register_command("view.toggle_legend", "Toggle Legend", [&]() {
            undoable_toggle_legend(&undo_mgr, *active_figure);
        }, "L", "View", static_cast<uint16_t>(ui::Icon::Eye));

        cmd_registry.register_command("view.toggle_border", "Toggle Border", [&]() {
            undoable_toggle_border_all(&undo_mgr, *active_figure);
        }, "B", "View");

        cmd_registry.register_command("view.fullscreen", "Toggle Fullscreen Canvas", [&]() {
            if (imgui_ui) {
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
                    [&imgui_ui, old_inspector, old_nav]() {
                        if (imgui_ui) {
                            imgui_ui->get_layout_manager().set_inspector_visible(old_inspector);
                            imgui_ui->get_layout_manager().set_nav_rail_expanded(old_nav);
                        }
                    },
                    [&imgui_ui, new_inspector, new_nav]() {
                        if (imgui_ui) {
                            imgui_ui->get_layout_manager().set_inspector_visible(new_inspector);
                            imgui_ui->get_layout_manager().set_nav_rail_expanded(new_nav);
                        }
                    }
                });
            }
        }, "F", "View", static_cast<uint16_t>(ui::Icon::Fullscreen));

        cmd_registry.register_command("view.home", "Home (Reset All Views)", [&]() {
            undoable_reset_view(&undo_mgr, *active_figure);
        }, "Home", "View", static_cast<uint16_t>(ui::Icon::Home));

        cmd_registry.register_command("view.zoom_in", "Zoom In", [&]() {
            // Zoom in 25% on active axes
            if (auto* ax = input_handler.active_axes()) {
                auto old_x = ax->x_limits();
                auto old_y = ax->y_limits();
                float xc = (old_x.min + old_x.max) * 0.5f, xr = (old_x.max - old_x.min) * 0.375f;
                float yc = (old_y.min + old_y.max) * 0.5f, yr = (old_y.max - old_y.min) * 0.375f;
                AxisLimits new_x{xc - xr, xc + xr};
                AxisLimits new_y{yc - yr, yc + yr};
                undoable_set_limits(&undo_mgr, *ax, new_x, new_y);
            }
        }, "", "View", static_cast<uint16_t>(ui::Icon::ZoomIn));

        cmd_registry.register_command("view.zoom_out", "Zoom Out", [&]() {
            if (auto* ax = input_handler.active_axes()) {
                auto old_x = ax->x_limits();
                auto old_y = ax->y_limits();
                float xc = (old_x.min + old_x.max) * 0.5f, xr = (old_x.max - old_x.min) * 0.625f;
                float yc = (old_y.min + old_y.max) * 0.5f, yr = (old_y.max - old_y.min) * 0.625f;
                AxisLimits new_x{xc - xr, xc + xr};
                AxisLimits new_y{yc - yr, yc + yr};
                undoable_set_limits(&undo_mgr, *ax, new_x, new_y);
            }
        }, "", "View");

        // Command palette
        cmd_registry.register_command("app.command_palette", "Command Palette", [&]() {
            cmd_palette.toggle();
        }, "Ctrl+K", "App", static_cast<uint16_t>(ui::Icon::Search));

        cmd_registry.register_command("app.cancel", "Cancel / Close", [&]() {
            if (cmd_palette.is_open()) {
                cmd_palette.close();
            }
        }, "Escape", "App");

        // File operations
        cmd_registry.register_command("file.export_png", "Export PNG", [&]() {
            active_figure->save_png("plotix_export.png");
        }, "Ctrl+S", "File", static_cast<uint16_t>(ui::Icon::Export));

        cmd_registry.register_command("file.export_svg", "Export SVG", [&]() {
            active_figure->save_svg("plotix_export.svg");
        }, "Ctrl+Shift+S", "File", static_cast<uint16_t>(ui::Icon::Export));

        cmd_registry.register_command("file.save_workspace", "Save Workspace", [&]() {
            std::vector<Figure*> figs;
            for (auto& f : figures_) figs.push_back(f.get());
            auto data = Workspace::capture(figs, active_figure_index,
                ui::ThemeManager::instance().current_theme_name(),
                imgui_ui->get_layout_manager().is_inspector_visible(),
                imgui_ui->get_layout_manager().inspector_width(),
                imgui_ui->get_layout_manager().is_nav_rail_expanded());
            // Capture interaction state
            if (data_interaction) {
                data.interaction.crosshair_enabled = data_interaction->crosshair_active();
                data.interaction.tooltip_enabled = data_interaction->tooltip_active();
                for (const auto& m : data_interaction->markers()) {
                    WorkspaceData::InteractionState::MarkerEntry me;
                    me.data_x = m.data_x;
                    me.data_y = m.data_y;
                    me.series_label = m.series ? m.series->label() : "";
                    me.point_index = m.point_index;
                    data.interaction.markers.push_back(std::move(me));
                }
            }
            // Capture tab titles from FigureManager
            for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i) {
                data.figures[i].custom_tab_title = fig_mgr.get_title(i);
                data.figures[i].is_modified = fig_mgr.is_modified(i);
            }
            // Capture undo metadata
            data.undo_count = undo_mgr.undo_count();
            data.redo_count = undo_mgr.redo_count();
            Workspace::save(Workspace::default_path(), data);
        }, "", "File", static_cast<uint16_t>(ui::Icon::Save));

        cmd_registry.register_command("file.load_workspace", "Load Workspace", [&]() {
            WorkspaceData data;
            if (Workspace::load(Workspace::default_path(), data)) {
                // Capture before-state for undo
                auto before_snap = capture_figure_axes(*active_figure);
                std::vector<Figure*> figs;
                for (auto& f : figures_) figs.push_back(f.get());
                Workspace::apply(data, figs);
                auto after_snap = capture_figure_axes(*active_figure);
                undo_mgr.push(UndoAction{
                    "Load workspace",
                    [before_snap]() { restore_figure_axes(before_snap); },
                    [after_snap]()  { restore_figure_axes(after_snap); }
                });
                // Restore interaction state
                if (data_interaction) {
                    data_interaction->set_crosshair(data.interaction.crosshair_enabled);
                    data_interaction->set_tooltip(data.interaction.tooltip_enabled);
                }
                // Restore tab titles
                for (size_t i = 0; i < data.figures.size() && i < fig_mgr.count(); ++i) {
                    if (!data.figures[i].custom_tab_title.empty()) {
                        fig_mgr.set_title(i, data.figures[i].custom_tab_title);
                    }
                }
                // Switch to saved active figure
                if (data.active_figure_index < fig_mgr.count()) {
                    fig_mgr.queue_switch(data.active_figure_index);
                }
                // Restore theme
                if (!data.theme_name.empty()) {
                    ui::ThemeManager::instance().set_theme(data.theme_name);
                    ui::ThemeManager::instance().apply_to_imgui();
                }
                // Restore panel state
                if (imgui_ui) {
                    auto& lm = imgui_ui->get_layout_manager();
                    lm.set_inspector_visible(data.panels.inspector_visible);
                    lm.set_nav_rail_expanded(data.panels.nav_rail_expanded);
                }
            }
        }, "", "File", static_cast<uint16_t>(ui::Icon::FolderOpen));

        // Edit commands (undo/redo)
        cmd_registry.register_command("edit.undo", "Undo", [&]() {
            undo_mgr.undo();
        }, "Ctrl+Z", "Edit", static_cast<uint16_t>(ui::Icon::Undo));

        cmd_registry.register_command("edit.redo", "Redo", [&]() {
            undo_mgr.redo();
        }, "Ctrl+Shift+Z", "Edit", static_cast<uint16_t>(ui::Icon::Redo));

        // Figure management
        cmd_registry.register_command("figure.new", "New Figure", [&]() {
            fig_mgr.queue_create();
        }, "Ctrl+T", "Figure", static_cast<uint16_t>(ui::Icon::Plus));

        cmd_registry.register_command("figure.close", "Close Figure", [&]() {
            if (figures_.size() > 1) {
                fig_mgr.queue_close(fig_mgr.active_index());
            }
        }, "Ctrl+W", "Figure", static_cast<uint16_t>(ui::Icon::Close));

        // Tab switching (1-9)
        for (int i = 0; i < 9; ++i) {
            cmd_registry.register_command(
                "figure.tab_" + std::to_string(i + 1),
                "Switch to Figure " + std::to_string(i + 1),
                [&fig_mgr, i]() { fig_mgr.queue_switch(static_cast<size_t>(i)); },
                std::to_string(i + 1), "Figure");
        }

        // Ctrl+Tab / Ctrl+Shift+Tab for cycling figures
        cmd_registry.register_command("figure.next_tab", "Next Figure Tab", [&fig_mgr]() {
            fig_mgr.switch_to_next();
        }, "Ctrl+Tab", "Figure");

        cmd_registry.register_command("figure.prev_tab", "Previous Figure Tab", [&fig_mgr]() {
            fig_mgr.switch_to_previous();
        }, "Ctrl+Shift+Tab", "Figure");

        // Series commands
        cmd_registry.register_command("series.cycle_selection", "Cycle Series Selection", [&]() {
            // Placeholder for series cycling
        }, "Tab", "Series");

        // Animation commands
        cmd_registry.register_command("anim.toggle_play", "Toggle Play/Pause", [&]() {
            // Placeholder for animation play/pause
        }, "Space", "Animation", static_cast<uint16_t>(ui::Icon::Play));

        cmd_registry.register_command("anim.step_back", "Step Frame Back", [&]() {
            // Placeholder
        }, "[", "Animation", static_cast<uint16_t>(ui::Icon::StepBackward));

        cmd_registry.register_command("anim.step_forward", "Step Frame Forward", [&]() {
            // Placeholder
        }, "]", "Animation", static_cast<uint16_t>(ui::Icon::StepForward));

        // Theme commands (undoable)
        cmd_registry.register_command("theme.dark", "Switch to Dark Theme", [&]() {
            auto& tm = ui::ThemeManager::instance();
            std::string old_theme = tm.current_theme_name();
            tm.set_theme("dark");
            tm.apply_to_imgui();
            undo_mgr.push(UndoAction{
                "Switch to dark theme",
                [old_theme]() { auto& t = ui::ThemeManager::instance(); t.set_theme(old_theme); t.apply_to_imgui(); },
                []()          { auto& t = ui::ThemeManager::instance(); t.set_theme("dark"); t.apply_to_imgui(); }
            });
        }, "", "Theme", static_cast<uint16_t>(ui::Icon::Moon));

        cmd_registry.register_command("theme.light", "Switch to Light Theme", [&]() {
            auto& tm = ui::ThemeManager::instance();
            std::string old_theme = tm.current_theme_name();
            tm.set_theme("light");
            tm.apply_to_imgui();
            undo_mgr.push(UndoAction{
                "Switch to light theme",
                [old_theme]() { auto& t = ui::ThemeManager::instance(); t.set_theme(old_theme); t.apply_to_imgui(); },
                []()          { auto& t = ui::ThemeManager::instance(); t.set_theme("light"); t.apply_to_imgui(); }
            });
        }, "", "Theme", static_cast<uint16_t>(ui::Icon::Sun));

        cmd_registry.register_command("theme.toggle", "Toggle Dark/Light Theme", [&]() {
            auto& tm = ui::ThemeManager::instance();
            std::string old_theme = tm.current_theme_name();
            std::string new_theme = (old_theme == "dark") ? "light" : "dark";
            tm.set_theme(new_theme);
            tm.apply_to_imgui();
            undo_mgr.push(UndoAction{
                "Toggle theme",
                [old_theme]() { auto& t = ui::ThemeManager::instance(); t.set_theme(old_theme); t.apply_to_imgui(); },
                [new_theme]() { auto& t = ui::ThemeManager::instance(); t.set_theme(new_theme); t.apply_to_imgui(); }
            });
        }, "", "Theme", static_cast<uint16_t>(ui::Icon::Contrast));

        // Panel commands (undoable)
        cmd_registry.register_command("panel.toggle_inspector", "Toggle Inspector Panel", [&]() {
            if (imgui_ui) {
                auto& lm = imgui_ui->get_layout_manager();
                bool old_val = lm.is_inspector_visible();
                lm.set_inspector_visible(!old_val);
                undo_mgr.push(UndoAction{
                    old_val ? "Hide inspector" : "Show inspector",
                    [&imgui_ui, old_val]() { if (imgui_ui) imgui_ui->get_layout_manager().set_inspector_visible(old_val); },
                    [&imgui_ui, old_val]() { if (imgui_ui) imgui_ui->get_layout_manager().set_inspector_visible(!old_val); }
                });
            }
        }, "", "Panel");

        cmd_registry.register_command("panel.toggle_nav_rail", "Toggle Navigation Rail", [&]() {
            if (imgui_ui) {
                auto& lm = imgui_ui->get_layout_manager();
                bool old_val = lm.is_nav_rail_expanded();
                lm.set_nav_rail_expanded(!old_val);
                undo_mgr.push(UndoAction{
                    old_val ? "Collapse nav rail" : "Expand nav rail",
                    [&imgui_ui, old_val]() { if (imgui_ui) imgui_ui->get_layout_manager().set_nav_rail_expanded(old_val); },
                    [&imgui_ui, old_val]() { if (imgui_ui) imgui_ui->get_layout_manager().set_nav_rail_expanded(!old_val); }
                });
            }
        }, "", "Panel", static_cast<uint16_t>(ui::Icon::Menu));

        // Tool mode commands
        cmd_registry.register_command("tool.pan", "Pan Tool", [&]() {
            input_handler.set_tool_mode(ToolMode::Pan);
        }, "", "Tools", static_cast<uint16_t>(ui::Icon::Hand));

        cmd_registry.register_command("tool.box_zoom", "Box Zoom Tool", [&]() {
            input_handler.set_tool_mode(ToolMode::BoxZoom);
        }, "", "Tools", static_cast<uint16_t>(ui::Icon::ZoomIn));

        // Register default shortcut bindings
        shortcut_mgr.register_defaults();

        PLOTIX_LOG_INFO("app", "Registered " + std::to_string(cmd_registry.count()) + " commands, " +
                        std::to_string(shortcut_mgr.count()) + " shortcuts");
    }
#endif

    scheduler.reset();
    
    // Add heartbeat tracking and resize loop detection
    auto last_heartbeat = std::chrono::steady_clock::now();
    const auto heartbeat_interval = std::chrono::seconds(5);
    int resize_count = 0;
    auto last_resize_time = std::chrono::steady_clock::now();
    const auto resize_burst_threshold = std::chrono::milliseconds(200); // Reduced to 200ms
    const int max_resizes_in_burst = 3; // Reduced to 3 resizes
    int total_recreations = 0;
    const int max_total_recreations = 20; // Hard limit to prevent infinite loops

    while (running) {
        PLOTIX_LOG_TRACE("main_loop", "Starting frame iteration, running=" + std::string(running ? "true" : "false"));
        
        // Check for heartbeat logging and resize loop detection
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= heartbeat_interval) {
            PLOTIX_LOG_INFO("heartbeat", "App is still running, frame " + std::to_string(scheduler.current_frame().number) + 
                           ", elapsed " + std::to_string(scheduler.elapsed_seconds()) + "s" + 
                           (is_resizing ? ", RESIZING" : ""));
            last_heartbeat = now;
        }
        
        // Detect resize loops and add hard limit
        if (needs_resize) {
            resize_count++;
            total_recreations++;
            
            auto time_since_last_resize = now - last_resize_time;
            if (time_since_last_resize < resize_burst_threshold) {
                if (resize_count > max_resizes_in_burst) {
                    PLOTIX_LOG_WARN("resize", "Detected resize loop! " + std::to_string(resize_count) + 
                                   " resizes in " + std::to_string(time_since_last_resize.count()) + "ms. "
                                   "Throttling resize processing.");
                    // Skip this resize to break the loop
                    needs_resize = false;
                    resize_count = 0;
                }
            } else {
                resize_count = 1; // Reset count if enough time has passed
            }
            
            // Hard limit to prevent infinite loops
            if (total_recreations > max_total_recreations) {
                PLOTIX_LOG_ERROR("resize", "Hard limit reached: " + std::to_string(total_recreations) + 
                                " swapchain recreations. Terminating to prevent infinite loop.");
                running = false;
                break;
            }
            
            last_resize_time = now;
        }
        
        try {
            scheduler.begin_frame();
        } catch (const std::exception& e) {
            PLOTIX_LOG_CRITICAL("main_loop", "Frame scheduler failed: " + std::string(e.what()));
            running = false;
            break;
        }

        // Drain command queue (apply app-thread mutations)
        size_t commands_processed = cmd_queue.drain();
        if (commands_processed > 0) {
            PLOTIX_LOG_TRACE("main_loop", "Processed " + std::to_string(commands_processed) + " commands");
        }

        // Evaluate keyframe animations
        animator.evaluate(scheduler.elapsed_seconds());

#ifdef PLOTIX_USE_GLFW
        // Update interaction animations (animated zoom, inertial pan, auto-fit)
        if (glfw) {
            input_handler.update(scheduler.dt());
        }
#endif

        // Call user on_frame callback
        if (has_animation && active_figure->anim_on_frame_) {
            Frame frame = scheduler.current_frame();
            active_figure->anim_on_frame_(frame);
        }

        // Start ImGui frame (updates layout manager with current window size).
        bool imgui_frame_started = false;
#ifdef PLOTIX_USE_IMGUI
        bool should_update_imgui = !is_resizing || (resize_frame_counter % 6 == 0);
        if (imgui_ui && should_update_imgui) {
            imgui_ui->new_frame();  // updates layout manager with current window size
            imgui_frame_started = true;
        }
#endif

#ifdef PLOTIX_USE_GLFW
        // Handle window resize with debouncing and frame skipping
        // Note: We only process resize when begin_frame succeeds to avoid duplicate recreation
        // The fallback path handles resize when begin_frame fails
        if (needs_resize && backend_->begin_frame()) {
            resize_frame_counter++;
            PLOTIX_LOG_DEBUG("resize", "Processing resize with successful begin_frame: frame_counter=" + std::to_string(resize_frame_counter) + 
                            ", target=" + std::to_string(new_width) + "x" + std::to_string(new_height));
            
            // Only process resize after a few frames to debounce rapid resize events
            if (resize_frame_counter > RESIZE_SKIP_FRAMES) {
                PLOTIX_LOG_DEBUG("resize", "Debounce threshold reached (counter=" + std::to_string(resize_frame_counter) + ")");
                PLOTIX_LOG_INFO("resize", "Recreating swapchain: " + std::to_string(new_width) + "x" + std::to_string(new_height) + 
                               " (recreation #" + std::to_string(total_recreations) + ")");
                needs_resize = false;
                resize_frame_counter = 0;
                is_resizing = false;
                
                // Recreate swapchain with new dimensions
                auto swapchain_start = std::chrono::high_resolution_clock::now();
                backend_->recreate_swapchain(new_width, new_height);
                auto swapchain_end = std::chrono::high_resolution_clock::now();
                auto swapchain_duration = std::chrono::duration_cast<std::chrono::milliseconds>(swapchain_end - swapchain_start);
                PLOTIX_LOG_INFO("resize", "Swapchain recreation completed in " + std::to_string(swapchain_duration.count()) + "ms");
                
                // Sync figure dimensions from actual swapchain extent
                // (may differ from callback values due to surface capabilities)
                active_figure->config_.width = backend_->swapchain_width();
                active_figure->config_.height = backend_->swapchain_height();
                PLOTIX_LOG_INFO("resize", "Swapchain recreated, actual extent: " + std::to_string(active_figure->config_.width) + "x" + std::to_string(active_figure->config_.height));

#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui) {
                    imgui_ui->on_swapchain_recreated(
                        *static_cast<VulkanBackend*>(backend_.get()));
                }
#endif
                
                // Recompute layout with new dimensions
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui) {
                    // Update layout manager with new window size before recomputing
                    imgui_ui->update_layout(
                        static_cast<float>(active_figure->config_.width),
                        static_cast<float>(active_figure->config_.height));
                    const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();
                    const auto rects = compute_subplot_layout(
                        canvas.w, canvas.h,
                        active_figure->grid_rows_, active_figure->grid_cols_,
                        {},
                        canvas.x, canvas.y);
                    for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i) {
                        if (active_figure->axes_mut()[i]) {
                            active_figure->axes_mut()[i]->set_viewport(rects[i]);
                        }
                    }
                } else
#endif
                {
                    active_figure->compute_layout();
                }
                
                // Update input handler viewport after layout recompute
                if (!active_figure->axes().empty() && active_figure->axes()[0]) {
                    auto& vp = active_figure->axes()[0]->viewport();
                    input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
            }
            
            // End the frame we started for resize checking
            backend_->end_frame();
        } else if (!needs_resize) {
            resize_frame_counter = 0;
        }

        // Update input handler with current active axes viewport
        if (glfw && !active_figure->axes().empty() && active_figure->axes()[0]) {
            auto& vp = active_figure->axes()[0]->viewport();
            input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
#endif

#ifdef PLOTIX_USE_IMGUI
        // Build ImGui UI (new_frame was already called above before layout computation)
        if (imgui_ui && imgui_frame_started) {
            imgui_ui->build_ui(*active_figure);

            if (figure_tabs) {
                Rect canvas_bounds = imgui_ui->get_layout_manager().canvas_rect();
                Rect tab_bounds{canvas_bounds.x, canvas_bounds.y, canvas_bounds.w, 36.0f};

                ImGui::SetNextWindowPos(ImVec2(tab_bounds.x, tab_bounds.y));
                ImGui::SetNextWindowSize(ImVec2(tab_bounds.w, tab_bounds.h));
                ImGuiWindowFlags tab_flags = ImGuiWindowFlags_NoDecoration |
                                             ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                                             ImGuiWindowFlags_NoFocusOnAppearing;
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin("##figure_tabs", nullptr, tab_flags)) {
                    figure_tabs->draw(tab_bounds);
                }
                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
            }
            
            // Handle interaction state from UI
            if (imgui_ui->should_reset_view()) {
                // Animated auto-fit all axes
                for (auto& ax : active_figure->axes_mut()) {
                    if (ax) {
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
                imgui_ui->clear_reset_view();
            }
            
            // Update input handler tool mode
            input_handler.set_tool_mode(imgui_ui->get_interaction_mode());
            
            // Feed cursor data to status bar
            auto readout = input_handler.cursor_readout();
            imgui_ui->set_cursor_data(readout.data_x, readout.data_y);

            // Update data interaction layer (nearest-point query, tooltip state)
            if (data_interaction) {
                data_interaction->update(readout, *active_figure);
            }
            
            // Feed zoom level (approximate: based on data bounds vs view)
            if (!active_figure->axes().empty() && active_figure->axes()[0]) {
                auto& ax = active_figure->axes()[0];
                auto xlim = ax->x_limits();
                float view_range = xlim.max - xlim.min;
                // Estimate data range from series x_data spans
                float data_min = xlim.max, data_max = xlim.min;
                for (auto& s : ax->series()) {
                    if (!s) continue;
                    std::span<const float> xd;
                    if (auto* ls = dynamic_cast<LineSeries*>(s.get()))
                        xd = ls->x_data();
                    else if (auto* sc = dynamic_cast<ScatterSeries*>(s.get()))
                        xd = sc->x_data();
                    if (!xd.empty()) {
                        auto [it_min, it_max] = std::minmax_element(xd.begin(), xd.end());
                        data_min = std::min(data_min, *it_min);
                        data_max = std::max(data_max, *it_max);
                    }
                }
                float data_range = data_max - data_min;
                if (view_range > 0.0f && data_range > 0.0f) {
                    imgui_ui->set_zoom_level(data_range / view_range);
                }
            }
            
            // Show tab bar when multiple figures exist
            imgui_ui->get_layout_manager().set_tab_bar_visible(figures_.size() > 1);
        }
#endif

#ifdef PLOTIX_USE_IMGUI
        // Process queued figure operations (create, close, switch)
        if (fig_mgr.process_pending()) {
            active_figure_index = fig_mgr.active_index();
            switch_active_figure(active_figure_index
#ifdef PLOTIX_USE_GLFW
                                 , glfw ? &input_handler : nullptr
#endif
            );
        }
#endif

        // Compute subplot layout AFTER build_ui() so that nav rail / inspector
        // toggles from the current frame are immediately reflected.
        if (!is_resizing) {
#ifdef PLOTIX_USE_IMGUI
            if (imgui_ui) {
                const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();
                const auto rects = compute_subplot_layout(
                    canvas.w, canvas.h,
                    active_figure->grid_rows_, active_figure->grid_cols_,
                    {},
                    canvas.x, canvas.y);

                for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i) {
                    if (active_figure->axes_mut()[i]) {
                        active_figure->axes_mut()[i]->set_viewport(rects[i]);
                    }
                }
            } else {
                active_figure->compute_layout();
            }
#else
            active_figure->compute_layout();
#endif
        }

        // Render (skip drawing if swapchain is stale, but keep the loop going)
        // During active resize, only render every few frames to maintain responsiveness
        // Note: begin_frame might have been called already in resize processing above
        bool already_begun_frame = (needs_resize && resize_frame_counter > 0);
        bool should_render = !is_resizing || (resize_frame_counter % 3 == 0);
        
        if (should_render && (already_begun_frame || backend_->begin_frame())) {
            PLOTIX_LOG_TRACE("resize", "begin_frame succeeded, rendering frame");

            // Use split render pass so ImGui can render inside the same pass
            renderer_->begin_render_pass();
            renderer_->render_figure_content(*active_figure);

#ifdef PLOTIX_USE_IMGUI
            // Render ImGui overlay inside the same render pass, after plot content
            if (imgui_ui && imgui_frame_started) {
                imgui_ui->render(*static_cast<VulkanBackend*>(backend_.get()));
            }
#endif

            renderer_->end_render_pass();

            if (!already_begun_frame) {
                backend_->end_frame();
            }
        } else {
            PLOTIX_LOG_DEBUG("resize", "begin_frame failed or should_render=false");
#ifdef PLOTIX_USE_IMGUI
            // If render failed, we still need to end the ImGui frame properly
            if (imgui_frame_started) {
                ImGui::EndFrame();
            }
#endif
#ifdef PLOTIX_USE_GLFW
            if (glfw) {
                // Swapchain is out of date — recreate with current framebuffer size
                uint32_t fb_width, fb_height;
                glfw->framebuffer_size(fb_width, fb_height);
                PLOTIX_LOG_INFO("resize", "begin_frame failed, recreating from fallback: " + std::to_string(fb_width) + "x" + std::to_string(fb_height));
                if (fb_width > 0 && fb_height > 0) {
                    auto fallback_start = std::chrono::high_resolution_clock::now();
                    backend_->recreate_swapchain(fb_width, fb_height);
                    auto fallback_end = std::chrono::high_resolution_clock::now();
                    auto fallback_duration = std::chrono::duration_cast<std::chrono::milliseconds>(fallback_end - fallback_start);
                    PLOTIX_LOG_INFO("resize", "Fallback swapchain recreation completed in " + std::to_string(fallback_duration.count()) + "ms");
                    // Sync figure dimensions from actual swapchain extent
                    active_figure->config_.width = backend_->swapchain_width();
                    active_figure->config_.height = backend_->swapchain_height();
                    PLOTIX_LOG_INFO("resize", "Fallback recreation complete, actual extent: " + std::to_string(active_figure->config_.width) + "x" + std::to_string(active_figure->config_.height));
#ifdef PLOTIX_USE_IMGUI
                    if (imgui_ui) {
                        imgui_ui->on_swapchain_recreated(
                            *static_cast<VulkanBackend*>(backend_.get()));
                    }
#endif
#ifdef PLOTIX_USE_IMGUI
                    if (imgui_ui) {
                        imgui_ui->update_layout(
                            static_cast<float>(active_figure->config_.width),
                            static_cast<float>(active_figure->config_.height));
                        const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();
                        const auto rects = compute_subplot_layout(
                            canvas.w, canvas.h,
                            active_figure->grid_rows_, active_figure->grid_cols_,
                            {},
                            canvas.x, canvas.y);
                        for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i) {
                            if (active_figure->axes_mut()[i]) {
                                active_figure->axes_mut()[i]->set_viewport(rects[i]);
                            }
                        }
                    } else
#endif
                    {
                        active_figure->compute_layout();
                    }
                    // Clear resize flags to prevent redundant double recreation
                    needs_resize = false;
                    resize_frame_counter = 0;
                    is_resizing = false;
                }
            }
#endif
        }

#ifdef PLOTIX_USE_FFMPEG
        // Capture frame for video recording
        if (video_exporter && video_exporter->is_open()) {
            if (backend_->readback_framebuffer(video_frame_pixels.data(),
                                                active_figure->width(), active_figure->height())) {
                video_exporter->write_frame(video_frame_pixels.data());
            }
        }
#endif

        scheduler.end_frame();

        // Check termination conditions
        if (active_figure->anim_duration_ > 0.0f &&
            scheduler.elapsed_seconds() >= active_figure->anim_duration_ &&
            !active_figure->anim_loop_) {
            running = false;
        }

        // Headless without animation: render one frame and stop
        if (config_.headless && !has_animation) {
            PLOTIX_LOG_INFO("main_loop", "Headless single frame mode, exiting loop");
            running = false;
        }

#ifdef PLOTIX_USE_GLFW
        if (glfw) {
            PLOTIX_LOG_TRACE("main_loop", "Polling GLFW events");
            glfw->poll_events();
            if (glfw->should_close()) {
                PLOTIX_LOG_INFO("main_loop", "GLFW window should close, exiting loop");
                running = false;
            }
        }
#endif
        
        PLOTIX_LOG_TRACE("main_loop", "Frame iteration completed");
    }
    
    PLOTIX_LOG_INFO("main_loop", "Exited main render loop");

#ifdef PLOTIX_USE_FFMPEG
    // Finalize video recording
    if (video_exporter) {
        video_exporter->finish();
        video_exporter.reset();
    }
#endif

    // Process exports for all figures (headless batch mode)
    for (auto& fig_ptr : figures_) {
        if (!fig_ptr) continue;
        auto& f = *fig_ptr;

        // Export PNG if requested (headless mode)
        if (config_.headless && !f.png_export_path_.empty()) {
            uint32_t export_w = f.png_export_width_  > 0 ? f.png_export_width_  : f.width();
            uint32_t export_h = f.png_export_height_ > 0 ? f.png_export_height_ : f.height();

            // Render this figure into an offscreen framebuffer at the target resolution
            bool needs_render = (&f != active_figure) ||
                                (export_w != f.width()) ||
                                (export_h != f.height());

            if (needs_render) {
                backend_->create_offscreen_framebuffer(export_w, export_h);
                static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

                // Temporarily override figure dimensions for layout
                uint32_t orig_w = f.config_.width;
                uint32_t orig_h = f.config_.height;
                f.config_.width  = export_w;
                f.config_.height = export_h;
                f.compute_layout();

                if (backend_->begin_frame()) {
                    renderer_->render_figure(f);
                    backend_->end_frame();
                }

                // Restore original dimensions
                f.config_.width  = orig_w;
                f.config_.height = orig_h;
                f.compute_layout();
            }

            std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
            if (backend_->readback_framebuffer(pixels.data(), export_w, export_h)) {
                if (!ImageExporter::write_png(f.png_export_path_, pixels.data(), export_w, export_h)) {
                    std::cerr << "[plotix] Failed to write PNG: " << f.png_export_path_ << "\n";
                }
            } else {
                std::cerr << "[plotix] Failed to readback framebuffer\n";
            }
        }

        // Export SVG if requested (works for any figure, no GPU needed)
        if (!f.svg_export_path_.empty()) {
            f.compute_layout();
            if (!SvgExporter::write_svg(f.svg_export_path_, f)) {
                std::cerr << "[plotix] Failed to write SVG: " << f.svg_export_path_ << "\n";
            }
        }
    }

#ifdef PLOTIX_USE_GLFW
    if (glfw) {
        glfw->shutdown();
    }
#endif
}

} // namespace plotix

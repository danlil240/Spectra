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
#include "animation_curve_editor.hpp"
#include "axis_link.hpp"
#include "box_zoom_overlay.hpp"
#include "command_palette.hpp"
#include "command_registry.hpp"
#include "data_interaction.hpp"
#include "dock_system.hpp"
#include "figure_manager.hpp"
#include "icons.hpp"
#include "imgui_integration.hpp"
#include "keyframe_interpolator.hpp"
#include "shortcut_manager.hpp"
#include "tab_bar.hpp"
#include "theme.hpp"
#include "timeline_editor.hpp"
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

    // Agent A Week 9: Dock system for split views
    DockSystem dock_system;
    bool dock_tab_sync_guard = false;  // Prevent circular tab↔dock updates

    // Agent B Week 10: Axis link manager for multi-axis linking
    AxisLinkManager axis_link_mgr;

    // Agent G: Timeline editor, keyframe interpolator, curve editor
    TimelineEditor timeline_editor;
    KeyframeInterpolator keyframe_interpolator;
    AnimationCurveEditor curve_editor;
    timeline_editor.set_interpolator(&keyframe_interpolator);
    curve_editor.set_interpolator(&keyframe_interpolator);

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
    auto resize_requested_time = std::chrono::steady_clock::now();
    static constexpr auto RESIZE_DEBOUNCE = std::chrono::milliseconds(50);
    static constexpr auto RESIZE_MAX_DELAY = std::chrono::milliseconds(200);

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
                , &imgui_ui, &dock_system, this
#endif
            ](double x, double y) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && (imgui_ui->wants_capture_mouse() || imgui_ui->is_tab_interacting())) {
                    PLOTIX_LOG_TRACE("input", "Mouse move ignored - ImGui wants capture");
                    return;
                }
                // In split mode, route to the figure under the cursor
                if (dock_system.is_split()) {
                    SplitPane* root = dock_system.split_view().root();
                    if (root) {
                        SplitPane* pane = root->find_at_point(static_cast<float>(x), static_cast<float>(y));
                        if (pane && pane->is_leaf()) {
                            size_t fi = pane->figure_index();
                            if (fi < figures_.size() && figures_[fi])
                                input_handler.set_figure(figures_[fi].get());
                        }
                    }
                }
#endif
                input_handler.on_mouse_move(x, y);
            };
            callbacks.on_mouse_button = [&input_handler
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui, &dock_system, this
#endif
            ](int button, int action, int mods, double x, double y) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && (imgui_ui->wants_capture_mouse() || imgui_ui->is_tab_interacting())) {
                    // Always forward RELEASE events so InputHandler can exit
                    // Dragging mode. Without this, starting a drag on the canvas
                    // and releasing over a UI element (menu, tab bar) leaves the
                    // handler stuck in Dragging — causing phantom panning.
                    constexpr int GLFW_RELEASE = 0;
                    if (action == GLFW_RELEASE) {
                        input_handler.on_mouse_button(button, action, mods, x, y);
                    }
                    return;
                }
                if (dock_system.is_split()) {
                    SplitPane* root = dock_system.split_view().root();
                    if (root) {
                        SplitPane* pane = root->find_at_point(static_cast<float>(x), static_cast<float>(y));
                        if (pane && pane->is_leaf()) {
                            size_t fi = pane->figure_index();
                            if (fi < figures_.size() && figures_[fi])
                                input_handler.set_figure(figures_[fi].get());
                        }
                    }
                }
#endif
                input_handler.on_mouse_button(button, action, mods, x, y);
            };
            callbacks.on_scroll = [&input_handler, &glfw
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui, &dock_system, &cmd_palette, this
#endif
            ](double x_offset, double y_offset) {
#ifdef PLOTIX_USE_IMGUI
                // Block scroll when command palette is open — it handles its own smooth scroll
                if (cmd_palette.is_open()) return;
                if (imgui_ui && imgui_ui->wants_capture_mouse()) {
                    // PLOTIX_LOG_DEBUG("input", "Scroll ignored - ImGui wants capture");
                    return;
                }
#endif
                double cx = 0.0, cy = 0.0;
                if (glfw) {
                    glfw->mouse_position(cx, cy);
                }
#ifdef PLOTIX_USE_IMGUI
                if (dock_system.is_split()) {
                    SplitPane* root = dock_system.split_view().root();
                    if (root) {
                        SplitPane* pane = root->find_at_point(static_cast<float>(cx), static_cast<float>(cy));
                        if (pane && pane->is_leaf()) {
                            size_t fi = pane->figure_index();
                            if (fi < figures_.size() && figures_[fi])
                                input_handler.set_figure(figures_[fi].get());
                        }
                    }
                }
#endif
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
            callbacks.on_resize = [&needs_resize, &new_width, &new_height, &resize_requested_time,
                                   this, &active_figure
#ifdef PLOTIX_USE_IMGUI
                                   , &imgui_ui, &dock_system
#endif
            ](int w, int h) {
                if (w <= 0 || h <= 0) return;
                uint32_t uw = static_cast<uint32_t>(w);
                uint32_t uh = static_cast<uint32_t>(h);
                PLOTIX_LOG_DEBUG("resize", "Callback: " + std::to_string(w) + "x" + std::to_string(h));

                // Recreate swapchain immediately in the callback
                auto* vk = static_cast<VulkanBackend*>(backend_.get());
                backend_->recreate_swapchain(uw, uh);
                vk->clear_swapchain_dirty();
                active_figure->config_.width = backend_->swapchain_width();
                active_figure->config_.height = backend_->swapchain_height();

#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui) {
                    imgui_ui->on_swapchain_recreated(*vk);
                    imgui_ui->new_frame();
                    imgui_ui->build_ui(*active_figure);

                    // Use ImGui-aware layout (same as main loop) so plot
                    // position accounts for nav rail / inspector / tab bar.
                    const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();
                    dock_system.update_layout(canvas);

                    if (dock_system.is_split()) {
                        auto pane_infos = dock_system.get_pane_infos();
                        for (const auto& pinfo : pane_infos) {
                            if (pinfo.figure_index < figures_.size()) {
                                auto* fig = figures_[pinfo.figure_index].get();
                                if (!fig) continue;
                                Margins pm;
                                pm.left   = std::clamp(pinfo.bounds.w * 0.15f, 40.0f, 60.0f);
                                pm.right  = std::clamp(pinfo.bounds.w * 0.08f, 15.0f, 30.0f);
                                pm.bottom = std::clamp(pinfo.bounds.h * 0.15f, 35.0f, 50.0f);
                                pm.top    = std::clamp(pinfo.bounds.h * 0.08f, 15.0f, 35.0f);
                                const auto rects = compute_subplot_layout(
                                    pinfo.bounds.w, pinfo.bounds.h,
                                    fig->grid_rows_, fig->grid_cols_,
                                    pm, pinfo.bounds.x, pinfo.bounds.y);
                                for (size_t i = 0; i < fig->axes_mut().size() && i < rects.size(); ++i) {
                                    if (fig->axes_mut()[i])
                                        fig->axes_mut()[i]->set_viewport(rects[i]);
                                }
                            }
                        }
                    } else {
                        SplitPane* root = dock_system.split_view().root();
                        Rect cb = (root && root->is_leaf()) ? root->content_bounds() : canvas;
                        const auto rects = compute_subplot_layout(
                            cb.w, cb.h,
                            active_figure->grid_rows_, active_figure->grid_cols_,
                            {}, cb.x, cb.y);
                        for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i) {
                            if (active_figure->axes_mut()[i])
                                active_figure->axes_mut()[i]->set_viewport(rects[i]);
                        }
                        for (size_t i = 0; i < active_figure->all_axes_mut().size() && i < rects.size(); ++i) {
                            if (active_figure->all_axes_mut()[i])
                                active_figure->all_axes_mut()[i]->set_viewport(rects[i]);
                        }
                    }
                } else
#endif
                {
                    active_figure->compute_layout();
                }

                // Render a full frame
                if (backend_->begin_frame()) {
                    renderer_->flush_pending_deletions();
                    renderer_->begin_render_pass();
                    renderer_->render_figure_content(*active_figure);
#ifdef PLOTIX_USE_IMGUI
                    if (imgui_ui) {
                        imgui_ui->render(*vk);
                    }
#endif
                    renderer_->end_render_pass();
                    backend_->end_frame();
                }
#ifdef PLOTIX_USE_IMGUI
                else if (imgui_ui) {
                    ImGui::EndFrame();
                }
#endif

                // Mark resize as handled so the main loop doesn't redo it
                needs_resize = false;
                new_width = uw;
                new_height = uh;
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

        // TabBar callbacks → FigureManager queued operations + dock sync
        figure_tabs->set_tab_change_callback([&fig_mgr, &dock_system, &dock_tab_sync_guard](size_t new_index) {
            if (dock_tab_sync_guard) return;
            dock_tab_sync_guard = true;
            fig_mgr.queue_switch(new_index);
            dock_system.set_active_figure_index(new_index);
            dock_tab_sync_guard = false;
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

        // Tab drag-to-dock: when a tab is dragged vertically out of the tab bar,
        // initiate a dock drag operation to split the view
        figure_tabs->set_tab_drag_out_callback([&dock_system](size_t index, float mx, float my) {
            dock_system.begin_drag(index, mx, my);
        });
        figure_tabs->set_tab_drag_update_callback([&dock_system](size_t /*index*/, float mx, float my) {
            dock_system.update_drag(mx, my);
        });
        figure_tabs->set_tab_drag_end_callback([&dock_system](size_t /*index*/, float mx, float my) {
            dock_system.end_drag(mx, my);
        });
        figure_tabs->set_tab_drag_cancel_callback([&dock_system](size_t /*index*/) {
            dock_system.cancel_drag();
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

        // Wire series click-to-select: clicking a series on the canvas
        // updates the inspector's selection context and opens the inspector.
        data_interaction->set_on_series_selected(
            [&imgui_ui](Figure* fig, Axes* ax, int ax_idx, Series* s, int s_idx) {
                if (!imgui_ui) return;
                imgui_ui->select_series(fig, ax, ax_idx, s, s_idx);
            });

        // Wire Agent B Week 10: Axis link manager
        input_handler.set_axis_link_manager(&axis_link_mgr);
        data_interaction->set_axis_link_manager(&axis_link_mgr);
        imgui_ui->set_axis_link_manager(&axis_link_mgr);
        imgui_ui->set_input_handler(&input_handler);

        // Wire Agent B Week 7: Box zoom overlay
        box_zoom_overlay.set_input_handler(&input_handler);
        imgui_ui->set_box_zoom_overlay(&box_zoom_overlay);

        // Wire Agent A Week 9: Dock system
        imgui_ui->set_dock_system(&dock_system);

        // Figure title lookup for per-pane tab headers
        imgui_ui->set_figure_title_callback([&figure_tabs](size_t fig_idx) -> std::string {
            if (figure_tabs && fig_idx < figure_tabs->get_tab_count()) {
                return figure_tabs->get_tab_title(fig_idx);
            }
            return "Figure " + std::to_string(fig_idx + 1);
        });

        // Dock system → tab bar sync: when a pane is clicked, update tab selection
        dock_system.split_view().set_on_active_changed(
            [&figure_tabs, &fig_mgr, &dock_tab_sync_guard](size_t figure_index) {
                if (dock_tab_sync_guard) return;
                dock_tab_sync_guard = true;
                if (figure_tabs && figure_index < figure_tabs->get_tab_count()) {
                    figure_tabs->set_active_tab(figure_index);
                }
                fig_mgr.queue_switch(figure_index);
                dock_tab_sync_guard = false;
            });

        // Wire Agent G: Timeline editor, keyframe interpolator, curve editor
        imgui_ui->set_timeline_editor(&timeline_editor);
        imgui_ui->set_keyframe_interpolator(&keyframe_interpolator);
        imgui_ui->set_curve_editor(&curve_editor);

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
            // Capture dock/split view state
            data.dock_state = dock_system.serialize();
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
                // Restore dock/split view state
                if (!data.dock_state.empty()) {
                    dock_system.deserialize(data.dock_state);
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

        // Animation commands — wired to TimelineEditor
        cmd_registry.register_command("anim.toggle_play", "Toggle Play/Pause", [&]() {
            timeline_editor.toggle_play();
        }, "Space", "Animation", static_cast<uint16_t>(ui::Icon::Play));

        cmd_registry.register_command("anim.step_back", "Step Frame Back", [&]() {
            timeline_editor.step_backward();
        }, "[", "Animation", static_cast<uint16_t>(ui::Icon::StepBackward));

        cmd_registry.register_command("anim.step_forward", "Step Frame Forward", [&]() {
            timeline_editor.step_forward();
        }, "]", "Animation", static_cast<uint16_t>(ui::Icon::StepForward));

        cmd_registry.register_command("anim.stop", "Stop Playback", [&]() {
            timeline_editor.stop();
        }, "", "Animation");

        cmd_registry.register_command("anim.go_to_start", "Go to Start", [&]() {
            timeline_editor.set_playhead(0.0f);
        }, "", "Animation");

        cmd_registry.register_command("anim.go_to_end", "Go to End", [&]() {
            timeline_editor.set_playhead(timeline_editor.duration());
        }, "", "Animation");

        // Panel toggle commands for timeline & curve editor
        cmd_registry.register_command("panel.toggle_timeline", "Toggle Timeline Panel", [&]() {
            if (imgui_ui) {
                imgui_ui->set_timeline_visible(!imgui_ui->is_timeline_visible());
            }
        }, "T", "Panel", static_cast<uint16_t>(ui::Icon::Play));

        cmd_registry.register_command("panel.toggle_curve_editor", "Toggle Curve Editor", [&]() {
            if (imgui_ui) {
                imgui_ui->set_curve_editor_visible(!imgui_ui->is_curve_editor_visible());
            }
        }, "", "Panel");

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

        // Split view commands (Agent A Week 9)
        // Splitting never creates new empty figures. Instead it redistributes
        // existing figure tabs between panes:
        //   First split:  all tabs → first pane, one tab moves → second pane
        //   Further splits: take a tab from the active pane → new pane
        auto do_split = [&](SplitDirection dir) {
            if (dock_system.is_split()) {
                // Already split — take a non-active tab from the active pane
                SplitPane* active_pane = dock_system.split_view().active_pane();
                if (!active_pane || active_pane->figure_count() < 2) return;

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
            } else {
                // Going from single view to split:
                // Need at least 2 figures to split without creating new ones
                if (figures_.size() < 2) return;

                size_t orig_active = fig_mgr.active_index();

                // Pick a figure to move to the second pane (first non-active)
                size_t move_fig = SIZE_MAX;
                for (size_t i = 0; i < figures_.size(); ++i) {
                    if (i != orig_active) { move_fig = i; break; }
                }
                if (move_fig == SIZE_MAX) return;

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
                if (new_pane) {
                    SplitPane* root = dock_system.split_view().root();
                    SplitPane* first_pane = root ? root->first() : nullptr;
                    if (first_pane && first_pane->is_leaf()) {
                        // Remove move_fig from first pane (it belongs in second only)
                        if (first_pane->has_figure(move_fig)) {
                            first_pane->remove_figure(move_fig);
                        }
                        // Add any remaining figures not yet in first pane
                        for (size_t i = 0; i < figures_.size(); ++i) {
                            if (i == move_fig) continue;
                            if (!first_pane->has_figure(i)) {
                                first_pane->add_figure(i);
                            }
                        }
                        // Restore the originally active figure as the active tab
                        for (size_t li = 0; li < first_pane->figure_indices().size(); ++li) {
                            if (first_pane->figure_indices()[li] == orig_active) {
                                first_pane->set_active_local_index(li);
                                break;
                            }
                        }
                    }
                }

                dock_system.set_active_figure_index(orig_active);
            }
        };

        cmd_registry.register_command("view.split_right", "Split Right", [&, do_split]() {
            do_split(SplitDirection::Horizontal);
        }, "Ctrl+\\", "View");

        cmd_registry.register_command("view.split_down", "Split Down", [&, do_split]() {
            do_split(SplitDirection::Vertical);
        }, "Ctrl+Shift+\\", "View");

        cmd_registry.register_command("view.close_split", "Close Split Pane", [&]() {
            if (dock_system.is_split()) {
                dock_system.close_split(dock_system.active_figure_index());
            }
        }, "", "View");

        cmd_registry.register_command("view.reset_splits", "Reset All Splits", [&]() {
            dock_system.reset_splits();
        }, "", "View");

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
    
    while (running) {
        PLOTIX_LOG_TRACE("main_loop", "Starting frame iteration");

#ifdef PLOTIX_USE_GLFW
        // Handle minimized window (0x0): sleep until restored
        if (glfw) {
            uint32_t fb_w = 0, fb_h = 0;
            glfw->framebuffer_size(fb_w, fb_h);
            while (fb_w == 0 || fb_h == 0) {
                glfw->wait_events();
                glfw->framebuffer_size(fb_w, fb_h);
                if (glfw->should_close()) { running = false; break; }
            }
            if (!running) break;
        }
#endif

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

#ifdef PLOTIX_USE_IMGUI
        // Advance timeline editor (drives playback + interpolator evaluation)
        timeline_editor.advance(scheduler.dt());
#endif

#ifdef PLOTIX_USE_GLFW
        // Update interaction animations (animated zoom, inertial pan, auto-fit)
        if (glfw) {
            input_handler.update(scheduler.dt());
        }
#endif

        // Ensure all axes have the deferred-deletion callback wired
        // BEFORE the user's on_frame callback can call clear_series().
        if (renderer_) {
            auto removal_cb = [this](const Series* s) { renderer_->notify_series_removed(s); };
            for (auto& axes_ptr : active_figure->axes()) {
                if (axes_ptr) axes_ptr->set_series_removed_callback(removal_cb);
            }
            for (auto& axes_ptr : active_figure->all_axes()) {
                if (axes_ptr) axes_ptr->set_series_removed_callback(removal_cb);
            }
        }

        // Call user on_frame callback
        if (has_animation && active_figure->anim_on_frame_) {
            Frame frame = scheduler.current_frame();
            active_figure->anim_on_frame_(frame);
        }

        // Start ImGui frame (updates layout manager with current window size).
        bool imgui_frame_started = false;
#ifdef PLOTIX_USE_IMGUI
        if (imgui_ui) {
            imgui_ui->new_frame();
            imgui_frame_started = true;
        }
#endif

#ifdef PLOTIX_USE_GLFW
        // Time-based resize debounce: recreate swapchain only when size has
        // stabilized (no new callback for RESIZE_DEBOUNCE ms). During drag,
        // we keep rendering with the old swapchain (slightly stretched but
        // no black flash). swapchain_dirty_ is set by present OUT_OF_DATE/SUBOPTIMAL.
        if (needs_resize && glfw) {
            auto now_resize = std::chrono::steady_clock::now();
            auto since_last = now_resize - resize_requested_time;
            if (since_last >= RESIZE_DEBOUNCE) {
                PLOTIX_LOG_INFO("resize", "Recreating swapchain: " +
                    std::to_string(new_width) + "x" + std::to_string(new_height));
                needs_resize = false;
                auto* vk = static_cast<VulkanBackend*>(backend_.get());
                vk->clear_swapchain_dirty();
                backend_->recreate_swapchain(new_width, new_height);

                active_figure->config_.width = backend_->swapchain_width();
                active_figure->config_.height = backend_->swapchain_height();
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui) {
                    imgui_ui->on_swapchain_recreated(*vk);
                }
#endif
            }
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

            // Old TabBar is replaced by unified pane tab headers
            // (drawn by draw_pane_tab_headers in ImGuiIntegration)
            // Always hide the layout manager's tab bar zone so canvas
            // extends into that space — pane headers draw in the canvas area.
            imgui_ui->get_layout_manager().set_tab_bar_visible(false);
            
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
            
            // Always hide old tab bar — unified pane tab headers handle all tabs
            imgui_ui->get_layout_manager().set_tab_bar_visible(false);
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

        // Sync root pane's figure_indices_ with actual figures when not split.
        // The unified pane tab headers always read from the root pane.
        if (!dock_system.is_split()) {
            SplitPane* root = dock_system.split_view().root();
            if (root && root->is_leaf()) {
                // Ensure root has exactly the right figures
                const auto& current = root->figure_indices();
                bool needs_sync = (current.size() != figures_.size());
                if (!needs_sync) {
                    for (size_t i = 0; i < figures_.size(); ++i) {
                        if (!root->has_figure(i)) { needs_sync = true; break; }
                    }
                }
                if (needs_sync) {
                    // Rebuild figure_indices_ to match actual figures
                    while (root->figure_count() > 0) {
                        root->remove_figure(root->figure_indices().back());
                    }
                    for (size_t i = 0; i < figures_.size(); ++i) {
                        root->add_figure(i);
                    }
                }
                // Sync active tab
                size_t active = fig_mgr.active_index();
                dock_system.set_active_figure_index(active);
                for (size_t li = 0; li < root->figure_indices().size(); ++li) {
                    if (root->figure_indices()[li] == active) {
                        root->set_active_local_index(li);
                        break;
                    }
                }
            }
        }
#endif

        // Compute subplot layout AFTER build_ui() so that nav rail / inspector
        // toggles from the current frame are immediately reflected.
        {
#ifdef PLOTIX_USE_IMGUI
            if (imgui_ui) {
                const Rect canvas = imgui_ui->get_layout_manager().canvas_rect();

                // Update dock system layout with current canvas bounds
                dock_system.update_layout(canvas);

                if (dock_system.is_split()) {
                    // Per-pane layout: each pane renders its own figure
                    auto pane_infos = dock_system.get_pane_infos();
                    for (const auto& pinfo : pane_infos) {
                        if (pinfo.figure_index < figures_.size()) {
                            auto* fig = figures_[pinfo.figure_index].get();
                            if (!fig) continue;
                            Margins pane_margins;
                            pane_margins.left   = std::min(60.0f, pinfo.bounds.w * 0.15f);
                            pane_margins.left   = std::max(pane_margins.left, 40.0f);
                            pane_margins.right  = std::min(30.0f, pinfo.bounds.w * 0.08f);
                            pane_margins.right  = std::max(pane_margins.right, 15.0f);
                            pane_margins.bottom = std::min(50.0f, pinfo.bounds.h * 0.15f);
                            pane_margins.bottom = std::max(pane_margins.bottom, 35.0f);
                            pane_margins.top    = std::min(35.0f, pinfo.bounds.h * 0.08f);
                            pane_margins.top    = std::max(pane_margins.top, 15.0f);
                            const auto rects = compute_subplot_layout(
                                pinfo.bounds.w, pinfo.bounds.h,
                                fig->grid_rows_, fig->grid_cols_,
                                pane_margins,
                                pinfo.bounds.x, pinfo.bounds.y);
                            for (size_t i = 0; i < fig->axes_mut().size() && i < rects.size(); ++i) {
                                if (fig->axes_mut()[i]) {
                                    fig->axes_mut()[i]->set_viewport(rects[i]);
                                }
                            }
                        }
                    }
                } else {
                    SplitPane* root = dock_system.split_view().root();
                    Rect cb = (root && root->is_leaf()) ? root->content_bounds() : canvas;
                    const auto rects = compute_subplot_layout(
                        cb.w, cb.h,
                        active_figure->grid_rows_, active_figure->grid_cols_,
                        {},
                        cb.x, cb.y);

                    for (size_t i = 0; i < active_figure->axes_mut().size() && i < rects.size(); ++i) {
                        if (active_figure->axes_mut()[i]) {
                            active_figure->axes_mut()[i]->set_viewport(rects[i]);
                        }
                    }
                    for (size_t i = 0; i < active_figure->all_axes_mut().size() && i < rects.size(); ++i) {
                        if (active_figure->all_axes_mut()[i]) {
                            active_figure->all_axes_mut()[i]->set_viewport(rects[i]);
                        }
                    }
                }
            } else {
                active_figure->compute_layout();
            }
#else
            active_figure->compute_layout();
#endif
        }

        // Render frame. If begin_frame fails (OUT_OF_DATE), recreate and
        // retry once so we present content immediately (no black-flash gap).
        bool frame_ok = backend_->begin_frame();

        if (!frame_ok) {
            // Swapchain truly unusable — recreate and retry
#ifdef PLOTIX_USE_IMGUI
            if (imgui_frame_started) {
                ImGui::EndFrame();
                imgui_frame_started = false;
            }
#endif
#ifdef PLOTIX_USE_GLFW
            if (glfw) {
                uint32_t fb_width = 0, fb_height = 0;
                glfw->framebuffer_size(fb_width, fb_height);
                if (fb_width > 0 && fb_height > 0) {
                    PLOTIX_LOG_INFO("resize", "OUT_OF_DATE, recreating: " +
                                   std::to_string(fb_width) + "x" + std::to_string(fb_height));
                    backend_->recreate_swapchain(fb_width, fb_height);
                    auto* vk_fb = static_cast<VulkanBackend*>(backend_.get());
                    vk_fb->clear_swapchain_dirty();
                    active_figure->config_.width = backend_->swapchain_width();
                    active_figure->config_.height = backend_->swapchain_height();
                    needs_resize = false;
#ifdef PLOTIX_USE_IMGUI
                    if (imgui_ui) {
                        imgui_ui->on_swapchain_recreated(*vk_fb);
                    }
#endif
                    // Retry begin_frame with the new swapchain
                    frame_ok = backend_->begin_frame();
                }
            }
#endif
        }

        if (frame_ok) {
            // begin_frame() just waited on the in-flight fence, so all GPU
            // work from DELETION_RING_SIZE frames ago is guaranteed complete.
            // Safe to free those deferred resources now.
            renderer_->flush_pending_deletions();

            renderer_->begin_render_pass();

#ifdef PLOTIX_USE_IMGUI
            if (dock_system.is_split()) {
                auto pane_infos = dock_system.get_pane_infos();
                for (const auto& pinfo : pane_infos) {
                    if (pinfo.figure_index < figures_.size() && figures_[pinfo.figure_index]) {
                        renderer_->render_figure_content(*figures_[pinfo.figure_index]);
                    }
                }
            } else {
                renderer_->render_figure_content(*active_figure);
            }
#else
            renderer_->render_figure_content(*active_figure);
#endif

#ifdef PLOTIX_USE_IMGUI
            // Only render ImGui if we have a valid frame (not a retry frame
            // where we already ended the ImGui frame)
            if (imgui_ui && imgui_frame_started) {
                imgui_ui->render(*static_cast<VulkanBackend*>(backend_.get()));
            }
#endif

            renderer_->end_render_pass();
            backend_->end_frame();
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

    // Ensure all GPU work is complete before destructors clean up resources
    if (backend_) {
        backend_->wait_idle();
    }
}

} // namespace plotix

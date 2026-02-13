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
#include "command_queue.hpp"
#include "input.hpp"

#ifdef PLOTIX_USE_GLFW
#include "glfw_adapter.hpp"
#endif

#ifdef PLOTIX_USE_IMGUI
#include "imgui_integration.hpp"
#include "tab_bar.hpp"
#include <imgui.h>
#endif

#include <iostream>
#include <memory>
#include <filesystem>

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
    std::unique_ptr<TabBar> figure_tabs;
    size_t pending_tab_switch = SIZE_MAX;
    size_t pending_tab_close = SIZE_MAX;
    bool pending_tab_add = false;
#endif

#ifdef PLOTIX_USE_GLFW
    std::unique_ptr<GlfwAdapter> glfw;
    InputHandler input_handler;
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

        // Synchronize tab labels with existing figures.
        figure_tabs->set_tab_title(0, "Figure 1");
        for (size_t i = 1; i < figures_.size(); ++i) {
            figure_tabs->add_tab("Figure " + std::to_string(i + 1));
        }
        figure_tabs->set_active_tab(active_figure_index);

        figure_tabs->set_tab_change_callback([&pending_tab_switch](size_t new_index) {
            pending_tab_switch = new_index;
        });
        figure_tabs->set_tab_close_callback([&pending_tab_close](size_t index) {
            pending_tab_close = index;
        });
        figure_tabs->set_tab_add_callback([&pending_tab_add]() {
            pending_tab_add = true;
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

        // Call user on_frame callback
        if (has_animation && active_figure->anim_on_frame_) {
            Frame frame = scheduler.current_frame();
            active_figure->anim_on_frame_(frame);
        }

        // Update ImGui layout manager BEFORE computing subplot layout so
        // canvas_rect() reflects the current window size.
        bool imgui_frame_started = false;
#ifdef PLOTIX_USE_IMGUI
        bool should_update_imgui = !is_resizing || (resize_frame_counter % 6 == 0);
        if (imgui_ui && should_update_imgui) {
            imgui_ui->new_frame();  // updates layout manager with current window size
            imgui_frame_started = true;
        }
#endif

        // Compute layout (skip during active resize for better performance)
        if (!is_resizing) {
#ifdef PLOTIX_USE_IMGUI
            // UI-aware layout: place subplots inside canvas content region.
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
                // Reset all axes to auto-fit
                for (auto& ax : active_figure->axes_mut()) {
                    if (ax) {
                        ax->auto_fit();
                    }
                }
                imgui_ui->clear_reset_view();
            }
            
            // Update input handler tool mode
            input_handler.set_tool_mode(imgui_ui->get_interaction_mode());
        }
#endif

#ifdef PLOTIX_USE_IMGUI
        if (pending_tab_add) {
            FigureConfig cfg = active_figure->config_;
            figures_.push_back(std::make_unique<Figure>(cfg));
            size_t new_index = figures_.size() - 1;
            if (figure_tabs) {
                figure_tabs->set_tab_title(new_index, "Figure " + std::to_string(new_index + 1));
            }
            pending_tab_switch = new_index;
            pending_tab_add = false;
        }

        if (pending_tab_close != SIZE_MAX && figures_.size() > 1 && pending_tab_close < figures_.size()) {
            figures_.erase(figures_.begin() + static_cast<std::ptrdiff_t>(pending_tab_close));
            if (active_figure_index >= figures_.size()) {
                active_figure_index = figures_.size() - 1;
            } else if (active_figure_index > pending_tab_close) {
                --active_figure_index;
            }
            switch_active_figure(active_figure_index
#ifdef PLOTIX_USE_GLFW
                                 , glfw ? &input_handler : nullptr
#endif
            );
            pending_tab_close = SIZE_MAX;
            pending_tab_switch = active_figure_index;
        }

        if (pending_tab_switch != SIZE_MAX && pending_tab_switch < figures_.size()) {
            switch_active_figure(pending_tab_switch
#ifdef PLOTIX_USE_GLFW
                                 , glfw ? &input_handler : nullptr
#endif
            );
            pending_tab_switch = SIZE_MAX;
        }
#endif

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

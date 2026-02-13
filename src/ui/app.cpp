#include <plotix/app.hpp>
#include <plotix/animator.hpp>
#include <plotix/export.hpp>
#include <plotix/figure.hpp>
#include <plotix/frame.hpp>
#include <plotix/logger.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "command_queue.hpp"
#include "input.hpp"

#ifdef PLOTIX_USE_GLFW
#include "glfw_adapter.hpp"
#endif

#ifdef PLOTIX_USE_IMGUI
#include "imgui_integration.hpp"
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

    // For now, drive the first figure
    Figure& fig = *figures_[0];

    CommandQueue cmd_queue;
    FrameScheduler scheduler(fig.anim_fps_);
    Animator animator;

    bool has_animation = static_cast<bool>(fig.anim_on_frame_);
    bool running = true;

#ifdef PLOTIX_USE_FFMPEG
    bool is_recording = !fig.video_record_path_.empty();
#else
    if (!fig.video_record_path_.empty()) {
        std::cerr << "[plotix] Video recording requested but PLOTIX_USE_FFMPEG is not enabled\n";
    }
#endif

#ifdef PLOTIX_USE_FFMPEG
    std::unique_ptr<VideoExporter> video_exporter;
    std::vector<uint8_t> video_frame_pixels;
    if (is_recording) {
        VideoExporter::Config vcfg;
        vcfg.output_path = fig.video_record_path_;
        vcfg.width  = fig.width();
        vcfg.height = fig.height();
        vcfg.fps    = fig.anim_fps_;
        video_exporter = std::make_unique<VideoExporter>(vcfg);
        if (!video_exporter->is_open()) {
            std::cerr << "[plotix] Failed to open video exporter for: "
                      << fig.video_record_path_ << "\n";
            video_exporter.reset();
        } else {
            video_frame_pixels.resize(
                static_cast<size_t>(fig.width()) * fig.height() * 4);
        }
        // Recording always runs headless
        if (!config_.headless) {
            config_.headless = true;
        }
    }
#endif

#ifdef PLOTIX_USE_IMGUI
    std::unique_ptr<ImGuiIntegration> imgui_ui;
#endif

#ifdef PLOTIX_USE_GLFW
    std::unique_ptr<GlfwAdapter> glfw;
    InputHandler input_handler;
    bool needs_resize = false;
    uint32_t new_width = fig.width();
    uint32_t new_height = fig.height();
    bool is_resizing = false;
    int resize_frame_counter = 0;
    static constexpr int RESIZE_SKIP_FRAMES = 1;  // Skip frames during rapid resize

    if (!config_.headless) {
        glfw = std::make_unique<GlfwAdapter>();
        if (!glfw->init(fig.width(), fig.height(), "Plotix")) {
            std::cerr << "[plotix] Failed to create GLFW window\n";
            glfw.reset();
        } else {
            // Create Vulkan surface from GLFW window
            backend_->create_surface(glfw->native_window());
            backend_->create_swapchain(fig.width(), fig.height());

            // Wire input handler — set figure for multi-axes hit-testing
            input_handler.set_figure(&fig);
            if (!fig.axes().empty() && fig.axes()[0]) {
                input_handler.set_active_axes(fig.axes()[0].get());
                auto& vp = fig.axes()[0]->viewport();
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
                if (imgui_ui && imgui_ui->wants_capture_mouse()) return;
#endif
                input_handler.on_mouse_move(x, y);
            };
            callbacks.on_mouse_button = [&input_handler
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui
#endif
            ](int button, int action, double x, double y) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && imgui_ui->wants_capture_mouse()) return;
#endif
                input_handler.on_mouse_button(button, action, x, y);
            };
            callbacks.on_scroll = [&input_handler, &glfw
#ifdef PLOTIX_USE_IMGUI
                , &imgui_ui
#endif
            ](double x_offset, double y_offset) {
#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui && imgui_ui->wants_capture_mouse()) return;
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
    }
#endif

    if (config_.headless) {
        backend_->create_offscreen_framebuffer(fig.width(), fig.height());
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
        if (has_animation && fig.anim_on_frame_) {
            Frame frame = scheduler.current_frame();
            fig.anim_on_frame_(frame);
        }

        // Compute layout (skip during active resize for better performance)
        if (!is_resizing) {
#ifdef PLOTIX_USE_IMGUI
            // Account for menu bar height in figure layout
            float menubar_offset = 0.0f;
            if (imgui_ui) {
                menubar_offset = 52.0f + 12.0f; // menubar_height + margin
            }
            
            // Temporarily adjust figure height for layout computation
            auto original_height = fig.config_.height;
            fig.config_.height = static_cast<uint32_t>(std::max(1.0f, static_cast<float>(original_height) - menubar_offset));
            
            fig.compute_layout();
            
            // Restore original height
            fig.config_.height = original_height;
            
            // Adjust all axes viewports to account for menu bar offset
            for (auto& ax : fig.axes_mut()) {
                if (ax) {
                    auto vp = ax->viewport();
                    vp.y += menubar_offset;
                    ax->set_viewport(vp);
                }
            }
#else
            fig.compute_layout();
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
                fig.config_.width = backend_->swapchain_width();
                fig.config_.height = backend_->swapchain_height();
                PLOTIX_LOG_INFO("resize", "Swapchain recreated, actual extent: " + std::to_string(fig.config_.width) + "x" + std::to_string(fig.config_.height));

#ifdef PLOTIX_USE_IMGUI
                if (imgui_ui) {
                    imgui_ui->on_swapchain_recreated(
                        *static_cast<VulkanBackend*>(backend_.get()));
                }
#endif
                
                // Recompute layout with new dimensions
                fig.compute_layout();
                
                // Update input handler viewport after layout recompute
                if (!fig.axes().empty() && fig.axes()[0]) {
                    auto& vp = fig.axes()[0]->viewport();
                    input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
                }
            }
            
            // End the frame we started for resize checking
            backend_->end_frame();
        } else if (!needs_resize) {
            resize_frame_counter = 0;
        }

        // Update input handler with current active axes viewport
        if (glfw && !fig.axes().empty() && fig.axes()[0]) {
            auto& vp = fig.axes()[0]->viewport();
            input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
#endif

        bool imgui_frame_started = false;
        
#ifdef PLOTIX_USE_IMGUI
        // Start ImGui frame before rendering (skip during active resize for better performance)
        // Also reduce ImGui update frequency during resize
        bool should_update_imgui = !is_resizing || (resize_frame_counter % 6 == 0);
        if (imgui_ui && should_update_imgui) {
            imgui_ui->new_frame();
            imgui_ui->build_ui(fig);
            
            // Handle interaction state from UI
            if (imgui_ui->should_reset_view()) {
                // Reset all axes to auto-fit
                for (auto& ax : fig.axes_mut()) {
                    if (ax) {
                        ax->auto_fit();
                    }
                }
                imgui_ui->clear_reset_view();
            }
            
            // Update input handler mode
            input_handler.set_mode(imgui_ui->get_interaction_mode());
            
            imgui_frame_started = true;
        }
#endif

        // Render (skip drawing if swapchain is stale, but keep the loop going)
        // During active resize, only render every few frames to maintain responsiveness
        // Note: begin_frame might have been called already in resize processing above
        bool already_begun_frame = (needs_resize && resize_frame_counter > 0);
        bool should_render = !is_resizing || (resize_frame_counter % 3 == 0);
        
        if (should_render && (already_begun_frame || backend_->begin_frame())) {
            PLOTIX_LOG_TRACE("resize", "begin_frame succeeded, rendering frame");
            renderer_->render_figure(fig);

#ifdef PLOTIX_USE_IMGUI
            // Render ImGui overlay (inside the render pass, after plot content)
            if (imgui_ui && imgui_frame_started) {
                imgui_ui->render(*static_cast<VulkanBackend*>(backend_.get()));
            }
#endif

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
                    fig.config_.width = backend_->swapchain_width();
                    fig.config_.height = backend_->swapchain_height();
                    PLOTIX_LOG_INFO("resize", "Fallback recreation complete, actual extent: " + std::to_string(fig.config_.width) + "x" + std::to_string(fig.config_.height));
#ifdef PLOTIX_USE_IMGUI
                    if (imgui_ui) {
                        imgui_ui->on_swapchain_recreated(
                            *static_cast<VulkanBackend*>(backend_.get()));
                    }
#endif
                    fig.compute_layout();
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
                                                fig.width(), fig.height())) {
                video_exporter->write_frame(video_frame_pixels.data());
            }
        }
#endif

        scheduler.end_frame();

        // Check termination conditions
        if (fig.anim_duration_ > 0.0f &&
            scheduler.elapsed_seconds() >= fig.anim_duration_ &&
            !fig.anim_loop_) {
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
            bool needs_render = (&f != &fig) ||
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

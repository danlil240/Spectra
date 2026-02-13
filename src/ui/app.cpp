#include <plotix/app.hpp>
#include <plotix/animator.hpp>
#include <plotix/export.hpp>
#include <plotix/figure.hpp>
#include <plotix/frame.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "command_queue.hpp"
#include "input.hpp"

#ifdef PLOTIX_USE_GLFW
#include "glfw_adapter.hpp"
#endif

#include <iostream>
#include <memory>

namespace plotix {

// ─── App ─────────────────────────────────────────────────────────────────────

App::App(const AppConfig& config)
    : config_(config)
{
    // Create Vulkan backend
    backend_ = std::make_unique<VulkanBackend>();
    if (!backend_->init(config_.headless)) {
        std::cerr << "[plotix] Failed to initialize Vulkan backend\n";
        return;
    }

    // Create renderer
    renderer_ = std::make_unique<Renderer>(*backend_);
    if (!renderer_->init()) {
        std::cerr << "[plotix] Failed to initialize renderer\n";
        return;
    }
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

#ifdef PLOTIX_USE_GLFW
    std::unique_ptr<GlfwAdapter> glfw;
    InputHandler input_handler;
    bool needs_resize = false;
    uint32_t new_width = fig.width();
    uint32_t new_height = fig.height();

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
            callbacks.on_mouse_move = [&input_handler](double x, double y) {
                input_handler.on_mouse_move(x, y);
            };
            callbacks.on_mouse_button = [&input_handler](int button, int action, double x, double y) {
                input_handler.on_mouse_button(button, action, x, y);
            };
            callbacks.on_scroll = [&input_handler, &glfw](double x_offset, double y_offset) {
                double cx = 0.0, cy = 0.0;
                if (glfw) {
                    glfw->mouse_position(cx, cy);
                }
                input_handler.on_scroll(x_offset, y_offset, cx, cy);
            };
            callbacks.on_key = [&input_handler](int key, int action, int mods) {
                input_handler.on_key(key, action, mods);
            };
            callbacks.on_resize = [&needs_resize, &new_width, &new_height](int w, int h) {
                if (w > 0 && h > 0) {
                    needs_resize = true;
                    new_width = static_cast<uint32_t>(w);
                    new_height = static_cast<uint32_t>(h);
                }
            };
            glfw->set_callbacks(callbacks);
        }
    }
#endif

    if (config_.headless) {
        backend_->create_offscreen_framebuffer(fig.width(), fig.height());
    }

    // Now that render pass exists, create real Vulkan pipelines from SPIR-V
    static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

    scheduler.reset();

    while (running) {
        scheduler.begin_frame();

        // Drain command queue (apply app-thread mutations)
        cmd_queue.drain();

        // Evaluate keyframe animations
        animator.evaluate(scheduler.elapsed_seconds());

        // Call user on_frame callback
        if (has_animation && fig.anim_on_frame_) {
            Frame frame = scheduler.current_frame();
            fig.anim_on_frame_(frame);
        }

        // Compute layout
        fig.compute_layout();

#ifdef PLOTIX_USE_GLFW
        // Handle window resize
        if (needs_resize) {
            needs_resize = false;
            backend_->recreate_swapchain(new_width, new_height);
            // Update input handler viewport after layout recompute
            if (!fig.axes().empty() && fig.axes()[0]) {
                auto& vp = fig.axes()[0]->viewport();
                input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
            }
        }

        // Update input handler with current active axes viewport
        if (glfw && !fig.axes().empty() && fig.axes()[0]) {
            auto& vp = fig.axes()[0]->viewport();
            input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
#endif

        // Render
        if (backend_->begin_frame()) {
            renderer_->render_figure(fig);
            backend_->end_frame();
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
            running = false;
        }

#ifdef PLOTIX_USE_GLFW
        if (glfw) {
            glfw->poll_events();
            if (glfw->should_close()) {
                running = false;
            }
        }
#endif
    }

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

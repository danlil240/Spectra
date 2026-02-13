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

            // Wire input handler — default to first axes if available
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

    // Export PNG if requested (headless mode)
    if (config_.headless && !fig.png_export_path_.empty()) {
        uint32_t w = fig.width();
        uint32_t h = fig.height();
        std::vector<uint8_t> pixels(w * h * 4);
        if (backend_->readback_framebuffer(pixels.data(), w, h)) {
            if (ImageExporter::write_png(fig.png_export_path_, pixels.data(), w, h)) {
                // success
            } else {
                std::cerr << "[plotix] Failed to write PNG: " << fig.png_export_path_ << "\n";
            }
        } else {
            std::cerr << "[plotix] Failed to readback framebuffer\n";
        }
    }

#ifdef PLOTIX_USE_GLFW
    if (glfw) {
        glfw->shutdown();
    }
#endif
}

} // namespace plotix

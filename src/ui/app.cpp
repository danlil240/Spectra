#include <plotix/app.hpp>
#include <plotix/animator.hpp>
#include <plotix/figure.hpp>
#include <plotix/frame.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "command_queue.hpp"

#ifdef PLOTIX_USE_GLFW
#include "glfw_adapter.hpp"
#endif

#include <iostream>

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
    GlfwAdapter* glfw = nullptr;
    if (!config_.headless) {
        glfw = new GlfwAdapter();
        if (!glfw->init(fig.width(), fig.height(), "Plotix")) {
            std::cerr << "[plotix] Failed to create GLFW window\n";
            delete glfw;
            glfw = nullptr;
        } else {
            // Create Vulkan surface from GLFW window
            backend_->create_surface(glfw->native_window());
            backend_->create_swapchain(fig.width(), fig.height());
        }
    }
#endif

    if (config_.headless) {
        backend_->create_offscreen_framebuffer(fig.width(), fig.height());
    }

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

#ifdef PLOTIX_USE_GLFW
    if (glfw) {
        glfw->shutdown();
        delete glfw;
    }
#endif
}

} // namespace plotix

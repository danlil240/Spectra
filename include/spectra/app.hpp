#pragma once

#include <memory>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>

#include "../src/ui/figure_registry.hpp"

namespace spectra
{

struct AppConfig
{
    bool headless = false;
};

#ifdef SPECTRA_USE_GLFW
class GlfwAdapter;
class WindowManager;
#endif

class App
{
   public:
    explicit App(const AppConfig& config = {});
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    Figure& figure(const FigureConfig& config = {});

    // Run the application (blocking — processes all figures)
    void run();

    bool is_headless() const { return config_.headless; }

    // Access internals (for renderer integration)
    Backend* backend() { return backend_.get(); }
    Renderer* renderer() { return renderer_.get(); }

   private:
    // Per-frame mutable state passed between update_window / render_window.
    struct FrameState
    {
        Figure* active_figure = nullptr;
        FigureId active_figure_id = INVALID_FIGURE_ID;
        bool has_animation = false;
        float anim_time = 0.0f;
        bool imgui_frame_started = false;
    };

    // Update all per-window UI subsystems for one frame (animation, ImGui,
    // layout computation).  Called once per window per frame.
    void update_window(WindowUIContext& ui_ctx, FrameState& fs, FrameScheduler& scheduler
#ifdef SPECTRA_USE_GLFW
                       , GlfwAdapter* glfw, WindowManager* window_mgr
#endif
    );

    // Render one window's content (begin_frame, render pass, ImGui, end_frame).
    // Returns true if the frame was successfully presented.
    bool render_window(WindowUIContext& ui_ctx, FrameState& fs
#ifdef SPECTRA_USE_GLFW
                       , GlfwAdapter* glfw
#endif
    );

    // Render a secondary window (no ImGui, figure-only).
    void render_secondary_window(struct WindowContext* wctx);

    AppConfig config_;
    FigureRegistry registry_;
    std::unique_ptr<Backend> backend_;
    std::unique_ptr<Renderer> renderer_;
};

}  // namespace spectra

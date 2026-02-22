#pragma once

#include <spectra/fwd.hpp>

#include "../anim/frame_profiler.hpp"

namespace spectra
{

class Backend;
class Renderer;
class FigureRegistry;
class FrameScheduler;
struct WindowContext;
struct WindowUIContext;

#ifdef SPECTRA_USE_GLFW
class WindowManager;
#endif

// Per-window mutable state passed between update / render.
struct FrameState
{
    Figure* active_figure = nullptr;
    FigureId active_figure_id = INVALID_FIGURE_ID;
    bool has_animation = false;
    float anim_time = 0.0f;
    bool imgui_frame_started = false;
};

// Per-window event loop body.
// Wraps the update + render cycle for a single window.
// Extracted from App::run() (Phase 2) so the same logic can be
// reused by both the in-process runtime and a future window-agent.
class WindowRuntime
{
   public:
    WindowRuntime(Backend& backend, Renderer& renderer, FigureRegistry& registry);

    // Advance animations, build ImGui UI, compute layout for one window.
    void update(WindowUIContext& ui_ctx,
                FrameState& fs,
                FrameScheduler& scheduler,
                FrameProfiler* profiler = nullptr
#ifdef SPECTRA_USE_GLFW
                ,
                WindowManager* window_mgr = nullptr
#endif
    );

    // Render one window: begin_frame, render pass, figure content, ImGui, end_frame.
    // Returns true if the frame was successfully presented.
    bool render(WindowUIContext& ui_ctx, FrameState& fs, FrameProfiler* profiler = nullptr);

   private:
    Backend& backend_;
    Renderer& renderer_;
    FigureRegistry& registry_;
};

}  // namespace spectra

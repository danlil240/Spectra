#pragma once

#include <cstdint>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

#include "anim/frame_profiler.hpp"
#include "window_runtime.hpp"

namespace spectra
{

class Backend;
class Renderer;
class FigureRegistry;
class FrameScheduler;
class Animator;
class CommandQueue;
struct WindowContext;
struct WindowUIContext;

#ifdef SPECTRA_USE_GLFW
class GlfwAdapter;
class WindowManager;
#endif

// Deferred tab-detach request (queued during ImGui frame, processed after).
struct PendingDetach
{
    FigureId    figure_id = INVALID_FIGURE_ID;
    uint32_t    width     = 800;
    uint32_t    height    = 600;
    std::string title;
    int         screen_x = 0;
    int         screen_y = 0;
};

// Deferred cross-window move request.
struct PendingMove
{
    FigureId figure_id        = INVALID_FIGURE_ID;
    uint32_t target_window_id = 0;
    int      drop_zone        = 0;      // 0=None/Center(add tab), 1=Left, 2=Right, 3=Top, 4=Bottom
    float    local_x          = 0.0f;   // Cursor position in target window's local coords
    float    local_y          = 0.0f;
    FigureId target_figure_id = INVALID_FIGURE_ID;   // figure in the pane under cursor
};

// Session-level orchestration.
// Owns the per-frame loop body: poll events, process pending closes,
// iterate windows (via WindowRuntime), and check exit condition.
// Extracted from App::run() (Phase 2) so the same code can be used
// by both the in-process runtime and a future standalone backend process.
class SessionRuntime
{
   public:
    SessionRuntime(Backend& backend, Renderer& renderer, FigureRegistry& registry);
    ~SessionRuntime();

    // Non-copyable, non-movable
    SessionRuntime(const SessionRuntime&)            = delete;
    SessionRuntime& operator=(const SessionRuntime&) = delete;

    // Access the window runtime (for callers that need per-window control).
    WindowRuntime& window_runtime() { return win_rt_; }

    // Access the frame scheduler.
    FrameScheduler& scheduler();

    // Queue a deferred detach request (called from ImGui callbacks).
    void queue_detach(PendingDetach pd);

    // Queue a deferred cross-window move (called from TabDragController).
    void queue_move(PendingMove pm);

    // Execute one tick of the session loop:
    //   1. begin_frame (scheduler)
    //   2. drain command queue
    //   3. evaluate animations
    //   4. for each window: update + render via WindowRuntime
    //   5. process deferred detaches
    //   6. poll events + process pending closes
    // The caller provides the GLFW/WindowManager pointers and the
    // headless flag.  Returns the updated FrameState for the initial window.
    FrameState tick(FrameScheduler&  scheduler,
                    Animator&        animator,
                    CommandQueue&    cmd_queue,
                    bool             headless,
                    WindowUIContext* headless_ui_ctx,
#ifdef SPECTRA_USE_GLFW
                    WindowManager* window_mgr,
#endif
                    FrameState& frame_state);

    // Returns true when the session should exit (no windows remain open,
    // or headless single-frame completed).
    bool should_exit() const { return !running_; }

    // Mark the session as done (called by external termination conditions).
    void request_exit() { running_ = false; }

   private:
    Backend&        backend_;
    Renderer&       renderer_;
    FigureRegistry& registry_;
    WindowRuntime   win_rt_;

    bool running_ = true;

    // IDs of windows created this frame (skip their first render).
    std::vector<uint32_t> newly_created_window_ids_;

    // Deferred detach requests.
    std::vector<PendingDetach> pending_detaches_;

    // Deferred cross-window move requests.
    std::vector<PendingMove> pending_moves_;

    // DEBUG-only per-frame performance profiler.
    FrameProfiler profiler_{600};
};

}   // namespace spectra

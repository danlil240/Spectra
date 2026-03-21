#pragma once

// redraw_tracker.hpp — Event-driven rendering support.
// Tracks whether the application needs to redraw.  When nothing has changed
// (no animation, no user input, no commands, no data mutation) the main loop
// can sleep via glfwWaitEventsTimeout() instead of spinning at full frame rate.
//
// Usage:
//   tracker.mark_dirty("user input");   // something changed
//   if (tracker.needs_redraw())          // should we render this frame?
//       ...render...
//   tracker.end_frame();                 // advance grace counter

#include <spectra/logger.hpp>

#include <cstdint>
#include <string>

namespace spectra
{

class RedrawTracker
{
   public:
    // Mark the frame as needing a redraw.
    // `reason` is logged at TRACE level for debugging.
    void mark_dirty(const char* reason = "unknown")
    {
        if (!dirty_)
        {
            SPECTRA_LOG_DEBUG("redraw", std::string("dirty: ") + reason);
        }
        dirty_        = true;
        grace_frames_ = GRACE_FRAME_COUNT;
    }

    // Returns true if the current frame should be rendered.
    bool needs_redraw() const { return dirty_ || grace_frames_ > 0 || always_on_; }

    // Returns true if we are currently idle (can use WaitEvents).
    bool is_idle() const { return !needs_redraw(); }

    // Call at the end of each frame to decrement the grace counter.
    void end_frame()
    {
        dirty_ = false;
        if (grace_frames_ > 0)
            --grace_frames_;
    }

    // Force continuous rendering (e.g. during animation).
    void set_continuous(bool on) { always_on_ = on; }
    bool is_continuous() const { return always_on_; }

   private:
    bool     dirty_        = true;    // Something changed this frame
    uint32_t grace_frames_ = 10;      // Extra frames after last dirty (let ImGui settle)
    bool     always_on_    = false;   // Animation or other continuous source active

    // Number of extra frames to render after the last dirty event.
    // ImGui needs a few frames to settle hover states, tooltips, etc.
    static constexpr uint32_t GRACE_FRAME_COUNT = 3;
};

}   // namespace spectra

#pragma once

// fps_badge.hpp — State/label logic for the status-bar FPS badge.
//
// Spectra renders on demand: when nothing animates or streams, the main loop
// sleeps and only draws a handful of frames per input event.  A naive frame
// rate (ImGui's io.Framerate) then reads "0 fps" / "1 fps", which looks broken.
//
// FpsBadgeState watches the timestamps of rendered frames and shows the live
// frame rate only while rendering is *sustained* (animation, streaming data,
// continuous interaction).  Otherwise it shows "Idle".
//
//   * A frame counts as continuous when it follows the previous one within
//     kContinuousFrameMaxS (i.e. >= 4 fps).
//   * The badge goes live after kEnterLiveS of uninterrupted continuous frames,
//     so the short bursts produced by a click or hover never switch it.
//   * It drops back to Idle only after kHoldS without a continuous frame, so it
//     does not flicker on each input event.
//   * Because no frame is drawn while the app sleeps, needs_settle_frame()
//     reports when the badge is stale (still live but past the hold); the
//     caller requests one redraw so the badge can switch to Idle.

#include <chrono>
#include <cmath>
#include <optional>
#include <string>

namespace spectra::ui
{

class FpsBadgeState
{
   public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr double kContinuousFrameMaxS = 0.25;
    static constexpr double kEnterLiveS          = 0.5;
    static constexpr double kHoldS               = 1.0;
    static constexpr double kSmoothing           = 0.1;   // EMA weight of the newest frame

    // Call once per rendered frame.
    void on_frame(TimePoint now)
    {
        if (!prev_frame_)
        {
            prev_frame_ = now;
            return;
        }

        const double    dt   = seconds(now - *prev_frame_);
        const TimePoint prev = *prev_frame_;
        prev_frame_          = now;

        if (dt > 0.0 && dt <= kContinuousFrameMaxS)
        {
            if (!run_start_)
                run_start_ = prev;
            last_continuous_  = now;
            const double inst = 1.0 / dt;
            fps_              = fps_ > 0.0 ? fps_ + (inst - fps_) * kSmoothing : inst;
            if (!live_ && seconds(now - *run_start_) >= kEnterLiveS)
                live_ = true;
        }
        else
        {
            run_start_.reset();
            if (!live_)
                fps_ = 0.0;
        }

        if (live_ && (!last_continuous_ || seconds(now - *last_continuous_) > kHoldS))
        {
            live_ = false;
            fps_  = 0.0;
        }
    }

    // True while the badge shows the measured frame rate.
    bool is_live() const { return live_; }

    // Smoothed frame rate over continuous frames (0 when idle).
    double fps() const { return live_ ? fps_ : 0.0; }

    // "Idle" or e.g. "60 fps".
    std::string label() const
    {
        if (!live_)
            return "Idle";
        return std::to_string(static_cast<int>(std::lround(fps_))) + " fps";
    }

    // True when the badge still reads live but rendering stopped longer than
    // the hold time ago: one more frame is needed to show "Idle".
    bool needs_settle_frame(TimePoint now) const
    {
        return live_ && last_continuous_ && seconds(now - *last_continuous_) > kHoldS;
    }

   private:
    static double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

    std::optional<TimePoint> prev_frame_;
    std::optional<TimePoint> run_start_;
    std::optional<TimePoint> last_continuous_;
    double                   fps_  = 0.0;
    bool                     live_ = false;
};

}   // namespace spectra::ui

#pragma once

#include <algorithm>
#include <chrono>
#include <optional>

namespace spectra
{

class AnimationTickGate
{
   public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void clear()
    {
        next_due_.reset();
        slack_      = Clock::duration::zero();
        pending_dt_ = 0.0f;
    }

    bool active() const { return next_due_.has_value(); }

    void accumulate_dt(float dt) { pending_dt_ += std::max(0.0f, dt); }

    float accumulated_dt() const { return pending_dt_; }

    float consume_accumulated_dt()
    {
        float dt    = pending_dt_;
        pending_dt_ = 0.0f;
        return dt;
    }

    // A tick is due once `now` is within a quarter period of the deadline. Frames
    // arrive on vsync, so a frame a hair before the deadline must still tick:
    // otherwise the tick slips a whole refresh and a 175 fps animation on a
    // 174 Hz display runs at about half rate.
    bool should_tick(TimePoint now) const
    {
        return !next_due_.has_value() || now + slack_ >= *next_due_;
    }

    void schedule_next(TimePoint now, float fps)
    {
        if (fps <= 0.0f)
        {
            clear();
            return;
        }

        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / static_cast<double>(fps)));
        slack_ = period / 4;

        // Keep a fixed cadence from the previous deadline, so the time spent
        // rendering and presenting does not stretch every period. The deadline
        // never trails `now`, so after a stall only the next frame ticks early
        // and the cadence then resumes (no burst of catch-up ticks).
        if (next_due_.has_value())
            next_due_ = std::max(*next_due_ + period, now);
        else
            next_due_ = now + period;
    }

    double wait_timeout_seconds(TimePoint now, double fallback_s) const
    {
        fallback_s = std::max(0.0, fallback_s);
        if (!next_due_.has_value())
            return fallback_s;
        const TimePoint tick_at = *next_due_ - slack_;   // as should_tick() decides
        if (now >= tick_at)
            return 0.0;

        double remaining_s = std::chrono::duration<double>(tick_at - now).count();
        return std::min(fallback_s, remaining_s);
    }

   private:
    std::optional<TimePoint> next_due_;
    Clock::duration          slack_      = Clock::duration::zero();
    float                    pending_dt_ = 0.0f;
};

}   // namespace spectra

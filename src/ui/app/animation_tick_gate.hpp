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

    bool should_tick(TimePoint now) const { return !next_due_.has_value() || now >= *next_due_; }

    void schedule_next(TimePoint now, float fps)
    {
        if (fps <= 0.0f)
        {
            clear();
            return;
        }

        next_due_ = now
                    + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(1.0 / static_cast<double>(fps)));
    }

    double wait_timeout_seconds(TimePoint now, double fallback_s) const
    {
        fallback_s = std::max(0.0, fallback_s);
        if (!next_due_.has_value())
            return fallback_s;
        if (now >= *next_due_)
            return 0.0;

        double remaining_s = std::chrono::duration<double>(*next_due_ - now).count();
        return std::min(fallback_s, remaining_s);
    }

   private:
    std::optional<TimePoint> next_due_;
    float                    pending_dt_ = 0.0f;
};

}   // namespace spectra

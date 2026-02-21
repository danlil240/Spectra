#include "frame_scheduler.hpp"

#include <spectra/logger.hpp>
#include <thread>

namespace spectra
{

FrameScheduler::FrameScheduler(float target_fps, Mode mode) : target_fps_(target_fps), mode_(mode)
{
    reset();
}

void FrameScheduler::set_target_fps(float fps)
{
    if (fps > 0.0f)
    {
        target_fps_ = fps;
    }
}

void FrameScheduler::set_fixed_timestep(float dt)
{
    use_fixed_timestep_ = true;
    fixed_dt_ = dt;
    accumulator_ = 0.0f;
}

void FrameScheduler::clear_fixed_timestep()
{
    use_fixed_timestep_ = false;
    accumulator_ = 0.0f;
}

void FrameScheduler::begin_frame()
{
    SPECTRA_LOG_TRACE("scheduler", "begin_frame called");
    frame_start_ = Clock::now();

    if (first_frame_)
    {
        first_frame_ = false;
        start_time_ = frame_start_;
        last_frame_start_ = frame_start_;
        last_frame_end_ = frame_start_;
        frame_.dt = 0.0f;
        frame_.elapsed_sec = 0.0f;
        frame_.number = 0;
        return;
    }

    Duration elapsed_since_start = frame_start_ - start_time_;
    Duration dt_duration = frame_start_ - last_frame_start_;
    last_frame_start_ = frame_start_;

    float raw_dt = static_cast<float>(dt_duration.count());

    // Clamp dt to avoid spiral of death
    if (raw_dt > 0.25f)
    {
        raw_dt = 0.25f;
    }

    if (use_fixed_timestep_)
    {
        accumulator_ += raw_dt;
        frame_.dt = fixed_dt_;
    }
    else
    {
        frame_.dt = raw_dt;
    }

    frame_.elapsed_sec = static_cast<float>(elapsed_since_start.count());
    frame_.number++;

    // Track frame timing stats for hitch detection
    update_stats(raw_dt * 1000.0f);
}

void FrameScheduler::end_frame()
{
    SPECTRA_LOG_TRACE("scheduler", "end_frame called");
    last_frame_end_ = Clock::now();

    if (mode_ == Mode::TargetFPS && target_fps_ > 0.0f)
    {
        Duration target_frame_time{1.0 / static_cast<double>(target_fps_)};
        Duration frame_duration = last_frame_end_ - frame_start_;

        if (frame_duration < target_frame_time)
        {
            Duration remaining = target_frame_time - frame_duration;

            // Sleep for most of the remaining time (leave 1ms for spin-wait)
            auto sleep_time = remaining - Duration{0.001};
            if (sleep_time.count() > 0.0)
            {
                std::this_thread::sleep_for(
                    std::chrono::duration_cast<std::chrono::microseconds>(sleep_time));
            }

            // Spin-wait for the rest (precision)
            auto spin_start = Clock::now();
            while (Clock::now() - frame_start_
                   < std::chrono::duration_cast<Clock::duration>(target_frame_time))
            {
                // Busy wait
                auto spin_duration = Clock::now() - spin_start;
                if (spin_duration.count() > 0.01)
                {  // Log if spinning for more than 10ms
                    // This could indicate a problem with timing or high CPU load
                    break;
                }
            }
        }

        last_frame_end_ = Clock::now();
    }
    // VSync and Uncapped modes: no waiting here (swapchain present handles VSync)
}

void FrameScheduler::reset()
{
    first_frame_ = true;
    frame_ = Frame{};
    accumulator_ = 0.0f;
    stats_ = FrameStats{};
    last_dt_ms_ = 0.0f;
    max_dt_in_window_ = 0.0f;
    dt_sum_in_window_ = 0.0;
    hitches_in_window_ = 0;
    window_counter_ = 0;
}

void FrameScheduler::update_stats(float dt_ms)
{
    last_dt_ms_ = dt_ms;
    if (dt_ms > max_dt_in_window_)
        max_dt_in_window_ = dt_ms;
    dt_sum_in_window_ += dt_ms;
    window_counter_++;

    float target_ms = (target_fps_ > 0.0f) ? (1000.0f / target_fps_) : 16.667f;
    if (dt_ms > target_ms * 2.0f)
    {
        hitches_in_window_++;
        SPECTRA_LOG_DEBUG("hitch",
                          "Frame " + std::to_string(frame_.number)
                              + " hitch: " + std::to_string(dt_ms) + "ms"
                              + " (target: " + std::to_string(target_ms) + "ms)");
    }

    if (window_counter_ >= STATS_WINDOW_FRAMES)
    {
        stats_.max_frame_time_ms = max_dt_in_window_;
        stats_.avg_frame_time_ms = static_cast<float>(dt_sum_in_window_ / window_counter_);
        stats_.p95_frame_time_ms = max_dt_in_window_ * 0.8f;  // rough estimate
        stats_.hitch_count = hitches_in_window_;
        stats_.window_frame_count = window_counter_;

        if (hitches_in_window_ > 0)
        {
            SPECTRA_LOG_INFO("perf",
                             "Stats (" + std::to_string(STATS_WINDOW_FRAMES)
                                 + " frames):" + " avg=" + std::to_string(stats_.avg_frame_time_ms)
                                 + "ms" + " max=" + std::to_string(stats_.max_frame_time_ms) + "ms"
                                 + " hitches=" + std::to_string(hitches_in_window_));
        }

        max_dt_in_window_ = 0.0f;
        dt_sum_in_window_ = 0.0;
        hitches_in_window_ = 0;
        window_counter_ = 0;
    }
}

}  // namespace spectra

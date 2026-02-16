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
    PLOTIX_LOG_TRACE("scheduler", "begin_frame called");
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
}

void FrameScheduler::end_frame()
{
    PLOTIX_LOG_TRACE("scheduler", "end_frame called");
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
}

}  // namespace spectra

#pragma once

#include <chrono>
#include <cstdint>
#include <spectra/frame.hpp>

namespace spectra
{

class FrameScheduler
{
   public:
    enum class Mode
    {
        TargetFPS,  // Sleep + spin-wait to hit target FPS
        VSync,      // Let the swapchain/driver handle pacing
        Uncapped,   // Run as fast as possible
    };

    explicit FrameScheduler(float target_fps = 60.0f, Mode mode = Mode::TargetFPS);

    // Set target FPS (only used in TargetFPS mode)
    void set_target_fps(float fps);
    float target_fps() const { return target_fps_; }

    void set_mode(Mode mode) { mode_ = mode; }
    Mode mode() const { return mode_; }

    // Fixed timestep for deterministic replay
    void set_fixed_timestep(float dt);
    void clear_fixed_timestep();
    bool has_fixed_timestep() const { return use_fixed_timestep_; }

    // Call at the start and end of each frame
    void begin_frame();
    void end_frame();

    // Reset timing (e.g., after pause)
    void reset();

    // Current frame info
    const Frame& current_frame() const { return frame_; }
    float elapsed_seconds() const { return frame_.elapsed_sec; }
    float dt() const { return frame_.dt; }
    uint64_t frame_number() const { return frame_.number; }

   private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::duration<double>;

    float target_fps_ = 60.0f;
    Mode mode_ = Mode::TargetFPS;

    // Fixed timestep
    bool use_fixed_timestep_ = false;
    float fixed_dt_ = 1.0f / 60.0f;
    float accumulator_ = 0.0f;

    // Timing
    TimePoint start_time_;
    TimePoint frame_start_;
    TimePoint last_frame_start_;
    TimePoint last_frame_end_;
    bool first_frame_ = true;

    Frame frame_;
};

}  // namespace spectra

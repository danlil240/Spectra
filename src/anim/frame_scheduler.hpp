#pragma once

#include <chrono>
#include <cstdint>
#include <spectra/frame.hpp>
#include <spectra/logger.hpp>

namespace spectra
{

class FrameScheduler
{
   public:
    enum class Mode
    {
        TargetFPS,   // Sleep + spin-wait to hit target FPS
        VSync,       // Let the swapchain/driver handle pacing
        Uncapped,    // Run as fast as possible
    };

    explicit FrameScheduler(float target_fps = 60.0f, Mode mode = Mode::TargetFPS);

    // Set target FPS (only used in TargetFPS mode)
    void  set_target_fps(float fps);
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
    float        elapsed_seconds() const { return frame_.elapsed_sec; }
    float        dt() const { return frame_.dt; }
    uint64_t     frame_number() const { return frame_.number; }

    // Hitch detection stats (rolling window)
    struct FrameStats
    {
        float    max_frame_time_ms  = 0.0f;   // max dt in current window
        float    avg_frame_time_ms  = 0.0f;   // average dt in current window
        float    p95_frame_time_ms  = 0.0f;   // approximate p95
        uint32_t hitch_count        = 0;      // frames > 2× target in window
        uint64_t window_frame_count = 0;      // frames in current window
    };
    FrameStats frame_stats() const { return stats_; }
    float      last_dt_ms() const { return last_dt_ms_; }

   private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = std::chrono::duration<double>;

    float target_fps_ = 60.0f;
    Mode  mode_       = Mode::TargetFPS;

    // Fixed timestep
    bool  use_fixed_timestep_ = false;
    float fixed_dt_           = 1.0f / 60.0f;
    float accumulator_        = 0.0f;

    // Timing
    TimePoint start_time_;
    TimePoint frame_start_;
    TimePoint last_frame_start_;
    TimePoint last_frame_end_;
    bool      first_frame_ = true;

    Frame frame_;

    // Hitch detection
    static constexpr size_t STATS_WINDOW_FRAMES = 600;   // ~10s at 60fps
    FrameStats              stats_;
    float                   last_dt_ms_        = 0.0f;
    float                   max_dt_in_window_  = 0.0f;
    double                  dt_sum_in_window_  = 0.0;
    uint32_t                hitches_in_window_ = 0;
    uint64_t                window_counter_    = 0;

    // Circular buffer for actual percentile computation.
    std::array<float, STATS_WINDOW_FRAMES> dt_samples_{};
    // Scratch for sorting (avoids per-window allocation).
    std::array<float, STATS_WINDOW_FRAMES> dt_sorted_{};

    void update_stats(float dt_ms);
};

}   // namespace spectra

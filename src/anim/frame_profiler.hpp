#pragma once

// FrameProfiler — DEBUG-only per-frame timing instrumentation.
// Zero overhead in Release builds (all macros expand to nothing).
// Usage:
//   SPECTRA_PROFILE_SCOPE(profiler, "stage_name")   — times a scope
//   profiler.begin_frame() / end_frame()             — frame boundaries
//   profiler.log_if_ready()                          — periodic summary

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <spectra/logger.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{

#ifndef NDEBUG

class FrameProfiler
{
   public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // Per-stage accumulated time for current frame
    struct StageTimer
    {
        TimePoint start;
        double accumulated_us = 0.0;  // microseconds
    };

    // Rolling statistics for one stage
    struct StageStats
    {
        double avg_us = 0.0;
        double p95_us = 0.0;
        double max_us = 0.0;
        uint32_t sample_count = 0;
    };

    explicit FrameProfiler(uint32_t log_interval_frames = 600) : log_interval_(log_interval_frames)
    {
    }

    void begin_frame()
    {
        frame_start_ = Clock::now();
        current_stages_.clear();
        counters_.clear();
    }

    void begin_stage(const char* name)
    {
        auto& timer = current_stages_[name];
        timer.start = Clock::now();
    }

    void end_stage(const char* name)
    {
        auto it = current_stages_.find(name);
        if (it == current_stages_.end())
            return;
        auto elapsed =
            std::chrono::duration<double, std::micro>(Clock::now() - it->second.start).count();
        it->second.accumulated_us += elapsed;
    }

    void end_frame()
    {
        auto frame_end = Clock::now();
        double frame_us =
            std::chrono::duration<double, std::micro>(frame_end - frame_start_).count();

        // Record total frame time
        record_sample("_total_frame", frame_us);

        // Record per-stage times
        for (auto& [name, timer] : current_stages_)
        {
            record_sample(name, timer.accumulated_us);
        }

        frame_count_++;
        total_frame_count_++;

        double frame_ms = frame_us / 1000.0;
        if (frame_ms > 2.0 * target_frame_ms_ && total_frame_count_ > 10)
        {
            hitch_count_++;
        }

        log_if_ready();
    }

    void set_target_fps(float fps)
    {
        if (fps > 0.0f)
            target_frame_ms_ = 1000.0 / static_cast<double>(fps);
    }

    void increment_counter(const char* name, uint32_t count = 1)
    {
        counters_[name] += count;
        // Also accumulate into total history
        history_counters_[name] += count;
    }

    void log_if_ready()
    {
        if (frame_count_ < log_interval_)
            return;

        // Compute stats for each stage
        std::string report =
            "=== Frame Profiler (" + std::to_string(frame_count_) + " frames) ===\n";

        // Total frame first
        auto total_it = history_.find("_total_frame");
        if (total_it != history_.end())
        {
            auto stats = compute_stats(total_it->second);
            double avg_fps = (stats.avg_us > 0.0) ? (1000000.0 / stats.avg_us) : 0.0;
            report += "  Total frame:  avg=" + format_us(stats.avg_us)
                      + "  p95=" + format_us(stats.p95_us) + "  max=" + format_us(stats.max_us)
                      + "  (~" + std::to_string(static_cast<int>(avg_fps)) + " FPS)\n";
        }

        // Stages sorted by avg time (descending)
        struct RankedStage
        {
            std::string name;
            StageStats stats;
        };
        std::vector<RankedStage> ranked;
        for (auto& [name, samples] : history_)
        {
            if (name[0] == '_')
                continue;  // skip _total_frame
            ranked.push_back({name, compute_stats(samples)});
        }
        std::sort(ranked.begin(),
                  ranked.end(),
                  [](const RankedStage& a, const RankedStage& b)
                  { return a.stats.avg_us > b.stats.avg_us; });

        for (auto& r : ranked)
        {
            report += "  " + pad_right(r.name, 24) + " avg=" + format_us(r.stats.avg_us) + "  p95="
                      + format_us(r.stats.p95_us) + "  max=" + format_us(r.stats.max_us) + "\n";
        }

        report += "\n  --- Counters (since last log) ---\n";
        for (const auto& [name, count] : history_counters_)
        {
            report += "  " + pad_right(name, 24) + " " + std::to_string(count) + "\n";
        }

        report += "\n  Hitches (>2x target): " + std::to_string(hitch_count_) + "/"
                  + std::to_string(frame_count_);

        SPECTRA_LOG_INFO("profiler", report);

        // Reset for next window
        frame_count_ = 0;
        hitch_count_ = 0;
        history_.clear();
        history_counters_.clear();
    }

    uint64_t total_frame_count() const { return total_frame_count_; }
    uint32_t hitch_count() const { return hitch_count_; }

   private:
    void record_sample(const std::string& name, double us)
    {
        auto& samples = history_[name];
        samples.push_back(us);
    }

    StageStats compute_stats(std::vector<double>& samples) const
    {
        StageStats s;
        if (samples.empty())
            return s;

        s.sample_count = static_cast<uint32_t>(samples.size());

        double sum = 0.0;
        s.max_us = 0.0;
        for (double v : samples)
        {
            sum += v;
            if (v > s.max_us)
                s.max_us = v;
        }
        s.avg_us = sum / samples.size();

        // Actual p95: sort and pick 95th percentile
        std::sort(samples.begin(), samples.end());
        size_t p95_idx = static_cast<size_t>(samples.size() * 0.95);
        if (p95_idx >= samples.size())
            p95_idx = samples.size() - 1;
        s.p95_us = samples[p95_idx];

        return s;
    }

    static std::string format_us(double us)
    {
        if (us >= 1000.0)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2fms", us / 1000.0);
            return buf;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0fus", us);
        return buf;
    }

    static std::string pad_right(const std::string& s, size_t width)
    {
        if (s.size() >= width)
            return s;
        return s + std::string(width - s.size(), ' ');
    }

    TimePoint frame_start_;
    std::unordered_map<std::string, StageTimer> current_stages_;
    std::unordered_map<std::string, std::vector<double>> history_;
    std::map<std::string, uint32_t> counters_;
    std::map<std::string, uint32_t> history_counters_;

    uint32_t log_interval_ = 600;
    uint32_t frame_count_ = 0;
    uint64_t total_frame_count_ = 0;
    uint32_t hitch_count_ = 0;
    double target_frame_ms_ = 16.667;  // 60 FPS default
};

// RAII scope timer
struct ProfileScope
{
    FrameProfiler& profiler;
    const char* name;
    ProfileScope(FrameProfiler& p, const char* n) : profiler(p), name(n)
    {
        profiler.begin_stage(n);
    }
    ~ProfileScope() { profiler.end_stage(name); }
};

    #define SPECTRA_PROFILE_SCOPE(profiler, name) \
        spectra::ProfileScope _profile_##__LINE__(profiler, name)
    #define SPECTRA_PROFILE_BEGIN(profiler, name) profiler.begin_stage(name)
    #define SPECTRA_PROFILE_END(profiler, name) profiler.end_stage(name)

#else  // NDEBUG — Release builds: zero overhead

class FrameProfiler
{
   public:
    explicit FrameProfiler(uint32_t = 600) {}
    void begin_frame() {}
    void begin_stage(const char*) {}
    void end_stage(const char*) {}
    void end_frame() {}
    void set_target_fps(float) {}
    void increment_counter(const char*, uint32_t = 1) {}
    void log_if_ready() {}
    uint64_t total_frame_count() const { return 0; }
    uint32_t hitch_count() const { return 0; }
};

    #define SPECTRA_PROFILE_SCOPE(profiler, name) ((void)0)
    #define SPECTRA_PROFILE_BEGIN(profiler, name) ((void)0)
    #define SPECTRA_PROFILE_END(profiler, name) ((void)0)

#endif  // NDEBUG

}  // namespace spectra

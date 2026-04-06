#pragma once

// PerfMetrics — lightweight performance instrumentation for startup,
// automation latency, and redraw tracking.
// Thread-safe for concurrent reads; writes expected from a single thread
// per metric category (startup from main, automation from server thread).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace spectra
{

struct LatencyStats
{
    double   total_us = 0.0;
    double   max_us   = 0.0;
    uint64_t count    = 0;

    double avg_us() const { return count > 0 ? total_us / static_cast<double>(count) : 0.0; }

    void record(double us)
    {
        total_us += us;
        if (us > max_us)
            max_us = us;
        ++count;
    }
};

class PerfMetrics
{
   public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static PerfMetrics& instance()
    {
        static PerfMetrics s;
        return s;
    }

    // ─── Startup timing ─────────────────────────────────────────────────

    void mark_startup_begin() { startup_begin_ = Clock::now(); }

    void mark_startup_phase(const char* phase)
    {
        auto   now = Clock::now();
        double us  = std::chrono::duration<double, std::micro>(now - last_phase_end_).count();
        std::lock_guard lk(mu_);
        startup_phases_[phase] = us;
        last_phase_end_        = now;
    }

    void mark_startup_end()
    {
        startup_total_us_ =
            std::chrono::duration<double, std::micro>(Clock::now() - startup_begin_).count();
    }

    double startup_total_us() const { return startup_total_us_; }

    // Returns a snapshot of startup phase durations.
    std::unordered_map<std::string, double> startup_phases() const
    {
        std::lock_guard lk(mu_);
        return startup_phases_;
    }

    // ─── Automation latency ─────────────────────────────────────────────

    void record_automation_latency(const std::string& method, double us)
    {
        std::lock_guard lk(mu_);
        automation_latency_[method].record(us);
    }

    std::unordered_map<std::string, LatencyStats> automation_latency() const
    {
        std::lock_guard lk(mu_);
        return automation_latency_;
    }

    // ─── Frame counter (atomic, lockfree) ────────────────────────────────

    void     increment_frame_count() { frame_count_.fetch_add(1, std::memory_order_relaxed); }
    uint64_t frame_count() const { return frame_count_.load(std::memory_order_relaxed); }

   private:
    PerfMetrics()  = default;
    ~PerfMetrics() = default;

    mutable std::mutex mu_;

    // Startup
    TimePoint                               startup_begin_    = Clock::now();
    TimePoint                               last_phase_end_   = Clock::now();
    double                                  startup_total_us_ = 0.0;
    std::unordered_map<std::string, double> startup_phases_;

    // Automation
    std::unordered_map<std::string, LatencyStats> automation_latency_;

    // Frames
    std::atomic<uint64_t> frame_count_{0};
};

}   // namespace spectra

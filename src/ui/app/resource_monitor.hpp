#pragma once

// resource_monitor.hpp — Periodic system resource utilization logger.
// Samples CPU%, RSS memory (MB), and frame timing every N seconds and
// writes a single INFO log line.  Linux-only via /proc/self/stat and
// /proc/self/status.  No external dependencies.
//
// Usage:
//   ResourceMonitor monitor;           // default: log every 5s
//   // each frame:
//   monitor.tick(frame_ms, gpu_ms);

#include <spectra/logger.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>

#ifdef __linux__
    #include <sys/resource.h>
    #include <sys/times.h>
    #include <unistd.h>
#endif

namespace spectra
{

class ResourceMonitor
{
   public:
    explicit ResourceMonitor(double interval_seconds = 5.0)
        : interval_s_(interval_seconds), last_log_(Clock::now())
    {
#ifdef __linux__
        ticks_per_sec_ = static_cast<double>(sysconf(_SC_CLK_TCK));
        sample_cpu_ticks(prev_utime_, prev_stime_, prev_wall_ns_);
#endif
    }

    // Call once per frame with wall-clock frame time and GPU frame time (both ms).
    // Logs when the interval elapses.
    void tick(double frame_ms, double gpu_ms = 0.0)
    {
        // Accumulate for averages
        frame_ms_acc_ += frame_ms;
        gpu_ms_acc_ += gpu_ms;
        ++sample_count_;

        auto   now     = Clock::now();
        double elapsed = std::chrono::duration<double>(now - last_log_).count();
        if (elapsed < interval_s_)
            return;

        double avg_frame_ms = sample_count_ > 0 ? frame_ms_acc_ / sample_count_ : 0.0;
        double avg_gpu_ms   = sample_count_ > 0 ? gpu_ms_acc_ / sample_count_ : 0.0;
        double avg_fps      = avg_frame_ms > 0.0 ? 1000.0 / avg_frame_ms : 0.0;

        double cpu_pct = read_cpu_percent(elapsed);
        double rss_mb  = read_rss_mb();

        // Skip first interval — baseline not established yet (cpu_pct == -1.0)
        if (cpu_pct < 0.0)
        {
            frame_ms_acc_ = 0.0;
            gpu_ms_acc_   = 0.0;
            sample_count_ = 0;
            last_log_     = now;
            return;
        }

        char buf[256];
        snprintf(buf,
                 sizeof(buf),
                 "CPU: %.1f%%  RAM: %.0f MB  frame: %.1f ms (%.0f fps)  GPU: %.2f ms",
                 cpu_pct,
                 rss_mb,
                 avg_frame_ms,
                 avg_fps,
                 avg_gpu_ms);

        SPECTRA_LOG_INFO("resources", buf);

        // Reset accumulators
        frame_ms_acc_ = 0.0;
        gpu_ms_acc_   = 0.0;
        sample_count_ = 0;
        last_log_     = now;
    }

   private:
    using Clock = std::chrono::steady_clock;

    double            interval_s_;
    Clock::time_point last_log_;
    double            frame_ms_acc_ = 0.0;
    double            gpu_ms_acc_   = 0.0;
    uint64_t          sample_count_ = 0;

#ifdef __linux__
    double   ticks_per_sec_      = 100.0;
    uint64_t prev_utime_         = 0;
    uint64_t prev_stime_         = 0;
    int64_t  prev_wall_ns_       = 0;
    bool     cpu_baseline_ready_ = false;

    // Read utime+stime ticks and wall-clock ns from /proc/self/stat.
    static bool sample_cpu_ticks(uint64_t& utime, uint64_t& stime, int64_t& wall_ns)
    {
        FILE* f = fopen("/proc/self/stat", "r");
        if (!f)
            return false;

        // Fields: pid comm state ppid pgroup session tty_nr tpgid flags minflt cminflt
        //         majflt cmajflt utime stime cutime cstime priority nice num_threads ...
        // utime = field 14, stime = field 15 (1-indexed)
        unsigned long ut = 0;
        unsigned long st = 0;

        int           pid = 0;
        char          comm[256]{};
        char          state = 0;
        int           ppid = 0, pgrp = 0, session = 0, tty = 0, tpgid = 0;
        unsigned      flags  = 0;
        unsigned long minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0;

        int nfields = fscanf(f,
                             "%d %255s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu",
                             &pid,
                             comm,
                             &state,
                             &ppid,
                             &pgrp,
                             &session,
                             &tty,
                             &tpgid,
                             &flags,
                             &minflt,
                             &cminflt,
                             &majflt,
                             &cmajflt,
                             &ut,
                             &st);
        fclose(f);
        if (nfields < 15)
            return false;

        utime   = static_cast<uint64_t>(ut);
        stime   = static_cast<uint64_t>(st);
        wall_ns = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
        return true;
    }

    // Returns CPU usage percentage since last call, or -1.0 on first call
    // (no baseline yet — caller should skip logging).
    double read_cpu_percent(double elapsed_wall_s)
    {
        uint64_t utime   = 0;
        uint64_t stime   = 0;
        int64_t  wall_ns = 0;
        if (!sample_cpu_ticks(utime, stime, wall_ns))
            return 0.0;

        double cpu_pct = -1.0;
        if (cpu_baseline_ready_)
        {
            uint64_t delta_ticks = (utime + stime) - (prev_utime_ + prev_stime_);
            double   delta_cpu_s = static_cast<double>(delta_ticks) / ticks_per_sec_;
            cpu_pct = (elapsed_wall_s > 0.0) ? (delta_cpu_s / elapsed_wall_s) * 100.0 : 0.0;
        }

        prev_utime_         = utime;
        prev_stime_         = stime;
        prev_wall_ns_       = wall_ns;
        cpu_baseline_ready_ = true;

        return cpu_pct;
    }

    // Returns resident set size in MB from /proc/self/status.
    static double read_rss_mb()
    {
        FILE* f = fopen("/proc/self/status", "r");
        if (!f)
            return 0.0;

        char line[128];
        while (fgets(line, sizeof(line), f))
        {
            unsigned long kb = 0;
            if (sscanf(line, "VmRSS: %lu kB", &kb) == 1)
            {
                fclose(f);
                return static_cast<double>(kb) / 1024.0;
            }
        }
        fclose(f);
        return 0.0;
    }
#else
    // Non-Linux stubs
    double        read_cpu_percent(double) { return 0.0; }
    static double read_rss_mb() { return 0.0; }
#endif
};

}   // namespace spectra

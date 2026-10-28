#pragma once
// qa_stats.hpp — memory / frame-time heuristics of the Spectra QA agent.
//
// Kept header-only and free of Spectra/Vulkan dependencies so the rules the
// QA report applies are unit tested (tests/unit/test_qa_stats.cpp).
//
// Memory: RSS on a fresh process grows on first use of each feature (glibc
// arenas, driver/JIT pipeline caches, font glyph caches, X/Vulkan WSI pools)
// and then plateaus.  A single before/after delta therefore cannot tell a leak
// from warm-up.  The agent judges retention across *repetitions*: warn only on
// sustained growth after the first (warm-up) repetition.
//
// Frame time: the first frames that use a feature (pipeline/JIT compile, glyph
// rasterisation, atlas re-upload, swapchain creation) are legitimately slow.
// Spike statistics ignore a declared warm-up window that follows each such
// event, so warnings point at steady-state hitches only.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace spectra::qa
{

// Post-teardown RSS samples (bytes) of N repetitions of the same workload.
struct RetentionVerdict
{
    bool    judged             = false;   // >= 3 repetitions available
    bool    sustained_growth   = false;   // leak-like: keeps growing after warm-up
    int64_t warmup_bytes       = 0;       // growth of the first repetition (baseline -> rep 1)
    int64_t per_rep_bytes      = 0;       // mean growth per repetition after warm-up
    int64_t late_per_rep_bytes = 0;       // mean growth per repetition over the second half
};

// `baseline` is RSS before the first repetition, `after[i]` RSS after
// repetition i.  Sustained growth = the mean growth per repetition after the
// first exceeds `per_rep_threshold` and the second half still grows by at
// least half of that (a plateau that settles after a few reps is not a leak).
inline RetentionVerdict judge_retention(int64_t                     baseline,
                                        const std::vector<int64_t>& after,
                                        int64_t                     per_rep_threshold)
{
    RetentionVerdict v;
    if (after.empty())
        return v;
    v.warmup_bytes = after.front() - baseline;
    if (after.size() < 3)
        return v;
    v.judged          = true;
    const size_t n    = after.size();
    v.per_rep_bytes   = (after.back() - after.front()) / static_cast<int64_t>(n - 1);
    const size_t mid  = (n - 1) / 2;
    const size_t span = (n - 1) - mid;
    v.late_per_rep_bytes =
        span > 0 ? (after.back() - after[mid]) / static_cast<int64_t>(span) : v.per_rep_bytes;
    v.sustained_growth =
        v.per_rep_bytes > per_rep_threshold && v.late_per_rep_bytes * 2 > per_rep_threshold;
    return v;
}

// Running RSS check: a transient peak (e.g. five windows open at once) is not
// growth.  Growth counts only when the *floor* of the last `window` samples
// rises above baseline + threshold, i.e. memory stays up across ~window
// samples instead of being released again.
class RssFloorMonitor
{
   public:
    explicit RssFloorMonitor(size_t window = 5) : window_(window < 1 ? 1 : window) {}

    void add(size_t rss)
    {
        samples_.push_back(rss);
        if (samples_.size() > window_)
            samples_.pop_front();
    }

    bool   full() const { return samples_.size() == window_; }
    size_t floor() const
    {
        return samples_.empty() ? 0 : *std::min_element(samples_.begin(), samples_.end());
    }

    // True once the window is full and its minimum exceeds baseline + threshold.
    bool sustained_above(size_t baseline, size_t threshold) const
    {
        return full() && floor() > baseline + threshold;
    }

   private:
    size_t             window_;
    std::deque<size_t> samples_;
};

// Frame-time spike detector with declared warm-up windows.
class SpikeDetector
{
   public:
    static constexpr uint64_t STARTUP_WARMUP_FRAMES = 30;   // first frames of the run
    static constexpr uint64_t FEATURE_WARMUP_FRAMES = 3;    // frames after a first-use event
    static constexpr float    MIN_SPIKE_MS          = 33.0f;
    static constexpr float    SPIKE_MULTIPLIER      = 3.0f;

    // Declare that the next frames may pay one-time costs (new figure/series
    // type, new window or swapchain, theme / UI-scale change, ...).
    void declare_warmup(uint64_t frame, uint64_t frames = FEATURE_WARMUP_FRAMES)
    {
        warmup_until_ = std::max(warmup_until_, frame + frames);
    }

    bool in_warmup(uint64_t frame) const
    {
        return frame <= STARTUP_WARMUP_FRAMES || frame <= warmup_until_;
    }

    // `frame` is 1-based; returns true when the frame is a steady-state spike
    // relative to the running average `ema_ms`.  Warm-up frames are counted
    // separately and never flagged.
    bool is_spike(uint64_t frame, float ms, float ema_ms)
    {
        const bool slow = ema_ms > 0.5f && ms > MIN_SPIKE_MS && ms > ema_ms * SPIKE_MULTIPLIER;
        if (!slow)
            return false;
        if (in_warmup(frame))
        {
            ++warmup_spikes_;
            return false;
        }
        return true;
    }

    uint32_t warmup_spikes() const { return warmup_spikes_; }

   private:
    uint64_t warmup_until_  = 0;
    uint32_t warmup_spikes_ = 0;
};

}   // namespace spectra::qa

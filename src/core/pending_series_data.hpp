#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "spinlock.hpp"

namespace spectra
{

// ═══════════════════════════════════════════════════════════════════════════════
// PendingSeriesData — accumulates background-thread mutations for a Series.
//
// Background (producer) threads call replace_x/y(), append(), erase_before()
// under a lightweight spinlock.  The main (consumer) thread calls commit()
// at frame boundary to apply all pending operations to the live data vectors.
//
// Operation ordering within a single commit:
//   1. Full replace (set_x / set_y) — if pending, moves replacement into live
//   2. Erase-before — removes points below threshold
//   3. Append — appends accumulated points
//
// This ordering ensures that a full replace followed by appends within the
// same frame produces the expected result.
//
// Thread safety:
//   - All mutating producer methods (replace_x/y, append, erase_before) are
//     safe to call concurrently from any thread.
//   - commit() must be called from a single thread (the main/render thread).
//   - has_pending() is a lock-free atomic check.
// ═══════════════════════════════════════════════════════════════════════════════

class PendingSeriesData
{
   public:
    PendingSeriesData()  = default;
    ~PendingSeriesData() = default;

    PendingSeriesData(const PendingSeriesData&)            = delete;
    PendingSeriesData& operator=(const PendingSeriesData&) = delete;

    // ── Producer methods (any thread) ────────────────────────────────────

    void replace_x(std::span<const float> x)
    {
        SpinLockGuard guard(lock_);
        pending_x_.emplace(x.begin(), x.end());
        set_pending();
    }

    void replace_y(std::span<const float> y)
    {
        SpinLockGuard guard(lock_);
        pending_y_.emplace(y.begin(), y.end());
        set_pending();
    }

    void append(float x, float y)
    {
        SpinLockGuard guard(lock_);
        append_x_.push_back(x);
        append_y_.push_back(y);
        set_pending();
    }

    void erase_before(float threshold)
    {
        SpinLockGuard guard(lock_);
        erase_threshold_ = threshold;
        has_erase_       = true;
        set_pending();
    }

    // ── Consumer methods (main thread only) ──────────────────────────────

    // Apply all pending operations to the live data vectors.
    // Returns true if any data was modified.
    bool commit(std::vector<float>& x, std::vector<float>& y)
    {
        SpinLockGuard guard(lock_);

        if (!has_pending_.load(std::memory_order_relaxed))
            return false;

        bool changed = false;

        // 1. Full replace
        if (pending_x_.has_value())
        {
            x = std::move(*pending_x_);
            pending_x_.reset();
            changed = true;
        }
        if (pending_y_.has_value())
        {
            y = std::move(*pending_y_);
            pending_y_.reset();
            changed = true;
        }

        // 2. Erase-before (sorted-x binary search)
        if (has_erase_ && !x.empty())
        {
            auto it = std::lower_bound(x.begin(), x.end(), erase_threshold_);
            auto n  = static_cast<ptrdiff_t>(it - x.begin());
            if (n > 0)
            {
                x.erase(x.begin(), x.begin() + n);
                y.erase(y.begin(), y.begin() + n);
                changed = true;
            }
            has_erase_ = false;
        }
        else
        {
            has_erase_ = false;
        }

        // 3. Append accumulated points
        if (!append_x_.empty())
        {
            x.insert(x.end(), append_x_.begin(), append_x_.end());
            y.insert(y.end(), append_y_.begin(), append_y_.end());
            append_x_.clear();
            append_y_.clear();
            changed = true;
        }

        has_pending_.store(false, std::memory_order_relaxed);
        return changed;
    }

    // Lock-free check: are there pending operations?
    bool has_pending() const noexcept { return has_pending_.load(std::memory_order_acquire); }

    // ── Wake callback ────────────────────────────────────────────────────

    // Set a callback invoked (once) when has_pending transitions false→true.
    // Useful for waking an idle render loop via glfwPostEmptyEvent().
    void set_wake_fn(std::function<void()> fn) { wake_fn_ = std::move(fn); }

   private:
    void set_pending()
    {
        // Transition false→true: invoke wake callback.
        if (!has_pending_.exchange(true, std::memory_order_release))
        {
            if (wake_fn_)
                wake_fn_();
        }
    }

    SpinLock          lock_;
    std::atomic<bool> has_pending_{false};

    // Full-replace buffers (applied as move, so zero-copy for large datasets).
    std::optional<std::vector<float>> pending_x_;
    std::optional<std::vector<float>> pending_y_;

    // Append accumulator.
    std::vector<float> append_x_;
    std::vector<float> append_y_;

    // Erase-before state.
    float erase_threshold_ = 0.0f;
    bool  has_erase_       = false;

    // Wake callback (called outside lock via set_pending).
    std::function<void()> wake_fn_;
};

}   // namespace spectra

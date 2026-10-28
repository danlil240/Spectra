#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

#include "data_origin.hpp"
#include "spinlock.hpp"

namespace spectra
{

// True when x is non-decreasing AND contains no NaN.  std::is_sorted alone
// reports NaN-separated traces (e.g. {0, 1, NaN, 0, 1}) as sorted because every
// comparison against NaN is false; render culling would then binary-search an
// unsorted array and drop visible segments.
template <typename It>
inline bool x_sorted_without_nan(It first, It last)
{
    if (first == last)
        return true;
    if (std::isnan(*first))
        return false;
    It prev = first;
    for (It it = std::next(first); it != last; prev = it, ++it)
    {
        if (std::isnan(*it) || *it < *prev)
            return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PendingSeriesData — accumulates background-thread mutations for a Series.
//
// Background (producer) threads call replace_x/y(), append(), erase_before(),
// erase_after(), trim_to_max_points()
// under a lightweight spinlock.  The main (consumer) thread calls commit()
// at frame boundary to apply all pending operations to the live data vectors.
//
// Operation ordering within a single commit:
//   1. Full replace (set_x / set_y) — if pending, moves replacement into live
//   2. Erase-before — removes points below threshold
//   3. Erase-after — removes points above threshold
//   4. Trim-to-max — drops oldest points when count exceeds cap
//   5. Append — appends accumulated points
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

    // Full replace from the double-precision setters: the data are already
    // float offsets from `offset`, which is applied together with them at
    // commit so the series never pairs new data with an old offset.
    void replace_x(std::vector<float>&& x, double offset)
    {
        SpinLockGuard guard(lock_);
        pending_x_        = std::move(x);
        pending_x_offset_ = offset;
        set_pending();
    }

    void replace_y(std::vector<float>&& y, double offset)
    {
        SpinLockGuard guard(lock_);
        pending_y_        = std::move(y);
        pending_y_offset_ = offset;
        set_pending();
    }

    void append(float x, float y)
    {
        SpinLockGuard guard(lock_);
        appends_.push_back({x, y, false});
        set_pending();
    }

    // Append of absolute double values (Series::append(double, double)).
    // Converted to float offsets at commit, where the live offsets are known.
    void append_f64(double x, double y)
    {
        SpinLockGuard guard(lock_);
        appends_.push_back({x, y, true});
        set_pending();
    }

    // `absolute` thresholds are logical x values (offset included), resolved
    // against the live x offset at commit.
    void erase_before(double threshold, bool absolute = false)
    {
        SpinLockGuard guard(lock_);
        erase_threshold_ = threshold;
        erase_absolute_  = absolute;
        has_erase_       = true;
        set_pending();
    }

    void erase_after(double threshold, bool absolute = false)
    {
        SpinLockGuard guard(lock_);
        erase_after_threshold_ = threshold;
        erase_after_absolute_  = absolute;
        has_erase_after_       = true;
        set_pending();
    }

    void trim_to_max_points(size_t max_points)
    {
        SpinLockGuard guard(lock_);
        trim_max_points_ = max_points;
        has_trim_        = true;
        set_pending();
    }

    // ── Consumer methods (main thread only) ──────────────────────────────

    // Apply all pending operations to the live data vectors.
    // Returns true if any data was modified.
    // x_offset / y_offset receive the offsets queued by the double-precision
    // replace_x / replace_y overloads (left untouched when none is queued).
    bool commit(std::vector<float>& x,
                std::vector<float>& y,
                bool*               x_sorted = nullptr,
                double*             x_offset = nullptr,
                double*             y_offset = nullptr)
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
            if (x_sorted)
                *x_sorted = x_sorted_without_nan(x.begin(), x.end());
            changed = true;
        }
        if (pending_y_.has_value())
        {
            y = std::move(*pending_y_);
            pending_y_.reset();
            changed = true;
        }
        if (pending_x_offset_.has_value())
        {
            if (x_offset)
                *x_offset = *pending_x_offset_;
            pending_x_offset_.reset();
            changed = true;
        }
        if (pending_y_offset_.has_value())
        {
            if (y_offset)
                *y_offset = *pending_y_offset_;
            pending_y_offset_.reset();
            changed = true;
        }

        // Stored-value threshold for an erase: absolute thresholds are made
        // relative to the x offset in effect after the replace above.
        auto stored_threshold = [&](double threshold, bool absolute) -> float
        {
            const double origin = (absolute && x_offset) ? *x_offset : 0.0;
            return static_cast<float>(threshold - origin);
        };

        // 2. Erase-before (sorted-x binary search).
        // x and y may differ in length (replace_x / replace_y are queued
        // independently), so all erase counts are derived from the paired
        // point count min(x, y), matching LineSeries' non-pending paths.
        if (has_erase_)
        {
            const size_t count = std::min(x.size(), y.size());
            if (count > 0)
            {
                auto it = std::lower_bound(x.begin(),
                                           x.begin() + static_cast<ptrdiff_t>(count),
                                           stored_threshold(erase_threshold_, erase_absolute_));
                auto n  = static_cast<ptrdiff_t>(it - x.begin());
                if (n > 0)
                {
                    x.erase(x.begin(), x.begin() + n);
                    y.erase(y.begin(), y.begin() + n);
                    changed = true;
                }
            }
        }
        has_erase_ = false;

        // 3. Erase-after (sorted-x binary search). Removes the paired range
        // [keep, count) from both vectors so x/y pairs stay aligned.
        if (has_erase_after_)
        {
            const size_t count = std::min(x.size(), y.size());
            if (count > 0)
            {
                auto it = std::upper_bound(
                    x.begin(),
                    x.begin() + static_cast<ptrdiff_t>(count),
                    stored_threshold(erase_after_threshold_, erase_after_absolute_));
                const auto keep = static_cast<ptrdiff_t>(it - x.begin());
                const auto end  = static_cast<ptrdiff_t>(count);
                if (end > keep)
                {
                    x.erase(x.begin() + keep, x.begin() + end);
                    y.erase(y.begin() + keep, y.begin() + end);
                    changed = true;
                }
            }
        }
        has_erase_after_ = false;

        // 4. Append accumulated points.  Double appends become float offsets
        // from the live origins; the first one into an empty axis without an
        // origin picks it (same rule as the non-pending path).
        if (!appends_.empty())
        {
            std::vector<float> add_x;
            std::vector<float> add_y;
            add_x.reserve(appends_.size());
            add_y.reserve(appends_.size());
            double x_origin = x_offset ? *x_offset : 0.0;
            double y_origin = y_offset ? *y_offset : 0.0;
            for (const auto& a : appends_)
            {
                if (!a.absolute)
                {
                    add_x.push_back(static_cast<float>(a.x));
                    add_y.push_back(static_cast<float>(a.y));
                    continue;
                }
                if (x_offset && x_origin == 0.0 && x.empty() && add_x.empty())
                    *x_offset = x_origin = detail::choose_stream_origin(a.x);
                if (y_offset && y_origin == 0.0 && y.empty() && add_y.empty())
                    *y_offset = y_origin = detail::choose_stream_origin(a.y);
                add_x.push_back(static_cast<float>(a.x - x_origin));
                add_y.push_back(static_cast<float>(a.y - y_origin));
            }
            if (x_sorted && *x_sorted)
            {
                const bool boundary_sorted = x.empty() || x.back() <= add_x.front();
                *x_sorted = boundary_sorted && x_sorted_without_nan(add_x.begin(), add_x.end());
            }
            x.insert(x.end(), add_x.begin(), add_x.end());
            y.insert(y.end(), add_y.begin(), add_y.end());
            appends_.clear();
            changed = true;
        }

        // 5. Trim oldest points when over cap (FIFO for streaming plots).
        if (has_trim_)
        {
            const size_t count = std::min(x.size(), y.size());
            if (count > trim_max_points_)
            {
                const auto remove = static_cast<ptrdiff_t>(count - trim_max_points_);
                x.erase(x.begin(), x.begin() + remove);
                y.erase(y.begin(), y.begin() + remove);
                changed = true;
            }
        }
        has_trim_ = false;

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
    std::optional<double>             pending_x_offset_;
    std::optional<double>             pending_y_offset_;

    // Append accumulator.  Float appends keep their exact float values
    // (widened losslessly); `absolute` marks double appends.
    struct PendingAppend
    {
        double x;
        double y;
        bool   absolute;
    };
    std::vector<PendingAppend> appends_;

    // Erase-before / erase-after state.
    double erase_threshold_       = 0.0;
    double erase_after_threshold_ = 0.0;
    bool   erase_absolute_        = false;
    bool   erase_after_absolute_  = false;
    bool   has_erase_             = false;
    bool   has_erase_after_       = false;

    // Trim-to-max state.
    size_t trim_max_points_ = 0;
    bool   has_trim_        = false;

    // Wake callback (called outside lock via set_pending).
    std::function<void()> wake_fn_;
};

}   // namespace spectra

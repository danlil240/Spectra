#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "data/decimation.hpp"

namespace spectra::data
{

/// Pre-computed decimation pyramid for multi-resolution rendering.
///
/// Stores N levels of progressively decimated data:
///   - Level 0: full resolution (reference only, not stored — caller owns the source)
///   - Level 1: ~1:4 decimation via min-max
///   - Level 2: ~1:16 decimation
///   - Level N: ~1:(4^N) decimation
///
/// The `query()` method selects the coarsest level that satisfies the requested
/// point budget for a given x-range, enabling smooth zooming over very large
/// datasets without re-decimating on every frame.
class LodCache
{
   public:
    /// Default decimation ratio between adjacent levels (each level is
    /// reduced by this factor relative to the previous level).
    static constexpr std::size_t DEFAULT_RATIO = 4;

    /// Minimum number of points at any level (below this, stop building levels).
    static constexpr std::size_t MIN_LEVEL_POINTS = 64;

    /// Build the LoD pyramid from full-resolution source data.
    /// `x` and `y` must be the same length with x sorted ascending.
    /// `ratio` controls the decimation factor between levels (default 4x).
    void build(std::span<const float> x,
               std::span<const float> y,
               std::size_t            ratio = DEFAULT_RATIO)
    {
        levels_.clear();
        source_size_ = x.size();

        if (x.size() < MIN_LEVEL_POINTS)
            return;

        // Build levels by successively decimating the previous level.
        // Level 0 data is the source data itself (not stored).
        std::vector<float> prev_x(x.begin(), x.end());
        std::vector<float> prev_y(y.begin(), y.end());

        while (prev_x.size() > MIN_LEVEL_POINTS)
        {
            std::size_t target = (prev_x.size() + ratio - 1) / ratio;
            if (target < MIN_LEVEL_POINTS)
                target = MIN_LEVEL_POINTS;

            // Use min-max decimation to preserve peaks
            auto decimated = min_max_decimate(prev_x, prev_y, target);
            if (decimated.size() >= prev_x.size())
                break;   // No further reduction possible

            Level level;
            level.x.reserve(decimated.size());
            level.y.reserve(decimated.size());
            for (auto& [dx, dy] : decimated)
            {
                level.x.push_back(dx);
                level.y.push_back(dy);
            }
            prev_x = level.x;
            prev_y = level.y;
            levels_.push_back(std::move(level));
        }
        ++generation_;
    }

    /// Query the best level of detail for a given viewport.
    ///
    /// Returns a pair of spans (x, y) from the coarsest level whose point
    /// count in the visible range [x_min, x_max] does not exceed `max_points`.
    /// If no cached level is coarse enough, returns the coarsest available.
    ///
    /// `source_x` and `source_y` are the full-resolution data (level 0).
    struct QueryResult
    {
        std::span<const float> x;
        std::span<const float> y;
        std::size_t            level = 0;   // 0 = full resolution
    };

    [[nodiscard]] QueryResult query(std::span<const float> source_x,
                                    std::span<const float> source_y,
                                    float                  x_min,
                                    float                  x_max,
                                    std::size_t            max_points) const
    {
        // First check full resolution — if it fits, use it directly
        auto [src_lo, src_hi] = visible_range_span(source_x, x_min, x_max);
        std::size_t src_count = src_hi - src_lo;

        if (src_count <= max_points || levels_.empty())
        {
            return {
                source_x.subspan(src_lo, src_count),
                source_y.subspan(src_lo, src_count),
                0,
            };
        }

        // Try cached levels from finest (level 1) to coarsest (level N).
        // Return the first (finest) level that fits within the budget.
        for (std::size_t i = 0; i < levels_.size(); ++i)
        {
            const auto& lvl   = levels_[i];
            auto [lo, hi]     = visible_range(lvl.x, x_min, x_max);
            std::size_t count = hi - lo;
            if (count <= max_points)
            {
                return {
                    std::span<const float>(lvl.x).subspan(lo, count),
                    std::span<const float>(lvl.y).subspan(lo, count),
                    i + 1,
                };
            }
        }

        // All levels exceed budget; return the coarsest level
        const auto& coarsest = levels_.back();
        auto [lo, hi]        = visible_range(coarsest.x, x_min, x_max);
        std::size_t count    = hi - lo;
        return {
            std::span<const float>(coarsest.x).subspan(lo, count),
            std::span<const float>(coarsest.y).subspan(lo, count),
            levels_.size(),
        };
    }

    /// Number of cached levels (0 = no pyramid built).
    [[nodiscard]] std::size_t level_count() const { return levels_.size(); }

    /// Point count at a given cached level (1-based; level 0 is source).
    [[nodiscard]] std::size_t level_size(std::size_t level) const
    {
        if (level == 0)
            return source_size_;
        if (level <= levels_.size())
            return levels_[level - 1].x.size();
        return 0;
    }

    /// Generation counter — incremented on each build() call.
    [[nodiscard]] uint64_t generation() const { return generation_; }

    /// Whether the cache has been built.
    [[nodiscard]] bool empty() const { return levels_.empty(); }

    /// Total memory used by all cached levels (bytes).
    [[nodiscard]] std::size_t memory_bytes() const
    {
        std::size_t total = 0;
        for (auto& lvl : levels_)
            total += (lvl.x.capacity() + lvl.y.capacity()) * sizeof(float);
        return total;
    }

   private:
    struct Level
    {
        std::vector<float> x;
        std::vector<float> y;
    };

    /// Find the index range [lo, hi) of elements in `v` that fall within
    /// [x_min, x_max] using binary search.
    static std::pair<std::size_t, std::size_t> visible_range(const std::vector<float>& v,
                                                             float                     x_min,
                                                             float                     x_max)
    {
        if (v.empty())
            return {0, 0};
        auto lo = static_cast<std::size_t>(std::lower_bound(v.begin(), v.end(), x_min) - v.begin());
        auto hi = static_cast<std::size_t>(std::upper_bound(v.begin(), v.end(), x_max) - v.begin());
        return {lo, hi};
    }

    /// Same as above but for a span.
    static std::pair<std::size_t, std::size_t> visible_range_span(std::span<const float> v,
                                                                  float                  x_min,
                                                                  float                  x_max)
    {
        if (v.empty())
            return {0, 0};
        auto lo = static_cast<std::size_t>(std::lower_bound(v.begin(), v.end(), x_min) - v.begin());
        auto hi = static_cast<std::size_t>(std::upper_bound(v.begin(), v.end(), x_max) - v.begin());
        return {lo, hi};
    }

    std::vector<Level> levels_;
    std::size_t        source_size_ = 0;
    uint64_t           generation_  = 0;
};

}   // namespace spectra::data

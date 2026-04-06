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

    /// Statistics returned alongside a query result.
    struct QueryStats
    {
        std::size_t lod_level_used  = 0;     // 0 = full resolution
        std::size_t points_returned = 0;     // Points in the returned span
        std::size_t points_in_range = 0;     // Points in source at this range
        bool        cache_hit       = false; // True if a cached level was used (level >= 1)
    };

    /// Combined query result with diagnostic statistics.
    struct QueryResultWithStats
    {
        std::span<const float> x;
        std::span<const float> y;
        std::size_t            level = 0;
        QueryStats             stats;
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
            auto& lvl         = levels_[i];
            auto [lo, hi]     = visible_range(lvl.x, x_min, x_max);
            std::size_t count = hi - lo;
            if (count <= max_points)
            {
                lvl.last_query_time = ++lru_counter_;
                return {
                    std::span<const float>(lvl.x).subspan(lo, count),
                    std::span<const float>(lvl.y).subspan(lo, count),
                    i + 1,
                };
            }
        }

        // All levels exceed budget; return the coarsest level
        auto& coarsest   = levels_.back();
        auto [lo, hi]    = visible_range(coarsest.x, x_min, x_max);
        std::size_t count = hi - lo;
        coarsest.last_query_time = ++lru_counter_;
        return {
            std::span<const float>(coarsest.x).subspan(lo, count),
            std::span<const float>(coarsest.y).subspan(lo, count),
            levels_.size(),
        };
    }

    /// Like query(), but also returns diagnostic QueryStats.
    [[nodiscard]] QueryResultWithStats query_with_stats(std::span<const float> source_x,
                                                        std::span<const float> source_y,
                                                        float                  x_min,
                                                        float                  x_max,
                                                        std::size_t            max_points) const
    {
        auto [src_lo, src_hi] = visible_range_span(source_x, x_min, x_max);
        std::size_t src_count = src_hi - src_lo;

        if (src_count <= max_points || levels_.empty())
        {
            QueryStats stats;
            stats.lod_level_used  = 0;
            stats.points_returned = src_count;
            stats.points_in_range = src_count;
            stats.cache_hit       = false;
            return {
                source_x.subspan(src_lo, src_count),
                source_y.subspan(src_lo, src_count),
                0,
                stats,
            };
        }

        for (std::size_t i = 0; i < levels_.size(); ++i)
        {
            auto& lvl         = levels_[i];
            auto [lo, hi]     = visible_range(lvl.x, x_min, x_max);
            std::size_t count = hi - lo;
            if (count <= max_points)
            {
                lvl.last_query_time = ++lru_counter_;
                QueryStats stats;
                stats.lod_level_used  = i + 1;
                stats.points_returned = count;
                stats.points_in_range = src_count;
                stats.cache_hit       = true;
                return {
                    std::span<const float>(lvl.x).subspan(lo, count),
                    std::span<const float>(lvl.y).subspan(lo, count),
                    i + 1,
                    stats,
                };
            }
        }

        // All levels exceed budget; return the coarsest level
        auto& coarsest            = levels_.back();
        auto [lo, hi]             = visible_range(coarsest.x, x_min, x_max);
        std::size_t count         = hi - lo;
        coarsest.last_query_time  = ++lru_counter_;
        QueryStats stats;
        stats.lod_level_used  = levels_.size();
        stats.points_returned = count;
        stats.points_in_range = src_count;
        stats.cache_hit       = true;
        return {
            std::span<const float>(coarsest.x).subspan(lo, count),
            std::span<const float>(coarsest.y).subspan(lo, count),
            levels_.size(),
            stats,
        };
    }

    /// Evict cache if it belongs to a stale generation.
    /// Returns true if eviction occurred.
    bool evict_if_stale(uint64_t current_generation)
    {
        if (current_generation > generation_)
        {
            levels_.clear();
            return true;
        }
        return false;
    }

    /// Memory pressure as a ratio in [0.0, 1.0].
    /// Returns 0.0 if no memory budget is set (max_memory_bytes_ == 0).
    [[nodiscard]] float memory_pressure() const
    {
        if (max_memory_bytes_ == 0)
            return 0.0f;
        return static_cast<float>(memory_bytes()) / static_cast<float>(max_memory_bytes_);
    }

    /// Set a memory budget for the cache. Evicts coarsest levels to fit.
    void set_memory_budget(std::size_t bytes)
    {
        max_memory_bytes_ = bytes;
        if (max_memory_bytes_ == 0)
            return;
        // Evict coarsest levels until we are within budget
        while (!levels_.empty() && memory_bytes() > max_memory_bytes_)
            levels_.pop_back();
    }

    /// Return level indices sorted by least-recently-used (oldest first).
    /// Useful for external eviction policies.
    [[nodiscard]] std::vector<std::size_t> levels_lru_order() const
    {
        std::vector<std::size_t> order(levels_.size());
        for (std::size_t i = 0; i < levels_.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(),
                  [this](std::size_t a, std::size_t b)
                  { return levels_[a].last_query_time < levels_[b].last_query_time; });
        return order;
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
        mutable uint64_t   last_query_time = 0;   // LRU timestamp
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

    mutable std::vector<Level> levels_;   // mutable: const query() updates last_query_time
    std::size_t                source_size_      = 0;
    uint64_t                   generation_       = 0;
    std::size_t                max_memory_bytes_ = 0;   // 0 = unlimited
    mutable uint64_t           lru_counter_      = 0;   // Not thread-safe; single-threaded use only
};

}   // namespace spectra::data

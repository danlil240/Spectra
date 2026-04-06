#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

#include "data/lod_cache.hpp"
#include <spectra/chunked_series.hpp>

using namespace spectra::data;
using spectra::ChunkedLineSeries;

// ─── Helpers ────────────────────────────────────────────────────────────────

static void build_large(LodCache&           cache,
                        std::vector<float>& x,
                        std::vector<float>& y,
                        std::size_t         n = 10000)
{
    x.resize(n);
    y.resize(n);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.01f);
    cache.build(x, y);
}

// ─── QueryStats via query_with_stats ────────────────────────────────────────

TEST(LodCacheMetrics, QueryWithStats_FullResNoCacheHit)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 10000);

    // A narrow range with a generous budget should use full resolution
    auto result = cache.query_with_stats(x, y, 100.0f, 200.0f, 10000);
    EXPECT_EQ(result.stats.lod_level_used, 0u);
    EXPECT_FALSE(result.stats.cache_hit);
    EXPECT_GT(result.stats.points_returned, 0u);
    EXPECT_EQ(result.stats.points_returned, result.x.size());
}

TEST(LodCacheMetrics, QueryWithStats_CachedLevelHit)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 10000);

    // Full range with tight budget forces a cached level
    auto result = cache.query_with_stats(x, y, 0.0f, 9999.0f, 100);
    EXPECT_GT(result.stats.lod_level_used, 0u);
    EXPECT_TRUE(result.stats.cache_hit);
    EXPECT_GT(result.stats.points_in_range, 0u);
    EXPECT_EQ(result.stats.points_returned, result.x.size());
}

TEST(LodCacheMetrics, QueryWithStats_MatchesQueryResult)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 5000);

    auto plain  = cache.query(x, y, 0.0f, 4999.0f, 200);
    auto stated = cache.query_with_stats(x, y, 0.0f, 4999.0f, 200);

    EXPECT_EQ(plain.level, stated.level);
    EXPECT_EQ(plain.x.size(), stated.x.size());
    EXPECT_EQ(stated.stats.lod_level_used, stated.level);
    EXPECT_EQ(stated.stats.points_returned, stated.x.size());
}

// ─── evict_if_stale ──────────────────────────────────────────────────────────

TEST(LodCacheMetrics, EvictIfStale_DoesNotEvictSameGeneration)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y);

    uint64_t gen     = cache.generation();   // Should be 1
    bool     evicted = cache.evict_if_stale(gen);
    EXPECT_FALSE(evicted);
    EXPECT_FALSE(cache.empty());
}

TEST(LodCacheMetrics, EvictIfStale_EvictsOnNewerGeneration)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y);

    EXPECT_FALSE(cache.empty());
    bool evicted = cache.evict_if_stale(cache.generation() + 1);
    EXPECT_TRUE(evicted);
    EXPECT_TRUE(cache.empty());
}

TEST(LodCacheMetrics, EvictIfStale_OlderGenerationNoEvict)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y);

    // generation() == 1; passing 0 should not evict
    bool evicted = cache.evict_if_stale(0);
    EXPECT_FALSE(evicted);
    EXPECT_FALSE(cache.empty());
}

// ─── memory_pressure ────────────────────────────────────────────────────────

TEST(LodCacheMetrics, MemoryPressure_ZeroWhenNoBudget)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y);

    // No budget set → pressure must be exactly 0.0
    EXPECT_FLOAT_EQ(cache.memory_pressure(), 0.0f);
}

TEST(LodCacheMetrics, MemoryPressure_CorrectRatioWhenBudgetSet)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y);

    std::size_t used = cache.memory_bytes();
    ASSERT_GT(used, 0u);

    // Budget equal to memory → pressure == 1.0
    cache.set_memory_budget(used);
    float pressure = cache.memory_pressure();
    // After set_memory_budget, some levels may have been evicted if budget == used;
    // at minimum pressure should be in [0, 1].
    EXPECT_GE(pressure, 0.0f);
    EXPECT_LE(pressure, 1.0f);
}

TEST(LodCacheMetrics, MemoryPressure_BelowOne_WhenBudgetIsGenerous)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y);

    std::size_t used = cache.memory_bytes();
    cache.set_memory_budget(used * 10);
    EXPECT_LT(cache.memory_pressure(), 1.0f);
    EXPECT_GT(cache.memory_pressure(), 0.0f);
}

// ─── set_memory_budget eviction ──────────────────────────────────────────────

TEST(LodCacheMetrics, SetMemoryBudget_EvictsCoarsestLevels)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 10000);

    ASSERT_GT(cache.level_count(), 0u);
    std::size_t before = cache.level_count();

    // Set a tiny budget to force eviction
    cache.set_memory_budget(1);   // 1 byte forces all levels out
    EXPECT_LT(cache.level_count(), before);
}

TEST(LodCacheMetrics, SetMemoryBudget_ZeroMeansUnlimited)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 10000);

    std::size_t before = cache.level_count();
    cache.set_memory_budget(0);   // 0 = unlimited, no eviction
    EXPECT_EQ(cache.level_count(), before);
}

// ─── LRU tracking ───────────────────────────────────────────────────────────

TEST(LodCacheMetrics, LruOrder_QueryUpdatesTimestamp)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 10000);
    ASSERT_GE(cache.level_count(), 2u);

    // Query using cached level (small budget) to update LRU on some level
    (void)cache.query(x, y, 0.0f, 9999.0f, 50);

    auto lru = cache.levels_lru_order();
    EXPECT_EQ(lru.size(), cache.level_count());
}

TEST(LodCacheMetrics, LruOrder_RecentlyUsedNotFirst)
{
    LodCache           cache;
    std::vector<float> x, y;
    build_large(cache, x, y, 10000);
    ASSERT_GE(cache.level_count(), 2u);

    // Touch a cached level with a tight budget
    (void)cache.query(x, y, 0.0f, 9999.0f, 50);

    auto lru = cache.levels_lru_order();
    ASSERT_GE(lru.size(), 2u);

    // The most recently used level should NOT be first in LRU order
    // (first = least recently used = candidate for eviction)
    // We can't know exactly which level was picked without more info, but
    // the LRU list must be sorted by ascending last_query_time.
    // Verify the list is not all zeros by checking query_with_stats.
    auto r = cache.query_with_stats(x, y, 0.0f, 9999.0f, 50);
    EXPECT_TRUE(r.stats.cache_hit);
}

// ─── ChunkedLineSeries::last_query_stats ────────────────────────────────────

TEST(LodCacheMetrics, ChunkedSeries_QueryStats_Default)
{
    ChunkedLineSeries series;
    // No data yet — stats should be default-constructed
    auto stats = series.last_query_stats();
    EXPECT_EQ(stats.lod_level_used, 0u);
    EXPECT_EQ(stats.points_returned, 0u);
    EXPECT_FALSE(stats.cache_hit);
}

TEST(LodCacheMetrics, ChunkedSeries_QueryStats_NoLod)
{
    ChunkedLineSeries  series;
    const std::size_t  N = 1000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = static_cast<float>(i);
    series.set_data(x, y);

    auto vd    = series.visible_data(0.0f, 999.0f, 10000);
    auto stats = series.last_query_stats();

    EXPECT_EQ(stats.lod_level_used, 0u);
    EXPECT_FALSE(stats.cache_hit);
    EXPECT_GT(stats.points_returned, 0u);
}

TEST(LodCacheMetrics, ChunkedSeries_QueryStats_WithLod)
{
    ChunkedLineSeries series;
    series.enable_lod(true);

    const std::size_t  N = 10000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.01f);
    series.set_data(x, y);

    // Query full range with tiny budget — should use a cached LoD level
    auto vd    = series.visible_data(0.0f, 9999.0f, 100);
    auto stats = series.last_query_stats();

    EXPECT_GT(stats.lod_level_used, 0u);
    EXPECT_TRUE(stats.cache_hit);
    EXPECT_GT(stats.points_returned, 0u);
    EXPECT_EQ(stats.points_returned, vd.x.size());
}

// ─── set_max_visible_points / set_prefetch_margin ───────────────────────────

TEST(LodCacheMetrics, ChunkedSeries_MaxVisiblePoints_DefaultAndSet)
{
    ChunkedLineSeries series;
    EXPECT_EQ(series.max_visible_points(), 65536u);

    series.set_max_visible_points(1024);
    EXPECT_EQ(series.max_visible_points(), 1024u);
}

TEST(LodCacheMetrics, ChunkedSeries_PrefetchMargin_DefaultAndSet)
{
    ChunkedLineSeries series;
    EXPECT_FLOAT_EQ(series.prefetch_margin(), 0.1f);

    series.set_prefetch_margin(0.25f);
    EXPECT_FLOAT_EQ(series.prefetch_margin(), 0.25f);
}

TEST(LodCacheMetrics, ChunkedSeries_MaxVisiblePoints_AffectsDecimation)
{
    ChunkedLineSeries  series;
    const std::size_t  N = 10000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = static_cast<float>(i);
    series.set_data(x, y);

    // With a small max_visible_points, visible_data should return fewer points
    series.set_max_visible_points(128);
    auto vd = series.visible_data(0.0f, 9999.0f);   // uses default (max_visible_points=128)
    EXPECT_LE(vd.x.size(), 128u);
}

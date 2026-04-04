#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

#include "data/lod_cache.hpp"

using namespace spectra::data;

// ─── Construction ───────────────────────────────────────────────────────────

TEST(LodCache, DefaultEmpty)
{
    LodCache cache;
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.level_count(), 0u);
    EXPECT_EQ(cache.generation(), 0u);
}

// ─── Build ──────────────────────────────────────────────────────────────────

TEST(LodCache, BuildFromSmallData)
{
    LodCache cache;

    // Too few points to build any level
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 0};
    cache.build(x, y);

    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.level_count(), 0u);
}

TEST(LodCache, BuildFromLargeData)
{
    LodCache cache;

    const std::size_t  N = 10000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.01f);

    cache.build(x, y);

    EXPECT_FALSE(cache.empty());
    EXPECT_GT(cache.level_count(), 0u);
    EXPECT_EQ(cache.generation(), 1u);

    // Level 0 is source data
    EXPECT_EQ(cache.level_size(0), N);

    // Each subsequent level should be smaller
    for (std::size_t i = 1; i <= cache.level_count(); ++i)
    {
        EXPECT_GT(cache.level_size(i), 0u);
        if (i > 1)
            EXPECT_LT(cache.level_size(i), cache.level_size(i - 1));
    }
}

TEST(LodCache, BuildIncrementsGeneration)
{
    LodCache cache;

    std::vector<float> x(1000), y(1000);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 1000; ++i)
        y[i] = static_cast<float>(i);

    cache.build(x, y);
    EXPECT_EQ(cache.generation(), 1u);

    cache.build(x, y);
    EXPECT_EQ(cache.generation(), 2u);
}

// ─── Query ──────────────────────────────────────────────────────────────────

TEST(LodCache, QueryFullRange)
{
    LodCache cache;

    const std::size_t  N = 10000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = static_cast<float>(i);

    cache.build(x, y);

    // Query full range with small budget should return a cached level (not full res)
    auto result = cache.query(x, y, 0.0f, static_cast<float>(N), 100);
    EXPECT_GT(result.level, 0u);   // Should use a cached level
    // The coarsest level may still exceed 100 points, but should be much less than N
    EXPECT_LT(result.x.size(), N);
}

TEST(LodCache, QueryZoomedIn)
{
    LodCache cache;

    const std::size_t  N = 10000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = static_cast<float>(i);

    cache.build(x, y);

    // Query a narrow range with generous budget → full resolution
    auto result = cache.query(x, y, 100.0f, 200.0f, 10000);
    EXPECT_EQ(result.level, 0u);   // Full resolution
    EXPECT_LE(result.x.size(), 101u);
    EXPECT_GT(result.x.size(), 0u);
}

TEST(LodCache, QueryResultDataIntegrity)
{
    LodCache cache;

    const std::size_t  N = 5000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.01f);

    cache.build(x, y);

    auto result = cache.query(x, y, 0.0f, static_cast<float>(N), 200);

    // Result should have matching x and y sizes
    EXPECT_EQ(result.x.size(), result.y.size());

    // x values should be sorted
    for (std::size_t i = 1; i < result.x.size(); ++i)
        EXPECT_LE(result.x[i - 1], result.x[i]);
}

// ─── Memory accounting ─────────────────────────────────────────────────────

TEST(LodCache, MemoryBytes)
{
    LodCache cache;
    EXPECT_EQ(cache.memory_bytes(), 0u);

    std::vector<float> x(10000), y(10000);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 10000; ++i)
        y[i] = static_cast<float>(i);

    cache.build(x, y);
    EXPECT_GT(cache.memory_bytes(), 0u);
    // LoD cache should be smaller than source data
    EXPECT_LT(cache.memory_bytes(), 10000u * 2 * sizeof(float));
}

// ─── Edge cases ─────────────────────────────────────────────────────────────

TEST(LodCache, EmptyQuery)
{
    LodCache           cache;
    std::vector<float> x, y;
    auto               result = cache.query(x, y, 0.0f, 100.0f, 1000);
    EXPECT_TRUE(result.x.empty());
    EXPECT_TRUE(result.y.empty());
}

TEST(LodCache, QueryOutsideRange)
{
    LodCache cache;

    std::vector<float> x(1000), y(1000);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 1000; ++i)
        y[i] = static_cast<float>(i);

    cache.build(x, y);

    // Query completely outside data range
    auto result = cache.query(x, y, 2000.0f, 3000.0f, 100);
    EXPECT_TRUE(result.x.empty());
}

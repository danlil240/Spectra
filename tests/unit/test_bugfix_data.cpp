// Regression tests for data-layer bug fixes (chunked array, chunked series,
// decimation, LoD cache, filters, mapped file).

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <spectra/chunked_series.hpp>
#include <vector>

#include "data/chunked_array.hpp"
#include "data/decimation.hpp"
#include "data/filters.hpp"
#include "data/lod_cache.hpp"
#include "data/mapped_file.hpp"

using spectra::ChunkedLineSeries;
using spectra::data::ChunkedArray;

namespace
{

std::vector<float> contents(const ChunkedArray& a)
{
    return a.read_vec(0, a.size());
}

std::filesystem::path temp_path(const char* name)
{
    return std::filesystem::temp_directory_path() / name;
}

}   // namespace

// ─── Bug #8: erase_front with a partial-chunk count ─────────────────────────

TEST(BugfixData, ChunkedArrayPartialEraseKeepsIndexing)
{
    ChunkedArray a(4);
    for (int i = 0; i < 6; ++i)
        a.push_back(static_cast<float>(i));

    EXPECT_EQ(a.erase_front(1), 1u);
    ASSERT_EQ(a.size(), 5u);
    for (std::size_t i = 0; i < a.size(); ++i)
        EXPECT_FLOAT_EQ(a[i], static_cast<float>(i + 1));

    a.push_back(6.0f);
    EXPECT_EQ(contents(a), (std::vector<float>{1, 2, 3, 4, 5, 6}));

    std::vector<float> more = {7, 8, 9, 10, 11};
    a.append(more);
    EXPECT_EQ(contents(a), (std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));

    // Read across the shifted chunk boundary.
    EXPECT_EQ(a.read_vec(2, 4), (std::vector<float>{3, 4, 5, 6}));
}

TEST(BugfixData, ChunkedArrayRepeatedPartialEraseReleasesChunks)
{
    ChunkedArray a(4);
    for (int i = 0; i < 12; ++i)
        a.push_back(static_cast<float>(i));
    EXPECT_EQ(a.chunk_count(), 3u);

    a.erase_front(3);
    EXPECT_EQ(a.chunk_count(), 3u);
    EXPECT_EQ(a.front_chunk_count(), 1u);
    a.erase_front(3);   // crosses into chunk 1 -> chunk 0 released
    EXPECT_EQ(a.chunk_count(), 2u);
    EXPECT_EQ(a.size(), 6u);
    EXPECT_EQ(contents(a), (std::vector<float>{6, 7, 8, 9, 10, 11}));

    // chunk_data honours the front offset and the partial last chunk.
    auto c0 = a.chunk_data(0);
    ASSERT_EQ(c0.size(), 2u);
    EXPECT_FLOAT_EQ(c0[0], 6.0f);
    auto c1 = a.chunk_data(1);
    ASSERT_EQ(c1.size(), 4u);
    EXPECT_FLOAT_EQ(c1[3], 11.0f);

    a.erase_front(100);
    EXPECT_TRUE(a.empty());
    EXPECT_EQ(a.chunk_count(), 0u);
    a.push_back(42.0f);
    EXPECT_FLOAT_EQ(a[0], 42.0f);
}

TEST(BugfixData, ChunkedSeriesEraseBeforeThenAppend)
{
    ChunkedLineSeries s;
    s.set_chunk_size(4);
    for (int i = 0; i < 10; ++i)
        s.append(static_cast<float>(i), static_cast<float>(i * 10));

    EXPECT_EQ(s.erase_before(3.0f), 3u);
    s.append(10.0f, 100.0f);

    auto x = s.x_range(0, s.point_count());
    auto y = s.y_range(0, s.point_count());
    ASSERT_EQ(x.size(), 8u);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        EXPECT_FLOAT_EQ(x[i], static_cast<float>(i + 3));
        EXPECT_FLOAT_EQ(y[i], static_cast<float>((i + 3) * 10));
    }
}

// ─── Bug #27: set_memory_budget must invalidate LoD and mark dirty ──────────

TEST(BugfixData, SetMemoryBudgetInvalidatesLod)
{
    ChunkedLineSeries s;
    s.set_chunk_size(64);
    s.enable_lod(true);
    std::vector<float> x(4096), y(4096);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i % 7);
    }
    s.set_data(x, y);
    (void)s.visible_data(-1e9f, 1e9f, 100);   // builds the LoD pyramid
    s.clear_dirty();

    // Keep at most 2 chunks per array -> only the last 128 points survive.
    s.set_memory_budget(2 * 2 * 64 * sizeof(float));
    EXPECT_TRUE(s.is_dirty());
    ASSERT_LE(s.point_count(), 128u);
    ASSERT_GT(s.point_count(), 0u);
    const float first_kept = s.x_range(0, 1)[0];

    auto vis = s.visible_data(-1e9f, 1e9f, 16);
    ASSERT_FALSE(vis.x.empty());
    for (float vx : vis.x)
        EXPECT_GE(vx, first_kept);
}

// ─── Bug #28: budget smaller than one chunk must not wipe the series ────────

TEST(BugfixData, TinyMemoryBudgetKeepsLastChunk)
{
    ChunkedLineSeries s;
    s.set_chunk_size(32);
    s.set_memory_budget(16);   // far less than one chunk per array
    for (int i = 0; i < 10; ++i)
        s.append(static_cast<float>(i), static_cast<float>(i));
    EXPECT_EQ(s.point_count(), 10u);

    for (int i = 10; i < 100; ++i)
        s.append(static_cast<float>(i), static_cast<float>(i));
    // Only the last (partial) chunk is kept: a rolling window, never empty.
    EXPECT_GT(s.point_count(), 0u);
    EXPECT_LE(s.point_count(), 32u);
    auto x = s.x_range(0, s.point_count());
    EXPECT_FLOAT_EQ(x.back(), 99.0f);
    EXPECT_FLOAT_EQ(x.front(), static_cast<float>(100 - s.point_count()));
}

// ─── Bug #33: min_max_decimate with NaN / inf buckets ───────────────────────

TEST(BugfixData, MinMaxDecimateNaNBucketNoBogusPoint)
{
    const std::size_t  n = 10000;
    std::vector<float> x(n), y(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = (i >= 5000 && i < 5300) ? std::numeric_limits<float>::quiet_NaN()
                                       : static_cast<float>(i % 10);
    }
    auto out = spectra::data::min_max_decimate(x, y, 100);
    ASSERT_FALSE(out.empty());
    bool saw_nan = false;
    for (auto& [px, py] : out)
    {
        EXPECT_NE(py, std::numeric_limits<float>::max());
        if (std::isnan(py))
        {
            saw_nan = true;
            EXPECT_GE(px, 5000.0f);
            EXPECT_LT(px, 5300.0f);
        }
        else
        {
            EXPECT_LE(py, 9.0f);
        }
    }
    EXPECT_TRUE(saw_nan);   // line break preserved
}

TEST(BugfixData, MinMaxDecimateInfBucket)
{
    const std::size_t  n = 1000;
    std::vector<float> x(n), y(n, 1.0f);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(i);
    for (std::size_t i = 500; i < 520; ++i)
        y[i] = std::numeric_limits<float>::infinity();
    auto out = spectra::data::min_max_decimate(x, y, 100);
    for (auto& [px, py] : out)
    {
        EXPECT_NE(py, std::numeric_limits<float>::max());
        if (std::isinf(py))
        {
            EXPECT_GE(px, 500.0f);
            EXPECT_LT(px, 520.0f);
        }
    }
}

// ─── Bug #34: inverted LoD query range ──────────────────────────────────────

TEST(BugfixData, LodCacheInvertedRangeIsEmpty)
{
    std::vector<float> x(10000), y(10000);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = std::sin(static_cast<float>(i) * 0.01f);
    }
    spectra::data::LodCache cache;
    cache.build(x, y);
    ASSERT_FALSE(cache.empty());

    auto r = cache.query_with_stats(x, y, 5000.0f, 100.0f, 65536);
    EXPECT_TRUE(r.x.empty());
    EXPECT_TRUE(r.y.empty());
    EXPECT_EQ(r.stats.points_in_range, 0u);

    auto q = cache.query(x, y, 5000.0f, 100.0f, 10);
    EXPECT_TRUE(q.x.empty());

    ChunkedLineSeries s;
    s.enable_lod(true);
    s.set_data(x, y);
    auto vis = s.visible_data(5000.0f, 100.0f, 65536);
    EXPECT_TRUE(vis.x.empty());
}

// ─── Bug #71: max_points == 1 should not return nothing ─────────────────────

TEST(BugfixData, VisibleDataMaxPointsOne)
{
    ChunkedLineSeries  s;
    std::vector<float> x(100), y(100);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i);
    }
    s.set_data(x, y);
    s.set_max_visible_points(1);
    auto vis = s.visible_data(0.0f, 100.0f);
    EXPECT_FALSE(vis.x.empty());
    EXPECT_EQ(vis.x.size(), vis.y.size());
}

// ─── Bug #84: gaussian_smooth with tiny sigma ───────────────────────────────

TEST(BugfixData, GaussianSmoothTinySigmaIsIdentity)
{
    std::vector<float> v   = {1, 2, 3, 4};
    auto               out = spectra::data::gaussian_smooth(v, 1e-20f, 0);
    ASSERT_EQ(out.size(), v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        EXPECT_FLOAT_EQ(out[i], v[i]);

    auto out2 = spectra::data::gaussian_smooth(v, 1e-20f, 2);
    ASSERT_EQ(out2.size(), v.size());
    for (float f : out2)
        EXPECT_FALSE(std::isnan(f));

    auto out_nan = spectra::data::gaussian_smooth(v, std::numeric_limits<float>::quiet_NaN(), 0);
    EXPECT_EQ(out_nan, v);
}

// ─── Bug #85: unaligned float offsets ───────────────────────────────────────

TEST(BugfixData, MappedFileUnalignedOffset)
{
    auto path = temp_path("spectra_bugfix_data_unaligned.bin");
    {
        std::ofstream f(path, std::ios::binary);
        const char    header[6] = {'H', 'E', 'A', 'D', 'E', 'R'};
        f.write(header, sizeof(header));
        const float vals[4] = {0.0f, 10.0f, 1.0f, 11.0f};   // x0,y0,x1,y1
        f.write(reinterpret_cast<const char*>(vals), sizeof(vals));
    }

    {
        spectra::data::MappedFile mf(path.string());
        ASSERT_TRUE(mf.is_open());
        EXPECT_TRUE(mf.subspan_float(6, 4).empty());   // misaligned -> refused
        auto copy = mf.copy_floats(6, 4);
        EXPECT_EQ(copy, (std::vector<float>{0.0f, 10.0f, 1.0f, 11.0f}));
        EXPECT_EQ(mf.subspan_float(4, 1).size(), 1u);   // aligned still works
    }

    ChunkedLineSeries s;
    s.load_binary(path.string(), 6, 0);
    ASSERT_EQ(s.point_count(), 2u);
    EXPECT_EQ(s.x_range(0, 2), (std::vector<float>{0.0f, 1.0f}));
    EXPECT_EQ(s.y_range(0, 2), (std::vector<float>{10.0f, 11.0f}));

    ChunkedLineSeries c;
    c.load_binary_columns(path.string(), 6, 10, 1);
    ASSERT_EQ(c.point_count(), 1u);
    EXPECT_FLOAT_EQ(c.x_range(0, 1)[0], 0.0f);
    EXPECT_FLOAT_EQ(c.y_range(0, 1)[0], 10.0f);

    std::filesystem::remove(path);
}

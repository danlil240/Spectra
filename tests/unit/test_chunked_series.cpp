#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <numeric>
#include <spectra/chunked_series.hpp>
#include <vector>

using namespace spectra;

// ─── Construction ───────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, DefaultConstruction)
{
    ChunkedLineSeries series;
    EXPECT_EQ(series.point_count(), 0u);
    EXPECT_TRUE(series.is_dirty());
    EXPECT_TRUE(series.visible());
    EXPECT_FLOAT_EQ(series.width(), 2.0f);
}

// ─── Set data ───────────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, SetData)
{
    ChunkedLineSeries  series;
    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y = {0.0f, 1.0f, 4.0f, 9.0f};

    series.set_data(x, y);
    EXPECT_EQ(series.point_count(), 4u);
    EXPECT_TRUE(series.is_dirty());

    auto rx = series.x_range(0, 4);
    auto ry = series.y_range(0, 4);
    ASSERT_EQ(rx.size(), 4u);
    ASSERT_EQ(ry.size(), 4u);
    EXPECT_FLOAT_EQ(rx[0], 0.0f);
    EXPECT_FLOAT_EQ(rx[3], 3.0f);
    EXPECT_FLOAT_EQ(ry[2], 4.0f);
}

// ─── Append ─────────────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, AppendSingle)
{
    ChunkedLineSeries series;
    series.append(1.0f, 10.0f);
    series.append(2.0f, 20.0f);
    series.append(3.0f, 30.0f);

    EXPECT_EQ(series.point_count(), 3u);

    auto x = series.x_range(0, 3);
    auto y = series.y_range(0, 3);
    EXPECT_FLOAT_EQ(x[0], 1.0f);
    EXPECT_FLOAT_EQ(y[2], 30.0f);
}

TEST(ChunkedLineSeries, AppendBatch)
{
    ChunkedLineSeries  series;
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f};

    series.append_batch(x, y);
    EXPECT_EQ(series.point_count(), 3u);

    auto rx = series.x_range(0, 3);
    EXPECT_FLOAT_EQ(rx[2], 3.0f);
}

// ─── Erase before ───────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, EraseBefore)
{
    ChunkedLineSeries  series;
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};

    series.set_data(x, y);

    auto removed = series.erase_before(3.0f);
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(series.point_count(), 3u);

    auto rx = series.x_range(0, 3);
    EXPECT_FLOAT_EQ(rx[0], 3.0f);
}

TEST(ChunkedLineSeries, EraseBeforeEmpty)
{
    ChunkedLineSeries series;
    auto              removed = series.erase_before(1.0f);
    EXPECT_EQ(removed, 0u);
}

// ─── Configuration ──────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, LodToggle)
{
    ChunkedLineSeries series;
    EXPECT_FALSE(series.lod_enabled());

    series.enable_lod(true);
    EXPECT_TRUE(series.lod_enabled());

    series.enable_lod(false);
    EXPECT_FALSE(series.lod_enabled());
}

TEST(ChunkedLineSeries, MemoryBudget)
{
    ChunkedLineSeries series;
    EXPECT_EQ(series.memory_budget(), 0u);

    series.set_memory_budget(1024);
    EXPECT_EQ(series.memory_budget(), 1024u);
}

TEST(ChunkedLineSeries, Width)
{
    ChunkedLineSeries series;
    EXPECT_FLOAT_EQ(series.width(), 2.0f);

    series.width(5.0f);
    EXPECT_FLOAT_EQ(series.width(), 5.0f);
}

// ─── Fluent API ─────────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, FluentAPI)
{
    ChunkedLineSeries series;
    auto& ref = series.label("chunked").color(colors::red).width(3.0f).enable_lod(true);

    EXPECT_EQ(&ref, &series);
    const Series& base = series;
    EXPECT_EQ(base.label(), "chunked");
    EXPECT_FLOAT_EQ(base.color().r, 1.0f);
    EXPECT_FLOAT_EQ(series.width(), 3.0f);
    EXPECT_TRUE(series.lod_enabled());
}

// ─── Visible data ───────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, VisibleDataSmall)
{
    ChunkedLineSeries  series;
    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y = {0.0f, 1.0f, 4.0f, 9.0f, 16.0f};
    series.set_data(x, y);

    auto vis = series.visible_data(0.0f, 4.0f, 100);
    EXPECT_EQ(vis.x.size(), 5u);
    EXPECT_EQ(vis.y.size(), 5u);
    EXPECT_EQ(vis.lod_level, 0u);   // Full resolution
}

TEST(ChunkedLineSeries, VisibleDataWithRange)
{
    ChunkedLineSeries  series;
    std::vector<float> x(100), y(100);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 100; ++i)
        y[i] = static_cast<float>(i * i);

    series.set_data(x, y);

    // Query a subset of the range
    auto vis = series.visible_data(20.0f, 40.0f, 100);
    EXPECT_GT(vis.x.size(), 0u);
    EXPECT_LE(vis.x.size(), 100u);
    // All returned x values should be in [20, 40]
    for (float xi : vis.x)
    {
        EXPECT_GE(xi, 20.0f);
        EXPECT_LE(xi, 40.0f);
    }
}

TEST(ChunkedLineSeries, VisibleDataEmpty)
{
    ChunkedLineSeries series;
    auto              vis = series.visible_data(0.0f, 100.0f, 1000);
    EXPECT_TRUE(vis.x.empty());
    EXPECT_TRUE(vis.y.empty());
}

TEST(ChunkedLineSeries, VisibleDataWithLod)
{
    ChunkedLineSeries series;
    series.enable_lod(true);

    const std::size_t  N = 10000;
    std::vector<float> x(N), y(N);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < N; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.01f);

    series.set_data(x, y);

    // Query full range with small budget → should use LoD level (not full res)
    auto vis = series.visible_data(0.0f, static_cast<float>(N), 100);
    EXPECT_GT(vis.x.size(), 0u);
    EXPECT_LT(vis.x.size(), N);   // Should be reduced from full resolution
}

// ─── Memory budget enforcement ──────────────────────────────────────────────

TEST(ChunkedLineSeries, MemoryBudgetEnforced)
{
    ChunkedLineSeries series;
    // Set a small budget (enough for ~64 floats per array)
    series.set_chunk_size(32);
    series.set_memory_budget(512);   // 512 bytes = 128 floats = 64 per array

    // Append more data than budget allows
    for (int i = 0; i < 200; ++i)
        series.append(static_cast<float>(i), static_cast<float>(i));

    // Memory should be within budget (allowing for chunk granularity)
    EXPECT_LE(series.memory_bytes(), 1024u);   // Some slack for chunk alignment
}

// ─── Load binary ────────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, LoadBinaryInterleaved)
{
    // Write interleaved [x0,y0,x1,y1,...] file
    std::vector<float> data;
    for (int i = 0; i < 100; ++i)
    {
        data.push_back(static_cast<float>(i));
        data.push_back(static_cast<float>(i * i));
    }

    std::string   path = "/tmp/spectra_test_interleaved.bin";
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size() * sizeof(float)));
    ofs.close();

    ChunkedLineSeries series;
    series.load_binary(path);

    EXPECT_EQ(series.point_count(), 100u);

    auto x = series.x_range(0, 3);
    auto y = series.y_range(0, 3);
    EXPECT_FLOAT_EQ(x[0], 0.0f);
    EXPECT_FLOAT_EQ(x[1], 1.0f);
    EXPECT_FLOAT_EQ(y[1], 1.0f);
    EXPECT_FLOAT_EQ(y[2], 4.0f);

    std::remove(path.c_str());
}

TEST(ChunkedLineSeries, LoadBinaryColumns)
{
    // Write column-major file: [x0,x1,...,xN,y0,y1,...,yN]
    const std::size_t  N = 50;
    std::vector<float> data;
    for (std::size_t i = 0; i < N; ++i)
        data.push_back(static_cast<float>(i));
    for (std::size_t i = 0; i < N; ++i)
        data.push_back(static_cast<float>(i * 2));

    std::string   path = "/tmp/spectra_test_columns.bin";
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size() * sizeof(float)));
    ofs.close();

    ChunkedLineSeries series;
    series.load_binary_columns(path, 0, N * sizeof(float), N);

    EXPECT_EQ(series.point_count(), N);

    auto x = series.x_range(0, 3);
    auto y = series.y_range(0, 3);
    EXPECT_FLOAT_EQ(x[0], 0.0f);
    EXPECT_FLOAT_EQ(x[2], 2.0f);
    EXPECT_FLOAT_EQ(y[0], 0.0f);
    EXPECT_FLOAT_EQ(y[2], 4.0f);

    std::remove(path.c_str());
}

// ─── Move semantics ─────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, MoveConstruction)
{
    ChunkedLineSeries  series;
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {10.0f, 20.0f, 30.0f};
    series.set_data(x, y);

    ChunkedLineSeries moved(std::move(series));
    EXPECT_EQ(moved.point_count(), 3u);

    auto rx = moved.x_range(0, 3);
    EXPECT_FLOAT_EQ(rx[0], 1.0f);
}

// ─── Memory bytes ───────────────────────────────────────────────────────────

TEST(ChunkedLineSeries, MemoryBytesReported)
{
    ChunkedLineSeries  series;
    std::vector<float> x(1000), y(1000);
    series.set_data(x, y);

    EXPECT_GT(series.memory_bytes(), 0u);
}

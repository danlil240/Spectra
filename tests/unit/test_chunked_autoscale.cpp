// Regression tests: ChunkedLineSeries participates in automatic axis extents,
// latest-x (presented-buffer follow) and windowed-Y autoscale (audit S15).

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/series.hpp>
#include <vector>

using namespace spectra;

namespace
{

// The edge-case gallery signal: 100k samples over [0,100].
void gallery_signal(std::vector<float>& x, std::vector<float>& y)
{
    constexpr int N = 100000;
    x.resize(N);
    y.resize(N);
    for (int i = 0; i < N; ++i)
    {
        x[i] = 100.0f * static_cast<float>(i) / static_cast<float>(N - 1);
        y[i] = std::sin(x[i] * 2.0f) + 0.15f * std::sin(x[i] * 80.0f);
    }
}

void expect_limits_eq(const AxisLimits& a, const AxisLimits& b)
{
    EXPECT_DOUBLE_EQ(a.min, b.min);
    EXPECT_DOUBLE_EQ(a.max, b.max);
}

}   // namespace

class ChunkedAutoscaleLod : public ::testing::TestWithParam<bool>
{
};

TEST_P(ChunkedAutoscaleLod, TightSpansFullGallerySignal)
{
    std::vector<float> x, y;
    gallery_signal(x, y);

    Axes ax;
    ax.chunked_line().set_data(x, y).enable_lod(GetParam());
    ax.autoscale_mode(AutoscaleMode::Tight);

    const auto xl = ax.x_limits();
    const auto yl = ax.y_limits();
    EXPECT_DOUBLE_EQ(xl.min, 0.0);
    EXPECT_DOUBLE_EQ(xl.max, 100.0);
    EXPECT_NEAR(yl.min, -1.15, 0.01);
    EXPECT_NEAR(yl.max, 1.15, 0.01);

    // Identical to the equivalent contiguous line.
    Axes ref;
    ref.line(x, y);
    ref.autoscale_mode(AutoscaleMode::Tight);
    expect_limits_eq(xl, ref.x_limits());
    expect_limits_eq(yl, ref.y_limits());
}

TEST_P(ChunkedAutoscaleLod, PaddedMatchesLineSeriesOnAllScales)
{
    std::vector<float> x, y;
    gallery_signal(x, y);
    for (auto& v : x)
        v += 1.0f;   // keep x positive for log/sqrt

    for (ScaleType xs : {ScaleType::Linear, ScaleType::Log10, ScaleType::Sqrt})
    {
        Axes ax;
        ax.chunked_line().set_data(x, y).enable_lod(GetParam());
        ax.xscale(xs);
        Axes ref;
        ref.line(x, y);
        ref.xscale(xs);
        expect_limits_eq(ax.x_limits(), ref.x_limits());
        expect_limits_eq(ax.y_limits(), ref.y_limits());
        EXPECT_LT(ax.x_limits().min, 1.0 + 1e-9);
        EXPECT_GT(ax.x_limits().max, 101.0 - 1e-9);
    }
}

TEST_P(ChunkedAutoscaleLod, RepeatedAutoFitIsStable)
{
    std::vector<float> x, y;
    gallery_signal(x, y);
    Axes  ax;
    auto& cs = ax.chunked_line().set_data(x, y).enable_lod(GetParam());
    ax.autoscale_mode(AutoscaleMode::Tight);
    const auto xl0 = ax.x_limits();
    const auto yl0 = ax.y_limits();
    for (int i = 0; i < 3; ++i)
    {
        // Rendering-style queries must not perturb the full-data fit.
        (void)cs.visible_data(10.0f, 20.0f, 256);
        ax.auto_fit();
        expect_limits_eq(ax.x_limits(), xl0);
        expect_limits_eq(ax.y_limits(), yl0);
    }
}

INSTANTIATE_TEST_SUITE_P(LodOnOff, ChunkedAutoscaleLod, ::testing::Bool());

TEST(ChunkedAutoscale, MixedWithLineSeries)
{
    Axes               ax;
    std::vector<float> lx = {0.0f, 1.0f};
    std::vector<float> ly = {0.0f, 1.0f};
    ax.line(lx, ly);
    std::vector<float> cx = {5.0f, 6.0f, 10.0f};
    std::vector<float> cy = {-4.0f, 20.0f, 3.0f};
    ax.chunked_line().set_data(cx, cy);
    ax.autoscale_mode(AutoscaleMode::Tight);

    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 10.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, -4.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 20.0);
}

TEST(ChunkedAutoscale, SkipsNonFiniteAndFallsBackWhenEmpty)
{
    const float        nan = std::numeric_limits<float>::quiet_NaN();
    const float        inf = std::numeric_limits<float>::infinity();
    std::vector<float> x   = {nan, 2.0f, 3.0f, inf, 4.0f};
    std::vector<float> y   = {1.0f, nan, -2.0f, 9.0f, 5.0f};

    Axes ax;
    ax.chunked_line().set_data(x, y);
    ax.autoscale_mode(AutoscaleMode::Tight);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 2.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 4.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, -2.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 9.0);

    Axes empty;
    empty.chunked_line();
    empty.autoscale_mode(AutoscaleMode::Tight);
    EXPECT_DOUBLE_EQ(empty.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(empty.x_limits().max, 1.0);
}

TEST(ChunkedAutoscale, AppliesDoubleOffsets)
{
    std::vector<float> x = {0.0f, 0.5f, 1.0f};
    std::vector<float> y = {-0.01f, 0.0f, 0.02f};
    Axes               ax;
    auto&              cs = ax.chunked_line().set_data(x, y);
    cs.x_offset(1720000000.0);
    cs.y_offset(1.0e7);
    Axes  ref;
    auto& ls = ref.line(x, y);
    ls.x_offset(1720000000.0);
    ls.y_offset(1.0e7);
    ax.autoscale_mode(AutoscaleMode::Tight);
    ref.autoscale_mode(AutoscaleMode::Tight);
    expect_limits_eq(ax.x_limits(), ref.x_limits());
    expect_limits_eq(ax.y_limits(), ref.y_limits());
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 1720000001.0);
}

TEST(ChunkedAutoscale, LogAxisUsesSmallestPositiveIncludingOffset)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> y = {-3.0f, -1.5f, 0.25f, 4.0f, 50.0f};

    for (double yoff : {0.0, 2.0})
    {
        Axes  ax;
        auto& cs = ax.chunked_line().set_data(x, y);
        cs.y_offset(yoff);
        ax.yscale(ScaleType::Log10);
        ax.autoscale_mode(AutoscaleMode::Tight);
        Axes  ref;
        auto& ls = ref.line(x, y);
        ls.y_offset(yoff);
        ref.yscale(ScaleType::Log10);
        ref.autoscale_mode(AutoscaleMode::Tight);
        expect_limits_eq(ax.y_limits(), ref.y_limits());
    }
    // With offset 2: logical values {-1, 0.5, 2.25, 6, 52} -> min positive 0.5.
    Axes  ax;
    auto& cs = ax.chunked_line().set_data(x, y);
    cs.y_offset(2.0);
    ax.yscale(ScaleType::Log10);
    ax.autoscale_mode(AutoscaleMode::Tight);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 0.5);
}

TEST(ChunkedAutoscale, AppendAndEraseUpdateBounds)
{
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    Axes               ax;
    auto&              cs = ax.chunked_line().set_chunk_size(2).set_data(x, y);
    ax.autoscale_mode(AutoscaleMode::Tight);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 2.0);

    cs.append(3.0f, 50.0f);
    std::vector<float> bx = {4.0f, 5.0f};
    std::vector<float> by = {-7.0f, 1.0f};
    cs.append_batch(bx, by);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 5.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 50.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, -7.0);

    cs.erase_before(3.5f);   // drops x = 0..3 (including y = 50)
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 4.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 1.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, -7.0);

    // set_data replaces the cached bounds entirely.
    std::vector<float> nx = {10.0f, 11.0f};
    std::vector<float> ny = {100.0f, 200.0f};
    cs.set_data(nx, ny);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 10.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 100.0);
}

TEST(ChunkedAutoscale, PresentedBufferFollowsChunkedLatestXAndWindowsY)
{
    Axes  ax;
    auto& cs = ax.chunked_line().set_chunk_size(4);
    for (int i = 0; i <= 20; ++i)
        cs.append(static_cast<float>(i), i < 15 ? 1000.0f : static_cast<float>(i));
    ax.autoscale_mode(AutoscaleMode::Tight);
    ax.presented_buffer(5.0f);

    const auto xl = ax.x_limits();
    EXPECT_DOUBLE_EQ(xl.max, 20.0);
    EXPECT_DOUBLE_EQ(xl.min, 15.0);
    // Window [15,20] excludes the 1000-valued history.
    const auto yl = ax.y_limits();
    EXPECT_DOUBLE_EQ(yl.min, 15.0);
    EXPECT_DOUBLE_EQ(yl.max, 20.0);

    cs.append(21.0f, 30.0f);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 21.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 30.0);
}

TEST(ChunkedAutoscale, DataBoundsCacheMatchesSamples)
{
    ChunkedLineSeries  cs;
    std::vector<float> x = {3.0f, -1.0f, 0.5f};
    std::vector<float> y = {0.0f, -2.0f, 0.125f};
    cs.set_chunk_size(2).set_data(x, y);
    auto b = cs.data_bounds();
    ASSERT_TRUE(b.has_x);
    ASSERT_TRUE(b.has_y);
    EXPECT_DOUBLE_EQ(b.x_min, -1.0);
    EXPECT_DOUBLE_EQ(b.x_max, 3.0);
    EXPECT_DOUBLE_EQ(b.x_min_positive, 0.5);
    EXPECT_DOUBLE_EQ(b.y_min_positive, 0.125);
    EXPECT_EQ(cs.storage_chunk_count(), 2u);
    EXPECT_EQ(cs.x_chunk(0).size() + cs.x_chunk(1).size(), 3u);
    EXPECT_EQ(cs.x_chunk(1).size(), cs.y_chunk(1).size());
    EXPECT_TRUE(cs.x_chunk(5).empty());
}

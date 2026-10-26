#include <gtest/gtest.h>
#include <spectra/series_stats.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace spectra;

namespace
{

bool all_finite(std::span<const float> v)
{
    return std::all_of(v.begin(), v.end(), [](float f) { return std::isfinite(f); });
}

// Outline vectors contain NaN line breaks by design; only reject infinities there.
bool no_inf(std::span<const float> v)
{
    return std::none_of(v.begin(), v.end(), [](float f) { return std::isinf(f); });
}

float total_count(const std::vector<float>& counts)
{
    float s = 0.0f;
    for (float c : counts)
        s += c;
    return s;
}

}   // namespace

// ─── Bug #31: wide finite range overflowed bin width ────────────────────────

TEST(BugfixCoreStats, HistogramWideFiniteRangeHasFiniteEdges)
{
    std::vector<float> data = {-3e38f, 0.0f, 3e38f};
    HistogramSeries    hist(data, 10);

    ASSERT_EQ(hist.bin_edges().size(), 11u);
    EXPECT_TRUE(all_finite(hist.bin_edges()));
    EXPECT_FLOAT_EQ(hist.bin_edges().front(), -3e38f);
    EXPECT_FLOAT_EQ(hist.bin_edges().back(), 3e38f);
    EXPECT_TRUE(std::is_sorted(hist.bin_edges().begin(), hist.bin_edges().end()));
    EXPECT_FLOAT_EQ(total_count(hist.bin_counts()), 3.0f);
    EXPECT_FLOAT_EQ(hist.bin_counts().front(), 1.0f);
    EXPECT_FLOAT_EQ(hist.bin_counts().back(), 1.0f);
    EXPECT_TRUE(all_finite(hist.fill_verts()));
}

TEST(BugfixCoreStats, HistogramNearFltMaxConstantDataHasFiniteEdges)
{
    const float        big  = std::numeric_limits<float>::max();
    std::vector<float> data = {big, big};
    HistogramSeries    hist(data, 5);

    EXPECT_TRUE(all_finite(hist.bin_edges()));
    EXPECT_LT(hist.bin_edges().front(), hist.bin_edges().back());
    EXPECT_FLOAT_EQ(total_count(hist.bin_counts()), 2.0f);
}

// ─── Bug #80: constant large-magnitude data collapsed bins ──────────────────

TEST(BugfixCoreStats, HistogramConstantLargeValuesHaveNonZeroWidth)
{
    std::vector<float> data(100, 1e8f);
    HistogramSeries    hist(data, 30);

    ASSERT_EQ(hist.bin_edges().size(), 31u);
    EXPECT_TRUE(all_finite(hist.bin_edges()));
    EXPECT_GT(hist.bin_edges().back(), hist.bin_edges().front());
    EXPECT_FLOAT_EQ(hist.bin_edges().front(), 1e8f);
    EXPECT_FLOAT_EQ(total_count(hist.bin_counts()), 100.0f);
    EXPECT_FLOAT_EQ(hist.bin_counts().front(), 100.0f);
    EXPECT_GT(hist.fill_vertex_count(), 0u);
}

TEST(BugfixCoreStats, HistogramConstantLargeValuesDensityIsFinite)
{
    std::vector<float> data(10, 1e8f);
    HistogramSeries    hist(data, 4);
    hist.density(true);
    hist.set_data(data, 4);

    EXPECT_TRUE(all_finite(hist.bin_counts()));
    EXPECT_GT(hist.bin_counts().front(), 0.0f);
}

TEST(BugfixCoreStats, HistogramConstantSmallValueKeepsUnitWidth)
{
    std::vector<float> data = {5.0f, 5.0f, 5.0f};
    HistogramSeries    hist(data, 1);

    ASSERT_EQ(hist.bin_edges().size(), 2u);
    EXPECT_FLOAT_EQ(hist.bin_edges()[0], 5.0f);
    EXPECT_FLOAT_EQ(hist.bin_edges()[1], 6.0f);
    EXPECT_FLOAT_EQ(hist.bin_counts()[0], 3.0f);
}

// ─── Bug #81: cumulative + density must end at 1 ────────────────────────────

TEST(BugfixCoreStats, HistogramCumulativeDensityEndsAtOne)
{
    std::vector<float> data;
    for (int i = 0; i <= 100; ++i)
        data.push_back(static_cast<float>(i) * 0.001f);   // range [0, 0.1]

    HistogramSeries hist(data, 10);
    hist.cumulative(true).density(true);
    hist.set_data(data, 10);

    ASSERT_EQ(hist.bin_counts().size(), 10u);
    EXPECT_NEAR(hist.bin_counts().back(), 1.0f, 1e-5f);
    EXPECT_TRUE(std::is_sorted(hist.bin_counts().begin(), hist.bin_counts().end()));
}

TEST(BugfixCoreStats, HistogramPlainDensityStillIntegratesToOne)
{
    std::vector<float> data;
    for (int i = 0; i <= 100; ++i)
        data.push_back(static_cast<float>(i) * 10.0f);   // range [0, 1000]

    HistogramSeries hist(data, 10);
    hist.density(true);
    hist.set_data(data, 10);

    float area = 0.0f;
    for (size_t i = 0; i < hist.bin_counts().size(); ++i)
        area += hist.bin_counts()[i] * (hist.bin_edges()[i + 1] - hist.bin_edges()[i]);
    EXPECT_NEAR(area, 1.0f, 1e-4f);
}

// ─── Bug #76: box plot with an infinite sample ──────────────────────────────

TEST(BugfixCoreStats, BoxPlotIgnoresInfiniteSamples)
{
    const float        inf  = std::numeric_limits<float>::infinity();
    std::vector<float> data = {1.0f, 2.0f, 3.0f, inf, -inf};
    BoxPlotStats       s    = BoxPlotSeries::compute_stats(data);

    EXPECT_FLOAT_EQ(s.median, 2.0f);
    EXPECT_FLOAT_EQ(s.q1, 1.5f);
    EXPECT_FLOAT_EQ(s.q3, 2.5f);
    EXPECT_TRUE(std::isfinite(s.whisker_low));
    EXPECT_TRUE(std::isfinite(s.whisker_high));
    EXPECT_TRUE(all_finite(s.outliers));

    BoxPlotSeries bp;
    bp.add_box(0.0f, data);
    EXPECT_TRUE(no_inf(bp.x_data()));
    EXPECT_TRUE(no_inf(bp.y_data()));
}

// ─── Bug #77: violin with an infinite sample ────────────────────────────────

TEST(BugfixCoreStats, ViolinIgnoresInfiniteSamples)
{
    const float        inf  = std::numeric_limits<float>::infinity();
    std::vector<float> data = {1.0f, 2.0f, 3.0f, inf};
    ViolinSeries       v;
    v.add_violin(1.0f, data);

    ASSERT_GT(v.fill_vertex_count(), 0u);
    EXPECT_TRUE(all_finite(v.fill_verts()));
    EXPECT_TRUE(no_inf(v.x_data()));
    EXPECT_TRUE(no_inf(v.y_data()));

    // The outline should actually have width (not all collapsed to x = 1).
    float max_x = 1.0f;
    for (float x : v.x_data())
        if (std::isfinite(x))
            max_x = std::max(max_x, x);
    EXPECT_GT(max_x, 1.1f);
}

// ─── Bug #78: constant violin data centered on its value ────────────────────

TEST(BugfixCoreStats, ViolinConstantDataIsCenteredOnValue)
{
    std::vector<float> data = {5.0f, 5.0f, 5.0f};
    ViolinSeries       v;
    v.show_box(false);
    v.add_violin(0.0f, data);

    float y_min = std::numeric_limits<float>::max();
    float y_max = std::numeric_limits<float>::lowest();
    for (float y : v.y_data())
    {
        if (!std::isfinite(y))
            continue;
        y_min = std::min(y_min, y);
        y_max = std::max(y_max, y);
    }
    EXPECT_LT(y_min, 5.0f);
    EXPECT_GT(y_max, 5.0f);
    EXPECT_NEAR((y_min + y_max) * 0.5f, 5.0f, 1e-4f);
}

TEST(BugfixCoreStats, ViolinConstantLargeValueDoesNotCollapse)
{
    std::vector<float> data = {1e8f, 1e8f};
    ViolinSeries       v;
    v.show_box(false);
    v.add_violin(0.0f, data);

    float y_min = std::numeric_limits<float>::max();
    float y_max = std::numeric_limits<float>::lowest();
    for (float y : v.y_data())
    {
        if (!std::isfinite(y))
            continue;
        y_min = std::min(y_min, y);
        y_max = std::max(y_max, y);
    }
    EXPECT_LT(y_min, 1e8f);
    EXPECT_GT(y_max, 1e8f);
}

// ─── Bug #79: violin gradient symmetric on both halves ──────────────────────

TEST(BugfixCoreStats, ViolinGradientIsSymmetric)
{
    std::vector<float> data = {1.0f, 2.0f, 2.5f, 3.0f, 4.0f};
    ViolinSeries       v;
    v.show_box(false).gradient(true);
    v.add_violin(0.0f, data);

    auto fv = v.fill_verts();
    ASSERT_EQ(fv.size() % 3, 0u);
    bool saw_left_edge  = false;
    bool saw_right_edge = false;
    for (size_t i = 0; i + 2 < fv.size(); i += 3)
    {
        float x = fv[i];
        float a = fv[i + 2];
        if (x < -1e-3f)
        {
            saw_left_edge = true;
            EXPECT_NEAR(a, 0.45f, 1e-5f);
        }
        else if (x > 1e-3f)
        {
            saw_right_edge = true;
            EXPECT_NEAR(a, 0.45f, 1e-5f);
        }
    }
    EXPECT_TRUE(saw_left_edge);
    EXPECT_TRUE(saw_right_edge);
}

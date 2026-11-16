// Series probing for cursor readouts: compare-at-X sampling (interpolated /
// nearest), hover hit tests (segments, bars, histogram bins, image cells,
// contour levels, chunked series) and date-time readout formatting.

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series_image.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "core/datetime_ticks.hpp"
#include "ui/overlay/axis_readout.hpp"
#include "ui/overlay/series_probe.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_HOVER 1
    #include <imgui.h>

    #include "ui/overlay/data_interaction.hpp"
#endif

using namespace spectra;
using namespace spectra::probe;

namespace
{
constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
}   // namespace

// ─── Sorted sampling ────────────────────────────────────────────────────────

TEST(SeriesProbeSorted, InterpolatesBetweenSamples)
{
    const std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
    const std::vector<float> y = {0.0f, 10.0f, 20.0f, 0.0f};

    const auto s = sample_sorted_at_x(x, y, 1.25, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_TRUE(s.interpolated);
    EXPECT_DOUBLE_EQ(s.x, 1.25);
    EXPECT_NEAR(s.y, 12.5, 1e-9);
    EXPECT_EQ(s.index, 1u);   // closer to x = 1

    const auto d = sample_sorted_at_x(x, y, 2.75, ProbeMode::Interpolate);
    ASSERT_TRUE(d.valid);
    EXPECT_NEAR(d.y, 5.0, 1e-9);
    EXPECT_EQ(d.index, 3u);
}

TEST(SeriesProbeSorted, ExactSampleIsNotInterpolated)
{
    const std::vector<float> x = {0.0f, 1.0f, 2.0f};
    const std::vector<float> y = {4.0f, 5.0f, 6.0f};
    const auto               s = sample_sorted_at_x(x, y, 1.0, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_FALSE(s.interpolated);
    EXPECT_DOUBLE_EQ(s.y, 5.0);

    // The last sample is reachable too.
    const auto last = sample_sorted_at_x(x, y, 2.0, ProbeMode::Interpolate);
    ASSERT_TRUE(last.valid);
    EXPECT_DOUBLE_EQ(last.y, 6.0);
    EXPECT_EQ(last.index, 2u);
}

TEST(SeriesProbeSorted, OutsideDataRangeHasNoValue)
{
    const std::vector<float> x = {0.0f, 1.0f};
    const std::vector<float> y = {0.0f, 1.0f};
    EXPECT_FALSE(sample_sorted_at_x(x, y, -0.1, ProbeMode::Interpolate).valid);
    EXPECT_FALSE(sample_sorted_at_x(x, y, 1.1, ProbeMode::Interpolate).valid);
    EXPECT_FALSE(sample_sorted_at_x(x, y, -0.1, ProbeMode::Nearest).valid);
    EXPECT_FALSE(sample_sorted_at_x({}, {}, 0.0, ProbeMode::Nearest).valid);
}

TEST(SeriesProbeSorted, NanGapIsNotInterpolatedAcross)
{
    const std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
    const std::vector<float> y = {1.0f, NaN, 3.0f, 4.0f};
    EXPECT_FALSE(sample_sorted_at_x(x, y, 0.5, ProbeMode::Interpolate).valid);
    // Nearest steps over the NaN sample to the finite neighbour.
    const auto n = sample_sorted_at_x(x, y, 0.9, ProbeMode::Nearest);
    ASSERT_TRUE(n.valid);
    EXPECT_EQ(n.index, 0u);
    EXPECT_DOUBLE_EQ(n.y, 1.0);
}

TEST(SeriesProbeSorted, ExactHitAtNanGapEdgeHasValue)
{
    const std::vector<float> x = {0.0f, 1.0f, 2.0f};
    const std::vector<float> y = {5.0f, NaN, 7.0f};
    const auto               a = sample_sorted_at_x(x, y, 0.0, ProbeMode::Interpolate);
    ASSERT_TRUE(a.valid);
    EXPECT_DOUBLE_EQ(a.y, 5.0);
    EXPECT_EQ(a.index, 0u);
    EXPECT_FALSE(a.interpolated);
    const auto b = sample_sorted_at_x(x, y, 2.0, ProbeMode::Interpolate);
    ASSERT_TRUE(b.valid);
    EXPECT_DOUBLE_EQ(b.y, 7.0);
    EXPECT_FALSE(sample_sorted_at_x(x, y, 0.5, ProbeMode::Interpolate).valid);
    EXPECT_FALSE(sample_sorted_at_x(x, y, 1.0, ProbeMode::Interpolate).valid);

    // Same for the unsorted scan.
    const auto u = sample_unsorted_at_x(x, y, 0.0, ProbeMode::Interpolate);
    ASSERT_TRUE(u.valid);
    EXPECT_DOUBLE_EQ(u.y, 5.0);
    EXPECT_FALSE(sample_unsorted_at_x(x, y, 0.5, ProbeMode::Interpolate).valid);
}

TEST(SeriesProbeSorted, InterpolatesInAxisSpaceWithOffsets)
{
    // Absolute y runs 1 → 100 (stored 0 → 99, y offset 1) on a log axis: the
    // drawn line passes y = 10 halfway along x.
    const std::vector<float> x = {0.0f, 1.0f};
    const std::vector<float> y = {0.0f, 99.0f};
    const ProbeScale         sc{ScaleType::Linear, ScaleType::Log10, 0.0, 1.0};
    const auto               s = sample_sorted_at_x(x, y, 0.5, ProbeMode::Interpolate, sc);
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.y, 9.0, 1e-9);   // local: absolute 10 minus the offset
    const auto u = sample_unsorted_at_x(x, y, 0.5, ProbeMode::Interpolate, sc);
    ASSERT_TRUE(u.valid);
    EXPECT_NEAR(u.y, 9.0, 1e-9);
}

TEST(SeriesProbeSorted, NearestPicksClosestSample)
{
    const std::vector<float> x = {0.0f, 1.0f, 2.0f};
    const std::vector<float> y = {5.0f, 7.0f, 9.0f};
    const auto               a = sample_sorted_at_x(x, y, 1.4, ProbeMode::Nearest);
    ASSERT_TRUE(a.valid);
    EXPECT_FALSE(a.interpolated);
    EXPECT_EQ(a.index, 1u);
    EXPECT_DOUBLE_EQ(a.x, 1.0);
    EXPECT_DOUBLE_EQ(a.y, 7.0);
    const auto b = sample_sorted_at_x(x, y, 1.6, ProbeMode::Nearest);
    EXPECT_EQ(b.index, 2u);
}

TEST(SeriesProbeSorted, MillionPointLookupIsExact)
{
    std::vector<float> x(1'000'000), y(1'000'000);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i) * 2.0f;
    }
    const auto s = sample_sorted_at_x(x, y, 654321.5, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.y, 1308643.0, 1e-3);
}

// ─── Unsorted sampling ──────────────────────────────────────────────────────

TEST(SeriesProbeUnsorted, InterpolatesFirstSpanningSegment)
{
    const std::vector<float> x = {3.0f, 1.0f, 2.0f};
    const std::vector<float> y = {30.0f, 10.0f, 20.0f};
    const auto               s = sample_unsorted_at_x(x, y, 2.0, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.y, 20.0, 1e-9);   // on segment 3 -> 1
    EXPECT_FALSE(sample_unsorted_at_x(x, y, 0.5, ProbeMode::Interpolate).valid);
}

TEST(SeriesProbeUnsorted, NearestStaysInsideDataRange)
{
    const std::vector<float> x = {3.0f, 1.0f, 2.0f};
    const std::vector<float> y = {30.0f, 10.0f, 20.0f};
    const auto               s = sample_unsorted_at_x(x, y, 1.2, ProbeMode::Nearest);
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.index, 1u);
    EXPECT_FALSE(sample_unsorted_at_x(x, y, 3.5, ProbeMode::Nearest).valid);
}

// ─── Series-level sampling ──────────────────────────────────────────────────

TEST(SeriesProbeSeries, LineAppliesOffsets)
{
    LineSeries         line;
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {0.0f, 1.0f};
    line.set_x(x).set_y(y);
    line.x_offset(1.7e9).y_offset(100.0);
    const auto s = sample_series_at_x(line, 1.7e9 + 5.0, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_DOUBLE_EQ(s.x, 1.7e9 + 5.0);
    EXPECT_NEAR(s.y, 100.5, 1e-9);
}

TEST(SeriesProbeSeries, ScatterAlwaysUsesNearestSample)
{
    ScatterSeries      sc;
    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 10.0f};
    sc.set_x(x).set_y(y);
    const auto s = sample_series_at_x(sc, 0.3, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_FALSE(s.interpolated);
    EXPECT_DOUBLE_EQ(s.y, 0.0);
}

TEST(SeriesProbeSeries, UnsortedScatterCloudHasNoValueAtX)
{
    ScatterSeries      sc;
    std::vector<float> x = {0.5f, 0.0f, 1.0f};
    std::vector<float> y = {3.0f, 0.0f, 10.0f};
    sc.set_x(x).set_y(y);
    ASSERT_FALSE(sc.x_is_sorted());
    EXPECT_FALSE(sample_series_at_x(sc, 0.4, ProbeMode::Nearest).valid);
}

TEST(SeriesProbeSeries, VerticalBarUnderX)
{
    std::vector<float> pos = {1.0f, 2.0f, 3.0f};
    std::vector<float> h   = {5.0f, 7.0f, 9.0f};
    BarSeries          bars(pos, h);
    const auto         s = sample_series_at_x(bars, 2.3, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.index, 1u);
    EXPECT_DOUBLE_EQ(s.x, 2.0);
    EXPECT_DOUBLE_EQ(s.y, 7.0);
    EXPECT_FALSE(sample_series_at_x(bars, 2.5, ProbeMode::Interpolate).valid);   // gap

    bars.orientation(BarOrientation::Horizontal);
    EXPECT_FALSE(sample_series_at_x(bars, 2.0, ProbeMode::Interpolate).valid);
}

TEST(SeriesProbeSeries, HistogramBinUnderX)
{
    std::vector<float> v = {0.0f, 0.5f, 1.5f, 2.5f, 2.6f, 2.7f, 4.0f};
    HistogramSeries    hist(v, 4);   // edges 0, 1, 2, 3, 4
    const auto         s = sample_series_at_x(hist, 2.9, ProbeMode::Nearest);
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.index, 2u);
    EXPECT_DOUBLE_EQ(s.y, 3.0);
    EXPECT_NEAR(s.x, 2.5, 1e-6);
}

TEST(SeriesProbeSeries, ChunkedLineUsesBinarySearchAcrossChunks)
{
    ChunkedLineSeries  cs;
    std::vector<float> x(5000), y(5000);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i) * 0.5f;
        y[i] = static_cast<float>(i);
    }
    cs.set_chunk_size(64);
    cs.set_data(x, y);
    ASSERT_GT(cs.storage_chunk_count(), 10u);

    const auto s = sample_series_at_x(cs, 1000.25, ProbeMode::Interpolate);
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.y, 2000.5, 1e-6);
    const auto n = sample_series_at_x(cs, 1000.2, ProbeMode::Nearest);
    ASSERT_TRUE(n.valid);
    EXPECT_EQ(n.index, 2000u);
    EXPECT_FALSE(sample_series_at_x(cs, 5000.0, ProbeMode::Nearest).valid);
}

// ─── Compare at X ───────────────────────────────────────────────────────────

TEST(CompareAtX, ListsVisibleSeriesInRequestedOrder)
{
    Figure             fig;
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f};
    std::vector<float> lo = {0.0f, 1.0f, 2.0f};
    std::vector<float> hi = {5.0f, 6.0f, 7.0f};
    std::vector<float> md = {2.0f, 3.0f, 4.0f};
    ax.line(x, lo).label("low");
    ax.line(x, hi).label("high");
    ax.line(x, md).label("mid");
    auto& hidden = ax.line(x, md);
    hidden.visible(false);
    ax.hline(100.0);

    auto rows = compare_at_x(ax, 0.5, ProbeMode::Interpolate, CompareSort::ValueDescending);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].series->label(), "high");
    EXPECT_EQ(rows[1].series->label(), "mid");
    EXPECT_EQ(rows[2].series->label(), "low");
    EXPECT_NEAR(rows[0].sample.y, 5.5, 1e-9);

    rows = compare_at_x(ax, 0.5, ProbeMode::Interpolate, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].series_index, 0);
    EXPECT_EQ(rows[1].series_index, 1);
    EXPECT_EQ(rows[2].series_index, 2);

    rows = compare_at_x(ax, 0.4, ProbeMode::Nearest, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_DOUBLE_EQ(rows[0].sample.y, 0.0);
    EXPECT_DOUBLE_EQ(rows[0].sample.x, 0.0);
}

TEST(CompareAtX, InterpolatesInLogAxisSpace)
{
    Figure             fig;
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {1.0f, 100.0f};
    std::vector<float> y  = {1.0f, 10000.0f};
    ax.line(x, y);

    // Linear axes: the straight line in data space.
    auto rows = compare_at_x(ax, 10.0, ProbeMode::Interpolate, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NEAR(rows[0].sample.y, 910.0, 1e-6);

    // Log-log: the drawn line passes through (10, 100).
    ax.xscale(ScaleType::Log10);
    ax.yscale(ScaleType::Log10);
    rows = compare_at_x(ax, 10.0, ProbeMode::Interpolate, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_DOUBLE_EQ(rows[0].sample.x, 10.0);
    EXPECT_NEAR(rows[0].sample.y, 100.0, 1e-6);
    EXPECT_TRUE(rows[0].sample.interpolated);

    // Log2 behaves the same way (base cancels out).
    ax.xscale(ScaleType::Log2);
    ax.yscale(ScaleType::Log2);
    rows = compare_at_x(ax, 10.0, ProbeMode::Interpolate, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NEAR(rows[0].sample.y, 100.0, 1e-6);
}

TEST(CompareAtX, InterpolatesInSqrtAxisSpace)
{
    Figure             fig;
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x  = {0.0f, 4.0f};
    std::vector<float> y  = {0.0f, 16.0f};
    ax.line(x, y);

    // Sqrt Y: halfway in x is halfway in sqrt(y) (2), i.e. y = 4 (not 8).
    ax.yscale(ScaleType::Sqrt);
    auto rows = compare_at_x(ax, 2.0, ProbeMode::Interpolate, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NEAR(rows[0].sample.y, 4.0, 1e-9);

    // Sqrt X as well: x = 1 is halfway in sqrt(x), so y = 4 again.
    ax.xscale(ScaleType::Sqrt);
    rows = compare_at_x(ax, 1.0, ProbeMode::Interpolate, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NEAR(rows[0].sample.y, 4.0, 1e-9);

    // Nearest mode still reports stored samples.
    rows = compare_at_x(ax, 1.0, ProbeMode::Nearest, CompareSort::Legend);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_DOUBLE_EQ(rows[0].sample.y, 0.0);
}

TEST(CompareAtX, NonFiniteValuesSortLast)
{
    std::vector<CompareRow> rows(3);
    rows[0].series_index = 0;
    rows[0].sample.y     = std::numeric_limits<double>::quiet_NaN();
    rows[1].series_index = 1;
    rows[1].sample.y     = 1.0;
    rows[2].series_index = 2;
    rows[2].sample.y     = 3.0;
    sort_compare_rows(rows, CompareSort::ValueDescending);
    EXPECT_EQ(rows[0].series_index, 2);
    EXPECT_EQ(rows[1].series_index, 1);
    EXPECT_EQ(rows[2].series_index, 0);
}

// ─── Geometric hit tests ────────────────────────────────────────────────────

TEST(SeriesProbeHit, ClosestOnSegment)
{
    auto c = closest_on_segment(0.0, 0.0, 10.0, 0.0, 4.0, 3.0);
    EXPECT_DOUBLE_EQ(c.t, 0.4);
    EXPECT_DOUBLE_EQ(c.dist2, 9.0);
    c = closest_on_segment(0.0, 0.0, 10.0, 0.0, -5.0, 0.0);   // clamped to a
    EXPECT_DOUBLE_EQ(c.t, 0.0);
    EXPECT_DOUBLE_EQ(c.dist2, 25.0);
    c = closest_on_segment(1.0, 1.0, 1.0, 1.0, 1.0, 2.0);   // degenerate
    EXPECT_DOUBLE_EQ(c.dist2, 1.0);
}

TEST(SeriesProbeHit, SortedWindowKeepsCrossingSegments)
{
    std::vector<float> x(1000);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(i) * 10.0f;
    // Window [503, 507] has no sample but lies on segment 50 -> 51.
    const auto r = sorted_window(x, 503.0, 507.0);
    EXPECT_LE(r.begin, 50u);
    EXPECT_GE(r.end, 52u);
    EXPECT_LE(r.end - r.begin, 3u);
}

TEST(SeriesProbeHit, BarRectangles)
{
    std::vector<float> pos = {0.0f, 1.0f};
    std::vector<float> h   = {4.0f, -2.0f};
    BarSeries          bars(pos, h);
    EXPECT_EQ(bar_at(bars, 0.1, 3.9), std::optional<size_t>(0));
    EXPECT_FALSE(bar_at(bars, 0.1, 4.1).has_value());
    EXPECT_EQ(bar_at(bars, 1.2, -1.0), std::optional<size_t>(1));   // negative bar
    EXPECT_FALSE(bar_at(bars, 0.5, 1.0).has_value());               // between bars

    bars.orientation(BarOrientation::Horizontal);
    EXPECT_EQ(bar_at(bars, 3.0, 0.2), std::optional<size_t>(0));
    EXPECT_FALSE(bar_at(bars, 0.2, 3.0).has_value());
}

TEST(SeriesProbeHit, HistogramBins)
{
    std::vector<float> v = {0.0f, 0.5f, 1.5f, 3.9f, 4.0f};
    HistogramSeries    hist(v, 4);   // counts 2, 1, 0, 2
    EXPECT_EQ(histogram_bin_at(hist, 0.5, 1.5), std::optional<size_t>(0));
    EXPECT_FALSE(histogram_bin_at(hist, 0.5, 2.5).has_value());              // above the bar
    EXPECT_FALSE(histogram_bin_at(hist, -0.1, 0.5).has_value());             // left of the edges
    EXPECT_EQ(histogram_bin_at(hist, 4.0, 1.0), std::optional<size_t>(3));   // right edge
    EXPECT_EQ(histogram_bin_at_x(hist, 2.5), std::optional<size_t>(2));
}

TEST(SeriesProbeHit, ContourLevelOfPoint)
{
    // z = column index: iso-lines are vertical at x = level.
    const size_t       cols = 11, rows = 11;
    std::vector<float> z(cols * rows);
    for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < cols; ++c)
            z[r * cols + c] = static_cast<float>(c);
    ContourSeries ct(z, cols, rows);
    ct.levels(std::vector<float>{2.5f, 5.5f});
    ASSERT_EQ(ct.level_ranges().size(), 2u);
    const auto& second = ct.level_ranges()[1];
    ASSERT_GT(second.point_count, 0u);
    const auto level = contour_level_for_point(ct, second.first_point);
    ASSERT_TRUE(level.has_value());
    EXPECT_FLOAT_EQ(*level, 5.5f);
    EXPECT_FALSE(contour_level_for_point(ct, ct.point_count() + 5).has_value());
}

// ─── Date-time readouts ─────────────────────────────────────────────────────

TEST(DatetimeReadout, FormatsAtResolution)
{
    const double t = 1709649012.0;   // 2024-03-05 14:30:12 UTC
    EXPECT_EQ(format_datetime_readout(t), "2024-03-05 14:30:12");
    EXPECT_EQ(format_datetime_readout(t + 0.25), "2024-03-05 14:30:12.250");
    EXPECT_EQ(format_datetime_readout(t, 2.0), "2024-03-05 14:30:12");
    EXPECT_EQ(format_datetime_readout(t, 120.0), "2024-03-05 14:30");
    EXPECT_EQ(format_datetime_readout(t, 3.0 * 86400.0), "2024-03-05");
    EXPECT_EQ(format_datetime_readout(t + 0.123456, 0.001), "2024-03-05 14:30:12.123");
    EXPECT_EQ(format_datetime_readout(t + 5.3, 0.1), "2024-03-05 14:30:17.3");
    // Rounding carries into the minute instead of printing ":60".
    EXPECT_EQ(format_datetime_readout(t + 47.9999, 0.01), "2024-03-05 14:31:00.00");
    EXPECT_EQ(format_datetime_readout(std::numeric_limits<double>::quiet_NaN()), "nan");
}

TEST(DatetimeReadout, AxisFormattersFollowDatetimeMode)
{
    Figure fig({.width = 1000, .height = 500});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(1709649012.0, 1709649012.0 + 1000.0);   // 1 s per pixel
    ax.ylim(0.0, 1.0);
    ax.set_viewport(Rect{0.0f, 0.0f, 1000.0f, 500.0f});

    // Numeric axis: raw epoch number.
    EXPECT_EQ(ui::format_axis_value(&ax, ui::ReadoutAxis::X, 1709649012.0), "1709649012.000");
    ax.xaxis_datetime(true);
    EXPECT_TRUE(ui::axis_is_datetime(&ax, ui::ReadoutAxis::X));
    EXPECT_FALSE(ui::axis_is_datetime(&ax, ui::ReadoutAxis::Y));
    EXPECT_EQ(ui::format_axis_value(&ax, ui::ReadoutAxis::X, 1709649012.0), "2024-03-05 14:30:12");
    EXPECT_EQ(ui::format_axis_value(&ax, ui::ReadoutAxis::Y, 0.25), "0.25");

    // Status bar: date-time X, pixel-resolution Y.
    const std::string status =
        ui::format_cursor_status(&ax, true, 1709649012.0 + 500.0, 0.5, 500.0, 250.0);
    EXPECT_EQ(status, "X: 2024-03-05 14:38:32  Y: 0.500");
    EXPECT_EQ(ui::format_cursor_status(nullptr, false, 0.0, 0.0, 0.0, 0.0),
              "X: \xE2\x80\x94  Y: \xE2\x80\x94");

    // A log scale disables date-time labelling.
    ax.xscale(ScaleType::Log10);
    EXPECT_FALSE(ui::axis_is_datetime(&ax, ui::ReadoutAxis::X));
}

TEST(DatetimeReadout, StatusBarDigitsFollowPixelResolution)
{
    Figure fig({.width = 1000, .height = 500});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);     // 0.01 per pixel
    ax.ylim(0.0, 1000.0);   // 2 per pixel
    ax.set_viewport(Rect{0.0f, 0.0f, 1000.0f, 500.0f});
    EXPECT_EQ(ui::format_cursor_status(&ax, true, 5.743412, 260.123, 574.0, 370.0),
              "X: 5.74  Y: 260");
}

// ─── Hover hit tests through DataInteraction ────────────────────────────────

#ifdef SPECTRA_TEST_HOVER
namespace
{

// Hover (sx, sy) over a 1000x500 viewport at the origin; return the pick.
spectra::NearestPointResult hover_pick(Figure& fig, double sx, double sy)
{
    DataInteraction di;
    ImGui::CreateContext();
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    CursorReadout cur;
    cur.valid    = true;
    cur.screen_x = sx;
    cur.screen_y = sy;
    di.update(cur, fig);
    ImGui::DestroyContext();
    return di.nearest_point();
}

Axes& make_axes(Figure& fig, double x0, double x1, double y0, double y1)
{
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(x0, x1);
    ax.ylim(y0, y1);
    ax.set_viewport(Rect{0.0f, 0.0f, 1000.0f, 500.0f});
    return ax;
}

std::string row_value(const std::vector<TooltipRow>& rows, const std::string& label)
{
    for (const auto& r : rows)
        if (r.label == label)
            return r.value;
    return "<missing " + label + ">";
}

}   // namespace

TEST(HoverHit, LineSegmentBetweenSamplesIsInterpolated)
{
    // Samples 100 px apart; the cursor sits on the line half way between two.
    Figure             fig({.width = 1000, .height = 500});
    auto&              ax = make_axes(fig, 0.0, 10.0, 0.0, 10.0);
    std::vector<float> x  = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> y  = {0.0f, 2.0f, 2.0f, 6.0f};
    ax.line(x, y);

    // x = 2.5 -> sx 250; y = 4 -> sy 300.
    const auto np = hover_pick(fig, 250.0, 300.0);
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.kind, HoverHitKind::Segment);
    EXPECT_LT(np.distance_px, 1.0f);
    EXPECT_NEAR(np.data_x, 2.5, 1e-3);
    EXPECT_NEAR(np.data_y, 4.0, 1e-3);
    EXPECT_TRUE(np.dy_dx_valid);
    EXPECT_NEAR(np.dy_dx, 4.0f, 1e-4f);

    const auto rows = tooltip_rows(np);
    EXPECT_EQ(row_value(rows, "X"), "2.5");
    EXPECT_EQ(row_value(rows, "Y (interp.)"), "4");

    // Next to a sample the exact sample still wins.
    const auto v = hover_pick(fig, 202.0, 401.0);
    ASSERT_TRUE(v.found);
    EXPECT_EQ(v.kind, HoverHitKind::Vertex);
    EXPECT_EQ(v.point_index, 2u);
}

TEST(HoverHit, LargeSortedLineUsesWindowedSearch)
{
    Figure             fig({.width = 1000, .height = 500});
    auto&              ax = make_axes(fig, 0.0, 1000.0, -1.0, 1.0);
    std::vector<float> x(200'000), y(200'000);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i) * 0.005f;
        y[i] = 0.0f;
    }
    ax.line(x, y);
    DataInteraction di;
    ImGui::CreateContext();
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    CursorReadout cur;
    cur.valid    = true;
    cur.screen_x = 500.0;
    cur.screen_y = 252.0;
    di.update(cur, fig);
    ImGui::DestroyContext();
    ASSERT_TRUE(di.nearest_point().found);
    EXPECT_LT(di.last_query_points_examined(), 5000u);
}

TEST(HoverHit, BarReportsHeight)
{
    Figure             fig({.width = 1000, .height = 500});
    auto&              ax  = make_axes(fig, 0.0, 4.0, 0.0, 10.0);
    std::vector<float> pos = {1.0f, 2.0f, 3.0f};
    std::vector<float> h   = {3.0f, 8.0f, 5.0f};
    ax.bar(pos, h);

    // Inside bar 2 (x = 2.1, y = 2): far from its outline vertices.
    const auto np = hover_pick(fig, 525.0, 400.0);
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.kind, HoverHitKind::Bar);
    EXPECT_EQ(np.point_index, 1u);
    EXPECT_DOUBLE_EQ(np.value, 8.0);
    EXPECT_LE(np.distance_px, 0.0f);
    EXPECT_EQ(row_value(tooltip_rows(np), "Value"), "8");
}

TEST(HoverHit, HistogramReportsBinRangeAndCount)
{
    Figure             fig({.width = 1000, .height = 500});
    auto&              ax = make_axes(fig, 0.0, 4.0, 0.0, 4.0);
    std::vector<float> v  = {0.0f, 1.2f, 1.4f, 1.6f, 4.0f};
    ax.histogram(v, 4);   // bins [0,1) [1,2) [2,3) [3,4]: 1, 3, 0, 1

    const auto np = hover_pick(fig, 375.0, 400.0);   // x = 1.5, y = 0.8
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.kind, HoverHitKind::Bin);
    EXPECT_EQ(np.point_index, 1u);
    EXPECT_DOUBLE_EQ(np.value, 3.0);
    EXPECT_NEAR(np.range_lo, 1.0, 1e-6);
    EXPECT_NEAR(np.range_hi, 2.0, 1e-6);
    const auto rows = tooltip_rows(np);
    EXPECT_EQ(row_value(rows, "Bin"), "[1, 2)");
    EXPECT_EQ(row_value(rows, "Count"), "3");
}

TEST(HoverHit, ImageReportsCellValue)
{
    Figure fig({.width = 1000, .height = 500});
    // 4 x 2 grid, default extent [-0.5, 3.5] x [-0.5, 1.5].
    auto&              ax = make_axes(fig, -0.5, 3.5, -0.5, 1.5);
    std::vector<float> v  = {0.0f, 1.0f, 2.0f, 3.0f, 10.0f, 11.0f, 12.0f, 13.0f};
    auto&              im = ax.imshow(v, 4, 2);

    // Data (2.1, 0.2): column 2.
    const auto np = hover_pick(fig, 650.0, 325.0);
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.kind, HoverHitKind::Pixel);
    EXPECT_EQ(np.cell_col, 2u);
    const auto expected = im.value_at(2.1, 0.2);
    ASSERT_TRUE(expected.has_value());
    ASSERT_TRUE(np.has_value);
    EXPECT_DOUBLE_EQ(np.value, *expected);
    EXPECT_EQ(row_value(tooltip_rows(np), "Value"), ui::format_readout_value(*expected, 6));
}

TEST(HoverHit, ContourReportsLevel)
{
    Figure             fig({.width = 1000, .height = 500});
    auto&              ax   = make_axes(fig, 0.0, 10.0, 0.0, 10.0);
    const size_t       cols = 11, rows = 11;
    std::vector<float> z(cols * rows);
    for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < cols; ++c)
            z[r * cols + c] = static_cast<float>(c);
    auto& ct = ax.contour(z, cols, rows);
    ct.levels(std::vector<float>{2.5f, 5.5f});

    // Iso-line x = 5.5 -> sx 550; cursor 3 px right of it, between nodes.
    const auto np = hover_pick(fig, 553.0, 230.0);
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.kind, HoverHitKind::Contour);
    EXPECT_LT(np.distance_px, 4.0f);
    EXPECT_DOUBLE_EQ(np.value, 5.5);
    EXPECT_EQ(row_value(tooltip_rows(np), "Level"), "5.5");
}

TEST(HoverHit, ChunkedLineSegment)
{
    Figure             fig({.width = 1000, .height = 500});
    auto&              ax = make_axes(fig, 0.0, 100.0, 0.0, 100.0);
    auto&              cs = ax.chunked_line();
    std::vector<float> x(101), y(101);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i);
    }
    cs.set_chunk_size(16);
    cs.set_data(x, y);

    // Between samples 40 and 41 on y = x.
    const auto np = hover_pick(fig, 405.0, 500.0 - 202.5);
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.series, &cs);
    EXPECT_LT(np.distance_px, 1.0f);
    EXPECT_NEAR(np.data_x, 40.5, 0.05);
}

TEST(HoverHit, DatetimeTooltipRows)
{
    Figure fig({.width = 1000, .height = 500});
    auto&  ax = make_axes(fig, 1709649012.0, 1709649012.0 + 1000.0, 0.0, 10.0);
    ax.xaxis_datetime(true);
    std::vector<float> x = {0.0f, 100.0f};
    std::vector<float> y = {5.0f, 5.0f};
    ax.line(x, y).x_offset(1709649012.0);

    const auto np = hover_pick(fig, 0.5, 250.0);
    ASSERT_TRUE(np.found);
    EXPECT_EQ(np.axes, &ax);
    EXPECT_EQ(row_value(tooltip_rows(np), "X"), "2024-03-05 14:30:12");
}
#endif

#include <gtest/gtest.h>
#include <stdexcept>
#include <spectra/axes.hpp>
#include <spectra/series_stats.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <vector>

using namespace spectra;

TEST(BandSeries, BuildsTwoTrianglesPerFiniteInterval)
{
    std::vector<float> x     = {0.0f, 1.0f, 2.0f};
    std::vector<float> lower = {-1.0f, 0.0f, 1.0f};
    std::vector<float> upper = {1.0f, 2.0f, 3.0f};
    BandSeries         band(x, lower, upper);

    EXPECT_EQ(band.sample_count(), 3u);
    EXPECT_EQ(band.fill_vertex_count(), 12u);
    EXPECT_EQ(band.point_count(), 7u);
    EXPECT_TRUE(std::isnan(band.x_data()[3]));
}

TEST(BandSeries, RejectsMisalignedInputs)
{
    std::vector<float> x     = {0.0f, 1.0f};
    std::vector<float> lower = {0.0f};
    std::vector<float> upper = {1.0f, 2.0f};
    EXPECT_THROW(BandSeries(x, lower, upper), std::invalid_argument);
}

TEST(BandSeries, ParticipatesInAutoscale)
{
    Axes               axes;
    std::vector<float> x     = {10.0f, 20.0f};
    std::vector<float> lower = {-4.0f, -3.0f};
    std::vector<float> upper = {7.0f, 9.0f};
    axes.band(x, lower, upper);
    axes.auto_fit();

    EXPECT_LE(axes.x_limits().min, 10.0);
    EXPECT_GE(axes.x_limits().max, 20.0);
    EXPECT_LE(axes.y_limits().min, -4.0);
    EXPECT_GE(axes.y_limits().max, 9.0);
}

// ═══════════════════════════════════════════════════════════════════════════
// BoxPlotSeries
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// Series x_offset — double-precision absolute x (epoch timestamps)
// ═══════════════════════════════════════════════════════════════════════════

TEST(SeriesXOffset, AxisLimitsIncludeOffset)
{
    // Epoch-scale timestamps stored as small relative floats + double offset.
    // Axis limits must reflect the absolute values at double precision.
    constexpr double   epoch = 1783350621.752999;
    std::vector<float> x     = {0.0f, 0.02f, 0.04f};
    std::vector<float> y     = {1.0f, 2.0f, 3.0f};

    Axes  ax;
    auto& line = ax.line(x, y);
    line.x_offset(epoch);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    // Limits bracket the absolute range [epoch, epoch + 0.04] with padding.
    EXPECT_LT(xlim.min, epoch);
    EXPECT_GT(xlim.max, epoch + 0.04);
    // Sub-second resolution preserved: the whole span is well under a second.
    EXPECT_LT(xlim.max - xlim.min, 1.0);
    EXPECT_NEAR(xlim.min, epoch, 0.1);
}

TEST(SeriesXOffset, DefaultOffsetIsZero)
{
    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {1.0f, 2.0f, 3.0f};

    Axes  ax;
    auto& line = ax.line(x, y);
    EXPECT_EQ(line.x_offset(), 0.0);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    EXPECT_LE(xlim.min, 1.0);
    EXPECT_GE(xlim.max, 3.0);
}

TEST(BoxPlotStats, ComputeFromData)
{
    std::vector<float> data  = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto               stats = BoxPlotSeries::compute_stats(data);

    EXPECT_FLOAT_EQ(stats.median, 5.5f);
    EXPECT_FLOAT_EQ(stats.q1, 3.25f);
    EXPECT_FLOAT_EQ(stats.q3, 7.75f);
    EXPECT_GE(stats.whisker_low, 1.0f);
    EXPECT_LE(stats.whisker_high, 10.0f);
    EXPECT_TRUE(stats.outliers.empty());
}

TEST(BoxPlotStats, OutlierDetection)
{
    std::vector<float> data  = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 100};
    auto               stats = BoxPlotSeries::compute_stats(data);

    EXPECT_FALSE(stats.outliers.empty());
    // 100 should be an outlier
    bool found_100 = false;
    for (float o : stats.outliers)
    {
        if (o == 100.0f)
            found_100 = true;
    }
    EXPECT_TRUE(found_100);
}

TEST(BoxPlotStats, EmptyData)
{
    std::vector<float> data;
    auto               stats = BoxPlotSeries::compute_stats(data);
    EXPECT_FLOAT_EQ(stats.median, 0.0f);
}

TEST(BoxPlotStats, SingleValue)
{
    std::vector<float> data  = {42.0f};
    auto               stats = BoxPlotSeries::compute_stats(data);
    EXPECT_FLOAT_EQ(stats.median, 42.0f);
    EXPECT_FLOAT_EQ(stats.q1, 42.0f);
    EXPECT_FLOAT_EQ(stats.q3, 42.0f);
}

TEST(BoxPlotStats, NaNFiltering)
{
    float              nan   = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> data  = {1, nan, 3, nan, 5};
    auto               stats = BoxPlotSeries::compute_stats(data);
    EXPECT_FLOAT_EQ(stats.median, 3.0f);
}

TEST(BoxPlotSeries, AddBoxFromData)
{
    BoxPlotSeries      bp;
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    bp.add_box(1.0f, data);

    EXPECT_EQ(bp.positions().size(), 1u);
    EXPECT_EQ(bp.stats().size(), 1u);
    EXPECT_GT(bp.point_count(), 0u);
}

TEST(BoxPlotSeries, AddBoxFromStats)
{
    BoxPlotSeries bp;
    bp.add_box(1.0f, 5.0f, 3.0f, 7.0f, 1.0f, 9.0f);

    EXPECT_EQ(bp.positions().size(), 1u);
    EXPECT_FLOAT_EQ(bp.stats()[0].median, 5.0f);
    EXPECT_GT(bp.point_count(), 0u);
}

TEST(BoxPlotSeries, MultipleBoxes)
{
    BoxPlotSeries      bp;
    std::vector<float> d1 = {1, 2, 3, 4, 5};
    std::vector<float> d2 = {10, 20, 30, 40, 50};
    bp.add_box(1.0f, d1);
    bp.add_box(2.0f, d2);

    EXPECT_EQ(bp.positions().size(), 2u);
    EXPECT_GT(bp.point_count(), 0u);
}

TEST(BoxPlotSeries, GeometryContainsNaNBreaks)
{
    BoxPlotSeries      bp;
    std::vector<float> data = {1, 2, 3, 4, 5};
    bp.add_box(1.0f, data);

    bool has_nan = false;
    for (size_t i = 0; i < bp.point_count(); ++i)
    {
        if (std::isnan(bp.x_data()[i]))
        {
            has_nan = true;
            break;
        }
    }
    EXPECT_TRUE(has_nan);
}

TEST(BoxPlotSeries, FluentAPI)
{
    BoxPlotSeries bp;
    bp.label("test").color(colors::red).box_width(0.4f).show_outliers(false);

    EXPECT_EQ(bp.label(), "test");
    EXPECT_FLOAT_EQ(bp.box_width(), 0.4f);
    EXPECT_FALSE(bp.show_outliers());
}

// ═══════════════════════════════════════════════════════════════════════════
// ViolinSeries
// ═══════════════════════════════════════════════════════════════════════════

TEST(ViolinSeries, AddViolin)
{
    ViolinSeries       vn;
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    vn.add_violin(1.0f, data);

    EXPECT_GT(vn.point_count(), 0u);
}

TEST(ViolinSeries, MultipleViolins)
{
    ViolinSeries       vn;
    std::vector<float> d1 = {1, 2, 3, 4, 5};
    std::vector<float> d2 = {10, 20, 30, 40, 50};
    vn.add_violin(1.0f, d1);
    vn.add_violin(2.0f, d2);

    EXPECT_GT(vn.point_count(), 0u);
}

TEST(ViolinSeries, EmptyData)
{
    ViolinSeries       vn;
    std::vector<float> data;
    vn.add_violin(1.0f, data);

    EXPECT_EQ(vn.point_count(), 0u);
}

TEST(ViolinSeries, Resolution)
{
    ViolinSeries vn;
    vn.resolution(20);
    EXPECT_EQ(vn.resolution(), 20);

    std::vector<float> data = {1, 2, 3, 4, 5};
    vn.add_violin(1.0f, data);

    // With resolution=20, each violin has 20 right + 20 left + 1 close + 1 NaN = 42 points
    // Plus inner box if show_box is true
    EXPECT_GT(vn.point_count(), 40u);
}

TEST(ViolinSeries, ResolutionIsClampedToSafeMinimum)
{
    ViolinSeries       vn;
    std::vector<float> data = {1, 2, 3, 4, 5};
    vn.add_violin(1.0f, data);

    vn.resolution(0);
    vn.rebuild_geometry();

    EXPECT_EQ(vn.resolution(), 2);
    EXPECT_GT(vn.point_count(), 0u);
}

TEST(ViolinSeries, FluentAPI)
{
    ViolinSeries vn;
    vn.label("violin").color(colors::green).violin_width(0.5f).show_box(false);

    EXPECT_EQ(vn.label(), "violin");
    EXPECT_FLOAT_EQ(vn.violin_width(), 0.5f);
    EXPECT_FALSE(vn.show_box());
}

// ═══════════════════════════════════════════════════════════════════════════
// HistogramSeries
// ═══════════════════════════════════════════════════════════════════════════

TEST(HistogramSeries, BasicConstruction)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    HistogramSeries    hist(data, 5);

    EXPECT_GT(hist.point_count(), 0u);
    EXPECT_EQ(hist.bins(), 5);
    EXPECT_EQ(hist.bin_edges().size(), 6u);   // bins + 1
    EXPECT_EQ(hist.bin_counts().size(), 5u);
}

TEST(HistogramSeries, BinCountsSum)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    HistogramSeries    hist(data, 5);

    float total = 0.0f;
    for (float c : hist.bin_counts())
        total += c;
    EXPECT_FLOAT_EQ(total, 10.0f);
}

TEST(HistogramSeries, EmptyData)
{
    std::vector<float> data;
    HistogramSeries    hist(data, 10);

    EXPECT_EQ(hist.point_count(), 0u);
    EXPECT_TRUE(hist.bin_edges().empty());
}

TEST(HistogramSeries, SingleValue)
{
    std::vector<float> data = {5.0f, 5.0f, 5.0f};
    HistogramSeries    hist(data, 10);

    EXPECT_GT(hist.point_count(), 0u);
}

TEST(HistogramSeries, Cumulative)
{
    std::vector<float> data = {1, 2, 3, 4, 5};
    HistogramSeries    hist;
    hist.cumulative(true);
    hist.set_data(data, 5);

    // Last bin count should equal total
    EXPECT_FLOAT_EQ(hist.bin_counts().back(), 5.0f);
}

TEST(HistogramSeries, Density)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    HistogramSeries    hist;
    hist.density(true);
    hist.set_data(data, 5);

    // Area under density histogram should be approximately 1
    float area      = 0.0f;
    float bin_width = (hist.bin_edges().back() - hist.bin_edges().front()) / 5.0f;
    for (float c : hist.bin_counts())
        area += c * bin_width;
    EXPECT_NEAR(area, 1.0f, 0.01f);
}

TEST(HistogramSeries, FluentAPI)
{
    HistogramSeries hist;
    hist.label("hist").color(colors::orange).bins(20);

    EXPECT_EQ(hist.label(), "hist");
    EXPECT_EQ(hist.bins(), 20);
}

TEST(HistogramSeries, GeometryIsStepFunction)
{
    std::vector<float> data = {1, 2, 3, 4, 5};
    HistogramSeries    hist(data, 3);

    // Step function starts and ends at y=0
    EXPECT_FLOAT_EQ(hist.y_data()[0], 0.0f);
    EXPECT_FLOAT_EQ(hist.y_data()[hist.point_count() - 1], 0.0f);
}

TEST(HistogramSeries, IgnoresNonFiniteSamples)
{
    const float        inf  = std::numeric_limits<float>::infinity();
    const float        nan  = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> data = {1.0f, inf, -inf, nan, 3.0f};
    HistogramSeries    hist(data, 2);

    ASSERT_EQ(hist.raw_values().size(), 2u);
    EXPECT_FLOAT_EQ(hist.raw_values()[0], 1.0f);
    EXPECT_FLOAT_EQ(hist.raw_values()[1], 3.0f);
    for (float edge : hist.bin_edges())
        EXPECT_TRUE(std::isfinite(edge));

    float total = 0.0f;
    for (float count : hist.bin_counts())
    {
        EXPECT_TRUE(std::isfinite(count));
        total += count;
    }
    EXPECT_FLOAT_EQ(total, 2.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// BarSeries
// ═══════════════════════════════════════════════════════════════════════════

TEST(BarSeries, BasicConstruction)
{
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    BarSeries          bars(pos, heights);

    EXPECT_GT(bars.point_count(), 0u);
    EXPECT_EQ(bars.bar_positions().size(), 3u);
    EXPECT_EQ(bars.bar_heights().size(), 3u);
}

TEST(BarSeries, EmptyData)
{
    std::vector<float> pos;
    std::vector<float> heights;
    BarSeries          bars(pos, heights);

    EXPECT_EQ(bars.point_count(), 0u);
}

TEST(BarSeries, BarWidth)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {10};
    BarSeries          bars(pos, heights);
    bars.bar_width(0.5f);

    EXPECT_FLOAT_EQ(bars.bar_width(), 0.5f);
}

TEST(BarSeries, Baseline)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {10};
    BarSeries          bars;
    bars.baseline(5.0f);
    bars.set_data(pos, heights);

    // Check that geometry includes baseline value
    bool has_baseline = false;
    for (size_t i = 0; i < bars.point_count(); ++i)
    {
        if (!std::isnan(bars.y_data()[i]) && bars.y_data()[i] == 5.0f)
        {
            has_baseline = true;
            break;
        }
    }
    EXPECT_TRUE(has_baseline);
}

TEST(BarSeries, HorizontalOrientation)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {10};
    BarSeries          bars;
    bars.orientation(BarOrientation::Horizontal);
    bars.set_data(pos, heights);

    EXPECT_GT(bars.point_count(), 0u);
}

TEST(BarSeries, GeometryContainsNaNBreaks)
{
    std::vector<float> pos     = {1, 2};
    std::vector<float> heights = {10, 20};
    BarSeries          bars(pos, heights);

    bool has_nan = false;
    for (size_t i = 0; i < bars.point_count(); ++i)
    {
        if (std::isnan(bars.x_data()[i]))
        {
            has_nan = true;
            break;
        }
    }
    EXPECT_TRUE(has_nan);
}

TEST(BarSeries, FluentAPI)
{
    BarSeries bars;
    bars.label("bars").color(colors::blue).bar_width(0.4f).baseline(1.0f);

    EXPECT_EQ(bars.label(), "bars");
    EXPECT_FLOAT_EQ(bars.bar_width(), 0.4f);
    EXPECT_FLOAT_EQ(bars.baseline(), 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Axes integration
// ═══════════════════════════════════════════════════════════════════════════

TEST(AxesStats, CreateBoxPlot)
{
    Axes               ax;
    auto&              bp   = ax.box_plot();
    std::vector<float> data = {1, 2, 3, 4, 5};
    bp.add_box(1.0f, data);

    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(AxesStats, CreateViolin)
{
    Axes               ax;
    auto&              vn   = ax.violin();
    std::vector<float> data = {1, 2, 3, 4, 5};
    vn.add_violin(1.0f, data);

    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(AxesStats, CreateHistogram)
{
    Axes               ax;
    std::vector<float> data = {1, 2, 3, 4, 5};
    auto&              hist = ax.histogram(data, 5);

    EXPECT_EQ(ax.series().size(), 1u);
    EXPECT_GT(hist.point_count(), 0u);
}

TEST(AxesStats, CreateBar)
{
    Axes               ax;
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    auto&              bars    = ax.bar(pos, heights);

    EXPECT_EQ(ax.series().size(), 1u);
    EXPECT_GT(bars.point_count(), 0u);
}

TEST(AxesStats, AutoFitWithStats)
{
    Axes               ax;
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    ax.bar(pos, heights);
    ax.auto_fit();

    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    // Should encompass the data
    EXPECT_LE(xlim.min, 1.0f);
    EXPECT_GE(xlim.max, 3.0f);
    EXPECT_LE(ylim.min, 0.0f);
    EXPECT_GE(ylim.max, 15.0f);
}

TEST(AxesStats, AutoFitIgnoresNonFiniteCoordinates)
{
    Axes               ax;
    const float        inf = std::numeric_limits<float>::infinity();
    std::vector<float> x   = {1.0f, inf, 2.0f};
    std::vector<float> y   = {3.0f, 4.0f, 5.0f};
    ax.line(x, y);

    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    EXPECT_TRUE(std::isfinite(xlim.min));
    EXPECT_TRUE(std::isfinite(xlim.max));
    EXPECT_TRUE(std::isfinite(ylim.min));
    EXPECT_TRUE(std::isfinite(ylim.max));
    EXPECT_LE(xlim.min, 1.0);
    EXPECT_GE(xlim.max, 2.0);
}

TEST(AxesStats, AutoFitIncludesStemSeries)
{
    Axes               ax;
    std::vector<float> x = {10.0f, 20.0f};
    std::vector<float> y = {100.0f, 200.0f};
    ax.stem(x, y).baseline(300.0f);

    const auto xlim = ax.x_limits();
    const auto ylim = ax.y_limits();
    EXPECT_LE(xlim.min, 10.0);
    EXPECT_GE(xlim.max, 20.0);
    EXPECT_LE(ylim.min, 100.0);
    EXPECT_GE(ylim.max, 300.0);
}

TEST(AxesStats, MixedSeriesTypes)
{
    Axes               ax;
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};
    ax.line(x, y);

    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    ax.bar(pos, heights);

    EXPECT_EQ(ax.series().size(), 2u);
    ax.auto_fit();
}

// ─── Fluent setters must rebuild geometry and bounds immediately ────────────
// Regression for P0-7: ax.histogram(v).density(true) autoscaled from stale count
// geometry, so the GPU projection disagreed with the rebuilt data.

namespace
{

std::vector<float> normal_like_samples(size_t n)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
    {
        // Deterministic, roughly bell-shaped: sum of three uniform-ish terms.
        float a = static_cast<float>((i * 7919) % 1000) / 1000.0f;
        float b = static_cast<float>((i * 104729) % 997) / 997.0f;
        float c = static_cast<float>((i * 1299709) % 991) / 991.0f;
        v[i]    = (a + b + c) * 4.0f;
    }
    return v;
}

float max_finite(std::span<const float> s)
{
    float m = -std::numeric_limits<float>::infinity();
    for (float f : s)
        if (std::isfinite(f))
            m = std::max(m, f);
    return m;
}

float max_fill_y(std::span<const float> verts)
{
    float m = -std::numeric_limits<float>::infinity();
    for (size_t i = 1; i < verts.size(); i += 3)
        m = std::max(m, verts[i]);
    return m;
}

// Reference histogram: flags set before data, which always produced correct output.
std::unique_ptr<HistogramSeries> reference_histogram(std::span<const float> v,
                                                     int                    bins,
                                                     bool                   density,
                                                     bool                   cumulative)
{
    auto ref = std::make_unique<HistogramSeries>();
    ref->density(density).cumulative(cumulative);
    ref->set_data(v, bins);
    return ref;
}

void expect_histogram_matches(const HistogramSeries& h, const HistogramSeries& ref)
{
    ASSERT_EQ(h.bin_counts().size(), ref.bin_counts().size());
    for (size_t i = 0; i < ref.bin_counts().size(); ++i)
        EXPECT_FLOAT_EQ(h.bin_counts()[i], ref.bin_counts()[i]) << "bin " << i;
    const float peak = max_finite(ref.bin_counts());
    EXPECT_FLOAT_EQ(max_finite(h.y_data()), peak);
    EXPECT_FLOAT_EQ(max_fill_y(h.fill_verts()), peak);
    EXPECT_EQ(h.point_count(), ref.point_count());
    EXPECT_EQ(h.fill_vertex_count(), ref.fill_vertex_count());
}

void expect_ylim_fits(const Axes& ax, float peak)
{
    const auto ylim = ax.y_limits();
    EXPECT_LE(ylim.min, 0.0);
    EXPECT_GE(ylim.max, peak);
    // Not autoscaled from a stale, much taller geometry.
    EXPECT_LE(ylim.max, peak * 1.5);
}

}   // namespace

TEST(HistogramSeries, DensitySetterAfterConstructionRebuildsBoundsAndGeometry)
{
    const auto v = normal_like_samples(5000);
    Axes       ax;
    auto&      h = ax.histogram(v, 40).density(true).gradient(true);

    const auto ref = reference_histogram(v, 40, true, false);
    expect_histogram_matches(h, *ref);
    const float peak = max_finite(ref->bin_counts());
    EXPECT_LT(peak, 1.0f);   // density, not counts
    expect_ylim_fits(ax, peak);
}

TEST(HistogramSeries, CumulativeSetterAfterConstructionRebuildsBoundsAndGeometry)
{
    const auto v = normal_like_samples(5000);
    Axes       ax;
    auto&      h = ax.histogram(v, 40).cumulative(true);

    const auto ref = reference_histogram(v, 40, false, true);
    expect_histogram_matches(h, *ref);
    EXPECT_FLOAT_EQ(max_finite(ref->bin_counts()), 5000.0f);
    expect_ylim_fits(ax, 5000.0f);
}

TEST(HistogramSeries, DensityCumulativeAnySetterOrder)
{
    const auto v = normal_like_samples(5000);

    using Setter                      = void (*)(HistogramSeries&);
    const std::vector<Setter> setters = {
        [](HistogramSeries& h) { h.density(true); },
        [](HistogramSeries& h) { h.cumulative(true); },
        [](HistogramSeries& h) { h.gradient(false); },
        [](HistogramSeries& h) { h.bins(25); },
    };
    std::vector<int> order = {0, 1, 2, 3};
    const auto       ref   = reference_histogram(v, 25, true, true);
    int              runs  = 0;
    do
    {
        Axes  ax;
        auto& h = ax.histogram(v, 40);
        for (int idx : order)
            setters[idx](h);
        SCOPED_TRACE(::testing::Message()
                     << "order " << order[0] << order[1] << order[2] << order[3]);
        expect_histogram_matches(h, *ref);
        EXPECT_FLOAT_EQ(h.bin_counts().back(), 1.0f);   // CDF ends at 1
        expect_ylim_fits(ax, 1.0f);
        ++runs;
    } while (std::next_permutation(order.begin(), order.end()));
    EXPECT_EQ(runs, 24);
}

TEST(HistogramSeries, BinsSetterRebuildsAndTogglingBackRestoresCounts)
{
    const auto v = normal_like_samples(1000);
    Axes       ax;
    auto&      h = ax.histogram(v, 10);
    h.bins(20);
    EXPECT_EQ(h.bin_counts().size(), 20u);
    EXPECT_EQ(h.bin_edges().size(), 21u);

    h.density(true).cumulative(true);
    h.density(false).cumulative(false);
    expect_histogram_matches(h, *reference_histogram(v, 20, false, false));
}

TEST(BarSeries, SettersAfterDataRebuildGeometryAndBounds)
{
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {10, 20, 15};
    Axes               ax;
    auto&              bars = ax.bar(pos, heights).baseline(-5.0f).bar_width(0.4f);

    EXPECT_FLOAT_EQ(*std::min_element(bars.y_data().begin(),
                                      bars.y_data().end(),
                                      [](float a, float b)
                                      { return std::isnan(b) || (!std::isnan(a) && a < b); }),
                    -5.0f);
    const float xmin = *std::min_element(bars.x_data().begin(),
                                         bars.x_data().end(),
                                         [](float a, float b)
                                         { return std::isnan(b) || (!std::isnan(a) && a < b); });
    EXPECT_FLOAT_EQ(xmin, 0.8f);
    EXPECT_LE(ax.y_limits().min, -5.0);

    // Horizontal orientation swaps the extents: heights run along x.
    bars.orientation(BarOrientation::Horizontal);
    EXPECT_FLOAT_EQ(max_finite(bars.x_data()), 20.0f);
    EXPECT_FLOAT_EQ(max_finite(bars.y_data()), 3.2f);
    EXPECT_GE(ax.x_limits().max, 20.0);
}

TEST(BoxPlotSeries, SettersAfterDataRebuildGeometry)
{
    std::vector<float> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    BoxPlotSeries      bp;
    bp.add_box(0.0f, data);
    bp.box_width(2.0f);
    EXPECT_FLOAT_EQ(max_finite(bp.x_data()), 1.0f);

    std::vector<float> with_outlier = {1, 2, 3, 4, 5, 6, 7, 8, 9, 100};
    BoxPlotSeries      bp2;
    bp2.add_box(0.0f, with_outlier);
    ASSERT_GT(bp2.outlier_count(), 0u);
    bp2.show_outliers(false);
    EXPECT_EQ(bp2.outlier_count(), 0u);
}

TEST(ViolinSeries, SettersAfterDataRebuildGeometry)
{
    std::vector<float> data = {1, 2, 2, 3, 3, 3, 4, 4, 5};
    ViolinSeries       vn;
    vn.add_violin(0.0f, data);
    const size_t before = vn.point_count();
    vn.resolution(120);
    EXPECT_GT(vn.point_count(), before);

    const float half_before = max_finite(vn.x_data());
    vn.violin_width(4.0f * vn.violin_width());
    EXPECT_NEAR(max_finite(vn.x_data()), 4.0f * half_before, 1e-4f);
}

// ─── Horizontal bars (P0-8) ─────────────────────────────────────────────────

namespace
{
float min_finite(std::span<const float> v)
{
    float m = std::numeric_limits<float>::infinity();
    for (float f : v)
        if (std::isfinite(f))
            m = std::min(m, f);
    return m;
}

float max_finite_h(std::span<const float> v)
{
    float m = -std::numeric_limits<float>::infinity();
    for (float f : v)
        if (std::isfinite(f))
            m = std::max(m, f);
    return m;
}
}   // namespace

TEST(BarSeries, HorizontalBarsSitOnCategoryPositionsAlongY)
{
    std::vector<float> pos     = {1, 2, 3};
    std::vector<float> heights = {3, -2, 9};
    BarSeries          bars(pos, heights);
    bars.bar_width(0.4f).orientation(BarOrientation::Horizontal);

    // Each bar's outline is 5 points + NaN break: positions on y, values on x.
    auto x = bars.x_data();
    auto y = bars.y_data();
    ASSERT_EQ(x.size(), 6u * pos.size());
    for (size_t i = 0; i < pos.size(); ++i)
    {
        auto bx = x.subspan(i * 6, 5);
        auto by = y.subspan(i * 6, 5);
        EXPECT_FLOAT_EQ(min_finite(by), pos[i] - 0.2f);
        EXPECT_FLOAT_EQ(max_finite_h(by), pos[i] + 0.2f);
        EXPECT_FLOAT_EQ(min_finite(bx), std::min(0.0f, heights[i]));
        EXPECT_FLOAT_EQ(max_finite_h(bx), std::max(0.0f, heights[i]));
    }

    // Fill quads cover the same rectangles.
    auto fv = bars.fill_verts();
    ASSERT_EQ(fv.size(), 6u * 3u * pos.size());
    for (size_t i = 0; i < pos.size(); ++i)
    {
        for (size_t k = 0; k < 6; ++k)
        {
            const float vx = fv[(i * 6 + k) * 3 + 0];
            const float vy = fv[(i * 6 + k) * 3 + 1];
            EXPECT_GE(vy, pos[i] - 0.2f - 1e-6f);
            EXPECT_LE(vy, pos[i] + 0.2f + 1e-6f);
            EXPECT_GE(vx, std::min(0.0f, heights[i]) - 1e-6f);
            EXPECT_LE(vx, std::max(0.0f, heights[i]) + 1e-6f);
        }
    }
}

TEST(BarSeries, HorizontalBarsAutoscaleToValuesOnXAndCategoriesOnY)
{
    std::vector<float> p1 = {0.8f, 1.8f, 2.8f}, p2 = {1.2f, 2.2f, 3.2f};
    std::vector<float> v1 = {3, -4, 9}, v2 = {2, 5, 6};
    Axes               ax;
    ax.bar(p1, v1).bar_width(0.4f).orientation(BarOrientation::Horizontal);
    ax.bar(p2, v2).bar_width(0.4f).orientation(BarOrientation::Horizontal);

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    // Values (including the negative bar and the longest bar) fit on x.
    EXPECT_LE(xl.min, -4.0);
    EXPECT_GE(xl.max, 9.0);
    // Category extents (incl. half bar width) fit on y, and y does not use the values.
    EXPECT_LE(yl.min, 0.6);
    EXPECT_GE(yl.max, 3.4);
    EXPECT_LT(yl.max, 5.0);
    EXPECT_GT(yl.min, -1.0);
}

TEST(BarSeries, HorizontalBarsHonourBaseline)
{
    std::vector<float> pos     = {10, 20};
    std::vector<float> heights = {12, 8};
    BarSeries          bars(pos, heights);
    bars.orientation(BarOrientation::Horizontal).baseline(10.0f).bar_width(6.0f);

    EXPECT_FLOAT_EQ(min_finite(bars.x_data()), 8.0f);
    EXPECT_FLOAT_EQ(max_finite_h(bars.x_data()), 12.0f);
    EXPECT_FLOAT_EQ(min_finite(bars.y_data()), 7.0f);
    EXPECT_FLOAT_EQ(max_finite_h(bars.y_data()), 23.0f);
}

TEST(BarSeries, HorizontalGradientRunsAcrossBarThickness)
{
    std::vector<float> pos     = {1, 2};
    std::vector<float> heights = {5, -5};
    BarSeries          bars(pos, heights);
    bars.orientation(BarOrientation::Horizontal).gradient(true);

    // Alpha depends only on y (the thickness edge), never on the bar's length or sign.
    auto fv = bars.fill_verts();
    for (size_t v = 0; v < bars.fill_vertex_count(); ++v)
    {
        const float vy = fv[v * 3 + 1];
        const float a  = fv[v * 3 + 2];
        const float c  = pos[v / 6];
        if (vy > c)
            EXPECT_FLOAT_EQ(a, 1.0f);
        else
            EXPECT_LT(a, 1.0f);
    }

    bars.gradient(false);
    for (size_t v = 0; v < bars.fill_vertex_count(); ++v)
        EXPECT_FLOAT_EQ(bars.fill_verts()[v * 3 + 2], 1.0f);
}

TEST(BarSeries, VerticalGradientRunsAcrossBarWidth)
{
    std::vector<float> pos     = {1};
    std::vector<float> heights = {-5};
    BarSeries          bars(pos, heights);
    bars.gradient(true);

    auto fv = bars.fill_verts();
    for (size_t v = 0; v < bars.fill_vertex_count(); ++v)
    {
        if (fv[v * 3] < 1.0f)
            EXPECT_FLOAT_EQ(fv[v * 3 + 2], 1.0f);
        else
            EXPECT_LT(fv[v * 3 + 2], 1.0f);
    }
}

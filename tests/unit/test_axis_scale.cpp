// Axis scale types: axis-space mapping, log autoscale limits and log ticks.

#include <cmath>
#include <functional>
#include <memory>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <string>
#include <vector>

#include <spectra/custom_series.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>

#include "core/axes_data_extent.hpp"
#include "core/axis_scale.hpp"

using namespace spectra;

namespace
{

// The gallery's log-log case: y = x^2 over 0.001..1000.
Axes& make_square_loglog(Axes& ax)
{
    std::vector<float> x;
    std::vector<float> y;
    for (int i = 0; i <= 60; ++i)
    {
        const float v = std::pow(10.0f, -3.0f + 0.1f * static_cast<float>(i));
        x.push_back(v);
        y.push_back(v * v);
    }
    ax.line(x, y);
    ax.xscale(ScaleType::Log10);
    ax.yscale(ScaleType::Log10);
    return ax;
}

}   // namespace

// ─── Axis-space mapping per ScaleType ───────────────────────────────────────

TEST(AxisScale, LinearAndSqrtAreIdentity)
{
    for (ScaleType s : {ScaleType::Linear, ScaleType::Sqrt})
    {
        EXPECT_DOUBLE_EQ(to_axis_space(s, -3.5), -3.5);
        EXPECT_DOUBLE_EQ(from_axis_space(s, 42.0), 42.0);
        const AxisLimits lim = axis_space_limits(s, {-2.0, 5.0});
        EXPECT_DOUBLE_EQ(lim.min, -2.0);
        EXPECT_DOUBLE_EQ(lim.max, 5.0);
    }
}

TEST(AxisScale, LogRoundTrip)
{
    EXPECT_DOUBLE_EQ(to_axis_space(ScaleType::Log10, 1000.0), 3.0);
    EXPECT_DOUBLE_EQ(to_axis_space(ScaleType::Log2, 8.0), 3.0);
    EXPECT_NEAR(from_axis_space(ScaleType::Log10, -2.0), 0.01, 1e-15);
    EXPECT_DOUBLE_EQ(from_axis_space(ScaleType::Log2, 5.0), 32.0);
}

TEST(AxisScale, LogClipsNonPositiveAndKeepsNaN)
{
    for (ScaleType s : {ScaleType::Log10, ScaleType::Log2})
    {
        EXPECT_DOUBLE_EQ(to_axis_space(s, 0.0), log_axis_floor(s));
        EXPECT_DOUBLE_EQ(to_axis_space(s, -7.0), log_axis_floor(s));
        EXPECT_TRUE(std::isfinite(log_axis_floor(s)));
        EXPECT_LT(log_axis_floor(s), -300.0);
        EXPECT_TRUE(std::isnan(to_axis_space(s, std::nan(""))));
    }
}

TEST(AxisScale, LogLimitsAreSanitized)
{
    // Non-positive lower limit → positive floor, or max / base^3 without one.
    AxisLimits l = sanitize_log_limits(ScaleType::Log10, {-5.0, 100.0}, 0.5);
    EXPECT_DOUBLE_EQ(l.min, 0.5);
    l = sanitize_log_limits(ScaleType::Log10, {0.0, 100.0});
    EXPECT_DOUBLE_EQ(l.min, 0.1);
    l = sanitize_log_limits(ScaleType::Log2, {0.0, 64.0});
    EXPECT_DOUBLE_EQ(l.min, 8.0);
    // Entirely non-positive → one decade.
    l = sanitize_log_limits(ScaleType::Log10, {-10.0, -1.0});
    EXPECT_DOUBLE_EQ(l.min, 1.0);
    EXPECT_DOUBLE_EQ(l.max, 10.0);

    const AxisLimits a = axis_space_limits(ScaleType::Log10, {0.001, 1000.0});
    EXPECT_NEAR(a.min, -3.0, 1e-12);
    EXPECT_NEAR(a.max, 3.0, 1e-12);
}

// ─── Autoscale ──────────────────────────────────────────────────────────────

TEST(AxisScale, LogAutoscalePadsInLogSpace)
{
    Axes ax;
    make_square_loglog(ax);

    // 5% of the log span on each side: 6 decades → 0.3, 12 decades → 0.6.
    const AxisLimits xl = ax.x_limits();
    EXPECT_NEAR(std::log10(xl.min), -3.3, 1e-3);
    EXPECT_NEAR(std::log10(xl.max), 3.3, 1e-3);
    const AxisLimits yl = ax.y_limits();
    EXPECT_NEAR(std::log10(yl.min), -6.6, 1e-3);
    EXPECT_NEAR(std::log10(yl.max), 6.6, 1e-3);
}

TEST(AxisScale, LogAutoscaleIgnoresNonPositiveData)
{
    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f, 10.0f, 100.0f};
    std::vector<float> y = {-1.0f, 1.0f, 2.0f, 3.0f};
    ax.line(x, y);
    ax.xscale(ScaleType::Log2);
    const AxisLimits xl = ax.x_limits();
    EXPECT_GT(xl.min, 0.0);
    EXPECT_LT(xl.min, 1.0);
    EXPECT_GT(xl.max, 100.0);
}

TEST(AxisScale, ExplicitNonPositiveLogLimitClampsToData)
{
    Axes ax;
    make_square_loglog(ax);
    ax.xlim(0.0, 100.0);
    const AxisLimits xl = ax.x_limits();
    EXPECT_NEAR(xl.min, 0.001, 1e-6);
    EXPECT_DOUBLE_EQ(xl.max, 100.0);

    // A linear axis keeps whatever the user set.
    ax.xscale(ScaleType::Linear);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
}

TEST(AxisScale, LinearAutoscaleUnchanged)
{
    Axes               ax;
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {-1.0f, 1.0f};
    ax.line(x, y);
    const AxisLimits xl = ax.x_limits();
    EXPECT_DOUBLE_EQ(xl.min, -0.5);
    EXPECT_DOUBLE_EQ(xl.max, 10.5);
}

// ─── Ticks ──────────────────────────────────────────────────────────────────

TEST(AxisScale, Log10DecadeTicks)
{
    Axes ax;
    make_square_loglog(ax);

    const TickResult               xt     = ax.compute_x_ticks();
    const std::vector<std::string> want_x = {"0.001", "0.01", "0.1", "1", "10", "100", "1000"};
    ASSERT_EQ(xt.labels, want_x);
    for (size_t i = 0; i < xt.positions.size(); ++i)
        EXPECT_NEAR(std::log10(xt.positions[i]), -3.0 + static_cast<double>(i), 1e-12);
}

TEST(AxisScale, Log10WideRangeStridesDecades)
{
    Axes ax;
    make_square_loglog(ax);

    // 13 decades (1e-6..1e6) → every second decade, aligned on 10^0.  The
    // ends leave the plain range, so every label is a power (never "10000"
    // next to an exponent form).
    const TickResult               yt     = ax.compute_y_ticks();
    const std::vector<std::string> want_y = {"10⁻⁶", "10⁻⁴", "10⁻²", "10⁰", "10²", "10⁴", "10⁶"};
    EXPECT_EQ(yt.labels, want_y);
    EXPECT_LE(yt.positions.size(), 8u);
}

TEST(AxisScale, Log2Ticks)
{
    Axes ax;
    ax.xscale(ScaleType::Log2);
    ax.xlim(1.0, 1024.0);
    const TickResult t = ax.compute_x_ticks();
    // 11 powers of two → stride 2.
    const std::vector<std::string> want = {"1", "4", "16", "64", "256", "1024"};
    EXPECT_EQ(t.labels, want);
    EXPECT_DOUBLE_EQ(t.positions.back(), 1024.0);

    ax.xlim(1.0 / 1048576.0, 0.5);
    const TickResult small = ax.compute_x_ticks();
    ASSERT_FALSE(small.labels.empty());
    // 20 powers → stride 3, aligned on 2^0: first label is 2^-18, and the
    // whole axis switches to power labels.
    const std::vector<std::string> want_small = {"2⁻¹⁸", "2⁻¹⁵", "2⁻¹²", "2⁻⁹", "2⁻⁶", "2⁻³"};
    EXPECT_EQ(small.labels, want_small);
}

TEST(AxisScale, LogNarrowRangeFallsBackToPositiveLinearTicks)
{
    Axes ax;
    ax.yscale(ScaleType::Log10);
    ax.ylim(2.0, 8.0);
    const TickResult t = ax.compute_y_ticks();
    ASSERT_GE(t.positions.size(), 2u);
    for (double p : t.positions)
    {
        EXPECT_GE(p, 2.0 - 1e-9);
        EXPECT_LE(p, 8.0 + 1e-9);
    }
}

TEST(AxisScale, SqrtTicksStayInRange)
{
    Axes ax;
    ax.xscale(ScaleType::Sqrt);
    ax.xlim(0.0, 100.0);
    const TickResult t = ax.compute_x_ticks();
    ASSERT_GE(t.positions.size(), 2u);
    for (double p : t.positions)
    {
        EXPECT_GE(p, 0.0);
        EXPECT_LE(p, 100.0 + 1e-9);
    }
}

TEST(AxisScale, SqrtTicksCoverAxisEvenly)
{
    // Sqrt axes are drawn untransformed, so their ticks must be evenly spaced
    // and leave no gap wider than one step at either end of the axis.
    for (const AxisLimits lim : {AxisLimits{0.0, 10.0},
                                 AxisLimits{0.0, 100.0},
                                 AxisLimits{-4.0, 1.0},
                                 AxisLimits{3.0, 7.0e5}})
    {
        Axes ax;
        ax.xscale(ScaleType::Sqrt);
        ax.xlim(lim.min, lim.max);
        const TickResult t = ax.compute_x_ticks();
        ASSERT_GE(t.positions.size(), 3u) << lim.min << ".." << lim.max;
        const double step = t.positions[1] - t.positions[0];
        ASSERT_GT(step, 0.0);
        for (size_t i = 1; i < t.positions.size(); ++i)
            EXPECT_NEAR(t.positions[i] - t.positions[i - 1], step, step * 1e-6)
                << lim.min << ".." << lim.max << " tick " << i;
        EXPECT_LE(t.positions.front() - lim.min, step + 1e-9);
        EXPECT_LE(lim.max - t.positions.back(), step + 1e-9);

        Axes linear;
        linear.xlim(lim.min, lim.max);
        EXPECT_EQ(t.positions, linear.compute_x_ticks().positions);
        EXPECT_EQ(t.labels, linear.compute_x_ticks().labels);
    }
}

TEST(AxisScale, LinearTicksUnchanged)
{
    Axes ax;
    ax.xlim(0.0, 10.0);
    const TickResult t = ax.compute_x_ticks();
    ASSERT_FALSE(t.positions.empty());
    EXPECT_DOUBLE_EQ(t.positions.front(), 0.0);
    EXPECT_DOUBLE_EQ(t.positions.back(), 10.0);
}

TEST(AxisScale, LogMinorTicks)
{
    const std::vector<double> m = log_minor_ticks(ScaleType::Log10, 1.0, 100.0);
    ASSERT_EQ(m.size(), 16u);   // 2..9 and 20..90
    EXPECT_DOUBLE_EQ(m.front(), 2.0);
    EXPECT_DOUBLE_EQ(m.back(), 90.0);

    // Partial decades are clipped to the range.
    const std::vector<double> part = log_minor_ticks(ScaleType::Log10, 0.25, 0.65);
    const std::vector<double> want = {0.3, 0.4, 0.5, 0.6};
    ASSERT_EQ(part.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i)
        EXPECT_NEAR(part[i], want[i], 1e-12);

    EXPECT_TRUE(log_minor_ticks(ScaleType::Linear, 1.0, 100.0).empty());
    EXPECT_TRUE(log_minor_ticks(ScaleType::Sqrt, 1.0, 100.0).empty());
    EXPECT_TRUE(log_minor_ticks(ScaleType::Log10, 1e-10, 1e10).empty());   // too dense
}

TEST(AxisScale, Log2MinorTicksSplitOctaves)
{
    // 1.25, 1.5, 1.75 × 2^k: each octave split into four equal linear steps.
    const std::vector<double> m    = log_minor_ticks(ScaleType::Log2, 1.0, 4.0);
    const std::vector<double> want = {1.25, 1.5, 1.75, 2.5, 3.0, 3.5};
    ASSERT_EQ(m.size(), want.size());
    for (size_t i = 0; i < want.size(); ++i)
        EXPECT_DOUBLE_EQ(m[i], want[i]);

    // Clipped to a partial octave.
    const std::vector<double> part = log_minor_ticks(ScaleType::Log2, 0.3, 0.45);
    ASSERT_EQ(part.size(), 3u);   // 0.3125, 0.375, 0.4375
    EXPECT_DOUBLE_EQ(part[0], 0.3125);
    EXPECT_DOUBLE_EQ(part[1], 0.375);
    EXPECT_DOUBLE_EQ(part[2], 0.4375);

    EXPECT_EQ(log_minor_ticks(ScaleType::Log2, 1.0, 1024.0).size(), 30u);
    EXPECT_TRUE(log_minor_ticks(ScaleType::Log2, 1.0, std::pow(2.0, 40.0)).empty());   // too dense
}

TEST(AxisScale, LogTickLabelsUseOneFormatPerAxis)
{
    auto is_power = [](const std::string& s)
    { return s.rfind("10", 0) == 0 && s.size() > 2 && static_cast<unsigned char>(s[2]) >= 0x80; };
    Axes ax;
    ax.yscale(ScaleType::Log10);

    // Inside 1e-4..1e4: all plain numbers.
    ax.ylim(1e-4, 1e4);
    TickResult t = ax.compute_y_ticks();
    ASSERT_GE(t.labels.size(), 2u);
    for (const auto& l : t.labels)
        EXPECT_FALSE(is_power(l)) << l;
    EXPECT_EQ(t.labels.back(), "10000");

    // One decade beyond: every label is a power, including 10^0..10^4.
    ax.ylim(1.0, 1e6);
    t = ax.compute_y_ticks();
    const std::vector<std::string> want = {"10⁰", "10¹", "10²", "10³", "10⁴", "10⁵", "10⁶"};
    EXPECT_EQ(t.labels, want);

    // Log2 within 1/16 .. 65536 stays plain.
    Axes a2;
    a2.xscale(ScaleType::Log2);
    a2.xlim(0.0625, 16.0);
    t = a2.compute_x_ticks();
    // 9 powers → stride 2 aligned on 2^0.
    const std::vector<std::string> want2 = {"0.0625", "0.25", "1", "4", "16"};
    EXPECT_EQ(t.labels, want2);
}

TEST(AxisScale, TranslateLimitsMovesInAxisSpace)
{
    const AxisLimits lin = translate_limits(ScaleType::Linear, {0.0, 10.0}, 2.5);
    EXPECT_DOUBLE_EQ(lin.min, 2.5);
    EXPECT_DOUBLE_EQ(lin.max, 12.5);

    // One decade on a log10 axis: ratios are kept, nothing crosses zero.
    const AxisLimits lg = translate_limits(ScaleType::Log10, {1.0, 100.0}, -1.0);
    EXPECT_NEAR(lg.min, 0.1, 1e-14);
    EXPECT_NEAR(lg.max, 10.0, 1e-12);

    const AxisLimits l2 = translate_limits(ScaleType::Log2, {1.0, 8.0}, 1.0);
    EXPECT_NEAR(l2.min, 2.0, 1e-14);
    EXPECT_NEAR(l2.max, 16.0, 1e-12);
}

// ─── Pan / zoom in axis space ───────────────────────────────────────────────

TEST(AxisScale, LogZoomAboutAnchorKeepsRatios)
{
    const AxisLimits z = zoom_limits_about(ScaleType::Log10, {1.0, 100.0}, 10.0, 0.5);
    EXPECT_NEAR(z.min, std::sqrt(10.0), 1e-12);
    EXPECT_NEAR(z.max, 10.0 * std::sqrt(10.0), 1e-12);

    const AxisLimits lin = zoom_limits_about(ScaleType::Linear, {0.0, 10.0}, 5.0, 0.5);
    EXPECT_DOUBLE_EQ(lin.min, 2.5);
    EXPECT_DOUBLE_EQ(lin.max, 7.5);
}

TEST(AxisScale, LogPanStaysPositive)
{
    const AxisLimits p = pan_limits(ScaleType::Log10, {1.0, 100.0}, -2.0);
    EXPECT_NEAR(p.min, 1e-4, 1e-16);
    EXPECT_NEAR(p.max, 0.01, 1e-14);

    const AxisLimits lin = pan_limits(ScaleType::Linear, {0.0, 10.0}, 0.5);
    EXPECT_DOUBLE_EQ(lin.min, 5.0);
    EXPECT_DOUBLE_EQ(lin.max, 15.0);
}

// ─── Zoom centre covers every 2D series type ────────────────────────────────

TEST(ZoomCenter, CoversEveryBuiltInSeriesType)
{
    // Each axes holds one series whose data spans x [10, 30], y [100, 300]
    // (or as close as the type allows); its centre must be found.
    const std::vector<float>                xs     = {10.0f, 20.0f, 30.0f};
    const std::vector<float>                ys     = {100.0f, 200.0f, 300.0f};
    std::vector<std::function<void(Axes&)>> makers = {
        [&](Axes& ax) { ax.bar(xs, ys); },
        [&](Axes& ax) { ax.stem(xs, ys); },
        [&](Axes& ax) { ax.band(xs, std::vector<float>{100, 100, 100}, ys); },
        [&](Axes& ax) { ax.shapes().rect(10.0, 100.0, 20.0, 200.0); },
    };
    for (size_t i = 0; i < makers.size(); ++i)
    {
        Axes ax;
        makers[i](ax);
        double cx = 0.0, cy = 0.0;
        ASSERT_TRUE(detail::data_center(ax, cx, cy)) << "series kind " << i;
        EXPECT_GT(cx, 9.0) << i;
        EXPECT_LT(cx, 31.0) << i;
        EXPECT_GT(cy, 0.0) << i;
        EXPECT_LT(cy, 301.0) << i;
    }

    // Statistical series have data too (histogram, box plot, violin).
    const std::vector<float> samples = {1.0f, 2.0f, 2.5f, 3.0f, 4.0f, 9.0f};
    for (int kind = 0; kind < 3; ++kind)
    {
        Axes ax;
        if (kind == 0)
            ax.histogram(samples);
        else if (kind == 1)
            ax.box_plot().add_box(5.0f, samples);
        else
            ax.violin().add_violin(5.0f, samples);
        double cx = 0.0, cy = 0.0;
        EXPECT_TRUE(detail::data_center(ax, cx, cy)) << "stat kind " << kind;
    }
}

TEST(ZoomCenter, SkipsHiddenSeriesAndHonoursOffsets)
{
    Axes ax;
    ax.line(std::vector<float>{0.0f, 2.0f}, std::vector<float>{0.0f, 2.0f});
    ax.scatter(std::vector<float>{100.0f, 102.0f}, std::vector<float>{50.0f, 52.0f}).visible(false);
    double cx = 0.0, cy = 0.0;
    ASSERT_TRUE(detail::data_center(ax, cx, cy));
    EXPECT_DOUBLE_EQ(cx, 1.0);
    EXPECT_DOUBLE_EQ(cy, 1.0);

    Axes empty;
    EXPECT_FALSE(detail::data_center(empty, cx, cy));
}

TEST(ZoomCenter, IncludesPluginSeriesOnceBoundsAreKnown)
{
    Axes  ax;
    auto  owned = std::make_unique<CustomSeries>("heatmap");
    auto* cs    = owned.get();
    ax.series_mut().push_back(std::move(owned));
    double cx = 0.0, cy = 0.0;
    EXPECT_FALSE(detail::data_center(ax, cx, cy)) << "no bounds computed yet";
    cs->set_bounds(-4.0f, 4.0f, 10.0f, 20.0f);
    ASSERT_TRUE(detail::data_center(ax, cx, cy));
    EXPECT_DOUBLE_EQ(cx, 0.0);
    EXPECT_DOUBLE_EQ(cy, 15.0);
}

TEST(ZoomCenter, ZoomLimitsCenteredInAxisSpace)
{
    // Linear: span scaled about the centre.
    AxisLimits z = zoom_limits_centered(ScaleType::Linear, {0.0, 10.0}, 2.0, 0.5);
    EXPECT_DOUBLE_EQ(z.min, -0.5);
    EXPECT_DOUBLE_EQ(z.max, 4.5);
    // Log: decades, centred geometrically; never non-positive.
    z = zoom_limits_centered(ScaleType::Log10, {1.0, 1e4}, 100.0, 0.5);
    EXPECT_NEAR(z.min, 10.0, 1e-9);
    EXPECT_NEAR(z.max, 1000.0, 1e-9);
    // A centre the axis cannot show keeps the view centre.
    z = zoom_limits_centered(ScaleType::Log10, {1.0, 1e4}, -5.0, 0.5);
    EXPECT_NEAR(z.min, 10.0, 1e-9);
    EXPECT_NEAR(z.max, 1000.0, 1e-9);
    z = zoom_limits_centered(ScaleType::Linear, {0.0, 10.0}, std::nan(""), 2.0);
    EXPECT_DOUBLE_EQ(z.min, -5.0);
    EXPECT_DOUBLE_EQ(z.max, 15.0);
    // Epoch-scale limits keep sub-second precision (double, not float).
    z = zoom_limits_centered(ScaleType::Linear, {1.7e9, 1.7e9 + 10.0}, 1.7e9 + 5.25, 0.5);
    EXPECT_DOUBLE_EQ(z.min, 1.7e9 + 2.75);
    EXPECT_DOUBLE_EQ(z.max, 1.7e9 + 7.75);
}

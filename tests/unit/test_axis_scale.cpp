// Axis scale types: axis-space mapping, log autoscale limits and log ticks.

#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <string>
#include <vector>

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

    // 13 decades (1e-6..1e6) → every second decade, aligned on 10^0.
    const TickResult               yt     = ax.compute_y_ticks();
    const std::vector<std::string> want_y = {"1e-6", "0.0001", "0.01", "1", "100", "10000", "1e6"};
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
    // 20 powers → stride 3, aligned on 2^0: first label is 2^-18.
    EXPECT_EQ(small.labels.front(), "2^-18");
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

    EXPECT_TRUE(log_minor_ticks(ScaleType::Log2, 1.0, 1024.0).empty());
    EXPECT_TRUE(log_minor_ticks(ScaleType::Linear, 1.0, 100.0).empty());
    EXPECT_TRUE(log_minor_ticks(ScaleType::Log10, 1e-10, 1e10).empty());   // too dense
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

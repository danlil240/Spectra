#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/axes.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "core/axis_ticks.hpp"

using namespace spectra;

// Helper: create axes with a single line series spanning [xmin,xmax] x [ymin,ymax]
// static std::unique_ptr<Axes> make_axes(float xmin, float xmax, float ymin, float ymax) {
//     auto ax = std::make_unique<Axes>();
//     float xs[] = {xmin, xmax};
//     float ys[] = {ymin, ymax};
//     ax->line(std::span<const float>(xs, 2), std::span<const float>(ys, 2));
//     return ax;
// }

// --- Basic tick generation ---

TEST(TickGeneration, PositiveRange)
{
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 3u);
    EXPECT_LE(ticks.positions.size(), 15u);
    EXPECT_EQ(ticks.positions.size(), ticks.labels.size());
    // All ticks should be within [0, 10]
    for (double v : ticks.positions)
    {
        EXPECT_GE(v, -0.1);
        EXPECT_LE(v, 10.1);
    }
}

TEST(TickGeneration, NegativeRange)
{
    Axes ax;
    ax.xlim(-100.0f, -10.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    for (double v : ticks.positions)
    {
        EXPECT_GE(v, -101.0);
        EXPECT_LE(v, -9.0);
    }
}

TEST(TickGeneration, CrossingZero)
{
    Axes ax;
    ax.xlim(-5.0f, 5.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 3u);
    // Should include zero (or very close to it)
    bool has_zero = false;
    for (double v : ticks.positions)
    {
        if (std::abs(v) < 0.01)
            has_zero = true;
    }
    EXPECT_TRUE(has_zero);
}

TEST(TickGeneration, VerySmallRange)
{
    Axes ax;
    ax.xlim(1.0f, 1.0f + 1e-12f);
    auto ticks = ax.compute_x_ticks();
    // Should produce at least one tick without crashing
    EXPECT_GE(ticks.positions.size(), 1u);
    EXPECT_EQ(ticks.positions.size(), ticks.labels.size());
}

TEST(TickGeneration, ZeroRange)
{
    Axes ax;
    ax.xlim(5.0f, 5.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 1u);
}

TEST(TickGeneration, LargeRange)
{
    Axes ax;
    ax.xlim(0.0f, 1e6f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    EXPECT_LE(ticks.positions.size(), 25u);
}

TEST(TickGeneration, TinyValues)
{
    Axes ax;
    ax.xlim(1e-8f, 2e-8f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 1u);
}

// --- No "-0" labels ---

TEST(TickGeneration, NoNegativeZeroLabel)
{
    Axes ax;
    ax.xlim(-1.0f, 1.0f);
    auto ticks = ax.compute_x_ticks();
    for (const auto& lbl : ticks.labels)
    {
        EXPECT_NE(lbl, "-0");
    }
}

// --- Deep zoom regression tests ---

TEST(TickGeneration, DeepZoomLabelsDistinguishable)
{
    // Simulate deep zoom near 7.9 (like the bug screenshot)
    // Range ~1e-5 around 7.9 — labels must NOT all show "7.9"
    Axes ax;
    ax.xlim(7.89999f, 7.90001f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    // All labels must be unique (distinguishable)
    for (size_t i = 0; i + 1 < ticks.labels.size(); ++i)
    {
        EXPECT_NE(ticks.labels[i], ticks.labels[i + 1])
            << "Tick labels must be distinguishable at deep zoom: index " << i;
    }
}

TEST(TickGeneration, DeepZoomScientificNotation)
{
    // Deep zoom: range ~1e-3 near a non-zero offset (100)
    // Should use enough digits so labels like "100.0001" vs "100.0002" are unique
    Axes ax;
    ax.xlim(100.0f, 100.001f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    EXPECT_EQ(ticks.positions.size(), ticks.labels.size());
    // Labels must be unique
    for (size_t i = 0; i + 1 < ticks.labels.size(); ++i)
    {
        EXPECT_NE(ticks.labels[i], ticks.labels[i + 1]);
    }
}

TEST(TickGeneration, DeepZoomNearZero)
{
    // Deep zoom near zero — should produce normal ticks
    Axes ax;
    ax.xlim(-1e-6f, 1e-6f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    // Labels should be unique
    for (size_t i = 0; i + 1 < ticks.labels.size(); ++i)
    {
        EXPECT_NE(ticks.labels[i], ticks.labels[i + 1]);
    }
}

TEST(TickGeneration, DeepZoomFloatPrecisionLimit)
{
    // At the float precision limit for value ~8.0
    // 8 * FLT_EPSILON ≈ 9.5e-7, so range of 1e-5 should work fine
    Axes  ax;
    float center = 8.0f;
    float half   = 5e-6f;
    ax.xlim(center - half, center + half);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 1u);
    EXPECT_EQ(ticks.positions.size(), ticks.labels.size());
}

// --- Y ticks work the same ---

TEST(TickGeneration, YTicksPositive)
{
    Axes ax;
    ax.ylim(0.0f, 100.0f);
    auto ticks = ax.compute_y_ticks();
    EXPECT_GE(ticks.positions.size(), 3u);
}

// --- Tick density follows the axis length on screen (P1-6) ---

namespace
{

Axes& with_viewport(Axes& ax, float w, float h)
{
    ax.set_viewport(Rect{0.0f, 0.0f, w, h});
    return ax;
}

double tick_step(const TickResult& t)
{
    return t.positions.size() >= 2 ? t.positions[1] - t.positions[0] : 0.0;
}

bool is_nice_step(double step)
{
    const double mant = step / std::pow(10.0, std::floor(std::log10(step)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
    {
        if (std::abs(mant - m) < 1e-9)
            return true;
    }
    return false;
}

size_t decimals_of(const std::string& s)
{
    const auto dot = s.find('.');
    return dot == std::string::npos ? 0 : s.size() - dot - 1;
}

}   // namespace

TEST(TickDensity, WideAxisGetsOneTickPerRoughly90Px)
{
    Axes ax;
    ax.xlim(0.0, 10.0);
    with_viewport(ax, 1200.0f, 700.0f);
    const auto t = ax.compute_x_ticks();
    EXPECT_GE(t.positions.size(), 10u);
    EXPECT_LE(t.positions.size(), 16u);
    const double px = tick_step(t) * 1200.0 / 10.0;
    EXPECT_GE(px, 60.0);
    EXPECT_LE(px, 140.0);
}

TEST(TickDensity, CountGrowsWithAxisLength)
{
    Axes ax;
    ax.xlim(0.0, 100.0);
    ax.ylim(0.0, 100.0);
    size_t last = 0;
    for (float w : {200.0f, 400.0f, 800.0f, 1600.0f})
    {
        with_viewport(ax, w, w);
        const auto t = ax.compute_x_ticks();
        EXPECT_GE(t.positions.size(), last) << "width " << w;
        EXPECT_GE(t.positions.size(), 2u) << "width " << w;
        last            = t.positions.size();
        const auto   ty = ax.compute_y_ticks();
        const double px = tick_step(ty) * w / 100.0;
        EXPECT_GE(px, 40.0) << "height " << w;
        EXPECT_LE(px, 160.0) << "height " << w;
    }
    EXPECT_GE(last, 12u);
}

TEST(TickDensity, StepsAreOneTwoTwoPointFiveFive)
{
    Axes ax;
    for (double hi : {0.7, 3.0, 13.0, 47.0, 250.0, 9000.0})
    {
        ax.xlim(0.0, hi);
        for (float w : {150.0f, 330.0f, 640.0f, 1200.0f})
        {
            with_viewport(ax, w, w);
            const auto t = ax.compute_x_ticks();
            if (t.positions.size() >= 2)
            {
                EXPECT_TRUE(is_nice_step(tick_step(t))) << "hi " << hi << " w " << w;
            }
        }
    }
}

TEST(TickDensity, XLabelsDoNotCollide)
{
    Axes ax;
    ax.xlim(0.0, 123456.0);
    with_viewport(ax, 300.0f, 300.0f);
    const auto t = ax.compute_x_ticks();
    ASSERT_GE(t.positions.size(), 2u);
    size_t widest = 0;
    for (const auto& l : t.labels)
        widest = std::max(widest, l.size());
    const double px = tick_step(t) * 300.0 / 123456.0;
    EXPECT_GE(px, static_cast<double>(widest) * TICK_LABEL_CHAR_PX);
}

TEST(TickDensity, NoViewportFallsBackToModerateCount)
{
    Axes ax;
    ax.xlim(0.0, 10.0);
    const auto t = ax.compute_x_ticks();
    EXPECT_GE(t.positions.size(), 4u);
    EXPECT_LE(t.positions.size(), 11u);
}

// --- Category axes: box, violin and bar series ---

TEST(CategoryTicks, ViolinFiveGroupsLabelsEveryGroup)
{
    Axes                     ax;
    const std::vector<float> v  = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 3.0f, 2.5f};
    auto&                    vi = ax.violin();
    for (int g = 1; g <= 5; ++g)
        vi.add_violin(static_cast<float>(g), v);
    ax.xlim(0.4, 5.6);
    with_viewport(ax, 330.0f, 300.0f);
    const auto t = ax.compute_x_ticks();
    ASSERT_EQ(t.positions.size(), 5u);
    for (int g = 1; g <= 5; ++g)
    {
        EXPECT_DOUBLE_EQ(t.positions[g - 1], static_cast<double>(g));
        EXPECT_EQ(t.labels[g - 1], std::to_string(g));
    }
}

TEST(CategoryTicks, BoxPlotTicksAtIntegerPositions)
{
    Axes                     ax;
    const std::vector<float> v = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    ax.box_plot().add_box(1, v).add_box(2, v).add_box(3, v).add_box(4, v);
    ax.xlim(0.3, 4.7);
    with_viewport(ax, 500.0f, 400.0f);
    const auto t = ax.compute_x_ticks();
    ASSERT_EQ(t.positions.size(), 4u);
    EXPECT_EQ(t.labels.front(), "1");
    EXPECT_EQ(t.labels.back(), "4");
}

TEST(CategoryTicks, HorizontalBarsCategoriesOnY)
{
    Axes                     ax;
    const std::vector<float> p = {1, 2, 3, 4, 5, 6};
    const std::vector<float> h = {3, 7, 2, 9, 4, 6};
    ax.bar(p, h).orientation(BarOrientation::Horizontal);
    ax.ylim(0.4, 6.6);
    ax.xlim(0.0, 10.0);
    with_viewport(ax, 300.0f, 500.0f);
    const auto t = ax.compute_y_ticks();
    ASSERT_EQ(t.positions.size(), 6u);
    for (size_t i = 0; i < 6; ++i)
    {
        EXPECT_DOUBLE_EQ(t.positions[i], static_cast<double>(i + 1));
    }
}

TEST(CategoryTicks, GroupedBarsGetIntegerTicks)
{
    Axes                     ax;
    const std::vector<float> p1 = {0.8f, 1.8f, 2.8f, 3.8f, 4.8f, 5.8f};
    const std::vector<float> p2 = {1.2f, 2.2f, 3.2f, 4.2f, 5.2f, 6.2f};
    const std::vector<float> h  = {3, 7, 2, 9, 4, 6};
    ax.bar(p1, h).bar_width(0.4f);
    ax.bar(p2, h).bar_width(0.4f);
    ax.xlim(0.4, 6.6);
    with_viewport(ax, 300.0f, 500.0f);
    const auto t = ax.compute_x_ticks();
    ASSERT_GE(t.positions.size(), 3u);
    for (double v : t.positions)
    {
        EXPECT_DOUBLE_EQ(v, std::round(v));
    }
    EXPECT_DOUBLE_EQ(tick_step(t), 1.0);
}

TEST(CategoryTicks, CrowdedCategoriesAreThinned)
{
    Axes               ax;
    std::vector<float> p(40);
    std::vector<float> h(40, 1.0f);
    for (int i = 0; i < 40; ++i)
        p[i] = static_cast<float>(i);
    ax.bar(p, h);
    ax.xlim(-0.5, 39.5);
    with_viewport(ax, 300.0f, 300.0f);
    const auto t = ax.compute_x_ticks();
    ASSERT_GE(t.positions.size(), 2u);
    EXPECT_LT(t.positions.size(), 40u);
    for (double v : t.positions)
    {
        EXPECT_DOUBLE_EQ(v, std::round(v));
    }
}

// --- Label formatting (P1-7) ---

TEST(TickFormatting, OnePrecisionPerAxis)
{
    Axes ax;
    ax.ylim(0.0, 1.0);
    with_viewport(ax, 400.0f, 200.0f);
    const auto t = ax.compute_y_ticks();
    ASSERT_GE(t.labels.size(), 3u);
    const size_t d = decimals_of(t.labels.front());
    for (const auto& l : t.labels)
    {
        EXPECT_EQ(decimals_of(l), d) << l;
    }
    EXPECT_EQ(t.labels.front(), d == 1 ? "0.0" : "0.00");
    EXPECT_TRUE(t.offset_label.empty());
}

TEST(TickFormatting, TinyValuesUseMultiplier)
{
    Axes ax;
    ax.ylim(-3.3e-6, 3.3e-6);
    with_viewport(ax, 600.0f, 300.0f);
    const auto t = ax.compute_y_ticks();
    EXPECT_EQ(t.offset_label, "×10⁻⁶");
    for (const auto& l : t.labels)
    {
        EXPECT_EQ(l.find('e'), std::string::npos) << l;
        EXPECT_LE(l.size(), 4u) << l;
    }
}

TEST(TickFormatting, LargeOffsetMovesToCorner)
{
    Axes ax;
    ax.ylim(1.0e7 - 3.3, 1.0e7 + 3.3);
    with_viewport(ax, 600.0f, 300.0f);
    const auto t = ax.compute_y_ticks();
    EXPECT_EQ(t.offset_label, "+10⁷");
    for (size_t i = 0; i < t.labels.size(); ++i)
    {
        EXPECT_LE(t.labels[i].size(), 3u) << t.labels[i];
        EXPECT_NEAR(std::stod(t.labels[i]) + 1.0e7, t.positions[i], 1e-6);
    }
}

TEST(TickFormatting, EpochSecondsGetOffset)
{
    Axes ax;
    ax.xlim(1700000000.0 - 4320.0, 1700086400.0 + 4320.0);
    with_viewport(ax, 560.0f, 300.0f);
    const auto t = ax.compute_x_ticks();
    EXPECT_EQ(t.offset_label, "+1.7×10⁹");
    ASSERT_GE(t.labels.size(), 3u);
    for (size_t i = 0; i < t.labels.size(); ++i)
    {
        EXPECT_LE(t.labels[i].size(), 6u) << t.labels[i];
        EXPECT_NEAR(std::stod(t.labels[i]) + 1.7e9, t.positions[i], 1e-3);
    }
}

TEST(TickFormatting, FormatterRoundTripsAndNeverPrintsNegativeZero)
{
    TickFormat f;
    f.decimals = 1;
    EXPECT_EQ(format_tick_label(-1e-17, f), "0.0");
    EXPECT_EQ(format_tick_label(0.5, f), "0.5");
    EXPECT_EQ(format_tick_label(1.0, f), "1.0");

    const std::vector<double> pos = {0.0, 0.25, 0.5, 0.75, 1.0};
    const TickFormat          q   = choose_tick_format(pos, 0.25, 0.0, 1.0);
    EXPECT_EQ(q.decimals, 2);
    EXPECT_EQ(q.exponent, 0);
    EXPECT_EQ(q.offset, 0.0);
    EXPECT_EQ(format_tick_label(0.75, q), "0.75");

    const std::vector<double> big = {0.0, 2e6, 4e6};
    const TickFormat          b   = choose_tick_format(big, 2e6, 0.0, 4e6);
    EXPECT_EQ(b.exponent, 6);
    EXPECT_EQ(format_tick_label(4e6, b), "4");
    EXPECT_EQ(format_tick_offset(b), "×10⁶");

    // Plain values in the normal range need no corner label.
    const std::vector<double> mid = {1000.0, 1002.0, 1004.0};
    EXPECT_EQ(format_tick_offset(choose_tick_format(mid, 2.0, 999.0, 1005.0)), "");
}

TEST(TickFormatting, LabelsMatchPositionsWithoutOffset)
{
    Axes ax;
    ax.xlim(-37.0, 112.0);
    with_viewport(ax, 900.0f, 300.0f);
    const auto t = ax.compute_x_ticks();
    EXPECT_TRUE(t.offset_label.empty());
    for (size_t i = 0; i < t.labels.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(std::stod(t.labels[i]), t.positions[i]);
    }
}

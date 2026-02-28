#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/axes.hpp>

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
    Axes ax;
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

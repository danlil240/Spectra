#include <gtest/gtest.h>

#include <plotix/axes.hpp>

#include <cmath>

using namespace plotix;

// Helper: create axes with a single line series spanning [xmin,xmax] x [ymin,ymax]
static Axes make_axes(float xmin, float xmax, float ymin, float ymax) {
    Axes ax;
    float xs[] = {xmin, xmax};
    float ys[] = {ymin, ymax};
    ax.line(std::span<const float>(xs, 2), std::span<const float>(ys, 2));
    return ax;
}

// --- Basic tick generation ---

TEST(TickGeneration, PositiveRange) {
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 3u);
    EXPECT_LE(ticks.positions.size(), 15u);
    EXPECT_EQ(ticks.positions.size(), ticks.labels.size());
    // All ticks should be within [0, 10]
    for (float v : ticks.positions) {
        EXPECT_GE(v, -0.1f);
        EXPECT_LE(v, 10.1f);
    }
}

TEST(TickGeneration, NegativeRange) {
    Axes ax;
    ax.xlim(-100.0f, -10.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    for (float v : ticks.positions) {
        EXPECT_GE(v, -101.0f);
        EXPECT_LE(v, -9.0f);
    }
}

TEST(TickGeneration, CrossingZero) {
    Axes ax;
    ax.xlim(-5.0f, 5.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 3u);
    // Should include zero (or very close to it)
    bool has_zero = false;
    for (float v : ticks.positions) {
        if (std::abs(v) < 0.01f) has_zero = true;
    }
    EXPECT_TRUE(has_zero);
}

TEST(TickGeneration, VerySmallRange) {
    Axes ax;
    ax.xlim(1.0f, 1.0f + 1e-12f);
    auto ticks = ax.compute_x_ticks();
    // Should produce at least one tick without crashing
    EXPECT_GE(ticks.positions.size(), 1u);
    EXPECT_EQ(ticks.positions.size(), ticks.labels.size());
}

TEST(TickGeneration, ZeroRange) {
    Axes ax;
    ax.xlim(5.0f, 5.0f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 1u);
}

TEST(TickGeneration, LargeRange) {
    Axes ax;
    ax.xlim(0.0f, 1e6f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 2u);
    EXPECT_LE(ticks.positions.size(), 25u);
}

TEST(TickGeneration, TinyValues) {
    Axes ax;
    ax.xlim(1e-8f, 2e-8f);
    auto ticks = ax.compute_x_ticks();
    EXPECT_GE(ticks.positions.size(), 1u);
}

// --- No "-0" labels ---

TEST(TickGeneration, NoNegativeZeroLabel) {
    Axes ax;
    ax.xlim(-1.0f, 1.0f);
    auto ticks = ax.compute_x_ticks();
    for (const auto& lbl : ticks.labels) {
        EXPECT_NE(lbl, "-0");
    }
}

// --- Y ticks work the same ---

TEST(TickGeneration, YTicksPositive) {
    Axes ax;
    ax.ylim(0.0f, 100.0f);
    auto ticks = ax.compute_y_ticks();
    EXPECT_GE(ticks.positions.size(), 3u);
}

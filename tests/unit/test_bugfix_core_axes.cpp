// Regression tests for core Axes autoscale / tick fixes and workspace limit restore.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "ui/workspace/figure_serializer.hpp"

using namespace spectra;

// ─── Tight autoscale covers all series types ────────────────────────────────

TEST(BugfixCoreAxes, TightAutoscaleIncludesHistogram)
{
    Axes               ax;
    std::vector<float> values = {10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 20.0f};
    ax.histogram(values, 5);
    ax.autoscale_mode(AutoscaleMode::Tight);

    const auto xl = ax.x_limits();
    const auto yl = ax.y_limits();
    // Histogram spans the value range in X (not the [0,1] fallback).
    EXPECT_LE(xl.min, 10.0);
    EXPECT_GE(xl.max, 20.0);
    EXPECT_LT(xl.max - xl.min, 15.0);
    EXPECT_GE(yl.max, 1.0);
}

TEST(BugfixCoreAxes, TightAutoscaleIncludesBarsOutsideLineRange)
{
    Axes               ax;
    std::vector<float> lx = {0.0f, 1.0f};
    std::vector<float> ly = {0.0f, 1.0f};
    ax.line(lx, ly);
    std::vector<float> bx = {5.0f, 6.0f};
    std::vector<float> bh = {10.0f, 20.0f};
    ax.bar(bx, bh);
    ax.autoscale_mode(AutoscaleMode::Tight);

    EXPECT_GE(ax.x_limits().max, 6.0);
    EXPECT_GE(ax.y_limits().max, 20.0);
}

TEST(BugfixCoreAxes, TightHasNoPaddingButPaddedDoes)
{
    Axes               ax;
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {0.0f, 100.0f};
    ax.line(x, y);

    ax.autoscale_mode(AutoscaleMode::Tight);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 10.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 100.0);

    ax.autoscale_mode(AutoscaleMode::Padded);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, -0.5);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 10.5);
}

// ─── Topic auto-zoom ────────────────────────────────────────────────────────

TEST(BugfixCoreAxes, TopicAutoZoomOnManualAxesFitsData)
{
    Axes               ax;
    std::vector<float> x = {100.0f, 200.0f};
    std::vector<float> y = {50.0f, 60.0f};
    ax.line(x, y);
    ax.autoscale_mode(AutoscaleMode::Manual);
    ax.xlim(0.0, 1.0);
    ax.ylim(0.0, 1.0);

    ax.set_topic_auto_zoom(true);
    ax.topic_auto_zoom_tick();

    const auto xl = ax.x_limits();
    const auto yl = ax.y_limits();
    EXPECT_LE(xl.min, 100.0);
    EXPECT_GE(xl.max, 200.0);
    EXPECT_LE(yl.min, 50.0);
    EXPECT_GE(yl.max, 60.0);

    // A second tick with unchanged data keeps auto-zoom on.
    ax.topic_auto_zoom_tick();
    EXPECT_TRUE(ax.topic_auto_zoom());
}

TEST(BugfixCoreAxes, TopicAutoZoomYieldsToPresentedBufferWithoutFreezingY)
{
    Axes               ax;
    std::vector<float> x    = {0.0f, 1.0f, 2.0f};
    std::vector<float> y    = {0.0f, 1.0f, 2.0f};
    auto&              line = ax.line(x, y);

    ax.set_topic_auto_zoom(true);
    ax.topic_auto_zoom_tick();    // installs explicit limits
    ax.presented_buffer(10.0f);   // live follow enabled afterwards

    line.append(3.0f, 100.0f);
    ax.topic_auto_zoom_tick();

    EXPECT_FALSE(ax.topic_auto_zoom());
    // Y must keep autoscaling over the live window, not stay at the stale fit.
    EXPECT_GE(ax.y_limits().max, 100.0);

    line.append(4.0f, 500.0f);
    EXPECT_GE(ax.y_limits().max, 500.0);
}

// ─── Log-scale ticks ────────────────────────────────────────────────────────

TEST(BugfixCoreAxes, LogScalePaddedAutoscaleStaysPositive)
{
    Axes               ax;
    std::vector<float> x = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y = {1.0f, 10.0f, 100.0f, 1000.0f};
    ax.line(x, y);
    ax.yscale(ScaleType::Log10);

    const auto yl = ax.y_limits();
    EXPECT_GT(yl.min, 0.0);
    EXPECT_LE(yl.min, 1.0);
    EXPECT_GE(yl.max, 1000.0);

    const auto ticks = ax.compute_y_ticks();
    ASSERT_EQ(ticks.positions.size(), 4u);
    EXPECT_DOUBLE_EQ(ticks.positions[0], 1.0);
    EXPECT_DOUBLE_EQ(ticks.positions[3], 1000.0);
}

TEST(BugfixCoreAxes, LogScaleTicksWithNonPositiveExplicitLimit)
{
    Axes               ax;
    std::vector<float> x = {1.0f, 10.0f, 100.0f};
    std::vector<float> y = {0.0f, 1.0f, 2.0f};
    ax.line(x, y);
    ax.xscale(ScaleType::Log10);
    ax.xlim(-5.0, 100.0);

    const auto ticks = ax.compute_x_ticks();
    ASSERT_GE(ticks.positions.size(), 3u);
    for (double p : ticks.positions)
        EXPECT_GT(p, 0.0);
}

// ─── Sqrt-scale ticks ───────────────────────────────────────────────────────

TEST(BugfixCoreAxes, SqrtTickLabelsMatchPositions)
{
    Axes ax;
    ax.xscale(ScaleType::Sqrt);
    ax.xlim(0.0, 10.0);

    const auto ticks = ax.compute_x_ticks();
    ASSERT_EQ(ticks.positions.size(), ticks.labels.size());
    ASSERT_GE(ticks.positions.size(), 2u);
    for (size_t i = 0; i < ticks.positions.size(); ++i)
    {
        const double label_value = std::stod(ticks.labels[i]);
        EXPECT_NEAR(label_value, ticks.positions[i], 0.1) << "tick " << i;
        EXPECT_GE(ticks.positions[i], 0.0);
        EXPECT_LE(ticks.positions[i], 10.0 + 1e-9);
    }
}

TEST(BugfixCoreAxes, SqrtTicksNegativeLimitsAreMonotonic)
{
    Axes ax;
    ax.yscale(ScaleType::Sqrt);
    ax.ylim(-4.0, 1.0);

    const auto ticks = ax.compute_y_ticks();
    ASSERT_GE(ticks.positions.size(), 2u);
    for (size_t i = 1; i < ticks.positions.size(); ++i)
        EXPECT_GT(ticks.positions[i], ticks.positions[i - 1]);
    EXPECT_GE(ticks.positions.front(), -4.0 - 1e-9);
    EXPECT_LE(ticks.positions.back(), 1.0 + 1e-9);
}

// ─── Workspace restore keeps saved zoom in auto modes ──────────────────────

class BugfixCoreAxesSerializer : public ::testing::Test
{
   protected:
    std::string tmp_path_;

    void SetUp() override
    {
        tmp_path_ =
            (std::filesystem::temp_directory_path() / "spectra_test_bugfix_core_axes.spectra")
                .string();
    }

    void TearDown() override { std::remove(tmp_path_.c_str()); }
};

TEST_F(BugfixCoreAxesSerializer, PaddedModeRestoresSavedZoom)
{
    Figure src({800, 600});
    auto&  ax = src.subplot(1, 1, 1);

    std::vector<float> x(500), y(500);
    for (int i = 0; i < 500; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i % 7);
    }
    ax.line(x, y);
    ASSERT_EQ(ax.autoscale_mode(), AutoscaleMode::Padded);
    ax.xlim(100.0, 200.0);

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));

    Figure dst({800, 600});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_EQ(dst.axes().size(), 1u);
    const auto& loaded = *dst.axes()[0];
    EXPECT_EQ(loaded.autoscale_mode(), AutoscaleMode::Padded);
    EXPECT_NEAR(loaded.x_limits().min, 100.0, 1e-3);
    EXPECT_NEAR(loaded.x_limits().max, 200.0, 1e-3);
}

TEST_F(BugfixCoreAxesSerializer, PaddedModeWithoutZoomStaysAutoFit)
{
    Figure src({800, 600});
    auto&  ax = src.subplot(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {0.0f, 1.0f, 4.0f};
    ax.line(x, y);

    ASSERT_TRUE(FigureSerializer::save(tmp_path_, src));

    Figure dst({800, 600});
    ASSERT_TRUE(FigureSerializer::load(tmp_path_, dst));
    ASSERT_EQ(dst.axes().size(), 1u);
    auto& loaded = *dst.axes()[0];
    EXPECT_EQ(loaded.autoscale_mode(), AutoscaleMode::Padded);

    // Still auto-fitting: new data extends the limits.
    ASSERT_FALSE(loaded.series().empty());
    auto* line = dynamic_cast<LineSeries*>(loaded.series()[0].get());
    ASSERT_NE(line, nullptr);
    line->append(50.0f, 1.0f);
    EXPECT_GE(loaded.x_limits().max, 50.0);
}

TEST(BugfixCoreAxes, TightPresentedBufferConstantYIsNotDegenerate)
{
    Axes               ax;
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    std::vector<float> y = {3.0f, 3.0f, 3.0f};
    ax.line(x, y);
    ax.autoscale_mode(AutoscaleMode::Tight);
    ax.presented_buffer(10.0f);

    const auto yl = ax.y_limits();
    EXPECT_LT(yl.min, 3.0);
    EXPECT_GT(yl.max, 3.0);
}

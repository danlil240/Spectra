#include <gtest/gtest.h>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "adapters/ros2/plot_series_pruning.hpp"

using namespace spectra;
using namespace spectra::adapters::ros2;

TEST(PlotSeriesPruning, LiveFollowAnchorsToNewestSampleWhenWallClockSkewed)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.presented_buffer(30.0f);
    // Wall-clock scroll edge is near zero while samples use header.stamp ~3600s.
    ax.set_presented_buffer_right_edge(5.0);

    LineSeries series;
    for (int i = 0; i < 50'000; ++i)
        series.append(3600.0f + static_cast<float>(i) * 0.01f, static_cast<float>(i));

    const size_t before = series.point_count();
    ASSERT_EQ(before, 50'000u);

    prune_time_series(series, ax, 20.0, true);

    EXPECT_LT(series.point_count(), before);
    EXPECT_LT(series.point_count(), 10'000u);
    EXPECT_GT(series.x_data().front(), 3600.0f);
}

TEST(PlotSeriesPruning, PausedViewTrimsBothSidesOfFrozenXlim)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.presented_buffer(30.0f);
    ax.xlim(100.0, 130.0);

    LineSeries series;
    for (int i = 0; i < 1000; ++i)
        series.append(static_cast<float>(i), static_cast<float>(i));

    prune_time_series(series, ax, 10.0, true);

    EXPECT_GE(series.x_data().front(), 90.0f);
    EXPECT_LE(series.x_data().back(), 140.0f);
    EXPECT_LT(series.point_count(), 200u);
}

TEST(PlotSeriesPruning, ThreadSafeErasesApplySameFrame)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.presented_buffer(10.0f);
    ax.set_presented_buffer_right_edge(100.0);

    LineSeries series;
    series.set_thread_safe(true);
    for (int i = 0; i < 20'000; ++i)
        series.append(static_cast<float>(i) * 0.01f, static_cast<float>(i));

    prune_time_series(series, ax, 5.0, true);

    EXPECT_LT(series.point_count(), 20'000u);
}

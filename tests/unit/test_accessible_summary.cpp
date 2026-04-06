#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/accessibility/accessible_summary.hpp"

using namespace spectra;

// ─── accessible_figure_summary ───────────────────────────────────────────────

TEST(AccessibleSummary, EmptyFigureProducesOutput)
{
    Figure      fig;
    std::string summary = accessible_figure_summary(fig);
    EXPECT_FALSE(summary.empty());
    // An empty figure still produces a reasonable description
    EXPECT_NE(summary.find("axes"), std::string::npos);
}

TEST(AccessibleSummary, FigureWithAxesContainsAxesCount)
{
    Figure fig;
    fig.subplot(1, 2, 1);
    fig.subplot(1, 2, 2);

    std::string summary = accessible_figure_summary(fig);
    EXPECT_NE(summary.find("2 axes"), std::string::npos);
}

TEST(AccessibleSummary, FigureWithGridDimensionsShown)
{
    Figure fig;
    fig.subplot(2, 2, 1);
    fig.subplot(2, 2, 2);
    fig.subplot(2, 2, 3);
    fig.subplot(2, 2, 4);

    std::string summary = accessible_figure_summary(fig);
    // Grid description should appear
    EXPECT_NE(summary.find("grid"), std::string::npos);
}

TEST(AccessibleSummary, FigureContainsSeriesLabels)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.title("Sensor Data");

    float x[] = {1.0f, 2.0f, 3.0f};
    float y[] = {10.0f, 20.0f, 30.0f};
    ax.line(x, y).label("temperature");

    std::string summary = accessible_figure_summary(fig);
    EXPECT_NE(summary.find("temperature"), std::string::npos);
}

TEST(AccessibleSummary, FigureContainsSeriesCount)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {1.0f, 2.0f};
    float y[] = {1.0f, 2.0f};
    ax.line(x, y).label("s1");
    ax.line(x, y).label("s2");
    ax.line(x, y).label("s3");

    std::string summary = accessible_figure_summary(fig);
    EXPECT_NE(summary.find("3 series"), std::string::npos);
}

// ─── accessible_axes_summary ─────────────────────────────────────────────────

TEST(AccessibleSummary, AxesSummaryContainsIndex)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.title("Temperature");

    std::string summary = accessible_axes_summary(ax, 0);
    EXPECT_NE(summary.find("Axes 1"), std::string::npos);
}

TEST(AccessibleSummary, AxesSummaryContainsTitle)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.title("Temperature vs Time");

    std::string summary = accessible_axes_summary(ax, 0);
    EXPECT_NE(summary.find("Temperature vs Time"), std::string::npos);
}

TEST(AccessibleSummary, AxesSummaryContainsLabels)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlabel("Time (s)");
    ax.ylabel("Temp (°C)");

    std::string summary = accessible_axes_summary(ax, 0);
    EXPECT_NE(summary.find("Time (s)"), std::string::npos);
    EXPECT_NE(summary.find("Y:"), std::string::npos);
}

TEST(AccessibleSummary, AxesSummaryContainsSeriesLabel)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {0.0f, 1.0f};
    float y[] = {0.0f, 1.0f};
    ax.line(x, y).label("my_series");

    std::string summary = accessible_axes_summary(ax, 0);
    EXPECT_NE(summary.find("my_series"), std::string::npos);
}

TEST(AccessibleSummary, AxesSummaryWithAxisLimits)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(-5.0, 5.0);

    SummaryOptions opts;
    opts.include_axis_ranges = true;
    std::string summary      = accessible_axes_summary(ax, 0, opts);
    EXPECT_NE(summary.find("X view"), std::string::npos);
    EXPECT_NE(summary.find("Y view"), std::string::npos);
}

// ─── accessible_series_summary ───────────────────────────────────────────────

TEST(AccessibleSummary, SeriesSummaryContainsTypeAndLabel)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {1.0f, 2.0f, 3.0f};
    float y[] = {4.0f, 5.0f, 6.0f};
    auto& s   = ax.line(x, y);
    s.label("signal");

    const auto& sv      = ax.series();
    std::string summary = accessible_series_summary(*sv[0], 0);
    EXPECT_NE(summary.find("line"), std::string::npos);
    EXPECT_NE(summary.find("signal"), std::string::npos);
}

TEST(AccessibleSummary, SeriesSummaryContainsPointCount)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float y[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    ax.line(x, y).label("pts");

    const auto& sv      = ax.series();
    SummaryOptions opts;
    opts.include_point_count = true;
    std::string summary = accessible_series_summary(*sv[0], 0, opts);
    EXPECT_NE(summary.find("5 points"), std::string::npos);
}

TEST(AccessibleSummary, SeriesSummaryContainsRange)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {0.0f, 1.0f, 2.0f};
    float y[] = {-10.0f, 0.0f, 10.0f};
    ax.line(x, y).label("range_test");

    const auto& sv      = ax.series();
    SummaryOptions opts;
    opts.include_series_ranges = true;
    std::string summary = accessible_series_summary(*sv[0], 0, opts);
    EXPECT_NE(summary.find("Y range"), std::string::npos);
}

TEST(AccessibleSummary, HiddenSeriesIndicatesHidden)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {0.0f};
    float y[] = {0.0f};
    ax.line(x, y).label("hidden").visible(false);

    const auto& sv      = ax.series();
    std::string summary = accessible_series_summary(*sv[0], 0);
    EXPECT_NE(summary.find("hidden"), std::string::npos);
}

// ─── ChunkedLineSeries LoD info ───────────────────────────────────────────────

TEST(AccessibleSummary, ChunkedSeriesIncludesLoDInfo)
{
    Figure fig;
    auto&  ax      = fig.subplot(1, 1, 1);
    auto&  chunked = ax.chunked_line();
    chunked.label("streaming");

    float x[] = {0.0f, 1.0f, 2.0f};
    float y[] = {0.0f, 1.0f, 0.0f};
    chunked.set_data(x, y);

    const auto& sv      = ax.series();
    SummaryOptions opts;
    opts.include_lod_info = true;
    std::string summary = accessible_series_summary(*sv[0], 0, opts);
    EXPECT_NE(summary.find("LoD level"), std::string::npos);
}

// ─── max_series_in_summary option ────────────────────────────────────────────

TEST(AccessibleSummary, MaxSeriesInSummaryLimitsDetail)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);

    float x[] = {0.0f, 1.0f};
    float y[] = {0.0f, 1.0f};
    for (int i = 0; i < 10; ++i)
        ax.line(x, y).label("s" + std::to_string(i));

    SummaryOptions opts;
    opts.max_series_in_summary = 3;
    std::string summary        = accessible_axes_summary(ax, 0, opts);

    // Should mention "and N more"
    EXPECT_NE(summary.find("more"), std::string::npos);
}

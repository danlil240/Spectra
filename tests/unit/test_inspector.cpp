#include <gtest/gtest.h>
#include <plotix/axes.hpp>
#include <plotix/figure.hpp>
#include <plotix/series.hpp>

// SelectionContext is header-only, no ImGui dependency
#include "ui/selection_context.hpp"

using namespace plotix;
using namespace plotix::ui;

// ─── SelectionContext Tests ─────────────────────────────────────────────────

TEST(SelectionContext, DefaultIsNone)
{
    SelectionContext ctx;
    EXPECT_EQ(ctx.type, SelectionType::None);
    EXPECT_EQ(ctx.figure, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
    EXPECT_EQ(ctx.series_index, -1);
}

TEST(SelectionContext, SelectFigure)
{
    Figure fig;
    SelectionContext ctx;
    ctx.select_figure(&fig);

    EXPECT_EQ(ctx.type, SelectionType::Figure);
    EXPECT_EQ(ctx.figure, &fig);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.series, nullptr);
}

TEST(SelectionContext, SelectAxes)
{
    Figure fig;
    auto& ax = fig.subplot(1, 1, 1);
    SelectionContext ctx;
    ctx.select_axes(&fig, &ax, 0);

    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig);
    EXPECT_EQ(ctx.axes, &ax);
    EXPECT_EQ(ctx.axes_index, 0);
    EXPECT_EQ(ctx.series, nullptr);
}

TEST(SelectionContext, SelectSeries)
{
    Figure fig;
    auto& ax = fig.subplot(1, 1, 1);
    float x[] = {1, 2, 3};
    float y[] = {4, 5, 6};
    auto& s = ax.line(x, y).label("test");

    SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s, 0);

    EXPECT_EQ(ctx.type, SelectionType::Series);
    EXPECT_EQ(ctx.figure, &fig);
    EXPECT_EQ(ctx.axes, &ax);
    EXPECT_EQ(ctx.series, &s);
    EXPECT_EQ(ctx.axes_index, 0);
    EXPECT_EQ(ctx.series_index, 0);
}

TEST(SelectionContext, ClearResetsAll)
{
    Figure fig;
    auto& ax = fig.subplot(1, 1, 1);
    float x[] = {1, 2, 3};
    float y[] = {4, 5, 6};
    auto& s = ax.line(x, y);

    SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s, 0);
    EXPECT_EQ(ctx.type, SelectionType::Series);

    ctx.clear();
    EXPECT_EQ(ctx.type, SelectionType::None);
    EXPECT_EQ(ctx.figure, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
    EXPECT_EQ(ctx.series_index, -1);
}

TEST(SelectionContext, SelectFigureClearsPrevious)
{
    Figure fig;
    auto& ax = fig.subplot(1, 1, 1);
    float x[] = {1, 2, 3};
    float y[] = {4, 5, 6};
    auto& s = ax.line(x, y);

    SelectionContext ctx;
    ctx.select_series(&fig, &ax, 0, &s, 0);
    EXPECT_EQ(ctx.type, SelectionType::Series);
    EXPECT_NE(ctx.series, nullptr);

    // Selecting figure should clear series/axes
    ctx.select_figure(&fig);
    EXPECT_EQ(ctx.type, SelectionType::Figure);
    EXPECT_EQ(ctx.series, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
}

TEST(SelectionContext, MultipleAxesSelection)
{
    Figure fig;
    auto& ax0 = fig.subplot(2, 1, 1);
    auto& ax1 = fig.subplot(2, 1, 2);

    SelectionContext ctx;
    ctx.select_axes(&fig, &ax0, 0);
    EXPECT_EQ(ctx.axes, &ax0);
    EXPECT_EQ(ctx.axes_index, 0);

    ctx.select_axes(&fig, &ax1, 1);
    EXPECT_EQ(ctx.axes, &ax1);
    EXPECT_EQ(ctx.axes_index, 1);
}

// ─── SelectionType enum coverage ────────────────────────────────────────────

TEST(SelectionType, AllValuesDistinct)
{
    EXPECT_NE(SelectionType::None, SelectionType::Figure);
    EXPECT_NE(SelectionType::Figure, SelectionType::Axes);
    EXPECT_NE(SelectionType::Axes, SelectionType::Series);
    EXPECT_NE(SelectionType::None, SelectionType::Series);
}

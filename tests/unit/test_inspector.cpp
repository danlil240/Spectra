#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

// SelectionContext is header-only, no ImGui dependency
#include "ui/selection_context.hpp"

using namespace spectra;
using namespace spectra::ui;

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

// ─── Axes Selection Preservation Tests ───────────────────────────────────────

TEST(SelectionContext, PreserveAxesIndexWhenSwitchingFigures)
{
    // Create two figures with different numbers of axes
    Figure fig1;
    Figure fig2;
    
    auto& fig1_ax0 = fig1.subplot(2, 1, 1);  // index 0
    auto& fig1_ax1 = fig1.subplot(2, 1, 2);  // index 1
    
    auto& fig2_ax0 = fig2.subplot(1, 1, 1);  // index 0 (only axes)
    
    SelectionContext ctx;
    
    // Select axes index 1 in fig1
    ctx.select_axes(&fig1, &fig1_ax1, 1);
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig1);
    EXPECT_EQ(ctx.axes, &fig1_ax1);
    EXPECT_EQ(ctx.axes_index, 1);
    
    // Simulate switching to fig2 with same axes index (invalid, should fallback to 0)
    // This mimics the logic in ImGuiIntegration::build_ui
    if (ctx.type == SelectionType::Axes && ctx.figure != &fig2)
    {
        int target_idx = ctx.axes_index;
        if (target_idx >= 0 && target_idx < static_cast<int>(fig2.axes().size()))
        {
            ctx.select_axes(&fig2, fig2.axes_mut()[target_idx].get(), target_idx);
        }
        else
        {
            // Index out of range, fall back to first axes
            ctx.select_axes(&fig2, fig2.axes_mut()[0].get(), 0);
        }
    }
    
    // Should have switched to fig2's axes at index 0 (fallback)
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig2);
    EXPECT_EQ(ctx.axes, &fig2_ax0);
    EXPECT_EQ(ctx.axes_index, 0);
    
    // Now try switching back to fig1 with valid index 0
    if (ctx.type == SelectionType::Axes && ctx.figure != &fig1)
    {
        int target_idx = 0;  // Explicitly test index 0
        if (target_idx >= 0 && target_idx < static_cast<int>(fig1.axes().size()))
        {
            ctx.select_axes(&fig1, fig1.axes_mut()[target_idx].get(), target_idx);
        }
        else
        {
            // Fallback (shouldn't happen in this case)
            ctx.select_axes(&fig1, fig1.axes_mut()[0].get(), 0);
        }
    }
    
    // Should have switched to fig1's axes at index 0
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig1);
    EXPECT_EQ(ctx.axes, &fig1_ax0);
    EXPECT_EQ(ctx.axes_index, 0);
}

TEST(SelectionContext, HandleEmptyFigureWhenSwitching)
{
    Figure fig_with_axes;
    Figure empty_fig;
    
    auto& ax = fig_with_axes.subplot(1, 1, 1);
    
    SelectionContext ctx;
    ctx.select_axes(&fig_with_axes, &ax, 0);
    
    EXPECT_EQ(ctx.type, SelectionType::Axes);
    EXPECT_EQ(ctx.figure, &fig_with_axes);
    
    // Simulate switching to empty figure (should clear selection)
    if (ctx.figure != &empty_fig && empty_fig.axes().empty())
    {
        ctx.clear();
    }
    
    // Selection should be cleared
    EXPECT_EQ(ctx.type, SelectionType::None);
    EXPECT_EQ(ctx.figure, nullptr);
    EXPECT_EQ(ctx.axes, nullptr);
    EXPECT_EQ(ctx.axes_index, -1);
}

// ─── SelectionType enum coverage ────────────────────────────────────────────

TEST(SelectionType, AllValuesDistinct)
{
    EXPECT_NE(SelectionType::None, SelectionType::Figure);
    EXPECT_NE(SelectionType::Figure, SelectionType::Axes);
    EXPECT_NE(SelectionType::Axes, SelectionType::Series);
    EXPECT_NE(SelectionType::None, SelectionType::Series);
}

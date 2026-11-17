// Several bar series on one axes: grouped (side by side), stacked and overlay
// layouts (Axes::bar_mode()), and the sticky baseline of bar autoscale.

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

using namespace spectra;

namespace
{

const std::vector<float> kPos = {0.0f, 1.0f, 2.0f};

// Centre of bar i along the category axis, from its outline (5 points + NaN).
float bar_centre(const BarSeries& b, size_t i)
{
    auto        cat = b.orientation() == BarOrientation::Vertical ? b.x_data() : b.y_data();
    const float lo  = std::min(cat[i * 6], cat[i * 6 + 2]);
    const float hi  = std::max(cat[i * 6], cat[i * 6 + 2]);
    return 0.5f * (lo + hi);
}

float bar_thickness(const BarSeries& b, size_t i)
{
    auto cat = b.orientation() == BarOrientation::Vertical ? b.x_data() : b.y_data();
    return std::abs(cat[i * 6 + 2] - cat[i * 6]);
}

}   // namespace

TEST(BarLayout, DefaultModeIsGrouped)
{
    Axes ax;
    EXPECT_EQ(ax.bar_mode(), BarMode::Grouped);
}

TEST(BarLayout, SingleSeriesKeepsPlainLayout)
{
    Axes  ax;
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3});
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.layout_offset(), 0.0f);
    EXPECT_FLOAT_EQ(a.drawn_width(), 0.8f);
    EXPECT_FLOAT_EQ(bar_centre(a, 1), 1.0f);
    EXPECT_FLOAT_EQ(bar_thickness(a, 1), 0.8f);
}

TEST(BarLayout, GroupedSeriesSitSideBySideCentredOnCategory)
{
    Axes  ax;
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3});
    auto& b = ax.bar(kPos, std::vector<float>{3, 2, 1});
    ax.sync_bar_layout();

    // Two slots of 0.4 inside the 0.8 bar width, 10% gap inside each slot.
    EXPECT_FLOAT_EQ(a.layout_offset(), -0.2f);
    EXPECT_FLOAT_EQ(b.layout_offset(), 0.2f);
    EXPECT_NEAR(a.drawn_width(), 0.36f, 1e-6f);
    EXPECT_NEAR(b.drawn_width(), 0.36f, 1e-6f);
    for (size_t i = 0; i < kPos.size(); ++i)
    {
        EXPECT_NEAR(bar_centre(a, i), kPos[i] - 0.2f, 1e-6f);
        EXPECT_NEAR(bar_centre(b, i), kPos[i] + 0.2f, 1e-6f);
        EXPECT_NEAR(bar_thickness(a, i), 0.36f, 1e-6f);
        // Group centre is the category position.
        EXPECT_NEAR(0.5f * (bar_centre(a, i) + bar_centre(b, i)), kPos[i], 1e-6f);
        // No overlap between neighbours.
        EXPECT_LT(bar_centre(a, i) + 0.18f, bar_centre(b, i) - 0.18f);
    }
    // Values are untouched.
    EXPECT_FLOAT_EQ(a.bar_top(2), 3.0f);
    EXPECT_FLOAT_EQ(a.bar_base(2), 0.0f);
}

TEST(BarLayout, GroupedThreeSeriesUseWidestBarWidth)
{
    Axes  ax;
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3}).bar_width(0.6f);
    auto& b = ax.bar(kPos, std::vector<float>{1, 2, 3}).bar_width(0.9f);
    auto& c = ax.bar(kPos, std::vector<float>{1, 2, 3});
    ax.sync_bar_layout();
    EXPECT_NEAR(a.layout_offset(), -0.3f, 1e-6f);
    EXPECT_NEAR(b.layout_offset(), 0.0f, 1e-6f);
    EXPECT_NEAR(c.layout_offset(), 0.3f, 1e-6f);
    EXPECT_NEAR(c.drawn_width(), 0.27f, 1e-6f);
}

TEST(BarLayout, SeriesWithoutSharedPositionsAreNotRegrouped)
{
    // The manual idiom: bars placed at x -/+ 0.2 with width 0.4.
    Axes  ax;
    auto& a = ax.bar(std::vector<float>{0.8f, 1.8f}, std::vector<float>{1, 2}).bar_width(0.4f);
    auto& b = ax.bar(std::vector<float>{1.2f, 2.2f}, std::vector<float>{2, 1}).bar_width(0.4f);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.layout_offset(), 0.0f);
    EXPECT_FLOAT_EQ(b.layout_offset(), 0.0f);
    EXPECT_FLOAT_EQ(a.drawn_width(), 0.4f);
}

TEST(BarLayout, HiddenSeriesLeaveTheGroup)
{
    Axes  ax;
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3});
    auto& b = ax.bar(kPos, std::vector<float>{3, 2, 1});
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.layout_offset(), -0.2f);
    b.visible(false);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.layout_offset(), 0.0f);
    EXPECT_FLOAT_EQ(a.drawn_width(), 0.8f);
}

TEST(BarLayout, OverlayKeepsOldBehaviour)
{
    Axes ax;
    ax.bar_mode(BarMode::Overlay);
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3});
    auto& b = ax.bar(kPos, std::vector<float>{3, 2, 1});
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(bar_centre(a, 0), 0.0f);
    EXPECT_FLOAT_EQ(bar_centre(b, 0), 0.0f);
    EXPECT_FLOAT_EQ(bar_thickness(b, 0), 0.8f);
    EXPECT_FLOAT_EQ(b.bar_base(0), 0.0f);
    EXPECT_FLOAT_EQ(b.bar_top(0), 3.0f);
}

TEST(BarLayout, StackedBasesAccumulatePositivesAndNegativesSeparately)
{
    Axes ax;
    ax.bar_mode(BarMode::Stacked);
    auto& a = ax.bar(kPos, std::vector<float>{2, -1, 3});
    auto& b = ax.bar(kPos, std::vector<float>{4, -2, -5});
    auto& c = ax.bar(kPos, std::vector<float>{-3, 5, 1});
    ax.sync_bar_layout();

    // Category 0: +2, +4 stack up, -3 goes down from 0.
    EXPECT_FLOAT_EQ(a.bar_base(0), 0.0f);
    EXPECT_FLOAT_EQ(a.bar_top(0), 2.0f);
    EXPECT_FLOAT_EQ(b.bar_base(0), 2.0f);
    EXPECT_FLOAT_EQ(b.bar_top(0), 6.0f);
    EXPECT_FLOAT_EQ(c.bar_base(0), 0.0f);
    EXPECT_FLOAT_EQ(c.bar_top(0), -3.0f);

    // Category 1: -1, -2 stack down, +5 starts at 0.
    EXPECT_FLOAT_EQ(a.bar_top(1), -1.0f);
    EXPECT_FLOAT_EQ(b.bar_base(1), -1.0f);
    EXPECT_FLOAT_EQ(b.bar_top(1), -3.0f);
    EXPECT_FLOAT_EQ(c.bar_base(1), 0.0f);
    EXPECT_FLOAT_EQ(c.bar_top(1), 5.0f);

    // Category 2: +3, -5, +1.
    EXPECT_FLOAT_EQ(b.bar_base(2), 0.0f);
    EXPECT_FLOAT_EQ(b.bar_top(2), -5.0f);
    EXPECT_FLOAT_EQ(c.bar_base(2), 3.0f);
    EXPECT_FLOAT_EQ(c.bar_top(2), 4.0f);

    // Each bar keeps its own value (hover / data tips report it).
    EXPECT_FLOAT_EQ(c.bar_heights()[2], 1.0f);
    // Stacked bars stay full width and centred.
    EXPECT_FLOAT_EQ(bar_centre(b, 1), 1.0f);
    EXPECT_FLOAT_EQ(bar_thickness(b, 1), 0.8f);

    // Autoscale covers the stack (-5 .. 6).
    const auto yl = ax.y_limits();
    EXPECT_LT(yl.min, -5.0);
    EXPECT_GT(yl.max, 6.0);
}

TEST(BarLayout, StackedFollowsDataChanges)
{
    Axes ax;
    ax.bar_mode(BarMode::Stacked);
    auto& a = ax.bar(kPos, std::vector<float>{1, 1, 1});
    auto& b = ax.bar(kPos, std::vector<float>{1, 1, 1});
    EXPECT_NEAR(ax.y_limits().max, 2.1, 1e-6);
    a.set_data(kPos, std::vector<float>{5, 5, 5});
    EXPECT_NEAR(ax.y_limits().max, 6.3, 1e-6);
    EXPECT_FLOAT_EQ(b.bar_base(0), 5.0f);
    // Back to grouped: plain bases again.
    ax.bar_mode(BarMode::Grouped);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(b.bar_base(0), 0.0f);
    EXPECT_FLOAT_EQ(b.bar_top(0), 1.0f);
}

TEST(BarLayout, HorizontalBarsGroupAndStackAlongY)
{
    Axes  ax;
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3}).orientation(BarOrientation::Horizontal);
    auto& b = ax.bar(kPos, std::vector<float>{3, 2, 1}).orientation(BarOrientation::Horizontal);
    ax.sync_bar_layout();
    EXPECT_NEAR(bar_centre(a, 1), 0.8f, 1e-6f);
    EXPECT_NEAR(bar_centre(b, 1), 1.2f, 1e-6f);

    ax.bar_mode(BarMode::Stacked);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(b.bar_base(0), 1.0f);
    EXPECT_FLOAT_EQ(b.bar_top(0), 4.0f);
    // Values run along x and stick to 0 on the left.
    const auto xl = ax.x_limits();
    EXPECT_DOUBLE_EQ(xl.min, 0.0);
    EXPECT_GT(xl.max, 4.0);
}

TEST(BarLayout, VerticalAndHorizontalSeriesDoNotGroupTogether)
{
    Axes  ax;
    auto& a = ax.bar(kPos, std::vector<float>{1, 2, 3});
    auto& b = ax.bar(kPos, std::vector<float>{3, 2, 1}).orientation(BarOrientation::Horizontal);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.layout_offset(), 0.0f);
    EXPECT_FLOAT_EQ(b.layout_offset(), 0.0f);
}

TEST(BarLayout, StackingMatchesLogicalPositionsWithOffsets)
{
    // Same stored positions, different x offsets: different categories.
    Axes ax;
    ax.bar_mode(BarMode::Stacked);
    auto& a = ax.bar(std::vector<float>{0.0f}, std::vector<float>{2.0f});
    auto& b = ax.bar(std::vector<float>{0.0f}, std::vector<float>{3.0f});
    b.x_offset(10.0);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.bar_top(0), 2.0f);
    EXPECT_FLOAT_EQ(b.bar_base(0), 0.0f);
    EXPECT_FLOAT_EQ(b.bar_top(0), 3.0f);

    // Different stored positions, same logical category: they stack.
    Axes  ax2;
    auto& c = ax2.bar(std::vector<float>{10.0f}, std::vector<float>{2.0f});
    auto& d = ax2.bar(std::vector<float>{0.0f}, std::vector<float>{3.0f});
    d.x_offset(10.0);
    ax2.bar_mode(BarMode::Stacked);
    ax2.sync_bar_layout();
    EXPECT_FLOAT_EQ(c.bar_base(0), 0.0f);
    EXPECT_FLOAT_EQ(d.bar_base(0), 2.0f);
    EXPECT_FLOAT_EQ(d.bar_top(0), 5.0f);

    // A value offset is honoured too: stored bases are relative to it.
    Axes  ax3;
    auto& e = ax3.bar(std::vector<float>{0.0f}, std::vector<float>{2.0f});
    auto& f = ax3.bar(std::vector<float>{0.0f}, std::vector<float>{3.0f});
    f.y_offset(100.0);
    ax3.bar_mode(BarMode::Stacked);
    ax3.sync_bar_layout();
    EXPECT_FLOAT_EQ(e.bar_top(0), 2.0f);
    EXPECT_FLOAT_EQ(f.bar_base(0), 2.0f - 100.0f);   // logical 2
    EXPECT_FLOAT_EQ(f.bar_top(0), 5.0f - 100.0f);    // logical 5
    EXPECT_NEAR(ax3.y_limits().max, 5.25, 1e-6);
}

TEST(BarLayout, GroupingMatchesLogicalPositionsWithOffsets)
{
    Axes  ax;
    auto& a = ax.bar(std::vector<float>{0.0f}, std::vector<float>{2.0f});
    auto& b = ax.bar(std::vector<float>{0.0f}, std::vector<float>{3.0f});
    b.x_offset(10.0);
    ax.sync_bar_layout();
    EXPECT_FLOAT_EQ(a.layout_offset(), 0.0f);   // not sharing a category
    EXPECT_FLOAT_EQ(b.layout_offset(), 0.0f);

    Axes  ax2;
    auto& c = ax2.bar(std::vector<float>{10.0f}, std::vector<float>{2.0f});
    auto& d = ax2.bar(std::vector<float>{0.0f}, std::vector<float>{3.0f});
    d.x_offset(10.0);
    ax2.sync_bar_layout();
    EXPECT_FLOAT_EQ(c.layout_offset(), -0.2f);
    EXPECT_FLOAT_EQ(d.layout_offset(), 0.2f);
}

// ─── Sticky baseline ────────────────────────────────────────────────────────

TEST(BarAutoscale, PositiveBarsSitOnTheAxis)
{
    Axes ax;
    ax.bar(kPos, std::vector<float>{2, 4, 10});
    const auto yl = ax.y_limits();
    EXPECT_DOUBLE_EQ(yl.min, 0.0);    // no margin below the baseline
    EXPECT_DOUBLE_EQ(yl.max, 10.5);   // 5% margin on the other side
    // The category axis keeps its margins.
    const auto xl = ax.x_limits();
    EXPECT_LT(xl.min, -0.4);
    EXPECT_GT(xl.max, 2.4);
}

TEST(BarAutoscale, NegativeBarsHangFromTheAxis)
{
    Axes ax;
    ax.bar(kPos, std::vector<float>{-2, -4, -10});
    const auto yl = ax.y_limits();
    EXPECT_DOUBLE_EQ(yl.max, 0.0);
    EXPECT_DOUBLE_EQ(yl.min, -10.5);
}

TEST(BarAutoscale, MixedSignsKeepBothMargins)
{
    Axes ax;
    ax.bar(kPos, std::vector<float>{-2, 4, 6});
    const auto yl = ax.y_limits();
    EXPECT_LT(yl.min, -2.0);
    EXPECT_GT(yl.max, 6.0);
}

TEST(BarAutoscale, BaselineIsStickyNotZero)
{
    Axes ax;
    ax.bar(kPos, std::vector<float>{3, 4, 5}).baseline(1.0f);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 1.0);
}

TEST(BarAutoscale, OtherDataBelowTheBaselineKeepsTheMargin)
{
    Axes ax;
    ax.bar(kPos, std::vector<float>{2, 4, 10});
    ax.line(std::vector<float>{0, 2}, std::vector<float>{-1, 1});
    EXPECT_LT(ax.y_limits().min, -1.0);
}

TEST(BarAutoscale, TightModeIsUnchanged)
{
    Axes ax;
    ax.autoscale_mode(AutoscaleMode::Tight);
    ax.bar(kPos, std::vector<float>{2, 4, 10});
    const auto yl = ax.y_limits();
    EXPECT_DOUBLE_EQ(yl.min, 0.0);
    EXPECT_DOUBLE_EQ(yl.max, 10.0);
}

TEST(BarAutoscale, AllZeroBarsStillGetARange)
{
    Axes ax;
    ax.bar(kPos, std::vector<float>{0, 0, 0});
    const auto yl = ax.y_limits();
    EXPECT_LT(yl.min, yl.max);
}

// ─── Vector export ──────────────────────────────────────────────────────────

namespace
{
std::string bars_svg(BarMode mode)
{
    Figure fig({.width = 400, .height = 300});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.bar_mode(mode);
    ax.bar(kPos, std::vector<float>{1, 2, 3});
    ax.bar(kPos, std::vector<float>{3, 2, 1});
    // Fixed limits: the export must lay the bars out itself (no autoscale).
    ax.xlim(-1.0, 3.0);
    ax.ylim(0.0, 6.0);
    fig.compute_layout();
    return SvgExporter::to_string(fig);
}
}   // namespace

TEST(BarLayout, SvgExportUsesTheBarMode)
{
    const std::string overlay = bars_svg(BarMode::Overlay);
    const std::string grouped = bars_svg(BarMode::Grouped);
    const std::string stacked = bars_svg(BarMode::Stacked);
    EXPECT_NE(overlay, grouped);
    EXPECT_NE(overlay, stacked);
    EXPECT_NE(grouped, stacked);
}

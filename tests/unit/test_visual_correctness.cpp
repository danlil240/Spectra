// Regression tests for visual-correctness fixes found in the release review:
// 3D auto-limits in headless/offscreen export, marker shape mapping,
// theme-aware 'k'/'w' colors, legend layout/placement, tooltip naming and
// deterministic offline-recording frame timing.

#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "anim/frame_scheduler.hpp"
#include "ui/overlay/legend_layout.hpp"
#include "ui/theme/theme.hpp"
#ifdef SPECTRA_USE_IMGUI
    #include "ui/overlay/tooltip.hpp"
#endif

using namespace spectra;

// ─── 3D auto-limits ─────────────────────────────────────────────────────────

TEST(Axes3DAutoLimits, UnsetLimitsFrameTheData)
{
    // Regression: headless/offscreen 3D export left axes at [0,1]^3, so a
    // scatter over x = 0..4 showed a single point.
    Axes3D             ax;
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {-2, -1, 0, 1, 2};
    std::vector<float> z = {10, 20, 30, 40, 50};
    ax.scatter3d(x, y, z);

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    auto zl = ax.z_limits();
    EXPECT_LE(xl.min, 0.0);
    EXPECT_GE(xl.max, 4.0);
    EXPECT_LE(yl.min, -2.0);
    EXPECT_GE(yl.max, 2.0);
    EXPECT_LE(zl.min, 10.0);
    EXPECT_GE(zl.max, 50.0);
    // ...and not absurdly loose (5% padding)
    EXPECT_GT(xl.min, -0.5);
    EXPECT_LT(xl.max, 4.5);
}

TEST(Axes3DAutoLimits, MatchesExplicitAutoFit)
{
    Axes3D             lazy;
    Axes3D             fitted;
    std::vector<float> x = {0, 4};
    std::vector<float> y = {1, 3};
    std::vector<float> z = {-1, 1};
    lazy.line3d(x, y, z);
    fitted.line3d(x, y, z);
    fitted.auto_fit();

    EXPECT_DOUBLE_EQ(lazy.x_limits().min, fitted.x_limits().min);
    EXPECT_DOUBLE_EQ(lazy.x_limits().max, fitted.x_limits().max);
    EXPECT_DOUBLE_EQ(lazy.y_limits().min, fitted.y_limits().min);
    EXPECT_DOUBLE_EQ(lazy.z_limits().max, fitted.z_limits().max);
}

TEST(Axes3DAutoLimits, ExplicitLimitsWinPerAxis)
{
    Axes3D             ax;
    std::vector<float> v = {0, 4};
    ax.scatter3d(v, v, v);
    ax.xlim(-10.0, 10.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, -10.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 10.0);
    EXPECT_LE(ax.y_limits().min, 0.0);   // still auto
    EXPECT_GE(ax.y_limits().max, 4.0);
}

TEST(Axes3DAutoLimits, EmptyAxesKeepUnitDefault)
{
    Axes3D ax;
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 1.0);
}

TEST(Axes3DAutoLimits, SmallScaleDataIsNotSwampedByAbsolutePadding)
{
    // Bug #26: a 1e-6 absolute threshold padded tiny-extent axes by +-0.5,
    // collapsing micro-scale data to a dot.
    Axes3D             ax;
    std::vector<float> x = {0.0f, 1e-7f, 2e-7f};
    std::vector<float> y = {1e-8f, 2e-8f, 3e-8f};
    std::vector<float> z = {0.0f, 5e-7f, 1e-6f};
    ax.scatter3d(x, y, z);
    ax.auto_fit();

    auto xl = ax.x_limits();
    auto yl = ax.y_limits();
    EXPECT_LE(xl.min, 0.0);
    EXPECT_GE(xl.max, 2e-7);
    EXPECT_LT(xl.max - xl.min, 1e-6);   // framed tightly, not +-0.5
    EXPECT_LT(yl.max - yl.min, 1e-7);
}

TEST(Axes3DAutoLimits, FlatAxisGetsNonZeroRange)
{
    Axes3D             ax;
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {5, 5, 5};   // flat at 5
    std::vector<float> z = {0, 0, 0};   // flat at 0
    ax.scatter3d(x, y, z);
    ax.auto_fit();
    auto yl = ax.y_limits();
    auto zl = ax.z_limits();
    EXPECT_LT(yl.min, 5.0);
    EXPECT_GT(yl.max, 5.0);
    EXPECT_NEAR(yl.max - yl.min, 0.5, 1e-6);   // +-5% of magnitude
    EXPECT_NEAR(zl.min, -0.5, 1e-9);
    EXPECT_NEAR(zl.max, 0.5, 1e-9);
}

// ─── Marker mapping ─────────────────────────────────────────────────────────

TEST(MarkerMapping, EnumValuesMatchShaderSwitch)
{
    // src/gpu/shaders/scatter.frag / scatter3d.frag switch on these values.
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::Star), 5u);
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::TriangleUp), 8u);
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::TriangleDown), 9u);
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::TriangleLeft), 10u);
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::TriangleRight), 11u);
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::Pentagon), 12u);
    EXPECT_EQ(static_cast<uint32_t>(MarkerStyle::FilledTriangleUp), 17u);
}

TEST(MarkerMapping, FormatCharsMapToExpectedShapes)
{
    EXPECT_EQ(parse_format_string("*").marker_style, MarkerStyle::Star);
    EXPECT_EQ(parse_format_string("^").marker_style, MarkerStyle::TriangleUp);
    EXPECT_EQ(parse_format_string("v").marker_style, MarkerStyle::TriangleDown);
    EXPECT_EQ(parse_format_string("<").marker_style, MarkerStyle::TriangleLeft);
    EXPECT_EQ(parse_format_string(">").marker_style, MarkerStyle::TriangleRight);
    EXPECT_EQ(parse_format_string("p").marker_style, MarkerStyle::Pentagon);
}

// ─── Theme-aware 'k' / 'w' ──────────────────────────────────────────────────

namespace
{
ui::ThemeColors theme_with_bg(float bg, float fg)
{
    ui::ThemeColors t{};
    t.bg_canvas    = ui::Color(bg, bg, bg, 1.0f);
    t.text_primary = ui::Color(fg, fg, fg, 1.0f);
    return t;
}
}   // namespace

TEST(ThemeSeriesColor, BlackBecomesForegroundOnDarkTheme)
{
    auto  dark = theme_with_bg(0.05f, 0.9f);
    Color k    = colors::black;
    Color out  = ui::adapt_series_color_to_theme(k, dark);
    EXPECT_FLOAT_EQ(out.r, 0.9f);
    EXPECT_FLOAT_EQ(out.g, 0.9f);
    EXPECT_FLOAT_EQ(out.b, 0.9f);
    EXPECT_FLOAT_EQ(out.a, k.a);
}

TEST(ThemeSeriesColor, FormatLetterKIsATunedShadeNotRemapped)
{
    // "k-" resolves to its tuned dark-background shade (P1-5), which already
    // reads on dark canvases, so the foreground fallback leaves it alone.
    auto  dark = theme_with_bg(0.05f, 0.9f);
    Color k    = parse_format_string("k-").color.value();
    Color out  = ui::adapt_series_color_to_theme(k, dark);
    EXPECT_FLOAT_EQ(out.r, format_colors::shade('k').r);
    EXPECT_GT(out.r, 0.75f);
}

TEST(ThemeSeriesColor, BlackStaysBlackOnLightTheme)
{
    auto  light = theme_with_bg(0.98f, 0.1f);
    Color out   = ui::adapt_series_color_to_theme(colors::black, light);
    EXPECT_FLOAT_EQ(out.r, 0.0f);
    EXPECT_FLOAT_EQ(out.g, 0.0f);
    EXPECT_FLOAT_EQ(out.b, 0.0f);
}

TEST(ThemeSeriesColor, WhiteBecomesForegroundOnLightTheme)
{
    auto  light = theme_with_bg(0.98f, 0.1f);
    Color out   = ui::adapt_series_color_to_theme(colors::white, light);
    EXPECT_FLOAT_EQ(out.r, 0.1f);
    Color red = ui::adapt_series_color_to_theme(colors::red, light);
    EXPECT_FLOAT_EQ(red.r, colors::red.r);
    EXPECT_FLOAT_EQ(red.g, colors::red.g);
}

TEST(ThemeSeriesColor, NearBlackIsLeftAlone)
{
    auto  dark = theme_with_bg(0.05f, 0.9f);
    Color c(0.07f, 0.07f, 0.07f, 0.5f);
    Color out = ui::adapt_series_color_to_theme(c, dark);
    EXPECT_FLOAT_EQ(out.r, 0.07f);
    EXPECT_FLOAT_EQ(out.a, 0.5f);
}

// ─── Legend layout ──────────────────────────────────────────────────────────

TEST(LegendLayout, FewEntriesSingleColumn)
{
    auto g = legend_layout::compute_grid(5, 120.0f, 22.0f, 10.0f, 8.0f, 14.0f, 800.0f, 400.0f);
    EXPECT_EQ(g.cols, 1);
    EXPECT_EQ(g.rows, 5);
    EXPECT_FALSE(g.scroll);
    EXPECT_FLOAT_EQ(g.height, 16.0f + 5 * 22.0f);
}

TEST(LegendLayout, ManyEntriesFlowIntoColumnsInsteadOfTruncating)
{
    // plot_styles_demo: 17 marker entries used to show only 8.
    auto g = legend_layout::compute_grid(17, 120.0f, 22.0f, 10.0f, 8.0f, 14.0f, 800.0f, 380.0f);
    EXPECT_GE(g.cols, 2);
    EXPECT_GE(g.rows * g.cols, 17);
    EXPECT_FALSE(g.scroll);
    EXPECT_EQ(g.visible_rows, g.rows);
    EXPECT_LE(g.width, 800.0f);
    EXPECT_LE(g.height, 380.0f);
}

TEST(LegendLayout, ScrollsOnlyWhenNothingElseFits)
{
    // Narrow and short viewport: one column, more entries than rows that fit.
    auto g = legend_layout::compute_grid(40, 200.0f, 22.0f, 10.0f, 8.0f, 14.0f, 230.0f, 200.0f);
    EXPECT_EQ(g.cols, 1);
    EXPECT_EQ(g.rows, 40);
    EXPECT_TRUE(g.scroll);
    EXPECT_LT(g.visible_rows, 40);
}

TEST(LegendLayout, BestCornerAvoidsData)
{
    Axes ax;
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    // Data crowded into the top-right corner.
    std::vector<float> x, y;
    for (int i = 0; i < 50; ++i)
    {
        x.push_back(8.0f + 0.04f * static_cast<float>(i));
        y.push_back(8.0f + 0.04f * static_cast<float>(i % 7));
    }
    ax.scatter(x, y);

    Rect vp{0.0f, 0.0f, 500.0f, 400.0f};
    auto pos = legend_layout::choose_best_corner(ax, vp, 120.0f, 80.0f, 12.0f);
    EXPECT_NE(pos, LegendPosition::TopRight);
    EXPECT_EQ(legend_layout::overlap_score(ax, vp, pos, 120.0f, 80.0f, 12.0f), 0u);
    EXPECT_GT(legend_layout::overlap_score(ax, vp, LegendPosition::TopRight, 120.0f, 80.0f, 12.0f),
              0u);
}

TEST(LegendLayout, BestCornerAvoidsTallBars)
{
    // stats_demo: the legend sat on top of the tallest (right-most) bar.
    Axes               ax;
    std::vector<float> pos = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<float> h   = {42, 58, 35, 72, 65, 88, 76, 94};
    ax.bar(pos, h).bar_width(0.6f);
    ax.xlim(0.0, 9.0);
    ax.ylim(0.0, 100.0);
    Rect vp{0.0f, 0.0f, 620.0f, 290.0f};
    EXPECT_GT(legend_layout::overlap_score(ax, vp, LegendPosition::TopRight, 140.0f, 40.0f, 12.0f),
              0u);
    EXPECT_EQ(legend_layout::choose_best_corner(ax, vp, 140.0f, 40.0f, 12.0f),
              LegendPosition::TopLeft);
}

TEST(LegendLayout, BestCornerPrefersTopRightWhenAllEmpty)
{
    Axes ax;
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    std::vector<float> x = {5.0f};
    std::vector<float> y = {5.0f};
    ax.scatter(x, y);
    Rect vp{0.0f, 0.0f, 500.0f, 400.0f};
    EXPECT_EQ(legend_layout::choose_best_corner(ax, vp, 100.0f, 60.0f, 12.0f),
              LegendPosition::TopRight);
}

TEST(LegendLayout, DefaultPositionIsBest)
{
    LegendConfig cfg;
    EXPECT_EQ(cfg.position, LegendPosition::Best);
}

// ─── Tooltip naming ─────────────────────────────────────────────────────────

#ifdef SPECTRA_USE_IMGUI
TEST(TooltipName, UnlabeledSeriesGetsIndexName)
{
    LineSeries         s;
    NearestPointResult r;
    r.series       = &s;
    r.series_index = 2;
    EXPECT_EQ(tooltip_series_name(r), "Series 3");
    s.label("velocity");
    EXPECT_EQ(tooltip_series_name(r), "velocity");
}
#endif

// ─── Offline recording clock ────────────────────────────────────────────────

TEST(FrameSchedulerOffline, AdvancesExactlyOneFramePerTick)
{
    FrameScheduler sched(60.0f, FrameScheduler::Mode::Offline);
    sched.begin_frame();
    sched.end_frame();
    EXPECT_FLOAT_EQ(sched.elapsed_seconds(), 0.0f);
    for (int i = 1; i <= 600; ++i)
    {
        sched.begin_frame();
        sched.end_frame();
        EXPECT_FLOAT_EQ(sched.dt(), 1.0f / 60.0f);
    }
    // 601 frames rendered -> last frame is at t = 600/60 = 10 s exactly,
    // regardless of how fast (or slow) the frames were produced.
    EXPECT_NEAR(sched.elapsed_seconds(), 10.0f, 1e-4f);
    EXPECT_EQ(sched.frame_number(), 600u);
}

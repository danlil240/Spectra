#include <gtest/gtest.h>

#include <cstdio>

#include "ui/layout/layout_manager.hpp"
#include "ui/theme/design_tokens.hpp"

using namespace spectra;

// ─── Basic Zone Computation ─────────────────────────────────────────────────

TEST(LayoutManager, DefaultZonesAt1280x720)
{
    LayoutManager lm;
    lm.update(1280.0f, 720.0f);

    // Command bar spans full width at top
    auto cb = lm.command_bar_rect();
    EXPECT_FLOAT_EQ(cb.x, 0.0f);
    EXPECT_FLOAT_EQ(cb.y, 0.0f);
    EXPECT_FLOAT_EQ(cb.w, 1280.0f);
    EXPECT_FLOAT_EQ(cb.h, LayoutManager::COMMAND_BAR_HEIGHT);

    // Status bar spans full width at bottom
    auto sb = lm.status_bar_rect();
    EXPECT_FLOAT_EQ(sb.x, 0.0f);
    EXPECT_FLOAT_EQ(sb.y, 720.0f - LayoutManager::STATUS_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(sb.w, 1280.0f);
    EXPECT_FLOAT_EQ(sb.h, LayoutManager::STATUS_BAR_HEIGHT);

    // Nav rail: collapsed by default (48px)
    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.x, 0.0f);
    EXPECT_FLOAT_EQ(nr.y, LayoutManager::COMMAND_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(nr.w, LayoutManager::NAV_RAIL_COLLAPSED_WIDTH);
    float content_h = 720.0f - LayoutManager::COMMAND_BAR_HEIGHT - LayoutManager::STATUS_BAR_HEIGHT;
    EXPECT_FLOAT_EQ(nr.h, content_h);

    // Inspector: hidden by default (width 0)
    EXPECT_FALSE(lm.is_inspector_visible());
    auto insp = lm.inspector_rect();
    EXPECT_FLOAT_EQ(insp.w, 0.0f);

    // Canvas starts after nav toolbar inset
    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.x, LayoutManager::NAV_TOOLBAR_INSET);
    EXPECT_FLOAT_EQ(cv.y, LayoutManager::COMMAND_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(cv.w, 1280.0f - LayoutManager::NAV_TOOLBAR_INSET);
    EXPECT_FLOAT_EQ(cv.h, content_h);
}

// ─── Inspector Visibility ───────────────────────────────────────────────────

TEST(LayoutManager, InspectorOpenReducesCanvas)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f);   // dt=0 snaps instantly

    auto insp = lm.inspector_rect();
    EXPECT_GT(insp.w, 0.0f);
    EXPECT_FLOAT_EQ(insp.w, LayoutManager::INSPECTOR_DEFAULT_WIDTH);

    auto  cv = lm.canvas_rect();
    float expected_canvas_w =
        1280.0f - LayoutManager::NAV_TOOLBAR_INSET - LayoutManager::INSPECTOR_DEFAULT_WIDTH;
    EXPECT_FLOAT_EQ(cv.w, expected_canvas_w);

    // Inspector is on the right edge
    EXPECT_FLOAT_EQ(insp.x + insp.w, 1280.0f);
}

TEST(LayoutManager, InspectorCloseExpandsCanvas)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f);   // snap open

    lm.set_inspector_visible(false);
    lm.update(1280.0f, 720.0f);   // snap closed (dt=0)

    auto  cv         = lm.canvas_rect();
    float expected_w = 1280.0f - LayoutManager::NAV_TOOLBAR_INSET;
    EXPECT_FLOAT_EQ(cv.w, expected_w);
}

// ─── Inspector Resize ───────────────────────────────────────────────────────

TEST(LayoutManager, InspectorWidthClamped)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);

    // Below minimum
    lm.set_inspector_width(100.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_MIN_WIDTH);

    // Above maximum
    lm.set_inspector_width(1000.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_MAX_WIDTH);

    // Within range
    lm.set_inspector_width(350.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), 350.0f);
}

TEST(LayoutManager, InspectorResizeActiveSnaps)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f);   // snap open

    // Simulate active drag: animated width should snap immediately
    lm.set_inspector_resize_active(true);
    lm.set_inspector_width(400.0f);

    EXPECT_FLOAT_EQ(lm.inspector_animated_width(), 400.0f);
}

TEST(LayoutManager, ResetInspectorWidth)
{
    LayoutManager lm;
    lm.set_inspector_width(400.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), 400.0f);

    lm.reset_inspector_width();
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_DEFAULT_WIDTH);
}

// ─── Nav Rail Expand/Collapse ───────────────────────────────────────────────

TEST(LayoutManager, NavRailExpandedWidth)
{
    LayoutManager lm;
    EXPECT_FALSE(lm.is_nav_rail_expanded());
    EXPECT_FLOAT_EQ(lm.nav_rail_width(), LayoutManager::NAV_RAIL_COLLAPSED_WIDTH);

    lm.set_nav_rail_expanded(true);
    lm.update(1280.0f, 720.0f);   // snap (dt=0)

    EXPECT_TRUE(lm.is_nav_rail_expanded());
    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.w, LayoutManager::NAV_RAIL_EXPANDED_WIDTH);

    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.x, LayoutManager::NAV_RAIL_EXPANDED_WIDTH);
    EXPECT_FLOAT_EQ(cv.w, 1280.0f - LayoutManager::NAV_RAIL_EXPANDED_WIDTH);
}

TEST(LayoutManager, NavRailHiddenExpandsCanvas)
{
    LayoutManager lm;
    lm.set_nav_rail_visible(false);
    lm.update(1280.0f, 720.0f);

    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.x, 0.0f);
    EXPECT_FLOAT_EQ(cv.w, 1280.0f);

    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.w, 0.0f);
}

TEST(LayoutManager, NavRailCustomWidth)
{
    LayoutManager lm;
    lm.set_nav_rail_width(180.0f);
    lm.set_nav_rail_expanded(true);
    lm.update(1280.0f, 720.0f);

    auto nr = lm.nav_rail_rect();
    EXPECT_FLOAT_EQ(nr.w, 180.0f);
}

// ─── Tab Bar ────────────────────────────────────────────────────────────────

TEST(LayoutManager, TabBarHiddenByDefault)
{
    LayoutManager lm;
    lm.update(1280.0f, 720.0f);

    EXPECT_FALSE(lm.is_tab_bar_visible());
    auto tb = lm.tab_bar_rect();
    EXPECT_FLOAT_EQ(tb.w, 0.0f);
    EXPECT_FLOAT_EQ(tb.h, 0.0f);
}

TEST(LayoutManager, TabBarVisibleOffsetsCanvas)
{
    LayoutManager lm;
    lm.set_tab_bar_visible(true);
    lm.update(1280.0f, 720.0f);

    auto tb = lm.tab_bar_rect();
    EXPECT_GT(tb.w, 0.0f);
    EXPECT_FLOAT_EQ(tb.h, LayoutManager::TAB_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(tb.y, LayoutManager::COMMAND_BAR_HEIGHT);

    // Canvas should be pushed down by tab bar height
    auto cv = lm.canvas_rect();
    EXPECT_FLOAT_EQ(cv.y, LayoutManager::COMMAND_BAR_HEIGHT + LayoutManager::TAB_BAR_HEIGHT);

    float content_h = 720.0f - LayoutManager::COMMAND_BAR_HEIGHT - LayoutManager::STATUS_BAR_HEIGHT
                      - LayoutManager::TAB_BAR_HEIGHT;
    EXPECT_FLOAT_EQ(cv.h, content_h);
}

// ─── Animation ──────────────────────────────────────────────────────────────

TEST(LayoutManager, AnimationProgressesWithDt)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);

    // First update with dt=0 snaps
    lm.update(1280.0f, 720.0f, 0.0f);
    EXPECT_FLOAT_EQ(lm.inspector_animated_width(), LayoutManager::INSPECTOR_DEFAULT_WIDTH);

    // Now close and animate with small dt steps
    lm.set_inspector_visible(false);
    lm.update(1280.0f, 720.0f, 0.016f);   // ~1 frame

    // Should be animating (not yet at target)
    float anim_w = lm.inspector_animated_width();
    EXPECT_GT(anim_w, 0.0f);
    EXPECT_LT(anim_w, LayoutManager::INSPECTOR_DEFAULT_WIDTH);
    EXPECT_TRUE(lm.is_animating());
}

TEST(LayoutManager, AnimationConverges)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f, 0.0f);   // snap open

    lm.set_inspector_visible(false);

    // Run many frames — should converge to 0
    for (int i = 0; i < 200; ++i)
    {
        lm.update(1280.0f, 720.0f, 0.016f);
    }

    EXPECT_FLOAT_EQ(lm.inspector_animated_width(), 0.0f);
    EXPECT_FALSE(lm.is_animating());
}

// ─── Window Resize ──────────────────────────────────────────────────────────

TEST(LayoutManager, ZonesAdaptToWindowResize)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1920.0f, 1080.0f);

    auto cb = lm.command_bar_rect();
    EXPECT_FLOAT_EQ(cb.w, 1920.0f);

    auto sb = lm.status_bar_rect();
    EXPECT_FLOAT_EQ(sb.y, 1080.0f - LayoutManager::STATUS_BAR_HEIGHT);
    EXPECT_FLOAT_EQ(sb.w, 1920.0f);

    auto insp = lm.inspector_rect();
    EXPECT_FLOAT_EQ(insp.x + insp.w, 1920.0f);
}

TEST(LayoutManager, SmallWindowClampsToZero)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(100.0f, 100.0f);

    auto cv = lm.canvas_rect();
    EXPECT_GE(cv.w, 0.0f);
    EXPECT_GE(cv.h, 0.0f);
}

// ─── Combined State ─────────────────────────────────────────────────────────

TEST(LayoutManager, AllZonesOpenSimultaneously)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.set_nav_rail_expanded(true);
    lm.set_tab_bar_visible(true);
    lm.update(1920.0f, 1080.0f);

    auto cv   = lm.canvas_rect();
    auto insp = lm.inspector_rect();
    auto tb   = lm.tab_bar_rect();

    const float nav_w = LayoutManager::NAV_RAIL_EXPANDED_WIDTH;
    EXPECT_NEAR(nav_w + cv.w + insp.w, 1920.0f, 1.0f);

    // Tab bar starts at plot grid (canvas + left margin) and is narrower
    EXPECT_FLOAT_EQ(tb.x, nav_w + LayoutManager::PLOT_LEFT_MARGIN);
    EXPECT_FLOAT_EQ(tb.w, cv.w - LayoutManager::PLOT_LEFT_MARGIN);

    EXPECT_FLOAT_EQ(cv.x, nav_w);

    // Canvas starts below tab bar
    EXPECT_FLOAT_EQ(cv.y, LayoutManager::COMMAND_BAR_HEIGHT + LayoutManager::TAB_BAR_HEIGHT);
}

TEST(LayoutManager, NavRailScaleFitsContent)
{
    const float nominal = LayoutManager::nav_rail_nominal_content_height();
    const float min_h   = LayoutManager::nav_rail_min_content_height();

    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_scale_for_height(nominal), 1.0f);
    EXPECT_NEAR(LayoutManager::nav_rail_scale_for_height(nominal * 0.75f), 0.75f, 0.01f);
    const float min_scale =
        LayoutManager::NAV_RAIL_CELL_HEIGHT_MIN / LayoutManager::NAV_RAIL_CELL_HEIGHT;
    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_scale_for_height(min_h), min_scale);
    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_scale_for_height(min_h - 1.0f), min_scale);
}

TEST(LayoutManager, NavRailScaleUsesItemCounts)
{
    const int buttons = LayoutManager::NAV_RAIL_BUTTON_COUNT;
    const int seps    = LayoutManager::NAV_RAIL_SEPARATOR_COUNT;
    EXPECT_FLOAT_EQ(LayoutManager::nav_rail_content_height(buttons, seps, 1.0f),
                    LayoutManager::nav_rail_nominal_content_height());

    const float h = 640.0f;
    EXPECT_LT(
        LayoutManager::nav_rail_scale_for_height(h, buttons, seps),
        LayoutManager::nav_rail_scale_for_height(h,
                                                 LayoutManager::NAV_RAIL_FALLBACK_BUTTON_COUNT,
                                                 seps));
}

// Height draw_nav_rail() / SpectraNavRail actually use: SPACE_3 window padding top and
// bottom, NAV_RAIL_CELL_HEIGHT per button and a SPACE_2 dummy above and below each
// separator hairline, all scaled by the rail scale.
static float drawn_nav_rail_height(int buttons, int seps, float scale)
{
    return 2.0f * ui::tokens::SPACE_3 + buttons * LayoutManager::NAV_RAIL_CELL_HEIGHT * scale
           + seps * 2.0f * ui::tokens::SPACE_2 * scale;
}

// Issue #110: the Qt demo's rail (12 buttons, 4 separators) in a 736 px tall rail was
// drawn at scale 1 and clipped its last button ("Help"), because the layout budgeted
// 9 px per separator while the rail draws 16 px.
TEST(LayoutManager, NavRailScaledContentFitsAvailableHeight)
{
    EXPECT_FLOAT_EQ(LayoutManager::NAV_RAIL_SEPARATOR_HEIGHT, 2.0f * ui::tokens::SPACE_2);
    EXPECT_FLOAT_EQ(LayoutManager::NAV_RAIL_VERTICAL_PADDING, 2.0f * ui::tokens::SPACE_3);

    const int seps = LayoutManager::NAV_RAIL_SEPARATOR_COUNT;
    for (int buttons :
         {LayoutManager::NAV_RAIL_FALLBACK_BUTTON_COUNT, LayoutManager::NAV_RAIL_BUTTON_COUNT})
    {
        const float min_h = LayoutManager::nav_rail_content_height(
            buttons,
            seps,
            LayoutManager::NAV_RAIL_CELL_HEIGHT_MIN / LayoutManager::NAV_RAIL_CELL_HEIGHT);
        for (float avail : {min_h, 600.0f, 700.0f, 736.0f, 760.0f, 900.0f})
        {
            if (avail < min_h)
                continue;
            const float scale = LayoutManager::nav_rail_scale_for_height(avail, buttons, seps);
            EXPECT_LE(drawn_nav_rail_height(buttons, seps, scale), avail + 0.01f)
                << "buttons=" << buttons << " available=" << avail;
        }
    }
}

// Issue #110: the inspector open/close tab sits on the canvas' right edge; subplot
// layout must keep the plot area clear of it.
TEST(LayoutManager, PlotMarginRightClearsInspectorTab)
{
    EXPECT_GT(LayoutManager::INSPECTOR_TAB_CLEARANCE, LayoutManager::INSPECTOR_TAB_WIDTH);
    EXPECT_FLOAT_EQ(LayoutManager::plot_margin_right(20.0f),
                    LayoutManager::INSPECTOR_TAB_CLEARANCE);
    EXPECT_FLOAT_EQ(LayoutManager::plot_margin_right(0.0f), LayoutManager::INSPECTOR_TAB_CLEARANCE);
    // A figure that already asks for more room keeps its own margin.
    EXPECT_FLOAT_EQ(LayoutManager::plot_margin_right(80.0f), 80.0f);
}

TEST(LayoutManager, MinWindowSizeAccountsForNavRail)
{
    const float with_nav    = LayoutManager::min_window_height(true, true, true);
    const float without_nav = LayoutManager::min_window_height(false, true, true);

    EXPECT_GT(with_nav, without_nav);
    EXPECT_GE(with_nav,
              LayoutManager::COMMAND_BAR_HEIGHT + LayoutManager::STATUS_BAR_HEIGHT
                  + LayoutManager::nav_rail_min_content_height());

    // The OS minimum stays rail + minimum canvas; open panels are fitted
    // responsively inside that (see NarrowWindow* tests below).
    EXPECT_FLOAT_EQ(
        LayoutManager::min_window_width(true),
        LayoutManager::NAV_RAIL_COLLAPSED_WIDTH + LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
}

// ─── Responsive fit at narrow window widths ─────────────────────────────────

namespace
{

struct PanelCombo
{
    bool  rail_visible;
    bool  rail_expanded;
    bool  inspector_visible;
    float inspector_width;
};

void expect_layout_usable(const LayoutManager& lm, float win_w, const char* ctx)
{
    const Rect nr = lm.nav_rail_rect();
    const Rect cv = lm.canvas_rect();
    const Rect in = lm.inspector_rect();

    // Every zone is on-screen.
    EXPECT_GE(nr.x, 0.0f) << ctx;
    EXPECT_LE(nr.x + nr.w, win_w + 0.01f) << ctx;
    EXPECT_GE(cv.x, 0.0f) << ctx;
    EXPECT_LE(cv.x + cv.w, win_w + 0.01f) << ctx;
    if (lm.is_inspector_visible() && !lm.is_inspector_auto_hidden())
    {
        EXPECT_GE(in.x, nr.x + nr.w - 0.01f) << ctx << " inspector overlaps rail";
        EXPECT_NEAR(in.x + in.w, win_w, 0.01f) << ctx;
        EXPECT_GE(in.w, LayoutManager::INSPECTOR_MIN_FIT_WIDTH - 0.01f) << ctx;
        // Docked inspector sits right of the canvas.
        EXPECT_NEAR(cv.x + cv.w, in.x, 0.01f) << ctx;
    }
    else
    {
        EXPECT_FLOAT_EQ(in.w, 0.0f) << ctx;
    }

    // The plot keeps at least the minimum canvas width (never squeezed to zero).
    EXPECT_GE(cv.w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH - 0.01f) << ctx;
}

}   // namespace

TEST(LayoutManager, NarrowWindowEveryPanelComboKeepsCanvasAndControlsOnScreen)
{
    const PanelCombo combos[] = {
        {true, false, false, LayoutManager::INSPECTOR_DEFAULT_WIDTH},
        {true, true, false, LayoutManager::INSPECTOR_DEFAULT_WIDTH},
        {true, false, true, LayoutManager::INSPECTOR_DEFAULT_WIDTH},
        {true, true, true, LayoutManager::INSPECTOR_DEFAULT_WIDTH},
        {true, true, true, LayoutManager::INSPECTOR_MAX_WIDTH},
        {false, false, true, LayoutManager::INSPECTOR_DEFAULT_WIDTH},
        {false, false, true, LayoutManager::INSPECTOR_MAX_WIDTH},
    };
    const float min_w = LayoutManager::min_window_width(true);
    for (const PanelCombo& c : combos)
    {
        for (float w = min_w; w <= 1400.0f; w += 8.0f)
        {
            LayoutManager lm;
            lm.set_nav_rail_visible(c.rail_visible);
            lm.set_nav_rail_expanded(c.rail_expanded);
            lm.set_inspector_width(c.inspector_width);
            lm.set_inspector_visible(c.inspector_visible);
            lm.update(w, 600.0f);

            char ctx[160];
            std::snprintf(ctx,
                          sizeof(ctx),
                          "w=%.0f rail=%d expanded=%d inspector=%d insp_w=%.0f",
                          w,
                          c.rail_visible,
                          c.rail_expanded,
                          c.inspector_visible,
                          c.inspector_width);
            expect_layout_usable(lm, w, ctx);

            // Saved preferences are never rewritten by the fit.
            EXPECT_FLOAT_EQ(lm.inspector_width(), c.inspector_width) << ctx;
            EXPECT_EQ(lm.is_nav_rail_expanded(), c.rail_expanded) << ctx;
        }
    }
}

TEST(LayoutManager, NarrowWindow400ClampsDefaultInspectorInsteadOfEightPixelCanvas)
{
    // Audit S11: at 400 px with collapsed rail + default inspector only 8 px of canvas
    // remained. Now the inspector shrinks to 400 - 72 - 160 = 168 px.
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(400.0f, 600.0f);

    EXPECT_FALSE(lm.is_inspector_auto_hidden());
    const Rect cv = lm.canvas_rect();
    const Rect in = lm.inspector_rect();
    EXPECT_FLOAT_EQ(cv.x, LayoutManager::NAV_RAIL_COLLAPSED_WIDTH);
    EXPECT_FLOAT_EQ(cv.w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
    EXPECT_FLOAT_EQ(
        in.x,
        LayoutManager::NAV_RAIL_COLLAPSED_WIDTH + LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
    EXPECT_FLOAT_EQ(in.w, 168.0f);
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_DEFAULT_WIDTH);

    // Same result with an expanded rail: it auto-collapses first.
    lm.set_nav_rail_expanded(true);
    lm.update(400.0f, 600.0f);
    EXPECT_TRUE(lm.is_nav_rail_auto_collapsed());
    EXPECT_FLOAT_EQ(lm.inspector_rect().w, 168.0f);
    EXPECT_FLOAT_EQ(lm.canvas_rect().w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
}

TEST(LayoutManager, NarrowWindowAtMinimumHidesInspectorInsteadOfOffscreen)
{
    // Audit S11: at the 232 px minimum the inspector started at X = -88.
    const float   w = LayoutManager::min_window_width(true);
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(w, 600.0f);

    EXPECT_TRUE(lm.is_inspector_visible());
    EXPECT_TRUE(lm.is_inspector_auto_hidden());
    const Rect in = lm.inspector_rect();
    EXPECT_FLOAT_EQ(in.w, 0.0f);
    EXPECT_FLOAT_EQ(in.x, w);
    EXPECT_FLOAT_EQ(lm.canvas_rect().w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH);

    // Widening brings it back at its saved width.
    lm.update(1280.0f, 600.0f);
    EXPECT_FALSE(lm.is_inspector_auto_hidden());
    EXPECT_FLOAT_EQ(lm.inspector_rect().w, LayoutManager::INSPECTOR_DEFAULT_WIDTH);
}

TEST(LayoutManager, MediumWindowShrinksDockedInspectorToFit)
{
    // 560 - 72 - 160 = 328 available: a 480 px inspector docks at 328 px.
    LayoutManager lm;
    lm.set_inspector_width(LayoutManager::INSPECTOR_MAX_WIDTH);
    lm.set_inspector_visible(true);
    lm.update(560.0f, 600.0f);

    EXPECT_FALSE(lm.is_inspector_auto_hidden());
    EXPECT_FLOAT_EQ(lm.inspector_rect().w, 328.0f);
    EXPECT_FLOAT_EQ(lm.inspector_fitted_width(), 328.0f);
    EXPECT_FLOAT_EQ(lm.canvas_rect().w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
    EXPECT_FLOAT_EQ(lm.inspector_width(), LayoutManager::INSPECTOR_MAX_WIDTH);
    EXPECT_FLOAT_EQ(lm.inspector_open_amount(), 1.0f);

    // Widening restores the saved width.
    lm.update(1280.0f, 600.0f);
    EXPECT_FLOAT_EQ(lm.inspector_rect().w, LayoutManager::INSPECTOR_MAX_WIDTH);
}

TEST(LayoutManager, NarrowWindowAutoCollapsesExpandedRailAndRestoresIt)
{
    LayoutManager lm;
    lm.set_nav_rail_expanded(true);
    lm.update(320.0f, 600.0f);   // 320 < 200 + 160

    EXPECT_TRUE(lm.is_nav_rail_expanded());
    EXPECT_TRUE(lm.is_nav_rail_auto_collapsed());
    EXPECT_FLOAT_EQ(lm.nav_rail_rect().w, LayoutManager::NAV_RAIL_COLLAPSED_WIDTH);
    EXPECT_FALSE(lm.is_animating());

    // With the inspector open the rail needs room for a minimum inspector too.
    lm.set_inspector_visible(true);
    lm.update(560.0f, 600.0f);   // 560 < 200 + 160 + 240
    EXPECT_TRUE(lm.is_nav_rail_auto_collapsed());
    EXPECT_FALSE(lm.is_inspector_auto_hidden());

    lm.update(600.0f, 600.0f);
    EXPECT_FALSE(lm.is_nav_rail_auto_collapsed());
    EXPECT_FLOAT_EQ(lm.nav_rail_rect().w, LayoutManager::NAV_RAIL_EXPANDED_WIDTH);
    EXPECT_FLOAT_EQ(lm.inspector_rect().w, LayoutManager::INSPECTOR_MIN_WIDTH);
    EXPECT_FLOAT_EQ(lm.canvas_rect().w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH);
}

TEST(LayoutManager, NarrowWindowAnimationStaysOnScreen)
{
    LayoutManager lm;
    lm.set_nav_rail_expanded(true);
    lm.update(400.0f, 600.0f);
    lm.set_inspector_visible(true);
    for (int i = 0; i < 60; ++i)
    {
        lm.update(400.0f, 600.0f, 1.0f / 60.0f);
        const Rect in = lm.inspector_rect();
        if (in.w > 0.0f)
        {
            EXPECT_GE(in.x, lm.nav_rail_rect().x + lm.nav_rail_rect().w - 0.01f) << i;
            EXPECT_NEAR(in.x + in.w, 400.0f, 0.01f) << i;
        }
        EXPECT_GE(lm.canvas_rect().w, LayoutManager::WINDOW_MIN_CANVAS_WIDTH - 0.01f) << i;
    }
    EXPECT_FALSE(lm.is_animating());
    EXPECT_FLOAT_EQ(lm.inspector_open_amount(), 1.0f);
}

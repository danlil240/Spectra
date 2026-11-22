// Window title / cascade placement rules and the status-bar zoom extent that
// ignores infinite reference lines.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "ui/app/zoom_level.hpp"
#include "ui/window/window_placement.hpp"

using namespace spectra;

TEST(WindowPlacement, TitleFromFigure)
{
    EXPECT_EQ(window_title_for_figure("Telemetry"), "Telemetry — Spectra");
    EXPECT_EQ(window_title_for_figure("Figure 2"), "Figure 2 — Spectra");
    EXPECT_EQ(window_title_for_figure(""), "Spectra");
}

TEST(WindowPlacement, CascadeOffsetsEachWindow)
{
    const ScreenWorkArea wa{0, 0, 1920, 1080};
    auto                 p0 = cascade_window_position(100, 80, 0, 800, 600, wa);
    auto                 p1 = cascade_window_position(100, 80, 1, 800, 600, wa);
    auto                 p2 = cascade_window_position(100, 80, 2, 800, 600, wa);
    EXPECT_EQ(p0, std::make_pair(100, 80));
    EXPECT_EQ(p1, std::make_pair(100 + WINDOW_CASCADE_STEP, 80 + WINDOW_CASCADE_STEP));
    EXPECT_EQ(p2, std::make_pair(100 + 2 * WINDOW_CASCADE_STEP, 80 + 2 * WINDOW_CASCADE_STEP));
}

TEST(WindowPlacement, CascadeWrapsToWorkArea)
{
    const ScreenWorkArea wa{0, 30, 1280, 800};   // e.g. a top panel
    const int            w = 800;
    const int            h = 600;
    for (int i = 0; i < 40; ++i)
    {
        auto [x, y] = cascade_window_position(0, 30, i, w, h, wa);
        EXPECT_GE(x, wa.x) << i;
        EXPECT_GE(y, wa.y) << i;
        EXPECT_LE(x + w, wa.x + wa.w) << i;
        EXPECT_LE(y + h, wa.y + wa.h) << i;
    }
    // (800 - 600) / 30 = 6 steps fit vertically -> 7 positions, then wrap.
    EXPECT_EQ(cascade_window_position(0, 30, 7, w, h, wa), std::make_pair(0, 30));
    EXPECT_EQ(cascade_window_position(0, 30, 8, w, h, wa),
              std::make_pair(WINDOW_CASCADE_STEP, 30 + WINDOW_CASCADE_STEP));
}

TEST(WindowPlacement, CascadeClampsOffscreenBaseAndOversizedWindow)
{
    const ScreenWorkArea wa{0, 0, 1024, 768};
    auto [x, y] = cascade_window_position(5000, -200, 0, 800, 600, wa);
    EXPECT_EQ(x, 1024 - 800);
    EXPECT_EQ(y, 0);
    // Window larger than the work area: pinned to its corner, never NaN-like.
    EXPECT_EQ(cascade_window_position(300, 300, 3, 2000, 2000, wa), std::make_pair(0, 0));
}

TEST(WindowPlacement, CascadeWithoutWorkAreaJustOffsets)
{
    EXPECT_EQ(cascade_window_position(10, 20, 3, 800, 600, ScreenWorkArea{}),
              std::make_pair(10 + 3 * WINDOW_CASCADE_STEP, 20 + 3 * WINDOW_CASCADE_STEP));
}

TEST(ZoomExtent, IgnoresReferenceLineSentinels)
{
    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();
    accumulate_zoom_extent(std::vector<float>{0.0f, 2.5f, 10.0f}, lo, hi);
    accumulate_zoom_extent(std::vector<float>{-1e12f, 1e12f}, lo, hi);   // hline x data
    accumulate_zoom_extent(
        std::vector<float>{std::nanf(""), std::numeric_limits<float>::infinity()},
        lo,
        hi);
    EXPECT_FLOAT_EQ(lo, 0.0f);
    EXPECT_FLOAT_EQ(hi, 10.0f);
    // With an hline present the default 5%-padded view now reads 100%, not 1.4e13%.
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(hi - lo, 11.0), 1.0);
}

TEST(ZoomExtent, OnlySentinelsLeavesExtentEmpty)
{
    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();
    accumulate_zoom_extent(std::vector<float>{-1e12f, 1e12f}, lo, hi);
    EXPECT_FALSE(hi - lo > 0.0f);
}

// ─── fit_window_to_work_area ────────────────────────────────────────────────

TEST(WindowFit, FitsUnchanged)
{
    const ScreenWorkArea wa{0, 0, 1920, 1050};
    EXPECT_EQ(fit_window_to_work_area(1500, 900, wa), std::make_pair(1500, 900));
}

TEST(WindowFit, ShrinksDefaultOnSmallLaptopKeepingAspect)
{
    // 1366x768 screen minus a 32 px panel.
    const ScreenWorkArea wa{0, 32, 1366, 736};
    const auto [w, h] = fit_window_to_work_area(1500, 900, wa);
    EXPECT_LE(w, static_cast<int>(1366 * WINDOW_MAX_WORK_AREA_FRACTION));
    EXPECT_LE(h, static_cast<int>(736 * WINDOW_MAX_WORK_AREA_FRACTION));
    EXPECT_NEAR(static_cast<double>(w) / h, 1500.0 / 900.0, 0.01);
    EXPECT_GE(h, 650);   // height is the limiting side: 0.9 * 736 = 662
}

TEST(WindowFit, ShrinksWidthLimitedWindow)
{
    const ScreenWorkArea wa{0, 0, 1400, 1000};
    const auto [w, h] = fit_window_to_work_area(1500, 900, wa);
    EXPECT_EQ(w, 1260);   // 0.9 * 1400
    EXPECT_EQ(h, 756);
}

TEST(WindowFit, UnknownWorkAreaOrBadSizeLeavesSizeAlone)
{
    EXPECT_EQ(fit_window_to_work_area(1500, 900, ScreenWorkArea{}), std::make_pair(1500, 900));
    EXPECT_EQ(fit_window_to_work_area(0, 900, ScreenWorkArea{0, 0, 800, 600}),
              std::make_pair(0, 900));
}

TEST(WindowFit, NeverBelowOnePixel)
{
    const auto [w, h] = fit_window_to_work_area(5000, 10, ScreenWorkArea{0, 0, 100, 100});
    EXPECT_GE(w, 1);
    EXPECT_GE(h, 1);
}

// QA M5: a first window the OS placed partly under the taskbar is pulled
// back into the work area, decorations included.
TEST(WindowPlacement, KeepWindowInWorkArea)
{
    const ScreenWorkArea wa{0, 0, 1366, 728};   // 768 px screen, 40 px taskbar
    // Fits already: unchanged.
    EXPECT_EQ(keep_window_in_work_area(100, 100, 800, 500, 8, 31, 8, 8, wa),
              (std::pair<int, int>{100, 100}));
    // Bottom under the taskbar: moved up so the bottom border ends at 728.
    EXPECT_EQ(keep_window_in_work_area(100, 300, 800, 500, 8, 31, 8, 8, wa),
              (std::pair<int, int>{100, 728 - 8 - 500}));
    // Off the left / top edge: the frame starts at the work-area corner.
    EXPECT_EQ(keep_window_in_work_area(-50, -10, 800, 500, 8, 31, 8, 8, wa),
              (std::pair<int, int>{8, 31}));
    // Taller than the work area: the title bar stays on screen.
    EXPECT_EQ(keep_window_in_work_area(100, 200, 800, 900, 8, 31, 8, 8, wa),
              (std::pair<int, int>{100, 31}));
    // Unknown work area: unchanged.
    EXPECT_EQ(keep_window_in_work_area(-50, 900, 800, 500, 8, 31, 8, 8, ScreenWorkArea{}),
              (std::pair<int, int>{-50, 900}));
}

// The clamp uses the monitor a window is on, not the primary one: a window
// the OS placed on a secondary display must stay there (PR #211 review).
TEST(WindowPlacement, WorkAreaWithMostOverlap)
{
    const ScreenWorkArea areas[] = {
        {0, 0, 1920, 1040},      // primary, taskbar at the bottom
        {1920, 0, 2560, 1400},   // secondary to the right
    };
    // Entirely on the secondary monitor.
    EXPECT_EQ(work_area_with_most_overlap(2100, 100, 1200, 800, areas), 1);
    // Straddling both: the larger share wins.
    EXPECT_EQ(work_area_with_most_overlap(1500, 100, 1200, 800, areas), 1);
    EXPECT_EQ(work_area_with_most_overlap(1000, 100, 1200, 800, areas), 0);
    // Off every monitor, or no monitors known.
    EXPECT_EQ(work_area_with_most_overlap(-5000, -5000, 100, 100, areas), -1);
    EXPECT_EQ(work_area_with_most_overlap(0, 0, 100, 100, std::span<const ScreenWorkArea>{}), -1);

    // The secondary monitor's work area keeps the window where it is.
    const ScreenWorkArea& wa = areas[work_area_with_most_overlap(2100, 100, 1200, 800, areas)];
    EXPECT_EQ(keep_window_in_work_area(2108, 131, 1184, 761, 8, 31, 8, 8, wa),
              (std::pair<int, int>{2108, 131}));
}

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

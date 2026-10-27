// Shared window defaults: the in-process app and the multi-process window agent
// (plus the backend daemon and IPC payloads) must open figures with the same
// size and tab naming, and report the same zoom level for an autoscaled view.

#include <gtest/gtest.h>
#include <spectra/figure.hpp>
#include <spectra/window_defaults.hpp>

#include "daemon/figure_model.hpp"
#include "ipc/message.hpp"
#include "ui/app/zoom_level.hpp"
#include "ui/figures/figure_manager.hpp"

namespace spectra
{

TEST(WindowDefaults, SizeIsSharedByEveryRuntimePath)
{
    EXPECT_EQ(DEFAULT_WINDOW_WIDTH, 1500u);
    EXPECT_EQ(DEFAULT_WINDOW_HEIGHT, 900u);

    // In-process: FigureConfig defaults.
    FigureConfig cfg;
    EXPECT_EQ(cfg.width, DEFAULT_WINDOW_WIDTH);
    EXPECT_EQ(cfg.height, DEFAULT_WINDOW_HEIGHT);

    // Multi-process: IPC payload defaults (Python REQ_CREATE_FIGURE, snapshots).
    ipc::ReqCreateFigurePayload req;
    EXPECT_EQ(req.width, DEFAULT_WINDOW_WIDTH);
    EXPECT_EQ(req.height, DEFAULT_WINDOW_HEIGHT);
    ipc::SnapshotFigureState snap;
    EXPECT_EQ(snap.width, DEFAULT_WINDOW_WIDTH);
    EXPECT_EQ(snap.height, DEFAULT_WINDOW_HEIGHT);
}

TEST(WindowDefaults, DaemonFigureModelUsesSharedSize)
{
    daemon::FigureModel model;
    const auto          id   = model.create_figure();
    const auto          snap = model.snapshot({id});
    ASSERT_EQ(snap.figures.size(), 1u);
    EXPECT_EQ(snap.figures[0].width, DEFAULT_WINDOW_WIDTH);
    EXPECT_EQ(snap.figures[0].height, DEFAULT_WINDOW_HEIGHT);
}

TEST(WindowDefaults, FigureTitlesAreNumbered)
{
    EXPECT_EQ(default_figure_title(1), "Figure 1");
    EXPECT_EQ(default_figure_title(12), "Figure 12");
    // The in-process tab naming goes through the same helper.
    EXPECT_EQ(FigureManager::default_title(1), default_figure_title(1));
    EXPECT_EQ(FigureManager::default_title(3), "Figure 3");
}

TEST(ZoomLevel, AutoscaledViewIsHundredPercent)
{
    // Data spans 10 units; the padded autoscale view spans 11 (5% each side).
    // This used to read "Zoom: 91%".
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 11.0), 1.0);
    // A tight fit (explicit limits equal to the data extent) is also 100%.
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 10.0), 1.0);
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 10.5), 1.0);
}

TEST(ZoomLevel, ZoomInAndOutOutsideTheFitBand)
{
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 5.0), 2.0);    // zoomed in 2x
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 22.0), 0.5);   // zoomed out 2x
    // Tight autoscale has no padding band.
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 20.0, 0.0), 0.5);
}

TEST(ZoomLevel, DegenerateRangesReportHundredPercent)
{
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(0.0, 10.0), 1.0);
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(10.0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(zoom_level_for_ranges(-1.0, 10.0), 1.0);
}

}   // namespace spectra

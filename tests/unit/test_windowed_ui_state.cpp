// test_windowed_ui_state.cpp — UI state the QA design review asserts on.
//
// * Timeline playback must move the playhead even when the active figure has
//   no animation callback (keyframe tracks over static data).  It used to be
//   advanced only by an animated figure, so Play froze at 0 (the design
//   review's 30_timeline_playing capture showed a stopped playhead).
// * WindowManager::detach_figure() must move the figure out of the window
//   that showed it.  It used to leave it listed by both windows, so closing
//   the new window destroyed a figure the primary window still showed.
//
// The windowed cases need an X display (registered under xvfb-run) and skip
// without one.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/window/window_manager.hpp"
#include "render/vulkan/window_context.hpp"

using namespace spectra;

namespace
{

void add_line(Figure& fig)
{
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x(50), y(50);
    for (int i = 0; i < 50; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i % 7);
    }
    ax.line(x, y);
}

void step(App& app, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        if (auto* s = app.session())
            s->redraw_tracker().mark_dirty("test");
        app.step();
    }
}

bool contains(const std::vector<FigureId>& v, FigureId id)
{
    return std::find(v.begin(), v.end(), id) != v.end();
}

}   // namespace

TEST(TimelinePlayback, AdvancesWithoutAnimatedFigure)
{
    App app({.headless = true, .socket_path = ""});
    if (!app.ok())
        GTEST_SKIP() << "No Vulkan device";
    add_line(app.figure({.width = 320, .height = 240}));
    app.init_runtime();
    auto* ui = app.ui_context();
    ASSERT_NE(ui, nullptr);

    auto& te = ui->timeline_editor;
    te.set_duration(5.0f);
    const uint32_t track = te.add_track("X", colors::red);
    te.add_keyframe(track, 0.0f);
    te.add_keyframe(track, 4.0f);
    te.play();
    ASSERT_TRUE(te.is_playing());
    ASSERT_FLOAT_EQ(te.playhead(), 0.0f);

    step(app, 20);

    EXPECT_TRUE(te.is_playing());
    EXPECT_GT(te.playhead(), 0.0f) << "Play must move the playhead of a static figure";
    app.shutdown_runtime();
}

TEST(WindowedDetach, FigureLeavesSourceWindow)
{
    if (!std::getenv("DISPLAY"))
        GTEST_SKIP() << "No X display";
    App app({.headless = false, .socket_path = ""});
    if (!app.ok())
        GTEST_SKIP() << "No Vulkan device / window";
    add_line(app.figure({.width = 640, .height = 480}));
    app.init_runtime();
    auto* wm = app.window_manager();
    if (!wm || wm->windows().empty() || !app.ui_context())
        GTEST_SKIP() << "No window";
    // A figure created at runtime opens as a tab of the primary window.
    add_line(app.figure({.width = 640, .height = 480}));
    step(app, 3);
    ASSERT_EQ(wm->windows().size(), 1u);

    const auto ids = app.figure_registry().all_ids();
    ASSERT_EQ(ids.size(), 2u);
    WindowContext* primary = wm->windows()[0];
    std::string    listed;
    for (FigureId f : primary->assigned_figures)
        listed += std::to_string(f) + " ";
    ASSERT_TRUE(contains(primary->assigned_figures, ids[1]))
        << "primary lists [" << listed << "], figure " << ids[1] << ", windows "
        << wm->windows().size();

    WindowContext* second = wm->detach_figure(ids[1], 400, 300, "Detached", 60, 60);
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(contains(primary->assigned_figures, ids[1]));
    EXPECT_FALSE(contains(app.ui_context()->fig_mgr->figure_ids(), ids[1]));
    EXPECT_TRUE(contains(second->assigned_figures, ids[1]));
    step(app, 3);
    EXPECT_FALSE(contains(primary->assigned_figures, ids[1])) << "re-synced from FigureManager";

    // The primary's last figure cannot be detached (the window would be empty).
    EXPECT_EQ(wm->detach_figure(ids[0], 400, 300, "Last", 60, 60), nullptr);
    EXPECT_TRUE(contains(primary->assigned_figures, ids[0]));

    // Closing the detached window destroys only its own figure.
    wm->request_close(second->id);
    wm->process_pending_closes();
    step(app, 3);
    EXPECT_EQ(wm->windows().size(), 1u);
    EXPECT_EQ(app.figure_registry().get(ids[1]), nullptr);
    ASSERT_NE(app.figure_registry().get(ids[0]), nullptr);
    EXPECT_EQ(app.ui_context()->fig_mgr->count(), 1u);
    app.shutdown_runtime();
}

#include <gtest/gtest.h>
#include <spectra/app.hpp>
#include <spectra/figure.hpp>

using namespace spectra;

// ─── FigureWindowAPI ─────────────────────────────────────────────────────────

TEST(FigureWindowAPI, EachFigureGetsOwnWindow)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig1 = app.figure();
    auto& fig2 = app.figure();

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 2u);
}

TEST(FigureWindowAPI, TabGoesInSameWindow)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig1 = app.figure();
    auto& fig2 = app.figure(fig1);

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 1u) << "Both figures should be in one window";
    EXPECT_EQ(wf.begin()->second.size(), 2u);
}

TEST(FigureWindowAPI, MultipleTabsInOneWindow)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig1 = app.figure();
    auto& fig2 = app.figure(fig1);
    auto& fig3 = app.figure(fig1);

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 1u);
    EXPECT_EQ(wf.begin()->second.size(), 3u);
}

TEST(FigureWindowAPI, MixedWindowsAndTabs)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig1 = app.figure();       // window A
    auto& fig2 = app.figure();       // window B
    auto& fig3 = app.figure(fig1);   // tab in window A

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 2u) << "Should have 2 windows";

    // Find the window with 2 figures
    bool found_two = false, found_one = false;
    for (auto& [wid, figs] : wf)
    {
        if (figs.size() == 2)
            found_two = true;
        if (figs.size() == 1)
            found_one = true;
    }
    EXPECT_TRUE(found_two);
    EXPECT_TRUE(found_one);
}

TEST(FigureWindowAPI, TabNextToUnknownFigureCreatesNewWindow)
{
    App    app({.headless = true, .socket_path = ""});
    Figure orphan;
    auto&  fig = app.figure(orphan);

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 1u) << "Should fall back to new window";
}

TEST(FigureWindowAPI, ChainedTabs)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig1 = app.figure();
    auto& fig2 = app.figure(fig1);
    auto& fig3 = app.figure(fig2);   // tab next to fig2 = same window as fig1

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 1u) << "All 3 should be in one window";
    EXPECT_EQ(wf.begin()->second.size(), 3u);
}

TEST(FigureWindowAPI, FourWindowsThreeTabs)
{
    App   app({.headless = true, .socket_path = ""});
    auto& a1 = app.figure();
    auto& a2 = app.figure(a1);
    auto& a3 = app.figure(a1);
    auto& b  = app.figure();
    auto& c  = app.figure();
    auto& d1 = app.figure();
    auto& d2 = app.figure(d1);

    auto& wf = app.window_figures();
    EXPECT_EQ(wf.size(), 4u);

    // Count total figures
    size_t total = 0;
    for (auto& [wid, figs] : wf)
        total += figs.size();
    EXPECT_EQ(total, 7u);
}

// ─── IPC: window_id round-trip in SnapshotFigureState ────────────────────────

#include "../../src/ipc/codec.hpp"
#include "../../src/ipc/message.hpp"

TEST(FigureWindowIPC, WindowIdRoundTrip)
{
    using namespace spectra::ipc;

    StateSnapshotPayload snap;
    snap.revision   = 1;
    snap.session_id = 42;

    SnapshotFigureState fig1;
    fig1.figure_id = 100;
    fig1.window_id = 5;
    fig1.title     = "Fig A";
    snap.figures.push_back(fig1);

    SnapshotFigureState fig2;
    fig2.figure_id = 101;
    fig2.window_id = 5;   // same window
    fig2.title     = "Fig B";
    snap.figures.push_back(fig2);

    SnapshotFigureState fig3;
    fig3.figure_id = 102;
    fig3.window_id = 0;   // own window
    fig3.title     = "Fig C";
    snap.figures.push_back(fig3);

    auto encoded = encode_state_snapshot(snap);
    auto decoded = decode_state_snapshot(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures.size(), 3u);

    EXPECT_EQ(decoded->figures[0].figure_id, 100u);
    EXPECT_EQ(decoded->figures[0].window_id, 5u);

    EXPECT_EQ(decoded->figures[1].figure_id, 101u);
    EXPECT_EQ(decoded->figures[1].window_id, 5u);

    EXPECT_EQ(decoded->figures[2].figure_id, 102u);
    EXPECT_EQ(decoded->figures[2].window_id, 0u);
}

TEST(FigureWindowIPC, WindowIdZeroOmittedInEncoding)
{
    using namespace spectra::ipc;

    SnapshotFigureState fig;
    fig.figure_id = 200;
    fig.window_id = 0;
    fig.title     = "Test";

    StateSnapshotPayload snap;
    snap.figures.push_back(fig);

    auto encoded = encode_state_snapshot(snap);
    auto decoded = decode_state_snapshot(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures.size(), 1u);
    EXPECT_EQ(decoded->figures[0].window_id, 0u);
}

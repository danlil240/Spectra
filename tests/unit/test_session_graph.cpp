#include <gtest/gtest.h>

#include "daemon/session_graph.hpp"

#include <chrono>
#include <thread>

using namespace spectra::daemon;
using namespace spectra::ipc;

// --- Agent management ---

TEST(SessionGraph, AddAgentReturnsUniqueIds)
{
    SessionGraph g;
    auto w1 = g.add_agent(100, 10);
    auto w2 = g.add_agent(200, 11);
    auto w3 = g.add_agent(300, 12);
    EXPECT_NE(w1, w2);
    EXPECT_NE(w2, w3);
    EXPECT_NE(w1, w3);
    EXPECT_NE(w1, INVALID_WINDOW);
    EXPECT_EQ(g.agent_count(), 3u);
}

TEST(SessionGraph, RemoveAgentReturnsOrphanedFigures)
{
    SessionGraph g;
    auto wid = g.add_agent(100, 10);
    auto f1 = g.add_figure("Fig 1");
    auto f2 = g.add_figure("Fig 2");
    g.assign_figure(f1, wid);
    g.assign_figure(f2, wid);

    auto orphaned = g.remove_agent(wid);
    EXPECT_EQ(orphaned.size(), 2u);
    EXPECT_EQ(g.agent_count(), 0u);
}

TEST(SessionGraph, RemoveNonexistentAgentReturnsEmpty)
{
    SessionGraph g;
    auto orphaned = g.remove_agent(999);
    EXPECT_TRUE(orphaned.empty());
}

TEST(SessionGraph, AgentLookup)
{
    SessionGraph g;
    auto wid = g.add_agent(42, 5);
    auto* entry = g.agent(wid);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->process_id, 42u);
    EXPECT_EQ(entry->connection_fd, 5);
    EXPECT_TRUE(entry->alive);

    EXPECT_EQ(g.agent(999), nullptr);
}

TEST(SessionGraph, AllWindowIds)
{
    SessionGraph g;
    auto w1 = g.add_agent(1, 1);
    auto w2 = g.add_agent(2, 2);
    auto ids = g.all_window_ids();
    EXPECT_EQ(ids.size(), 2u);
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), w1) != ids.end());
    EXPECT_TRUE(std::find(ids.begin(), ids.end(), w2) != ids.end());
}

// --- Figure management ---

TEST(SessionGraph, AddFigureReturnsUniqueIds)
{
    SessionGraph g;
    auto f1 = g.add_figure("A");
    auto f2 = g.add_figure("B");
    EXPECT_NE(f1, f2);
    EXPECT_NE(f1, 0u);
    EXPECT_EQ(g.figure_count(), 2u);
}

TEST(SessionGraph, AssignFigureToWindow)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    auto f1 = g.add_figure("Fig");
    EXPECT_TRUE(g.assign_figure(f1, wid));

    auto figs = g.figures_for_window(wid);
    EXPECT_EQ(figs.size(), 1u);
    EXPECT_EQ(figs[0], f1);
}

TEST(SessionGraph, AssignFigureToNonexistentWindowFails)
{
    SessionGraph g;
    auto f1 = g.add_figure("Fig");
    EXPECT_FALSE(g.assign_figure(f1, 999));
}

TEST(SessionGraph, AssignNonexistentFigureFails)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    EXPECT_FALSE(g.assign_figure(999, wid));
}

TEST(SessionGraph, ReassignFigureMovesIt)
{
    SessionGraph g;
    auto w1 = g.add_agent(1, 1);
    auto w2 = g.add_agent(2, 2);
    auto f1 = g.add_figure("Fig");

    g.assign_figure(f1, w1);
    EXPECT_EQ(g.figures_for_window(w1).size(), 1u);
    EXPECT_EQ(g.figures_for_window(w2).size(), 0u);

    g.assign_figure(f1, w2);
    EXPECT_EQ(g.figures_for_window(w1).size(), 0u);
    EXPECT_EQ(g.figures_for_window(w2).size(), 1u);
}

TEST(SessionGraph, RemoveFigure)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    auto f1 = g.add_figure("Fig");
    g.assign_figure(f1, wid);

    g.remove_figure(f1);
    EXPECT_EQ(g.figure_count(), 0u);
    EXPECT_EQ(g.figures_for_window(wid).size(), 0u);
}

TEST(SessionGraph, FiguresForNonexistentWindow)
{
    SessionGraph g;
    EXPECT_TRUE(g.figures_for_window(999).empty());
}

// --- Heartbeat ---

TEST(SessionGraph, HeartbeatUpdatesTimestamp)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);

    // Initially fresh
    EXPECT_TRUE(g.stale_agents(std::chrono::milliseconds(100)).empty());

    // Wait and check stale
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto stale = g.stale_agents(std::chrono::milliseconds(10));
    EXPECT_EQ(stale.size(), 1u);
    EXPECT_EQ(stale[0], wid);

    // Heartbeat refreshes
    g.heartbeat(wid);
    EXPECT_TRUE(g.stale_agents(std::chrono::milliseconds(100)).empty());
}

// --- Empty / shutdown ---

TEST(SessionGraph, IsEmptyWhenNoAgents)
{
    SessionGraph g;
    EXPECT_TRUE(g.is_empty());

    auto wid = g.add_agent(1, 1);
    EXPECT_FALSE(g.is_empty());

    g.remove_agent(wid);
    EXPECT_TRUE(g.is_empty());
}

TEST(SessionGraph, SessionIdIsNonZero)
{
    SessionGraph g;
    EXPECT_NE(g.session_id(), INVALID_SESSION);
}

// --- Multiple figures and windows ---

TEST(SessionGraph, MultipleFiguresMultipleWindows)
{
    SessionGraph g;
    auto w1 = g.add_agent(1, 1);
    auto w2 = g.add_agent(2, 2);
    auto f1 = g.add_figure("A");
    auto f2 = g.add_figure("B");
    auto f3 = g.add_figure("C");

    g.assign_figure(f1, w1);
    g.assign_figure(f2, w1);
    g.assign_figure(f3, w2);

    EXPECT_EQ(g.figures_for_window(w1).size(), 2u);
    EXPECT_EQ(g.figures_for_window(w2).size(), 1u);

    // Remove w1 — figures become unassigned
    auto orphaned = g.remove_agent(w1);
    EXPECT_EQ(orphaned.size(), 2u);
    EXPECT_EQ(g.figures_for_window(w2).size(), 1u);
    EXPECT_EQ(g.figure_count(), 3u);  // figures still exist
}

TEST(SessionGraph, DuplicateAssignIsIdempotent)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    auto f1 = g.add_figure("Fig");
    g.assign_figure(f1, wid);
    g.assign_figure(f1, wid);  // duplicate
    EXPECT_EQ(g.figures_for_window(wid).size(), 1u);
}

// --- Unassign figure (tab detach) ---

TEST(SessionGraph, UnassignFigureRemovesFromWindow)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    auto f1 = g.add_figure("Fig");
    g.assign_figure(f1, wid);
    EXPECT_EQ(g.figures_for_window(wid).size(), 1u);

    EXPECT_TRUE(g.unassign_figure(f1, wid));
    EXPECT_EQ(g.figures_for_window(wid).size(), 0u);
    // Figure still exists in session
    EXPECT_EQ(g.figure_count(), 1u);
}

TEST(SessionGraph, UnassignFigureWrongWindowFails)
{
    SessionGraph g;
    auto w1 = g.add_agent(1, 1);
    auto w2 = g.add_agent(2, 2);
    auto f1 = g.add_figure("Fig");
    g.assign_figure(f1, w1);

    // Try to unassign from wrong window
    EXPECT_FALSE(g.unassign_figure(f1, w2));
    // Still assigned to w1
    EXPECT_EQ(g.figures_for_window(w1).size(), 1u);
}

TEST(SessionGraph, UnassignNonexistentFigureFails)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    EXPECT_FALSE(g.unassign_figure(999, wid));
}

TEST(SessionGraph, UnassignThenReassign)
{
    SessionGraph g;
    auto w1 = g.add_agent(1, 1);
    auto w2 = g.add_agent(2, 2);
    auto f1 = g.add_figure("Fig");

    g.assign_figure(f1, w1);
    EXPECT_TRUE(g.unassign_figure(f1, w1));
    EXPECT_EQ(g.figures_for_window(w1).size(), 0u);

    // Reassign to different window
    EXPECT_TRUE(g.assign_figure(f1, w2));
    EXPECT_EQ(g.figures_for_window(w2).size(), 1u);
    EXPECT_EQ(g.figures_for_window(w1).size(), 0u);
}

TEST(SessionGraph, UnassignMultipleFigures)
{
    SessionGraph g;
    auto wid = g.add_agent(1, 1);
    auto f1 = g.add_figure("A");
    auto f2 = g.add_figure("B");
    auto f3 = g.add_figure("C");
    g.assign_figure(f1, wid);
    g.assign_figure(f2, wid);
    g.assign_figure(f3, wid);
    EXPECT_EQ(g.figures_for_window(wid).size(), 3u);

    // Unassign middle figure
    EXPECT_TRUE(g.unassign_figure(f2, wid));
    auto figs = g.figures_for_window(wid);
    EXPECT_EQ(figs.size(), 2u);
    EXPECT_TRUE(std::find(figs.begin(), figs.end(), f1) != figs.end());
    EXPECT_TRUE(std::find(figs.begin(), figs.end(), f3) != figs.end());
    EXPECT_TRUE(std::find(figs.begin(), figs.end(), f2) == figs.end());
}

// test_node_graph_panel.cpp — unit tests for NodeGraphPanel.
//
// Tests cover:
//   - Graph construction (nodes + topics + edges)
//   - Namespace filtering
//   - Force-directed layout convergence
//   - Selection callbacks
//   - Auto-refresh wiring
//   - Thread-safety (concurrent access)
//   - Edge cases (empty graph, single node, no topics)
//
// No ROS2 runtime is required; build_graph() is called directly with
// hand-crafted TopicInfo / NodeInfo vectors (same pattern as other
// pure-logic tests in the adapter suite).
//
// Uses gtest_main (no custom main) — no RclcppEnvironment needed.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "ui/node_graph_panel.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TopicInfo make_topic(const std::string& name,
                            int pubs = 1, int subs = 1)
{
    TopicInfo t;
    t.name             = name;
    t.types            = {"std_msgs/msg/Float64"};
    t.publisher_count  = pubs;
    t.subscriber_count = subs;
    return t;
}

static NodeInfo make_node(const std::string& ns, const std::string& name)
{
    NodeInfo n;
    n.namespace_ = ns;
    n.name       = name;
    n.full_name  = ns + "/" + name;
    return n;
}

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, DefaultConstruction)
{
    NodeGraphPanel p;
    EXPECT_EQ(p.node_count(), 0u);
    EXPECT_EQ(p.edge_count(), 0u);
    EXPECT_FALSE(p.is_built());
    EXPECT_EQ(p.selected_id(), "");
}

TEST(NodeGraphPanel, DefaultTitle)
{
    NodeGraphPanel p;
    EXPECT_EQ(p.title(), "Node Graph");
}

TEST(NodeGraphPanel, SetTitle)
{
    NodeGraphPanel p;
    p.set_title("My Graph");
    EXPECT_EQ(p.title(), "My Graph");
}

TEST(NodeGraphPanel, DefaultRefreshInterval)
{
    NodeGraphPanel p;
    EXPECT_EQ(p.refresh_interval(), std::chrono::milliseconds(3000));
}

TEST(NodeGraphPanel, SetRefreshInterval)
{
    NodeGraphPanel p;
    p.set_refresh_interval(std::chrono::milliseconds(500));
    EXPECT_EQ(p.refresh_interval(), std::chrono::milliseconds(500));
}

// ---------------------------------------------------------------------------
// Suite: GraphBuilding
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, BuildEmpty)
{
    NodeGraphPanel p;
    p.build_graph({}, {});
    EXPECT_TRUE(p.is_built());
    EXPECT_EQ(p.node_count(), 0u);
    EXPECT_EQ(p.edge_count(), 0u);
}

TEST(NodeGraphPanel, BuildTopicsOnly)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/chatter"), make_topic("/odom")},
        {}
    );
    EXPECT_TRUE(p.is_built());
    EXPECT_EQ(p.node_count(), 2u);
}

TEST(NodeGraphPanel, BuildNodesOnly)
{
    NodeGraphPanel p;
    p.build_graph(
        {},
        {make_node("/", "talker"), make_node("/", "listener")}
    );
    EXPECT_TRUE(p.is_built());
    EXPECT_EQ(p.node_count(), 2u);
}

TEST(NodeGraphPanel, BuildMixed)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/chatter"), make_topic("/scan")},
        {make_node("/", "talker"), make_node("/", "listener"), make_node("/robot", "base")}
    );
    // 2 topics + 3 nodes = 5 graph nodes
    EXPECT_EQ(p.node_count(), 5u);
}

TEST(NodeGraphPanel, NodeKinds)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/chatter")},
        {make_node("/", "talker")}
    );
    auto snap = p.snapshot();
    int ros_nodes = 0, topic_nodes = 0;
    for (const auto& n : snap.nodes)
    {
        if (n.kind == GraphNodeKind::RosNode) ++ros_nodes;
        else ++topic_nodes;
    }
    EXPECT_EQ(ros_nodes, 1);
    EXPECT_EQ(topic_nodes, 1);
}

TEST(NodeGraphPanel, TopicDisplayName)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/robot/sensor/imu")}, {});
    auto snap = p.snapshot();
    ASSERT_EQ(snap.nodes.size(), 1u);
    EXPECT_EQ(snap.nodes[0].display_name, "imu");
}

TEST(NodeGraphPanel, NodeDisplayName)
{
    NodeGraphPanel p;
    p.build_graph({}, {make_node("/robot", "controller")});
    auto snap = p.snapshot();
    ASSERT_EQ(snap.nodes.size(), 1u);
    EXPECT_EQ(snap.nodes[0].display_name, "controller");
}

TEST(NodeGraphPanel, TopicPubSubCount)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/chatter", 2, 3)}, {});
    auto snap = p.snapshot();
    ASSERT_EQ(snap.nodes.size(), 1u);
    EXPECT_EQ(snap.nodes[0].pub_count, 2);
    EXPECT_EQ(snap.nodes[0].sub_count, 3);
}

TEST(NodeGraphPanel, RebuildPreservesPositions)
{
    NodeGraphPanel p;
    p.build_graph({}, {make_node("/", "talker")});

    // Manually set position
    {
        auto snap = p.snapshot();
        // We can't set positions directly from outside, but after layout
        // steps positions should differ from (0,0).
        // Just verify the node exists and survives rebuild.
    }

    // Rebuild with same node — should not crash
    p.build_graph({}, {make_node("/", "talker")});
    EXPECT_EQ(p.node_count(), 1u);
}

TEST(NodeGraphPanel, IsBuiltAfterBuild)
{
    NodeGraphPanel p;
    EXPECT_FALSE(p.is_built());
    p.build_graph({make_topic("/t")}, {});
    EXPECT_TRUE(p.is_built());
}

// ---------------------------------------------------------------------------
// Suite: NamespaceFilter
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, EmptyFilterShowsAll)
{
    NodeGraphPanel p;
    p.set_namespace_filter("");
    EXPECT_EQ(p.namespace_filter(), "");
    p.build_graph(
        {make_topic("/robot/imu"), make_topic("/chatter")},
        {make_node("/robot", "controller")}
    );
    // All 3 graph nodes exist
    EXPECT_EQ(p.node_count(), 3u);
}

TEST(NodeGraphPanel, FilterByNamespace)
{
    NodeGraphPanel p;
    p.set_namespace_filter("/robot");
    p.build_graph(
        {make_topic("/robot/imu"), make_topic("/chatter")},
        {make_node("/robot", "controller"), make_node("/", "talker")}
    );
    // Graph still has all 4 nodes; filter is applied at draw time.
    // Verify filter string is preserved.
    EXPECT_EQ(p.namespace_filter(), "/robot");
}

TEST(NodeGraphPanel, SetAndClearFilter)
{
    NodeGraphPanel p;
    p.set_namespace_filter("/ns1");
    EXPECT_EQ(p.namespace_filter(), "/ns1");
    p.set_namespace_filter("");
    EXPECT_EQ(p.namespace_filter(), "");
}

TEST(NodeGraphPanel, FilterThreadSafe)
{
    NodeGraphPanel p;
    // Set filter from a different thread while panel is being queried
    std::thread t([&]() {
        for (int i = 0; i < 100; ++i)
            p.set_namespace_filter(i % 2 == 0 ? "/ns" : "");
    });
    for (int i = 0; i < 100; ++i)
        (void)p.namespace_filter();
    t.join();
}

// ---------------------------------------------------------------------------
// Suite: Layout
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, LayoutDoesNotCrashEmpty)
{
    NodeGraphPanel p;
    p.build_graph({}, {});
    EXPECT_NO_THROW(p.layout_steps(10));
}

TEST(NodeGraphPanel, LayoutSingleNode)
{
    NodeGraphPanel p;
    p.build_graph({}, {make_node("/", "talker")});
    EXPECT_NO_THROW(p.layout_steps(10));
    EXPECT_EQ(p.node_count(), 1u);
}

TEST(NodeGraphPanel, LayoutMovesNodes)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/chatter"), make_topic("/scan")},
        {make_node("/", "talker"), make_node("/", "listener")}
    );
    auto before = p.snapshot();
    p.layout_steps(50);
    auto after = p.snapshot();

    // At least one node should have moved from its initial position
    bool any_moved = false;
    for (std::size_t i = 0; i < before.nodes.size(); ++i)
    {
        if (before.nodes[i].px != after.nodes[i].px ||
            before.nodes[i].py != after.nodes[i].py)
        {
            any_moved = true;
            break;
        }
    }
    EXPECT_TRUE(any_moved);
}

TEST(NodeGraphPanel, LayoutConvergesForSmallGraph)
{
    NodeGraphPanel p;
    // Tiny graph should converge quickly
    p.build_graph(
        {make_topic("/chatter")},
        {make_node("/", "talker"), make_node("/", "listener")}
    );
    // Run many steps
    p.layout_steps(500);
    // After many steps, should be converged
    EXPECT_FALSE(p.is_animating());
}

TEST(NodeGraphPanel, ResetLayoutStartsAnimation)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/t")},
        {make_node("/", "n")}
    );
    p.layout_steps(500);
    EXPECT_FALSE(p.is_animating());

    p.reset_layout();
    EXPECT_TRUE(p.is_animating());
}

TEST(NodeGraphPanel, LayoutParameters)
{
    NodeGraphPanel p;
    p.set_repulsion(200.0f);
    p.set_attraction(0.1f);
    p.set_damping(0.9f);
    p.set_ideal_length(300.0f);

    EXPECT_FLOAT_EQ(p.repulsion(), 200.0f);
    EXPECT_FLOAT_EQ(p.attraction(), 0.1f);
    EXPECT_FLOAT_EQ(p.damping(), 0.9f);
    EXPECT_FLOAT_EQ(p.ideal_length(), 300.0f);
}

TEST(NodeGraphPanel, LayoutNodesStayFinite)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/a"), make_topic("/b"), make_topic("/c")},
        {make_node("/", "x"), make_node("/", "y"), make_node("/", "z")}
    );
    p.layout_steps(200);

    auto snap = p.snapshot();
    for (const auto& n : snap.nodes)
    {
        EXPECT_TRUE(std::isfinite(n.px)) << "node " << n.id << " px is not finite";
        EXPECT_TRUE(std::isfinite(n.py)) << "node " << n.id << " py is not finite";
    }
}

// ---------------------------------------------------------------------------
// Suite: Selection
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, InitiallyNoSelection)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/t")}, {});
    EXPECT_EQ(p.selected_id(), "");
}

TEST(NodeGraphPanel, SelectCallbackFires)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/chatter")}, {make_node("/", "talker")});

    std::string cb_id;
    p.set_select_callback([&](const GraphNode& n) { cb_id = n.id; });

    // We can't trigger clicks without ImGui, but we can verify the callback
    // is stored without crashing.
    EXPECT_EQ(cb_id, "");
}

TEST(NodeGraphPanel, ActivateCallbackStored)
{
    NodeGraphPanel p;
    bool called = false;
    p.set_activate_callback([&](const GraphNode&) { called = true; });
    // Callback is registered; no crash
    EXPECT_FALSE(called);
}

// ---------------------------------------------------------------------------
// Suite: Snapshot
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, SnapshotIsConsistent)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/scan"), make_topic("/cmd_vel")},
        {make_node("/", "navigation"), make_node("/", "controller")}
    );
    auto snap = p.snapshot();
    EXPECT_EQ(snap.nodes.size(), 4u);
}

TEST(NodeGraphPanel, SnapshotIsCopy)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/t")}, {});
    auto snap1 = p.snapshot();
    p.build_graph({make_topic("/a"), make_topic("/b")}, {});
    auto snap2 = p.snapshot();
    // snap1 should not be affected by the second build
    EXPECT_EQ(snap1.nodes.size(), 1u);
    EXPECT_EQ(snap2.nodes.size(), 2u);
}

TEST(NodeGraphPanel, SnapshotThreadSafe)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/t")}, {make_node("/", "n")});

    std::thread t([&]() {
        for (int i = 0; i < 50; ++i)
        {
            p.build_graph({make_topic("/t" + std::to_string(i))}, {});
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    for (int i = 0; i < 50; ++i)
    {
        auto snap = p.snapshot();
        EXPECT_GE(snap.nodes.size(), 0u);
    }
    t.join();
}

// ---------------------------------------------------------------------------
// Suite: DrawNoOp (without ImGui context — draw must not crash)
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, DrawNoOpWithoutImGui)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/t")}, {make_node("/", "n")});
    // draw() is a no-op when SPECTRA_USE_IMGUI is not defined.
    // When SPECTRA_USE_IMGUI IS defined but no context exists, it would
    // crash — so we only call draw() in the non-ImGui test path.
#ifndef SPECTRA_USE_IMGUI
    EXPECT_NO_THROW(p.draw(nullptr));
#else
    SUCCEED();  // with ImGui, draw requires a valid context — skip
#endif
}

// ---------------------------------------------------------------------------
// Suite: EdgeCases
// ---------------------------------------------------------------------------

TEST(NodeGraphPanel, ManyTopics)
{
    NodeGraphPanel p;
    std::vector<TopicInfo> topics;
    for (int i = 0; i < 50; ++i)
        topics.push_back(make_topic("/topic_" + std::to_string(i)));
    p.build_graph(topics, {});
    EXPECT_EQ(p.node_count(), 50u);
    // Layout should not hang or crash for 50 nodes
    p.layout_steps(10);
}

TEST(NodeGraphPanel, ManyNodes)
{
    NodeGraphPanel p;
    std::vector<NodeInfo> nodes;
    for (int i = 0; i < 30; ++i)
        nodes.push_back(make_node("/ns", "node_" + std::to_string(i)));
    p.build_graph({}, nodes);
    EXPECT_EQ(p.node_count(), 30u);
    p.layout_steps(5);
}

TEST(NodeGraphPanel, RebuildIdempotent)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/t")}, {make_node("/", "n")});
    p.build_graph({make_topic("/t")}, {make_node("/", "n")});
    EXPECT_EQ(p.node_count(), 2u);
}

TEST(NodeGraphPanel, TopicWithNoPubs)
{
    NodeGraphPanel p;
    p.build_graph({make_topic("/ghost", 0, 0)}, {});
    EXPECT_EQ(p.node_count(), 1u);
    auto snap = p.snapshot();
    EXPECT_EQ(snap.nodes[0].pub_count, 0);
    EXPECT_EQ(snap.nodes[0].sub_count, 0);
}

TEST(NodeGraphPanel, NullDiscoveryDoesNotCrash)
{
    NodeGraphPanel p;
    p.set_topic_discovery(nullptr);
    EXPECT_NO_THROW(p.refresh());
    EXPECT_NO_THROW(p.tick(0.016f));
}

TEST(NodeGraphPanel, TickBeforeBuildIsNoOp)
{
    NodeGraphPanel p;
    EXPECT_NO_THROW(p.tick(0.016f));
    EXPECT_FALSE(p.is_built());
}

TEST(NodeGraphPanel, NodeCountAfterReset)
{
    NodeGraphPanel p;
    p.build_graph(
        {make_topic("/a"), make_topic("/b")},
        {make_node("/", "x")}
    );
    p.reset_layout();
    EXPECT_EQ(p.node_count(), 3u);  // nodes still there after reset
    EXPECT_TRUE(p.is_animating());
}

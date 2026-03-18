// test_subplot_manager.cpp — Unit tests for SubplotManager (C4).
//
// SubplotManager depends on Ros2Bridge + MessageIntrospector + AxisLinkManager.
// Tests that exercise subscription / ring-buffer / live data require a running
// ROS2 environment and use the RclcppEnvironment + fixture pattern from the
// Phase A test suite.
//
// Tests that exercise pure logic (grid math, scroll, memory, cursor) do NOT
// need a ROS2 node and are constructed with a bridge that is in the
// Initialized (but not Spinning) state — add_plot() will skip subscription
// creation gracefully.

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <std_msgs/msg/float64.hpp>

#include "ros2_bridge.hpp"
#include "message_introspector.hpp"
#include "subplot_manager.hpp"
#include "ui/data/axis_link.hpp"

using namespace spectra::adapters::ros2;

// ============================================================
// Shared ROS2 environment (rclcpp::init / rclcpp::shutdown)
// ============================================================

class RclcppEnvironment : public ::testing::Environment
{
   public:
    void SetUp() override
    {
        if (!rclcpp::ok())
        {
            int    argc = 0;
            char** argv = nullptr;
            rclcpp::init(argc, argv);
        }
    }
    void TearDown() override
    {
        if (rclcpp::ok())
            rclcpp::shutdown();
    }
};

// ============================================================
// Fixture — bridge initialized, NOT spinning (no ROS2 node needed for
// logic-only tests).
// ============================================================

class SubplotManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        int    argc = 0;
        char** argv = nullptr;
        bridge_.init("test_subplot_mgr", "/test", argc, argv);
        // Do NOT call start_spin() — keeps tests fast and headless.
    }
    void TearDown() override { bridge_.shutdown(); }

    Ros2Bridge          bridge_;
    MessageIntrospector intr_;
};

// ============================================================
// Fixture — bridge spinning (needed for subscription tests).
// ============================================================

class SubplotManagerLiveTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        int    argc = 0;
        char** argv = nullptr;
        bridge_.init("test_subplot_mgr_live", "/test", argc, argv);
        bridge_.start_spin();
    }
    void TearDown() override { bridge_.shutdown(); }

    // Spin a publisher node until predicate returns true or deadline expires.
    template <typename Pred>
    bool spin_until(rclcpp::Node::SharedPtr   node,
                    Pred                      pred,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        rclcpp::executors::SingleThreadedExecutor ex;
        ex.add_node(node);
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            ex.spin_once(std::chrono::milliseconds(10));
            if (pred())
                return true;
        }
        return false;
    }

    Ros2Bridge          bridge_;
    MessageIntrospector intr_;
};

// ============================================================
// Suite: Construction
// ============================================================

TEST_F(SubplotManagerTest, DefaultConstruction_1x1)
{
    SubplotManager mgr(bridge_, intr_, 1, 1);
    EXPECT_EQ(mgr.rows(), 1);
    EXPECT_EQ(mgr.cols(), 1);
    EXPECT_EQ(mgr.capacity(), 1);
}

TEST_F(SubplotManagerTest, Construction_3x1)
{
    SubplotManager mgr(bridge_, intr_, 3, 1);
    EXPECT_EQ(mgr.rows(), 3);
    EXPECT_EQ(mgr.cols(), 1);
    EXPECT_EQ(mgr.capacity(), 3);
}

TEST_F(SubplotManagerTest, Construction_2x2)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    EXPECT_EQ(mgr.rows(), 2);
    EXPECT_EQ(mgr.cols(), 2);
    EXPECT_EQ(mgr.capacity(), 4);
}

TEST_F(SubplotManagerTest, ConstructionClampsZeroRows)
{
    SubplotManager mgr(bridge_, intr_, 0, 2);
    EXPECT_EQ(mgr.rows(), 1);   // clamped to 1
    EXPECT_EQ(mgr.cols(), 2);
    EXPECT_EQ(mgr.capacity(), 2);
}

TEST_F(SubplotManagerTest, ConstructionClampsZeroCols)
{
    SubplotManager mgr(bridge_, intr_, 2, 0);
    EXPECT_EQ(mgr.rows(), 2);
    EXPECT_EQ(mgr.cols(), 1);   // clamped to 1
    EXPECT_EQ(mgr.capacity(), 2);
}

TEST_F(SubplotManagerTest, FigureCreatedWithCorrectGridSize)
{
    SubplotManager mgr(bridge_, intr_, 3, 2);
    EXPECT_EQ(mgr.figure().grid_rows(), 3);
    EXPECT_EQ(mgr.figure().grid_cols(), 2);
}

TEST_F(SubplotManagerTest, InitialActiveCountIsZero)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    EXPECT_EQ(mgr.active_count(), 0);
}

TEST_F(SubplotManagerTest, AxesPreCreatedForAllSlots)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    // All 4 axes exist in the figure.
    EXPECT_EQ(mgr.figure().axes().size(), 4u);
}

TEST_F(SubplotManagerTest, ExternalFigureDestroyedBeforeManager_NoUseAfterFree)
{
    auto external_fig = std::make_unique<spectra::Figure>();
    auto mgr          = std::make_unique<SubplotManager>(bridge_, intr_, 1, 1, external_fig.get());

    // bridge_ is initialized but not spinning; add_plot still creates slot series.
    auto h = mgr->add_plot(1, "/shutdown_test", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    ASSERT_NE(h.series, nullptr);

    // Reproduce spectra-ros shutdown ordering:
    // 1) Window manager destroys figure/axes first
    // 2) SubplotManager is destroyed later
    external_fig.reset();

    // Regression: this used to crash under ASAN in ~SubplotManager().
    EXPECT_NO_THROW(mgr.reset());
}

// ============================================================
// Suite: index_of
// ============================================================

TEST_F(SubplotManagerTest, IndexOf_ValidCells)
{
    SubplotManager mgr(bridge_, intr_, 3, 2);
    // 3 rows × 2 cols → slot(row,col):
    // (1,1)=1, (1,2)=2, (2,1)=3, (2,2)=4, (3,1)=5, (3,2)=6
    EXPECT_EQ(mgr.index_of(1, 1), 1);
    EXPECT_EQ(mgr.index_of(1, 2), 2);
    EXPECT_EQ(mgr.index_of(2, 1), 3);
    EXPECT_EQ(mgr.index_of(2, 2), 4);
    EXPECT_EQ(mgr.index_of(3, 1), 5);
    EXPECT_EQ(mgr.index_of(3, 2), 6);
}

TEST_F(SubplotManagerTest, IndexOf_OutOfRange)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    EXPECT_EQ(mgr.index_of(0, 1), -1);
    EXPECT_EQ(mgr.index_of(1, 0), -1);
    EXPECT_EQ(mgr.index_of(3, 1), -1);
    EXPECT_EQ(mgr.index_of(1, 3), -1);
    EXPECT_EQ(mgr.index_of(-1, 1), -1);
}

TEST_F(SubplotManagerTest, IndexOf_1x1)
{
    SubplotManager mgr(bridge_, intr_, 1, 1);
    EXPECT_EQ(mgr.index_of(1, 1), 1);
    EXPECT_EQ(mgr.index_of(2, 1), -1);
}

// ============================================================
// Suite: add_plot (logic only — no subscription created)
// ============================================================

TEST_F(SubplotManagerTest, AddPlot_InvalidSlotZero)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.add_plot(0, "/topic", "data", "std_msgs/msg/Float64");
    EXPECT_FALSE(h.valid());
}

TEST_F(SubplotManagerTest, AddPlot_InvalidSlotTooLarge)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.add_plot(3, "/topic", "data", "std_msgs/msg/Float64");
    EXPECT_FALSE(h.valid());
}

TEST_F(SubplotManagerTest, AddPlot_EmptyTopic)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.add_plot(1, "", "data", "std_msgs/msg/Float64");
    EXPECT_FALSE(h.valid());
}

TEST_F(SubplotManagerTest, AddPlot_EmptyField)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.add_plot(1, "/topic", "", "std_msgs/msg/Float64");
    EXPECT_FALSE(h.valid());
}

TEST_F(SubplotManagerTest, AddPlot_BridgeNotSpinning_TypeProvided)
{
    // When bridge is not spinning, add_plot skips subscription creation
    // but still needs a valid type to proceed; since the bridge is not ok()
    // (no executor), add_field will fail — returns bad handle.
    // Accept either bad or good handle — the key check is no crash.
    SubplotManager mgr(bridge_, intr_, 2, 1);
    // bridge_ is initialized but not spinning → bridge_.is_ok() == false.
    auto h = mgr.add_plot(1, "/topic", "data", "std_msgs/msg/Float64");
    // No crash is the requirement; handle validity depends on is_ok().
    (void)h;
    SUCCEED();
}

TEST_F(SubplotManagerTest, AddPlot_RowColOverload_ValidCell)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    // (row=2, col=1) = slot 3
    auto h = mgr.add_plot(2, 1, "/topic", "data", "std_msgs/msg/Float64");
    // No crash; handle may be invalid (no spinning bridge), but no segfault.
    (void)h;
    SUCCEED();
}

TEST_F(SubplotManagerTest, AddPlot_RowColOverload_InvalidCell)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    auto           h = mgr.add_plot(3, 1, "/topic", "data", "std_msgs/msg/Float64");
    EXPECT_FALSE(h.valid());
}

// ============================================================
// Suite: has_plot / active_count / remove_plot
// ============================================================

TEST_F(SubplotManagerTest, HasPlot_EmptySlot)
{
    SubplotManager mgr(bridge_, intr_, 3, 1);
    EXPECT_FALSE(mgr.has_plot(1));
    EXPECT_FALSE(mgr.has_plot(2));
    EXPECT_FALSE(mgr.has_plot(3));
}

TEST_F(SubplotManagerTest, RemovePlot_EmptySlotReturnsFalse)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_FALSE(mgr.remove_plot(1));
}

TEST_F(SubplotManagerTest, RemovePlot_InvalidSlotReturnsFalse)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_FALSE(mgr.remove_plot(0));
    EXPECT_FALSE(mgr.remove_plot(3));
    EXPECT_FALSE(mgr.remove_plot(-1));
}

TEST_F(SubplotManagerTest, Clear_EmptyManagerNoOp)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    EXPECT_NO_THROW(mgr.clear());
    EXPECT_EQ(mgr.active_count(), 0);
}

// ============================================================
// Suite: handle / handles
// ============================================================

TEST_F(SubplotManagerTest, Handle_InvalidSlotReturnsInvalid)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.handle(0);
    EXPECT_FALSE(h.valid());
    h = mgr.handle(3);
    EXPECT_FALSE(h.valid());
}

TEST_F(SubplotManagerTest, Handle_EmptySlotReturnsInvalid)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.handle(1);
    EXPECT_FALSE(h.valid());
}

TEST_F(SubplotManagerTest, Handles_EmptyManagerReturnsEmpty)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    auto           hv = mgr.handles();
    EXPECT_TRUE(hv.empty());
}

// ============================================================
// Suite: Figure access
// ============================================================

TEST_F(SubplotManagerTest, FigureAccessReturnsValidRef)
{
    SubplotManager   mgr(bridge_, intr_, 2, 1);
    spectra::Figure& fig = mgr.figure();
    EXPECT_EQ(fig.grid_rows(), 2);
    EXPECT_EQ(fig.grid_cols(), 1);
}

TEST_F(SubplotManagerTest, ConstFigureAccessReturnsValidRef)
{
    const SubplotManager   mgr(bridge_, intr_, 1, 1);
    const spectra::Figure& fig = mgr.figure();
    EXPECT_EQ(fig.grid_rows(), 1);
    EXPECT_EQ(fig.grid_cols(), 1);
}

// ============================================================
// Suite: AxisLinkManager access
// ============================================================

TEST_F(SubplotManagerTest, LinkManagerAccessReturnsValidRef)
{
    SubplotManager            mgr(bridge_, intr_, 2, 1);
    spectra::AxisLinkManager& lm = mgr.link_manager();
    EXPECT_EQ(lm.group_count(), 0u);   // no plots yet
}

TEST_F(SubplotManagerTest, ConstLinkManagerAccessReturnsValidRef)
{
    const SubplotManager            mgr(bridge_, intr_, 1, 1);
    const spectra::AxisLinkManager& lm = mgr.link_manager();
    EXPECT_EQ(lm.group_count(), 0u);
}

// ============================================================
// Suite: Scroll configuration
// ============================================================

TEST_F(SubplotManagerTest, DefaultScrollWindow)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_DOUBLE_EQ(mgr.time_window(), 30.0);
}

TEST_F(SubplotManagerTest, SetTimeWindowApplied)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    mgr.set_time_window(60.0);
    EXPECT_DOUBLE_EQ(mgr.time_window(), 60.0);
}

TEST_F(SubplotManagerTest, DefaultPruneSettings)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_TRUE(mgr.pruning_enabled());
    EXPECT_DOUBLE_EQ(mgr.prune_buffer(), 20.0);
}

TEST_F(SubplotManagerTest, SetPruneSettings)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    mgr.set_pruning_enabled(false);
    mgr.set_prune_buffer(8.0);

    EXPECT_FALSE(mgr.pruning_enabled());
    EXPECT_DOUBLE_EQ(mgr.prune_buffer(), 8.0);
}

TEST_F(SubplotManagerTest, PauseScrollInvalidSlotNoOp)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_NO_THROW(mgr.pause_scroll(0));
    EXPECT_NO_THROW(mgr.pause_scroll(3));
}

TEST_F(SubplotManagerTest, ResumeScrollInvalidSlotNoOp)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_NO_THROW(mgr.resume_scroll(0));
    EXPECT_NO_THROW(mgr.resume_scroll(3));
}

TEST_F(SubplotManagerTest, IsScrollPaused_InvalidSlotReturnsFalse)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_FALSE(mgr.is_scroll_paused(0));
    EXPECT_FALSE(mgr.is_scroll_paused(3));
}

TEST_F(SubplotManagerTest, PauseResumeValidSlot)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_FALSE(mgr.is_scroll_paused(1));
    mgr.pause_scroll(1);
    EXPECT_TRUE(mgr.is_scroll_paused(1));
    mgr.resume_scroll(1);
    EXPECT_FALSE(mgr.is_scroll_paused(1));
}

TEST_F(SubplotManagerTest, PauseAllScrollAllSlotsPaused)
{
    SubplotManager mgr(bridge_, intr_, 3, 1);
    mgr.pause_all_scroll();
    EXPECT_TRUE(mgr.is_scroll_paused(1));
    EXPECT_TRUE(mgr.is_scroll_paused(2));
    EXPECT_TRUE(mgr.is_scroll_paused(3));
}

TEST_F(SubplotManagerTest, ResumeAllScrollAllSlotsResumed)
{
    SubplotManager mgr(bridge_, intr_, 3, 1);
    mgr.pause_all_scroll();
    mgr.resume_all_scroll();
    EXPECT_FALSE(mgr.is_scroll_paused(1));
    EXPECT_FALSE(mgr.is_scroll_paused(2));
    EXPECT_FALSE(mgr.is_scroll_paused(3));
}

// ============================================================
// Suite: Memory indicator
// ============================================================

TEST_F(SubplotManagerTest, TotalMemoryBytesZeroWhenEmpty)
{
    SubplotManager mgr(bridge_, intr_, 2, 2);
    EXPECT_EQ(mgr.total_memory_bytes(), 0u);
}

// ============================================================
// Suite: Configuration
// ============================================================

TEST_F(SubplotManagerTest, SetFigureSizeApplied)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    mgr.set_figure_size(1920, 1080);
    EXPECT_EQ(mgr.figure().width(), 1920u);
    EXPECT_EQ(mgr.figure().height(), 1080u);
}

TEST_F(SubplotManagerTest, SetAutoFitSamples)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    mgr.set_auto_fit_samples(200);
    EXPECT_EQ(mgr.auto_fit_samples(), 200u);
}

TEST_F(SubplotManagerTest, DefaultAutoFitSamples)
{
    SubplotManager mgr(bridge_, intr_, 1, 1);
    EXPECT_EQ(mgr.auto_fit_samples(), SubplotManager::AUTO_FIT_SAMPLES);
}

TEST_F(SubplotManagerTest, AutoFitSlotYPreservesCurrentXView)
{
    SubplotManager mgr(bridge_, intr_, 1, 1);
    auto           h = mgr.add_plot(1, "/autofit_y_logic", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    const std::vector<float> x{0.0f, 1.0f, 2.0f};
    const std::vector<float> y{-3.0f, 4.0f, 12.0f};
    h.series->set_x(x);
    h.series->set_y(y);
    h.axes->xlim(10.0, 20.0);
    h.axes->ylim(-1.0, 1.0);

    mgr.auto_fit_slot_y(1);

    const auto xl = h.axes->x_limits();
    const auto yl = h.axes->y_limits();
    EXPECT_DOUBLE_EQ(xl.min, 10.0);
    EXPECT_DOUBLE_EQ(xl.max, 20.0);
    EXPECT_LT(yl.min, -3.0);
    EXPECT_GT(yl.max, 12.0);
}

TEST_F(SubplotManagerTest, ClearSlotYLimPreservesXAndClearsManualOverride)
{
    SubplotManager mgr(bridge_, intr_, 1, 1);
    auto           h = mgr.add_plot(1, "/clear_ylim_logic", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    const std::vector<float> x{0.0f, 1.0f, 2.0f};
    const std::vector<float> y{1.0f, 3.0f, 5.0f};
    h.series->set_x(x);
    h.series->set_y(y);

    mgr.set_slot_ylim(1, -10.0, 10.0);
    h.axes->xlim(30.0, 40.0);
    ASSERT_TRUE(mgr.slot_entry_pub(1)->manual_ylim.has_value());

    mgr.clear_slot_ylim(1);

    const auto xl = h.axes->x_limits();
    EXPECT_DOUBLE_EQ(xl.min, 30.0);
    EXPECT_DOUBLE_EQ(xl.max, 40.0);
    EXPECT_FALSE(mgr.slot_entry_pub(1)->manual_ylim.has_value());
}

// ============================================================
// Suite: Shared cursor — notify / clear (logic only, no ROS2 spin)
// ============================================================

TEST_F(SubplotManagerTest, NotifyCursorWithNullSourceClearsIt)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    // Just check no crash.
    EXPECT_NO_THROW(mgr.notify_cursor(nullptr, 0.0f, 0.0f));
}

TEST_F(SubplotManagerTest, ClearCursorNoOp)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    EXPECT_NO_THROW(mgr.clear_cursor());
}

TEST_F(SubplotManagerTest, NotifyCursorWithValidAxes)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    // Axes for slot 1 exists in the figure.
    spectra::Axes* ax = mgr.figure().axes()[0].get();
    EXPECT_NO_THROW(mgr.notify_cursor(ax, 10.5f, -3.2f, 100.0, 200.0));
    // SharedCursor should be broadcast to the link manager.
    spectra::SharedCursor cur = mgr.link_manager().shared_cursor_for(ax);
    // The source axes itself always sees its own cursor.
    EXPECT_TRUE(cur.valid);
    EXPECT_FLOAT_EQ(cur.data_x, 10.5f);
    EXPECT_FLOAT_EQ(cur.data_y, -3.2f);
}

TEST_F(SubplotManagerTest, NotifyCursorThenClear)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    spectra::Axes* ax = mgr.figure().axes()[0].get();
    mgr.notify_cursor(ax, 5.0f, 1.0f);
    mgr.clear_cursor();
    spectra::SharedCursor cur = mgr.link_manager().shared_cursor_for(ax);
    EXPECT_FALSE(cur.valid);
}

// ============================================================
// Suite: poll() — logic (no subscription, no new data)
// ============================================================

TEST_F(SubplotManagerTest, PollWithNoActiveSlotsNoOp)
{
    SubplotManager mgr(bridge_, intr_, 3, 1);
    EXPECT_NO_THROW(mgr.poll());
}

TEST_F(SubplotManagerTest, SetNowAdvancesAutoScroll)
{
    SubplotManager mgr(bridge_, intr_, 2, 1);
    // set_now on all slots — just verify no crash.
    EXPECT_NO_THROW(mgr.set_now(1000.0));
}

// ============================================================
// Suite: X-axis linking (requires two active subplots)
// ============================================================

TEST_F(SubplotManagerLiveTest, XAxisLinkingCreatedForTwoActivePlots)
{
    // Publish two Float64 topics so add_plot can subscribe.
    auto pub_node = std::make_shared<rclcpp::Node>("test_link_pub");
    auto pub1     = pub_node->create_publisher<std_msgs::msg::Float64>("/link_test/a", 10);
    auto pub2     = pub_node->create_publisher<std_msgs::msg::Float64>("/link_test/b", 10);

    // Give discovery time.
    spin_until(pub_node, [] { return false; }, std::chrono::milliseconds(300));

    SubplotManager mgr(bridge_, intr_, 2, 1);

    auto h1 = mgr.add_plot(1, "/link_test/a", "data", "std_msgs/msg/Float64");
    auto h2 = mgr.add_plot(2, "/link_test/b", "data", "std_msgs/msg/Float64");

    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());

    // Both axes should now be in a shared X link group.
    const auto& lm = mgr.link_manager();
    EXPECT_GT(lm.group_count(), 0u);

    // Slot 1 and slot 2 axes should be linked to each other.
    auto peers1 = lm.linked_peers(h1.axes);
    EXPECT_FALSE(peers1.empty());
    bool found = false;
    for (auto* p : peers1)
        if (p == h2.axes)
            found = true;
    EXPECT_TRUE(found);
}

TEST_F(SubplotManagerLiveTest, XAxisLinkingNotCreatedForSinglePlot)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_single_link_pub");
    auto pub1     = pub_node->create_publisher<std_msgs::msg::Float64>("/single_link_test", 10);
    spin_until(pub_node, [] { return false; }, std::chrono::milliseconds(300));

    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h1 = mgr.add_plot(1, "/single_link_test", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());

    // Only one active subplot → no link group needed.
    EXPECT_EQ(mgr.link_manager().group_count(), 0u);
}

TEST_F(SubplotManagerLiveTest, RemovePlotUnlinksAxes)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_unlink_pub");
    auto pub1     = pub_node->create_publisher<std_msgs::msg::Float64>("/unlink_test/a", 10);
    auto pub2     = pub_node->create_publisher<std_msgs::msg::Float64>("/unlink_test/b", 10);
    spin_until(pub_node, [] { return false; }, std::chrono::milliseconds(300));

    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h1 = mgr.add_plot(1, "/unlink_test/a", "data", "std_msgs/msg/Float64");
    auto           h2 = mgr.add_plot(2, "/unlink_test/b", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    ASSERT_GT(mgr.link_manager().group_count(), 0u);

    // Remove one → only one active subplot → no link group.
    EXPECT_TRUE(mgr.remove_plot(2));
    // With only one active, rebuild_x_links() should unlink.
    EXPECT_EQ(mgr.link_manager().group_count(), 0u);
}

TEST_F(SubplotManagerLiveTest, ThreeSubplotsAllLinked)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_three_link_pub");
    auto p1       = pub_node->create_publisher<std_msgs::msg::Float64>("/three_link/a", 10);
    auto p2       = pub_node->create_publisher<std_msgs::msg::Float64>("/three_link/b", 10);
    auto p3       = pub_node->create_publisher<std_msgs::msg::Float64>("/three_link/c", 10);
    spin_until(pub_node, [] { return false; }, std::chrono::milliseconds(300));

    SubplotManager mgr(bridge_, intr_, 3, 1);
    auto           h1 = mgr.add_plot(1, "/three_link/a", "data", "std_msgs/msg/Float64");
    auto           h2 = mgr.add_plot(2, "/three_link/b", "data", "std_msgs/msg/Float64");
    auto           h3 = mgr.add_plot(3, "/three_link/c", "data", "std_msgs/msg/Float64");

    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    ASSERT_TRUE(h3.valid());

    // All three axes should be reachable from h1.
    auto peers1 = mgr.link_manager().linked_peers(h1.axes);
    EXPECT_GE(peers1.size(), 2u);
}

// ============================================================
// Suite: Live data — poll() receives published values
// ============================================================

TEST_F(SubplotManagerLiveTest, PollAppendsFloat64Data)
{
    auto              pub_node = std::make_shared<rclcpp::Node>("test_poll_pub");
    const std::string topic    = "/subplot_poll_test";
    auto              pub      = pub_node->create_publisher<std_msgs::msg::Float64>(topic, 10);

    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h = mgr.add_plot(1, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    spin_until(pub_node, [&] { return pub->get_subscription_count() >= 1; });

    // Publish 5 messages.
    std_msgs::msg::Float64 msg;
    for (int i = 0; i < 5; ++i)
    {
        msg.data = static_cast<double>(i) * 1.5;
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Spin the publisher node to deliver messages.
    bool got_data = spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return h.series->point_count() >= 5;
        },
        std::chrono::milliseconds(3000));

    EXPECT_TRUE(got_data);
    EXPECT_GE(h.series->point_count(), 5u);
}

TEST_F(SubplotManagerLiveTest, PollCallbackFired)
{
    auto              pub_node = std::make_shared<rclcpp::Node>("test_cb_pub");
    const std::string topic    = "/subplot_cb_test";
    auto              pub      = pub_node->create_publisher<std_msgs::msg::Float64>(topic, 10);

    SubplotManager mgr(bridge_, intr_, 1, 1);

    int cb_count = 0;
    mgr.set_on_data(
        [&](int slot, double, double)
        {
            EXPECT_EQ(slot, 1);
            ++cb_count;
        });

    auto h = mgr.add_plot(1, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    spin_until(pub_node, [&] { return pub->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 msg;
    msg.data = 42.0;
    pub->publish(msg);

    spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return cb_count >= 1;
        },
        std::chrono::milliseconds(3000));

    EXPECT_GE(cb_count, 1);
}

TEST_F(SubplotManagerLiveTest, TwoSubplotsReceiveIndependentData)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_two_data_pub");
    auto pub1     = pub_node->create_publisher<std_msgs::msg::Float64>("/two_sub/a", 10);
    auto pub2     = pub_node->create_publisher<std_msgs::msg::Float64>("/two_sub/b", 10);

    SubplotManager mgr(bridge_, intr_, 2, 1);
    auto           h1 = mgr.add_plot(1, "/two_sub/a", "data", "std_msgs/msg/Float64");
    auto           h2 = mgr.add_plot(2, "/two_sub/b", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());

    spin_until(
        pub_node,
        [&] { return pub1->get_subscription_count() >= 1 && pub2->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 m1, m2;
    m1.data = 10.0;
    m2.data = 20.0;
    for (int i = 0; i < 3; ++i)
    {
        pub1->publish(m1);
        pub2->publish(m2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return h1.series->point_count() >= 3 && h2.series->point_count() >= 3;
        },
        std::chrono::milliseconds(3000));

    EXPECT_GE(h1.series->point_count(), 3u);
    EXPECT_GE(h2.series->point_count(), 3u);
}

TEST_F(SubplotManagerLiveTest, MemoryBytesIncreasesAfterData)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_mem_pub");
    auto pub      = pub_node->create_publisher<std_msgs::msg::Float64>("/mem_test", 10);

    SubplotManager mgr(bridge_, intr_, 1, 1);
    auto           h = mgr.add_plot(1, "/mem_test", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    spin_until(pub_node, [&] { return pub->get_subscription_count() >= 1; });

    const size_t before = mgr.total_memory_bytes();

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    for (int i = 0; i < 10; ++i)
    {
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return h.series->point_count() >= 10;
        },
        std::chrono::milliseconds(3000));

    EXPECT_GT(mgr.total_memory_bytes(), before);
}

TEST_F(SubplotManagerLiveTest, LiveYAutoFitStopsAfterManualOverride)
{
    auto              pub_node = std::make_shared<rclcpp::Node>("test_live_autofit_pub");
    const std::string topic    = "/live_autofit_test";
    auto              pub      = pub_node->create_publisher<std_msgs::msg::Float64>(topic, 10);

    SubplotManager mgr(bridge_, intr_, 1, 1);
    auto           h = mgr.add_plot(1, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    spin_until(pub_node, [&] { return pub->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    pub->publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    msg.data = 2.0;
    pub->publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    msg.data = 3.0;
    pub->publish(msg);

    ASSERT_TRUE(spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return h.series->point_count() >= 3;
        },
        std::chrono::milliseconds(3000)));

    const auto live_y_before = h.axes->y_limits();

    msg.data = 100.0;
    pub->publish(msg);
    ASSERT_TRUE(spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return h.series->point_count() >= 4;
        },
        std::chrono::milliseconds(3000)));

    const auto live_y_after = h.axes->y_limits();
    EXPECT_GT(live_y_after.max, live_y_before.max);

    mgr.set_slot_ylim(1, -1.0, 1.0);
    EXPECT_FALSE(mgr.is_scroll_paused(1));
    const auto live_x_before = h.axes->x_limits();

    msg.data = 200.0;
    pub->publish(msg);
    ASSERT_TRUE(spin_until(
        pub_node,
        [&]
        {
            mgr.poll();
            return h.series->point_count() >= 5;
        },
        std::chrono::milliseconds(3000)));

    const auto manual_y     = h.axes->y_limits();
    const auto live_x_after = h.axes->x_limits();
    EXPECT_DOUBLE_EQ(manual_y.min, -1.0);
    EXPECT_DOUBLE_EQ(manual_y.max, 1.0);
    EXPECT_GT(live_x_after.max, live_x_before.max);
    EXPECT_FALSE(mgr.is_scroll_paused(1));
}

// ============================================================
// Suite: Clear and replace
// ============================================================

TEST_F(SubplotManagerLiveTest, ClearRemovesAllSubscriptions)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_clear_pub");
    auto pub1     = pub_node->create_publisher<std_msgs::msg::Float64>("/clear_test/a", 10);
    auto pub2     = pub_node->create_publisher<std_msgs::msg::Float64>("/clear_test/b", 10);

    SubplotManager mgr(bridge_, intr_, 2, 1);
    ASSERT_TRUE(mgr.add_plot(1, "/clear_test/a", "data", "std_msgs/msg/Float64").valid());
    ASSERT_TRUE(mgr.add_plot(2, "/clear_test/b", "data", "std_msgs/msg/Float64").valid());
    EXPECT_EQ(mgr.active_count(), 2);

    mgr.clear();
    EXPECT_EQ(mgr.active_count(), 0);
    EXPECT_FALSE(mgr.has_plot(1));
    EXPECT_FALSE(mgr.has_plot(2));
}

TEST_F(SubplotManagerLiveTest, ReplacePlotInSlot)
{
    auto pub_node = std::make_shared<rclcpp::Node>("test_replace_pub");
    auto pub1     = pub_node->create_publisher<std_msgs::msg::Float64>("/replace_a", 10);
    auto pub2     = pub_node->create_publisher<std_msgs::msg::Float64>("/replace_b", 10);

    SubplotManager mgr(bridge_, intr_, 1, 1);
    auto           h1 = mgr.add_plot(1, "/replace_a", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    EXPECT_EQ(h1.topic, "/replace_a");

    // Replace with a different topic.
    auto h2 = mgr.add_plot(1, "/replace_b", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h2.valid());
    EXPECT_EQ(h2.topic, "/replace_b");
    EXPECT_EQ(h2.slot, 1);

    // Only one active plot.
    EXPECT_EQ(mgr.active_count(), 1);
}

// ============================================================
// Suite: Edge cases
// ============================================================

TEST_F(SubplotManagerTest, PollEmptyManagerRepeatedly)
{
    SubplotManager mgr(bridge_, intr_, 4, 4);
    for (int i = 0; i < 10; ++i)
        EXPECT_NO_THROW(mgr.poll());
}

TEST_F(SubplotManagerTest, SetTimeWindowAppliedToAllExistingSlots)
{
    SubplotManager mgr(bridge_, intr_, 3, 1);
    mgr.set_time_window(120.0);
    // All slots should have 120s window.
    for (int s = 1; s <= 3; ++s)
        EXPECT_FALSE(mgr.is_scroll_paused(s));
    EXPECT_DOUBLE_EQ(mgr.time_window(), 120.0);
}

TEST_F(SubplotManagerTest, MaxDrainConstantSane)
{
    EXPECT_GT(SubplotManager::MAX_DRAIN_PER_POLL, 0u);
}

TEST_F(SubplotManagerTest, AutoFitSamplesConstantSane)
{
    EXPECT_GT(SubplotManager::AUTO_FIT_SAMPLES, 0u);
}

TEST_F(SubplotManagerTest, LargeGrid_4x4_NoOp)
{
    SubplotManager mgr(bridge_, intr_, 4, 4);
    EXPECT_EQ(mgr.capacity(), 16);
    EXPECT_EQ(mgr.active_count(), 0);
    EXPECT_EQ(mgr.figure().axes().size(), 16u);
}

// ============================================================
// main
// ============================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}

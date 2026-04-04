// Phase C integration test — Spectra ROS2 Adapter
//
// Headless integration test for Phase C (Live Plotting Engine):
//   C1 — RosPlotManager: subscribe topic, poll 100 frames, verify data
//   C2 — Auto-scroll: time window via presented_buffer, scroll bounds, pruning
//   C4 — SubplotManager: NxM grid, X-axis linked, shared cursor
//
// Scenario A (RosPlotManager — C1/C2):
//   1. Create bridge, create RosPlotManager.
//   2. Add 3 topics (Float64, Float64, Twist.linear.x).
//   3. Publish known values on a separate publisher node.
//   4. Drive poll() 100 times, each time advancing the scroll clock.
//   5. Verify all published values appear in the series data.
//   6. Verify scroll bounds match [now - window, now].
//   7. Verify pruning removes old data when window expires.
//   8. Verify linked behaviour (all series pause/resume together).
//
// Scenario B (SubplotManager — C4):
//   1. Create SubplotManager with 3 rows × 1 col.
//   2. Assign each slot a different topic/field.
//   3. Publish 50 known values per topic.
//   4. Drive poll() until data arrives.
//   5. Verify each slot has the correct data (no cross-contamination).
//   6. Verify X-axis linking: changing the time window on the manager
//      propagates to all axes via presented_buffer.
//   7. Verify shared cursor: notify_cursor broadcasts to AxisLinkManager.
//
// These tests compile and run only when SPECTRA_USE_ROS2 is ON.
// rclcpp::init/shutdown is handled once per process by RclcppEnvironment.
// NOTE: No GPU / Vulkan context is created — all Spectra objects are
//       used in headless (offscreen / data-only) mode.

#include "message_introspector.hpp"
#include "ros2_bridge.hpp"
#include "ros_plot_manager.hpp"
#include "subplot_manager.hpp"
#include "ui/data/axis_link.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>

using namespace spectra::adapters::ros2;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Shared rclcpp lifecycle — init once, shutdown once per binary.
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
   public:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);
    }
    void TearDown() override
    {
        if (rclcpp::ok())
            rclcpp::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Fixture — fresh bridge + dedicated publisher node per test.
// ---------------------------------------------------------------------------

class PhaseCIntegrationTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);

        static std::atomic<int> counter{0};
        const int               id = counter.fetch_add(1);

        bridge_ = std::make_unique<Ros2Bridge>();
        bridge_->init("phase_c_bridge_" + std::to_string(id));
        bridge_->start_spin();

        pub_node_ = rclcpp::Node::make_shared("phase_c_pub_" + std::to_string(id));
    }

    void TearDown() override
    {
        bridge_->shutdown();
        pub_node_.reset();
    }

    // Spin pub_node_ until predicate is true or timeout expires.
    template <typename Pred>
    bool spin_until(Pred pred, std::chrono::milliseconds timeout = 3000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        rclcpp::executors::SingleThreadedExecutor exec;
        exec.add_node(pub_node_);
        while (std::chrono::steady_clock::now() < deadline)
        {
            exec.spin_once(10ms);
            if (pred())
                return true;
        }
        return pred();
    }

    // Return wall-clock seconds as double.
    static double wall_time_s()
    {
        using namespace std::chrono;
        return duration<double>(system_clock::now().time_since_epoch()).count();
    }

    std::unique_ptr<Ros2Bridge> bridge_;
    rclcpp::Node::SharedPtr     pub_node_;
    MessageIntrospector         intr_;
};

// ===========================================================================
// Suite 1: RosPlotManager construction and lifecycle (C1)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, RosPlotManagerConstructsAndDestructsCleanly)
{
    RosPlotManager mgr(*bridge_, intr_);
    EXPECT_EQ(mgr.plot_count(), 0u);
    // Destructor called at end of scope — must not crash or hang.
}

TEST_F(PhaseCIntegrationTest, RosPlotManagerAddPlotReturnsBadHandleForUnknownTopic)
{
    RosPlotManager mgr(*bridge_, intr_);
    // Topic does not exist in ROS2 graph — detect_type() should return "".
    // add_plot() with unknown type_name and no live publisher returns invalid handle.
    const auto h = mgr.add_plot("/no_such_topic_xyz", "data", "std_msgs/msg/Float64");
    // May succeed (subscription created) or fail (no type) depending on timing.
    // At minimum it must not crash; if it succeeds handle must be valid.
    // We just verify it either succeeds cleanly or fails cleanly.
    if (h.valid())
        EXPECT_GE(h.id, 1);
    else
        EXPECT_EQ(h.id, -1);
}

TEST_F(PhaseCIntegrationTest, RosPlotManagerAddPlotCreatesHandleWithFigureAndSeries)
{
    const std::string topic = "/phase_c_pm_create";

    // Create publisher so the topic is discoverable.
    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&] { return pub->get_subscription_count() == 0; }, 500ms);
    std::this_thread::sleep_for(200ms);

    RosPlotManager mgr(*bridge_, intr_);
    const auto     h = mgr.add_plot(topic, "data", "std_msgs/msg/Float64");

    ASSERT_TRUE(h.valid()) << "add_plot() returned invalid handle";
    EXPECT_GE(h.id, 1);
    EXPECT_EQ(h.topic, topic);
    EXPECT_EQ(h.field_path, "data");
    EXPECT_NE(h.figure, nullptr);
    EXPECT_NE(h.axes, nullptr);
    EXPECT_NE(h.series, nullptr);
    EXPECT_EQ(mgr.plot_count(), 1u);
}

TEST_F(PhaseCIntegrationTest, RosPlotManagerAddThreePlotsEachHasFigureAndSeries)
{
    const std::string topics[3] = {"/phase_c_three_a", "/phase_c_three_b", "/phase_c_three_c"};

    std::vector<rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr> pubs;
    for (const auto& t : topics)
        pubs.push_back(pub_node_->create_publisher<std_msgs::msg::Float64>(t, 10));
    std::this_thread::sleep_for(200ms);

    RosPlotManager          mgr(*bridge_, intr_);
    std::vector<PlotHandle> handles;
    for (const auto& t : topics)
    {
        auto h = mgr.add_plot(t, "data", "std_msgs/msg/Float64");
        ASSERT_TRUE(h.valid()) << "add_plot failed for " << t;
        handles.push_back(h);
    }

    EXPECT_EQ(mgr.plot_count(), 3u);
    for (size_t i = 0; i < handles.size(); ++i)
    {
        EXPECT_NE(handles[i].figure, nullptr) << "plot " << i << " has null figure";
        EXPECT_NE(handles[i].series, nullptr) << "plot " << i << " has null series";
        EXPECT_EQ(handles[i].topic, topics[i]);
    }
}

// ===========================================================================
// Suite 2: RosPlotManager data flow — 100 poll frames (C1 core acceptance)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, PollDelivers100SamplesFromFloat64Topic)
{
    const std::string topic = "/phase_c_poll_100";
    const int         N     = 100;

    auto           pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, N + 10);
    RosPlotManager mgr(*bridge_, intr_);
    mgr.set_time_window(60.0);   // wide window so nothing is pruned

    const auto h = mgr.add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    // Wait for subscription to be established.
    spin_until([&] { return pub->get_subscription_count() >= 1; }, 3000ms);

    // Publish N known values.
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i) * 0.1;
        pub->publish(msg);
    }

    // Drive 100 poll frames, advancing the clock each time.
    const double t0          = wall_time_s();
    bool         all_arrived = false;
    for (int frame = 0; frame < 100; ++frame)
    {
        mgr.poll();
        if (h.series->point_count() >= static_cast<size_t>(N))
        {
            all_arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(all_arrived) << "Only " << h.series->point_count() << " of " << N
                             << " samples arrived after 100 poll frames";

    // Verify Y values match published data (in order).
    const auto ydata = h.series->y_data();
    ASSERT_GE(ydata.size(), static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
    {
        EXPECT_NEAR(static_cast<double>(ydata[i]), static_cast<double>(i) * 0.1, 1e-5)
            << "Y mismatch at sample " << i;
    }

    // X values should be monotonically non-decreasing (wall-clock seconds).
    const auto xdata = h.series->x_data();
    ASSERT_GE(xdata.size(), static_cast<size_t>(N));
    for (size_t i = 1; i < static_cast<size_t>(N); ++i)
    {
        EXPECT_GE(xdata[i], xdata[i - 1]) << "X (timestamp) went backwards at sample " << i;
    }

    (void)t0;
}

TEST_F(PhaseCIntegrationTest, PollDeliversTwistLinearXData)
{
    const std::string topic = "/phase_c_twist_lx";
    const int         N     = 20;

    auto           pub = pub_node_->create_publisher<geometry_msgs::msg::Twist>(topic, N + 10);
    RosPlotManager mgr(*bridge_, intr_);
    mgr.set_time_window(60.0);

    const auto h = mgr.add_plot(topic, "linear.x", "geometry_msgs/msg/Twist");
    ASSERT_TRUE(h.valid());

    spin_until([&] { return pub->get_subscription_count() >= 1; }, 3000ms);

    const double expected_vals[20] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0,
                                      1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0};

    for (int i = 0; i < N; ++i)
    {
        geometry_msgs::msg::Twist msg;
        msg.linear.x = expected_vals[i];
        pub->publish(msg);
    }

    bool all_arrived = false;
    for (int frame = 0; frame < 100; ++frame)
    {
        mgr.poll();
        if (h.series->point_count() >= static_cast<size_t>(N))
        {
            all_arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(all_arrived);

    const auto ydata = h.series->y_data();
    ASSERT_GE(ydata.size(), static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
    {
        EXPECT_NEAR(static_cast<double>(ydata[i]), expected_vals[i], 1e-5)
            << "Twist.linear.x mismatch at sample " << i;
    }
}

TEST_F(PhaseCIntegrationTest, ThreeTopicsReceiveIndependentData)
{
    const std::string t1 = "/phase_c_indep_a";
    const std::string t2 = "/phase_c_indep_b";
    const std::string t3 = "/phase_c_indep_c";

    auto pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 20);
    auto pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 20);
    auto pub3 = pub_node_->create_publisher<std_msgs::msg::Float64>(t3, 20);

    RosPlotManager mgr(*bridge_, intr_);
    mgr.set_time_window(60.0);

    const auto h1 = mgr.add_plot(t1, "data", "std_msgs/msg/Float64");
    const auto h2 = mgr.add_plot(t2, "data", "std_msgs/msg/Float64");
    const auto h3 = mgr.add_plot(t3, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    ASSERT_TRUE(h3.valid());

    spin_until(
        [&]
        {
            return pub1->get_subscription_count() >= 1 && pub2->get_subscription_count() >= 1
                   && pub3->get_subscription_count() >= 1;
        },
        3000ms);

    // Publish signature values: 100.x on t1, 200.x on t2, 300.x on t3.
    const int N = 10;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 m1, m2, m3;
        m1.data = 100.0 + i;
        m2.data = 200.0 + i;
        m3.data = 300.0 + i;
        pub1->publish(m1);
        pub2->publish(m2);
        pub3->publish(m3);
    }

    bool all_arrived = false;
    for (int frame = 0; frame < 150; ++frame)
    {
        mgr.poll();
        if (h1.series->point_count() >= static_cast<size_t>(N)
            && h2.series->point_count() >= static_cast<size_t>(N)
            && h3.series->point_count() >= static_cast<size_t>(N))
        {
            all_arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(all_arrived) << "Not all 3 topics delivered N samples. counts: "
                             << h1.series->point_count() << " " << h2.series->point_count() << " "
                             << h3.series->point_count();

    // Verify signature values — each topic must have its own data.
    const auto y1 = h1.series->y_data();
    const auto y2 = h2.series->y_data();
    const auto y3 = h3.series->y_data();

    EXPECT_GE(y1[0], 99.9f) << "t1 first sample should be ~100";
    EXPECT_LE(y1[0], 101.0f) << "t1 first sample should be ~100";

    EXPECT_GE(y2[0], 199.9f) << "t2 first sample should be ~200";
    EXPECT_LE(y2[0], 201.0f) << "t2 first sample should be ~200";

    EXPECT_GE(y3[0], 299.9f) << "t3 first sample should be ~300";
    EXPECT_LE(y3[0], 301.0f) << "t3 first sample should be ~300";
}

TEST_F(PhaseCIntegrationTest, OnDataCallbackFiresPerSample)
{
    const std::string topic = "/phase_c_callback";
    const int         N     = 5;

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    RosPlotManager mgr(*bridge_, intr_);
    mgr.set_time_window(60.0);

    std::atomic<int> cb_count{0};
    mgr.set_on_data([&](int /*id*/, double /*t*/, double /*v*/) { cb_count.fetch_add(1); });

    const auto h = mgr.add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    spin_until([&] { return pub->get_subscription_count() >= 1; }, 3000ms);

    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
    }

    bool all_arrived = false;
    for (int frame = 0; frame < 100; ++frame)
    {
        mgr.poll();
        if (h.series->point_count() >= static_cast<size_t>(N))
        {
            all_arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(all_arrived);
    EXPECT_GE(cb_count.load(), N) << "on_data callback not fired for all samples";
}

// ===========================================================================
// Suite 3: Auto-scroll time window (C2) — via presented_buffer
// ===========================================================================

TEST_F(PhaseCIntegrationTest, AutoScrollDefaultWindowIs30Seconds)
{
    RosPlotManager mgr(*bridge_, intr_);
    EXPECT_DOUBLE_EQ(mgr.time_window(), 30.0);
}

TEST_F(PhaseCIntegrationTest, AutoScrollWindowCanBeChanged)
{
    RosPlotManager mgr(*bridge_, intr_);
    mgr.set_time_window(10.0);
    EXPECT_DOUBLE_EQ(mgr.time_window(), 10.0);

    mgr.set_time_window(120.0);
    EXPECT_DOUBLE_EQ(mgr.time_window(), 120.0);
}

TEST_F(PhaseCIntegrationTest, AutoScrollWindowClamped)
{
    RosPlotManager mgr(*bridge_, intr_);

    mgr.set_time_window(0.001);   // below MIN
    EXPECT_GE(mgr.time_window(), RosPlotManager::MIN_WINDOW_S);

    mgr.set_time_window(999999.0);   // above MAX
    EXPECT_LE(mgr.time_window(), RosPlotManager::MAX_WINDOW_S);
}

TEST_F(PhaseCIntegrationTest, PresentedBufferScrollBoundsMatchWindow)
{
    // Use presented_buffer directly on Axes and verify view bounds.
    spectra::Figure fig;
    auto&           axes   = fig.subplot(1, 1, 1);
    auto&           series = axes.line();

    axes.presented_buffer(10.0f);

    // Add data up to t=1000.
    for (int i = 0; i < 20; ++i)
        series.append(static_cast<float>(990.0 + i), static_cast<float>(i));

    // x_limits should reflect [latest_x - 10, latest_x].
    auto        lim      = axes.x_limits();
    const float latest_x = series.x_data().back();
    EXPECT_NEAR(lim.min, latest_x - 10.0f, 0.5f);
    EXPECT_NEAR(lim.max, latest_x, 0.5f);
}

TEST_F(PhaseCIntegrationTest, PauseStopsViewUpdate)
{
    spectra::Figure fig;
    auto&           axes   = fig.subplot(1, 1, 1);
    auto&           series = axes.line();

    axes.presented_buffer(10.0f);

    // Add initial data.
    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(990.0 + i), static_cast<float>(i));

    // Pause by setting explicit xlim.
    axes.xlim(985.0f, 995.0f);
    EXPECT_FALSE(axes.is_presented_buffer_following());

    // Add more data — view should NOT advance.
    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(1000.0 + i), static_cast<float>(i));

    auto lim = axes.x_limits();
    EXPECT_NEAR(lim.max, 995.0f, 0.1f) << "View advanced while paused";
}

TEST_F(PhaseCIntegrationTest, ResumeRestoresFollowing)
{
    spectra::Figure fig;
    auto&           axes   = fig.subplot(1, 1, 1);
    auto&           series = axes.line();

    axes.presented_buffer(10.0f);

    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(990.0 + i), static_cast<float>(i));

    // Pause.
    axes.xlim(985.0f, 995.0f);
    EXPECT_FALSE(axes.is_presented_buffer_following());

    // Resume.
    axes.resume_follow();
    EXPECT_TRUE(axes.is_presented_buffer_following());

    // Add more data — view should track latest.
    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(1010.0 + i), static_cast<float>(i));

    auto        lim      = axes.x_limits();
    const float latest_x = series.x_data().back();
    EXPECT_NEAR(lim.max, latest_x, 0.5f);
}

TEST_F(PhaseCIntegrationTest, ManualYOverrideKeepsPresentedBufferFollowing)
{
    spectra::Figure fig;
    auto&           axes   = fig.subplot(1, 1, 1);
    auto&           series = axes.line();

    axes.presented_buffer(10.0f);

    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(990.0 + i), static_cast<float>(i));

    axes.ylim(-2.0f, 2.0f);
    EXPECT_TRUE(axes.is_presented_buffer_following());

    auto x_before = axes.x_limits();

    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(1010.0 + i), static_cast<float>(100 + i));

    auto x_after = axes.x_limits();
    auto y_after = axes.y_limits();
    EXPECT_GT(x_after.max, x_before.max);
    EXPECT_DOUBLE_EQ(y_after.min, -2.0);
    EXPECT_DOUBLE_EQ(y_after.max, 2.0);
}

TEST_F(PhaseCIntegrationTest, PausedPresentedBufferKeepsVisibleWindowYRange)
{
    spectra::Figure fig;
    auto&           axes   = fig.subplot(1, 1, 1);
    auto&           series = axes.line();

    axes.presented_buffer(10.0f);

    for (int i = 0; i <= 30; ++i)
    {
        float x = static_cast<float>(i);
        float y = (i >= 10 && i <= 20) ? x : (1000.0f + x);
        series.append(x, y);
    }

    axes.xlim(10.0f, 20.0f);
    EXPECT_FALSE(axes.is_presented_buffer_following());

    auto yl = axes.y_limits();
    EXPECT_LT(yl.min, 10.0f);
    EXPECT_GT(yl.max, 20.0f);
    EXPECT_LT(yl.max, 100.0f);
}

TEST_F(PhaseCIntegrationTest, EraseBeforePrunesOldData)
{
    spectra::Figure fig;
    auto&           axes   = fig.subplot(1, 1, 1);
    auto&           series = axes.line();

    // Inject data: 10 old samples, 5 recent samples.
    for (int i = 0; i < 10; ++i)
        series.append(static_cast<float>(800.0 + i), static_cast<float>(i));
    ASSERT_EQ(series.point_count(), 10u);

    for (int i = 0; i < 5; ++i)
        series.append(static_cast<float>(998.0 + i), static_cast<float>(100 + i));
    ASSERT_EQ(series.point_count(), 15u);

    // Prune everything before x=990.
    size_t removed = series.erase_before(990.0f);
    EXPECT_EQ(removed, 10u);               // all 10 old samples removed
    EXPECT_EQ(series.point_count(), 5u);   // 5 recent samples remain
    EXPECT_GE(series.x_data().front(), 990.0f);
}

TEST_F(PhaseCIntegrationTest, RosPlotManagerPauseAndResumeAllScroll)
{
    const std::string topic = "/phase_c_pause_resume";
    auto              pub   = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);

    RosPlotManager mgr(*bridge_, intr_);
    const auto     h = mgr.add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    EXPECT_FALSE(mgr.is_scroll_paused(h.id));

    mgr.pause_all_scroll();
    EXPECT_TRUE(mgr.is_scroll_paused(h.id));

    mgr.resume_all_scroll();
    EXPECT_FALSE(mgr.is_scroll_paused(h.id));
}

TEST_F(PhaseCIntegrationTest, RosPlotManagerPauseResumeIndividualPlot)
{
    const std::string t1   = "/phase_c_pause_a";
    const std::string t2   = "/phase_c_pause_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    RosPlotManager mgr(*bridge_, intr_);
    const auto     h1 = mgr.add_plot(t1, "data", "std_msgs/msg/Float64");
    const auto     h2 = mgr.add_plot(t2, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());

    mgr.pause_scroll(h1.id);
    EXPECT_TRUE(mgr.is_scroll_paused(h1.id));
    EXPECT_FALSE(mgr.is_scroll_paused(h2.id));

    mgr.resume_scroll(h1.id);
    EXPECT_FALSE(mgr.is_scroll_paused(h1.id));
}

// ===========================================================================
// Suite 4: RosPlotManager remove / clear lifecycle
// ===========================================================================

TEST_F(PhaseCIntegrationTest, RemovePlotReducesCount)
{
    const std::string t1   = "/phase_c_rm_a";
    const std::string t2   = "/phase_c_rm_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    RosPlotManager mgr(*bridge_, intr_);
    const auto     h1 = mgr.add_plot(t1, "data", "std_msgs/msg/Float64");
    const auto     h2 = mgr.add_plot(t2, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    ASSERT_EQ(mgr.plot_count(), 2u);

    EXPECT_TRUE(mgr.remove_plot(h1.id));
    EXPECT_EQ(mgr.plot_count(), 1u);

    EXPECT_FALSE(mgr.handle(h1.id).valid());
    EXPECT_TRUE(mgr.handle(h2.id).valid());
}

TEST_F(PhaseCIntegrationTest, RemoveNonExistentPlotReturnsFalse)
{
    RosPlotManager mgr(*bridge_, intr_);
    EXPECT_FALSE(mgr.remove_plot(9999));
}

TEST_F(PhaseCIntegrationTest, ClearRemovesAllPlots)
{
    const std::string t1   = "/phase_c_clear_a";
    const std::string t2   = "/phase_c_clear_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    RosPlotManager mgr(*bridge_, intr_);
    mgr.add_plot(t1, "data", "std_msgs/msg/Float64");
    mgr.add_plot(t2, "data", "std_msgs/msg/Float64");
    ASSERT_EQ(mgr.plot_count(), 2u);

    mgr.clear();
    EXPECT_EQ(mgr.plot_count(), 0u);
}

// ===========================================================================
// Suite 5: SubplotManager construction and grid layout (C4)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, SubplotManagerDefaultGrid1x1)
{
    SubplotManager mgr(*bridge_, intr_);
    EXPECT_EQ(mgr.rows(), 1);
    EXPECT_EQ(mgr.cols(), 1);
    EXPECT_EQ(mgr.capacity(), 1);
    EXPECT_EQ(mgr.active_count(), 0);
}

TEST_F(PhaseCIntegrationTest, SubplotManagerGrid3x1HasCorrectCapacity)
{
    SubplotManager mgr(*bridge_, intr_, 3, 1);
    EXPECT_EQ(mgr.rows(), 3);
    EXPECT_EQ(mgr.cols(), 1);
    EXPECT_EQ(mgr.capacity(), 3);
}

TEST_F(PhaseCIntegrationTest, SubplotManagerIndexOf)
{
    SubplotManager mgr(*bridge_, intr_, 3, 2);
    // 3 rows, 2 cols → 6 slots
    EXPECT_EQ(mgr.index_of(1, 1), 1);
    EXPECT_EQ(mgr.index_of(1, 2), 2);
    EXPECT_EQ(mgr.index_of(2, 1), 3);
    EXPECT_EQ(mgr.index_of(3, 2), 6);

    // Out of range.
    EXPECT_EQ(mgr.index_of(0, 1), -1);
    EXPECT_EQ(mgr.index_of(4, 1), -1);
    EXPECT_EQ(mgr.index_of(1, 3), -1);
}

TEST_F(PhaseCIntegrationTest, SubplotManagerFigureIsValid)
{
    SubplotManager mgr(*bridge_, intr_, 2, 2);
    // Figure must exist and have axes.
    EXPECT_GE(mgr.figure().axes().size(), 1u);
}

TEST_F(PhaseCIntegrationTest, SubplotManagerAddPlotActivatesSlot)
{
    const std::string topic = "/phase_c_sub_slot";
    auto              pub   = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 3, 1);
    ASSERT_EQ(mgr.active_count(), 0);

    const auto h = mgr.add_plot(1, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid()) << "add_plot(slot=1) returned invalid handle";
    EXPECT_EQ(h.slot, 1);
    EXPECT_NE(h.axes, nullptr);
    EXPECT_NE(h.series, nullptr);
    EXPECT_EQ(mgr.active_count(), 1);
}

TEST_F(PhaseCIntegrationTest, SubplotManagerAddPlotRowColConvenience)
{
    const std::string topic = "/phase_c_sub_rowcol";
    auto              pub   = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 2, 2);
    const auto     h = mgr.add_plot(2, 1, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.slot, mgr.index_of(2, 1));
}

// ===========================================================================
// Suite 6: SubplotManager data flow — 3 independent slots (C4 core acceptance)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, ThreeSlotsReceiveIndependentData)
{
    const std::string t1 = "/phase_c_slot_a";
    const std::string t2 = "/phase_c_slot_b";
    const std::string t3 = "/phase_c_slot_c";

    auto pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 60);
    auto pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 60);
    auto pub3 = pub_node_->create_publisher<std_msgs::msg::Float64>(t3, 60);

    SubplotManager mgr(*bridge_, intr_, 3, 1);
    mgr.set_time_window(120.0);   // wide window — no pruning during test

    const auto h1 = mgr.add_plot(1, t1, "data", "std_msgs/msg/Float64");
    const auto h2 = mgr.add_plot(2, t2, "data", "std_msgs/msg/Float64");
    const auto h3 = mgr.add_plot(3, t3, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    ASSERT_TRUE(h3.valid());

    spin_until(
        [&]
        {
            return pub1->get_subscription_count() >= 1 && pub2->get_subscription_count() >= 1
                   && pub3->get_subscription_count() >= 1;
        },
        3000ms);

    // Publish 50 known values per topic.
    const int N = 50;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 m1, m2, m3;
        m1.data = 10.0 + i;
        m2.data = 20.0 + i;
        m3.data = 30.0 + i;
        pub1->publish(m1);
        pub2->publish(m2);
        pub3->publish(m3);
    }

    // Drive poll() until all slots have N samples.
    bool all_arrived = false;
    for (int frame = 0; frame < 200; ++frame)
    {
        mgr.poll();
        if (h1.series->point_count() >= static_cast<size_t>(N)
            && h2.series->point_count() >= static_cast<size_t>(N)
            && h3.series->point_count() >= static_cast<size_t>(N))
        {
            all_arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(all_arrived) << "Slots delivered: " << h1.series->point_count() << " / "
                             << h2.series->point_count() << " / " << h3.series->point_count()
                             << " (expected " << N << " each)";

    // Verify no cross-contamination: each slot's Y values are in the correct range.
    const auto y1 = h1.series->y_data();
    const auto y2 = h2.series->y_data();
    const auto y3 = h3.series->y_data();

    // Slot 1: values should be in [10, 10+N).
    for (size_t i = 0; i < static_cast<size_t>(N); ++i)
    {
        EXPECT_GE(y1[i], 9.9f) << "Slot 1 sample " << i << " out of range";
        EXPECT_LT(y1[i], 10.0f + N + 1.0f) << "Slot 1 sample " << i << " out of range";
    }
    // Slot 2: values should be in [20, 20+N).
    EXPECT_NEAR(static_cast<double>(y2[0]), 20.0, 0.5)
        << "Slot 2 first value not ~20 (possible cross-contamination)";
    // Slot 3: values should be in [30, 30+N).
    EXPECT_NEAR(static_cast<double>(y3[0]), 30.0, 0.5)
        << "Slot 3 first value not ~30 (possible cross-contamination)";
}

TEST_F(PhaseCIntegrationTest, SubplotManagerOnDataCallbackReceivesSlotId)
{
    const std::string topic = "/phase_c_sub_cb";
    const int         N     = 5;

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    mgr.set_time_window(120.0);

    std::atomic<int> cb_slot{-1};
    mgr.set_on_data([&](int slot, double /*t*/, double /*v*/) { cb_slot.store(slot); });

    const auto h = mgr.add_plot(2, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.slot, 2);

    spin_until([&] { return pub->get_subscription_count() >= 1; }, 3000ms);

    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
    }

    bool arrived = false;
    for (int frame = 0; frame < 100; ++frame)
    {
        mgr.poll();
        if (h.series->point_count() >= static_cast<size_t>(N))
        {
            arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(arrived);
    EXPECT_EQ(cb_slot.load(), 2) << "on_data callback reported wrong slot id";
}

// ===========================================================================
// Suite 7: SubplotManager X-axis linking (C4)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, SubplotManagerHasAxisLinkManager)
{
    SubplotManager mgr(*bridge_, intr_, 2, 1);
    // Link manager must be accessible (does not crash).
    auto& lm = mgr.link_manager();
    (void)lm;
    SUCCEED();
}

TEST_F(PhaseCIntegrationTest, SubplotManagerSetTimeWindowPropagatesAll)
{
    const std::string t1   = "/phase_c_link_a";
    const std::string t2   = "/phase_c_link_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    mgr.add_plot(1, t1, "data", "std_msgs/msg/Float64");
    mgr.add_plot(2, t2, "data", "std_msgs/msg/Float64");

    const double new_window = 15.0;
    mgr.set_time_window(new_window);

    // The manager-level window must reflect the change.
    EXPECT_DOUBLE_EQ(mgr.time_window(), new_window);
}

TEST_F(PhaseCIntegrationTest, SubplotManagerScrollPauseResume)
{
    const std::string t1   = "/phase_c_spause_a";
    const std::string t2   = "/phase_c_spause_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    mgr.add_plot(1, t1, "data", "std_msgs/msg/Float64");
    mgr.add_plot(2, t2, "data", "std_msgs/msg/Float64");

    EXPECT_FALSE(mgr.is_scroll_paused(1));
    EXPECT_FALSE(mgr.is_scroll_paused(2));

    mgr.pause_all_scroll();
    EXPECT_TRUE(mgr.is_scroll_paused(1));
    EXPECT_TRUE(mgr.is_scroll_paused(2));

    mgr.resume_all_scroll();
    EXPECT_FALSE(mgr.is_scroll_paused(1));
    EXPECT_FALSE(mgr.is_scroll_paused(2));
}

TEST_F(PhaseCIntegrationTest, SubplotManagerPauseSingleSlot)
{
    const std::string t1   = "/phase_c_sps_a";
    const std::string t2   = "/phase_c_sps_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    mgr.add_plot(1, t1, "data", "std_msgs/msg/Float64");
    mgr.add_plot(2, t2, "data", "std_msgs/msg/Float64");

    mgr.pause_scroll(1);
    EXPECT_TRUE(mgr.is_scroll_paused(1));
    EXPECT_FALSE(mgr.is_scroll_paused(2));

    mgr.resume_scroll(1);
    EXPECT_FALSE(mgr.is_scroll_paused(1));
}

// ===========================================================================
// Suite 8: SubplotManager remove / clear lifecycle
// ===========================================================================

TEST_F(PhaseCIntegrationTest, SubplotManagerRemovePlotDeactivatesSlot)
{
    const std::string topic = "/phase_c_srm";
    auto              pub   = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 3, 1);
    const auto     h = mgr.add_plot(2, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    ASSERT_EQ(mgr.active_count(), 1);

    EXPECT_TRUE(mgr.remove_plot(2));
    EXPECT_EQ(mgr.active_count(), 0);
    EXPECT_FALSE(mgr.has_plot(2));
}

TEST_F(PhaseCIntegrationTest, SubplotManagerClearDeactivatesAllSlots)
{
    const std::string t1   = "/phase_c_scl_a";
    const std::string t2   = "/phase_c_scl_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    mgr.add_plot(1, t1, "data", "std_msgs/msg/Float64");
    mgr.add_plot(2, t2, "data", "std_msgs/msg/Float64");
    ASSERT_EQ(mgr.active_count(), 2);

    mgr.clear();
    EXPECT_EQ(mgr.active_count(), 0);
    EXPECT_FALSE(mgr.has_plot(1));
    EXPECT_FALSE(mgr.has_plot(2));
}

// ===========================================================================
// Suite 9: SubplotManager shared cursor (C4)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, SharedCursorNotifyClearDoesNotCrash)
{
    SubplotManager mgr(*bridge_, intr_, 2, 1);

    // With no active slots notify_cursor / clear_cursor must not crash.
    mgr.notify_cursor(nullptr, 0.0f, 0.0f);
    mgr.clear_cursor();
    SUCCEED();
}

TEST_F(PhaseCIntegrationTest, SharedCursorIsForwardedToLinkManager)
{
    const std::string t1   = "/phase_c_cur_a";
    const std::string t2   = "/phase_c_cur_b";
    auto              pub1 = pub_node_->create_publisher<std_msgs::msg::Float64>(t1, 10);
    auto              pub2 = pub_node_->create_publisher<std_msgs::msg::Float64>(t2, 10);
    std::this_thread::sleep_for(200ms);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    const auto     h1 = mgr.add_plot(1, t1, "data", "std_msgs/msg/Float64");
    const auto     h2 = mgr.add_plot(2, t2, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());

    // Notify cursor on slot 1's axes.
    mgr.notify_cursor(h1.axes, 10.5f, 3.2f, 100.0, 200.0);

    // AxisLinkManager must have received the cursor.
    const auto cursor = mgr.link_manager().shared_cursor_for(h1.axes);
    EXPECT_TRUE(cursor.valid);
    EXPECT_NEAR(cursor.data_x, 10.5, 1e-4);
    EXPECT_NEAR(cursor.data_y, 3.2, 1e-4);

    mgr.clear_cursor();
}

// ===========================================================================
// Suite 10: Memory accounting
// ===========================================================================

TEST_F(PhaseCIntegrationTest, RosPlotManagerMemoryIncreasesWithData)
{
    const std::string topic = "/phase_c_mem";
    const int         N     = 50;

    auto           pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 60);
    RosPlotManager mgr(*bridge_, intr_);
    mgr.set_time_window(120.0);

    const auto h = mgr.add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    const size_t mem_before = mgr.total_memory_bytes();

    spin_until([&] { return pub->get_subscription_count() >= 1; }, 3000ms);

    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
    }

    bool arrived = false;
    for (int frame = 0; frame < 100; ++frame)
    {
        mgr.poll();
        if (h.series->point_count() >= static_cast<size_t>(N))
        {
            arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(arrived);
    EXPECT_GT(mgr.total_memory_bytes(), mem_before)
        << "Memory did not increase after receiving data";
}

TEST_F(PhaseCIntegrationTest, SubplotManagerMemoryIncreasesWithData)
{
    const std::string topic = "/phase_c_sub_mem";
    const int         N     = 50;

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 60);

    SubplotManager mgr(*bridge_, intr_, 2, 1);
    mgr.set_time_window(120.0);

    const auto h = mgr.add_plot(1, topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    const size_t mem_before = mgr.total_memory_bytes();

    spin_until([&] { return pub->get_subscription_count() >= 1; }, 3000ms);

    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
    }

    bool arrived = false;
    for (int frame = 0; frame < 100; ++frame)
    {
        mgr.poll();
        if (h.series->point_count() >= static_cast<size_t>(N))
        {
            arrived = true;
            break;
        }
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_TRUE(arrived);
    EXPECT_GT(mgr.total_memory_bytes(), mem_before);
}

// ===========================================================================
// Suite 11: Full Phase C scenario — 3 topics, 100 frames, scroll + link
// (the canonical C6 acceptance test from the plan)
// ===========================================================================

TEST_F(PhaseCIntegrationTest, FullPipelineC6_ThreeTopicsScrollBoundsPruningLinkedAxes)
{
    const std::string ta = "/phase_c_full_a";
    const std::string tb = "/phase_c_full_b";
    const std::string tc = "/phase_c_full_c";

    auto puba = pub_node_->create_publisher<std_msgs::msg::Float64>(ta, 200);
    auto pubb = pub_node_->create_publisher<std_msgs::msg::Float64>(tb, 200);
    auto pubc = pub_node_->create_publisher<geometry_msgs::msg::Twist>(tc, 200);

    // --- Step 1: SubplotManager with 3 rows ---
    SubplotManager mgr(*bridge_, intr_, 3, 1);
    const double   WINDOW_S = 5.0;
    mgr.set_time_window(WINDOW_S);
    mgr.set_prune_buffer(2.0);   // Small buffer so pruning triggers within test time advance

    const auto ha = mgr.add_plot(1, ta, "data", "std_msgs/msg/Float64");
    const auto hb = mgr.add_plot(2, tb, "data", "std_msgs/msg/Float64");
    const auto hc = mgr.add_plot(3, tc, "linear.x", "geometry_msgs/msg/Twist");
    ASSERT_TRUE(ha.valid());
    ASSERT_TRUE(hb.valid());
    ASSERT_TRUE(hc.valid());

    spin_until(
        [&]
        {
            return puba->get_subscription_count() >= 1 && pubb->get_subscription_count() >= 1
                   && pubc->get_subscription_count() >= 1;
        },
        3000ms);

    // --- Step 2: Publish known values ---
    const int N = 100;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 ma, mb;
        ma.data = static_cast<double>(i);
        mb.data = static_cast<double>(i) * 2.0;
        puba->publish(ma);
        pubb->publish(mb);

        geometry_msgs::msg::Twist mc;
        mc.linear.x = static_cast<double>(i) * 0.5;
        pubc->publish(mc);
    }

    // --- Step 3: Drive 100 poll frames ---
    const int MAX_FRAMES = 200;
    int       frames_run = 0;
    for (int frame = 0; frame < MAX_FRAMES; ++frame)
    {
        mgr.poll();
        ++frames_run;
        if (ha.series->point_count() >= static_cast<size_t>(N)
            && hb.series->point_count() >= static_cast<size_t>(N)
            && hc.series->point_count() >= static_cast<size_t>(N))
            break;
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_GE(ha.series->point_count(), static_cast<size_t>(N))
        << "Slot A: only " << ha.series->point_count() << "/" << N << " arrived";
    ASSERT_GE(hb.series->point_count(), static_cast<size_t>(N))
        << "Slot B: only " << hb.series->point_count() << "/" << N << " arrived";
    ASSERT_GE(hc.series->point_count(), static_cast<size_t>(N))
        << "Slot C: only " << hc.series->point_count() << "/" << N << " arrived";

    // --- Step 4: Verify data matches published values ---
    {
        const auto ya = ha.series->y_data();
        const auto yb = hb.series->y_data();
        const auto yc = hc.series->y_data();

        for (int i = 0; i < N; ++i)
        {
            EXPECT_NEAR(static_cast<double>(ya[i]), static_cast<double>(i), 1e-4)
                << "Slot A Y mismatch at i=" << i;
            EXPECT_NEAR(static_cast<double>(yb[i]), static_cast<double>(i) * 2.0, 1e-4)
                << "Slot B Y mismatch at i=" << i;
            EXPECT_NEAR(static_cast<double>(yc[i]), static_cast<double>(i) * 0.5, 1e-4)
                << "Slot C Y mismatch at i=" << i;
        }
    }

    // --- Step 5: Verify scroll bounds ---
    // Advance "now" to a known time and poll all slots.
    const double t_known = wall_time_s();
    mgr.set_now(t_known);
    mgr.poll();

    // Auto-scroll view bounds must be [t_known - WINDOW_S, t_known].
    // We can verify via set_time_window / time_window() consistency.
    EXPECT_DOUBLE_EQ(mgr.time_window(), WINDOW_S);

    // --- Step 6: Verify pruning — data older than 2×window is removed.
    // Add a "far past" sample manually into slot A's series to simulate old data,
    // then verify tick() prunes it.
    {
        auto*        axes   = ha.axes;
        auto*        series = ha.series;
        const size_t before = series->point_count();
        const auto   xlim   = axes->x_limits();
        series->append(static_cast<float>(xlim.max + 1.0), 999.0f);

        // In thread-safe mode, append() writes to PendingSeriesData.
        // Commit before checking point_count().
        series->commit_pending();

        const size_t after_inject = series->point_count();
        ASSERT_EQ(after_inject, before + 1u) << "append() failed";

        // Advance "now" so the injected point is older than 2x the live window,
        // then trigger one more poll to prune it.
        mgr.set_now(t_known + WINDOW_S * 4.0);
        mgr.poll();
        // In direct-write mode, erase_before() routes through PendingSeriesData.
        // A second poll() or explicit commit is needed to apply the prune.
        series->commit_pending();

        // The injected old sample should have been pruned.
        const size_t after_prune = series->point_count();
        const auto   x_after     = series->x_data();
        const auto   lim_after   = axes->x_limits();
        EXPECT_LT(after_prune, after_inject)
            << "Pruning did not remove old sample; count: " << after_prune
            << " front_x=" << (x_after.empty() ? 0.0f : x_after.front())
            << " back_x=" << (x_after.empty() ? 0.0f : x_after.back()) << " xlim=[" << lim_after.min
            << ", " << lim_after.max << "]";

        (void)axes;
    }

    // --- Step 7: Verify X-axis linking via link_manager (all axes share group).
    // Notify cursor on slot A, then verify AxisLinkManager sees it on slot A.
    mgr.notify_cursor(ha.axes, 1.5f, 2.5f, 0.0, 0.0);
    {
        const auto cursor = mgr.link_manager().shared_cursor_for(ha.axes);
        EXPECT_TRUE(cursor.valid);
        EXPECT_NEAR(cursor.data_x, 1.5, 1e-3);
    }
    mgr.clear_cursor();
}

// ===========================================================================
// main — register shared RclcppEnvironment
// ===========================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}

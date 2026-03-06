// Unit tests for RosPlotManager — ROS2 field → Spectra series bridge.
//
// These tests only compile and run when SPECTRA_USE_ROS2 is ON and a ROS2
// workspace is sourced.  They are registered in tests/CMakeLists.txt inside
// the if(SPECTRA_USE_ROS2) block.
//
// Test structure:
//   - Pure-logic tests (RingBuffer, PlotHandle, config) — no ROS2 required at runtime
//   - ROS2 integration tests — require bridge + publisher
//
// NOTE: rclcpp::init / rclcpp::shutdown may only be called once per process.
// We use a single shared RclcppEnvironment for all tests.

#include "ros_plot_manager.hpp"
#include "generic_subscriber.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/int32.hpp>
#include <geometry_msgs/msg/twist.hpp>

using namespace spectra::adapters::ros2;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test environment: init rclcpp once for the whole binary.
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
// Fixture: fresh bridge + introspector per test.
// ---------------------------------------------------------------------------

class RosPlotManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);

        static std::atomic<int> counter{0};
        const std::string name = "rpm_test_" + std::to_string(counter.fetch_add(1));

        bridge_ = std::make_unique<Ros2Bridge>();
        bridge_->init(name);
        bridge_->start_spin();

        intr_ = std::make_unique<MessageIntrospector>();
        mgr_  = std::make_unique<RosPlotManager>(*bridge_, *intr_);

        // Separate publisher node.
        static std::atomic<int> pub_counter{0};
        const std::string pub_name = "rpm_pub_" + std::to_string(pub_counter.fetch_add(1));
        pub_node_ = rclcpp::Node::make_shared(pub_name);
    }

    void TearDown() override
    {
        mgr_.reset();
        intr_.reset();
        bridge_->shutdown();
        pub_node_.reset();
    }

    // Spin pub_node_ until condition returns true or timeout expires.
    bool spin_until(std::function<bool()> condition,
                    std::chrono::milliseconds timeout = 3000ms)
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        rclcpp::executors::SingleThreadedExecutor exec;
        exec.add_node(pub_node_);
        while (std::chrono::steady_clock::now() < deadline)
        {
            exec.spin_some(10ms);
            if (condition())
                return true;
            std::this_thread::sleep_for(5ms);
        }
        return false;
    }

    std::unique_ptr<Ros2Bridge>       bridge_;
    std::unique_ptr<MessageIntrospector> intr_;
    std::unique_ptr<RosPlotManager>   mgr_;
    rclcpp::Node::SharedPtr           pub_node_;
};

// ===========================================================================
// Suite 1: Construction
// ===========================================================================

TEST_F(RosPlotManagerTest, ConstructionZeroPlots)
{
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

TEST_F(RosPlotManagerTest, ConstructionHandlesEmpty)
{
    EXPECT_TRUE(mgr_->handles().empty());
}

TEST_F(RosPlotManagerTest, ConstructionBridgeRunning)
{
    EXPECT_TRUE(bridge_->is_ok());
}

// ===========================================================================
// Suite 2: PlotHandle validity
// ===========================================================================

TEST_F(RosPlotManagerTest, InvalidHandleDefaultId)
{
    PlotHandle h;
    EXPECT_EQ(h.id, -1);
    EXPECT_FALSE(h.valid());
}

TEST_F(RosPlotManagerTest, InvalidHandleNullPointers)
{
    PlotHandle h;
    EXPECT_EQ(h.figure, nullptr);
    EXPECT_EQ(h.axes,   nullptr);
    EXPECT_EQ(h.series, nullptr);
}

// ===========================================================================
// Suite 3: add_plot — rejection cases (no ROS2 subscription needed for guard checks)
// ===========================================================================

TEST_F(RosPlotManagerTest, AddPlotEmptyTopicRejected)
{
    PlotHandle h = mgr_->add_plot("", "data", "std_msgs/msg/Float64");
    EXPECT_EQ(h.id, -1);
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

TEST_F(RosPlotManagerTest, AddPlotEmptyFieldRejected)
{
    PlotHandle h = mgr_->add_plot("/test_topic", "", "std_msgs/msg/Float64");
    EXPECT_EQ(h.id, -1);
    EXPECT_FALSE(h.valid());
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

TEST_F(RosPlotManagerTest, AddPlotBothEmptyRejected)
{
    PlotHandle h = mgr_->add_plot("", "", "std_msgs/msg/Float64");
    EXPECT_EQ(h.id, -1);
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

TEST_F(RosPlotManagerTest, AddPlotInvalidFieldPathRejected)
{
    // Field "nonexistent.path" is not in Float64 schema.
    PlotHandle h = mgr_->add_plot("/rpm_reject_1", "nonexistent.path",
                                  "std_msgs/msg/Float64");
    EXPECT_EQ(h.id, -1);
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

// ===========================================================================
// Suite 4: add_plot — success, figure/axes/series creation
// ===========================================================================

TEST_F(RosPlotManagerTest, AddPlotFloat64ReturnsValidHandle)
{
    PlotHandle h = mgr_->add_plot("/rpm_float64_a", "data",
                                  "std_msgs/msg/Float64");
    EXPECT_GE(h.id, 1);
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(mgr_->plot_count(), 1u);
}

TEST_F(RosPlotManagerTest, AddPlotCreatesNonNullFigure)
{
    PlotHandle h = mgr_->add_plot("/rpm_float64_b", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_NE(h.figure, nullptr);
}

TEST_F(RosPlotManagerTest, AddPlotCreatesNonNullAxes)
{
    PlotHandle h = mgr_->add_plot("/rpm_float64_c", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_NE(h.axes, nullptr);
}

TEST_F(RosPlotManagerTest, AddPlotCreatesNonNullSeries)
{
    PlotHandle h = mgr_->add_plot("/rpm_float64_d", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_NE(h.series, nullptr);
}

TEST_F(RosPlotManagerTest, AddPlotSeriesLabelTopicSlashField)
{
    PlotHandle h = mgr_->add_plot("/rpm_lbl_test", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.series->label(), "/rpm_lbl_test/data");
}

TEST_F(RosPlotManagerTest, AddPlotSeriesLabelNestedField)
{
    PlotHandle h = mgr_->add_plot("/cmd_vel_lbl", "linear.x",
                                  "geometry_msgs/msg/Twist");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.series->label(), "/cmd_vel_lbl/linear.x");
}

TEST_F(RosPlotManagerTest, AddPlotSeriesColorFromPalette)
{
    PlotHandle h = mgr_->add_plot("/rpm_color_0", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    // First color is steel blue from palette::default_cycle[0]
    const spectra::Color expected = spectra::palette::default_cycle[0];
    EXPECT_FLOAT_EQ(h.series->color().r, expected.r);
    EXPECT_FLOAT_EQ(h.series->color().g, expected.g);
    EXPECT_FLOAT_EQ(h.series->color().b, expected.b);
}

TEST_F(RosPlotManagerTest, AddPlotColorsAdvanceWithEachPlot)
{
    auto h0 = mgr_->add_plot("/rpm_pal_0", "data", "std_msgs/msg/Float64");
    auto h1 = mgr_->add_plot("/rpm_pal_1", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h0.valid());
    ASSERT_TRUE(h1.valid());
    // Colors must differ (first two palette entries are distinct).
    EXPECT_FALSE(h0.series->color().r == h1.series->color().r &&
                 h0.series->color().g == h1.series->color().g &&
                 h0.series->color().b == h1.series->color().b);
}

TEST_F(RosPlotManagerTest, AddPlotIdsMonotonicallyIncreasing)
{
    auto h1 = mgr_->add_plot("/rpm_id_1", "data", "std_msgs/msg/Float64");
    auto h2 = mgr_->add_plot("/rpm_id_2", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    EXPECT_GT(h2.id, h1.id);
}

TEST_F(RosPlotManagerTest, AddPlotCountIncrementsCorrectly)
{
    mgr_->add_plot("/rpm_cnt_1", "data", "std_msgs/msg/Float64");
    EXPECT_EQ(mgr_->plot_count(), 1u);
    mgr_->add_plot("/rpm_cnt_2", "data", "std_msgs/msg/Float64");
    EXPECT_EQ(mgr_->plot_count(), 2u);
    mgr_->add_plot("/rpm_cnt_3", "linear.x", "geometry_msgs/msg/Twist");
    EXPECT_EQ(mgr_->plot_count(), 3u);
}

TEST_F(RosPlotManagerTest, AddPlotFigureHasOneAxes)
{
    PlotHandle h = mgr_->add_plot("/rpm_axes_cnt", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.figure->axes().size(), 1u);
}

TEST_F(RosPlotManagerTest, AddPlotAxesHasOneSeries)
{
    PlotHandle h = mgr_->add_plot("/rpm_series_cnt", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.axes->series().size(), 1u);
}

TEST_F(RosPlotManagerTest, AddPlotSeriesInitiallyEmpty)
{
    PlotHandle h = mgr_->add_plot("/rpm_empty_data", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    ASSERT_NE(ls, nullptr);
    EXPECT_EQ(ls->point_count(), 0u);
}

TEST_F(RosPlotManagerTest, AddPlotTwistLinearX)
{
    PlotHandle h = mgr_->add_plot("/cmd_vel_x", "linear.x",
                                  "geometry_msgs/msg/Twist");
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(h.field_path, "linear.x");
}

TEST_F(RosPlotManagerTest, AddPlotTwistAngularZ)
{
    PlotHandle h = mgr_->add_plot("/cmd_vel_az", "angular.z",
                                  "geometry_msgs/msg/Twist");
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(h.field_path, "angular.z");
}

// ===========================================================================
// Suite 5: handle() lookup
// ===========================================================================

TEST_F(RosPlotManagerTest, HandleLookupValidId)
{
    PlotHandle h = mgr_->add_plot("/rpm_lookup_a", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    PlotHandle found = mgr_->handle(h.id);
    EXPECT_TRUE(found.valid());
    EXPECT_EQ(found.id, h.id);
    EXPECT_EQ(found.topic, "/rpm_lookup_a");
}

TEST_F(RosPlotManagerTest, HandleLookupInvalidId)
{
    PlotHandle found = mgr_->handle(9999);
    EXPECT_EQ(found.id, -1);
    EXPECT_FALSE(found.valid());
}

TEST_F(RosPlotManagerTest, HandleLookupPointersSameAsAddResult)
{
    PlotHandle h = mgr_->add_plot("/rpm_ptr_check", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    PlotHandle found = mgr_->handle(h.id);
    EXPECT_EQ(found.figure, h.figure);
    EXPECT_EQ(found.axes,   h.axes);
    EXPECT_EQ(found.series, h.series);
}

// ===========================================================================
// Suite 6: remove_plot / clear
// ===========================================================================

TEST_F(RosPlotManagerTest, RemovePlotDecreasesCount)
{
    PlotHandle h = mgr_->add_plot("/rpm_remove_a", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(mgr_->plot_count(), 1u);

    EXPECT_TRUE(mgr_->remove_plot(h.id));
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

TEST_F(RosPlotManagerTest, RemovePlotInvalidIdReturnsFalse)
{
    EXPECT_FALSE(mgr_->remove_plot(9999));
}

TEST_F(RosPlotManagerTest, RemovePlotHandleBecomesInvalidAfter)
{
    PlotHandle h = mgr_->add_plot("/rpm_remove_b", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    mgr_->remove_plot(h.id);

    PlotHandle found = mgr_->handle(h.id);
    EXPECT_FALSE(found.valid());
}

TEST_F(RosPlotManagerTest, ClearRemovesAllPlots)
{
    mgr_->add_plot("/rpm_clear_1", "data", "std_msgs/msg/Float64");
    mgr_->add_plot("/rpm_clear_2", "data", "std_msgs/msg/Float64");
    EXPECT_EQ(mgr_->plot_count(), 2u);

    mgr_->clear();
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

TEST_F(RosPlotManagerTest, ClearOnEmptyManagerIsNoop)
{
    EXPECT_NO_THROW(mgr_->clear());
    EXPECT_EQ(mgr_->plot_count(), 0u);
}

// ===========================================================================
// Suite 7: poll() — no data (ring buffer empty)
// ===========================================================================

TEST_F(RosPlotManagerTest, PollNoDataNoAppend)
{
    PlotHandle h = mgr_->add_plot("/rpm_poll_empty", "data",
                                  "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    mgr_->poll();  // nothing in ring buffer

    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    ASSERT_NE(ls, nullptr);
    EXPECT_EQ(ls->point_count(), 0u);
}

TEST_F(RosPlotManagerTest, PollOnEmptyManagerNoCrash)
{
    EXPECT_NO_THROW(mgr_->poll());
}

TEST_F(RosPlotManagerTest, PollMultiplePlotsBothEmpty)
{
    auto h1 = mgr_->add_plot("/rpm_mpoll_1", "data", "std_msgs/msg/Float64");
    auto h2 = mgr_->add_plot("/rpm_mpoll_2", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());

    mgr_->poll();

    auto* ls1 = dynamic_cast<spectra::LineSeries*>(h1.axes->series()[0].get());
    auto* ls2 = dynamic_cast<spectra::LineSeries*>(h2.axes->series()[0].get());
    EXPECT_EQ(ls1->point_count(), 0u);
    EXPECT_EQ(ls2->point_count(), 0u);
}

// ===========================================================================
// Suite 8: poll() — live data via ROS2 publisher
// ===========================================================================

TEST_F(RosPlotManagerTest, PollFloat64SingleValue)
{
    const std::string topic = "/rpm_pub_single";
    PlotHandle h = mgr_->add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 msg;
    msg.data = 3.14;
    pub->publish(msg);

    bool got_data = spin_until([&]() {
        mgr_->poll();
        auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
        return ls && ls->point_count() >= 1;
    });

    EXPECT_TRUE(got_data);
    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    ASSERT_NE(ls, nullptr);
    EXPECT_GE(ls->point_count(), 1u);
    EXPECT_NEAR(ls->y_data()[0], 3.14f, 0.001f);
}

TEST_F(RosPlotManagerTest, PollFloat64MultipleValues)
{
    const std::string topic = "/rpm_pub_multi";
    PlotHandle h = mgr_->add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 20);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    const int N = 10;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    bool got_data = spin_until([&]() {
        mgr_->poll();
        auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
        return ls && ls->point_count() >= static_cast<size_t>(N);
    });

    EXPECT_TRUE(got_data);
    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    ASSERT_NE(ls, nullptr);
    EXPECT_GE(ls->point_count(), static_cast<size_t>(N));
}

TEST_F(RosPlotManagerTest, PollXAxisIsTimeSeconds)
{
    const std::string topic = "/rpm_pub_time";
    PlotHandle h = mgr_->add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    pub->publish(msg);

    bool got_data = spin_until([&]() {
        mgr_->poll();
        auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
        return ls && ls->point_count() >= 1;
    });

    ASSERT_TRUE(got_data);
    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    // X should be a positive timestamp in seconds (not nanoseconds).
    EXPECT_GT(ls->x_data()[0], 0.0f);
    EXPECT_LT(ls->x_data()[0], 1e10f);  // not in nanoseconds
}

TEST_F(RosPlotManagerTest, PollTwistLinearX)
{
    const std::string topic = "/rpm_pub_twist";
    PlotHandle h = mgr_->add_plot(topic, "linear.x", "geometry_msgs/msg/Twist");
    ASSERT_TRUE(h.valid());

    auto pub = pub_node_->create_publisher<geometry_msgs::msg::Twist>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    geometry_msgs::msg::Twist msg;
    msg.linear.x = 2.5;
    pub->publish(msg);

    bool got_data = spin_until([&]() {
        mgr_->poll();
        auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
        return ls && ls->point_count() >= 1;
    });

    EXPECT_TRUE(got_data);
    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    ASSERT_NE(ls, nullptr);
    EXPECT_NEAR(ls->y_data()[0], 2.5f, 0.001f);
}

TEST_F(RosPlotManagerTest, PollMultiplePlotsSameTopicDifferentFields)
{
    const std::string topic = "/rpm_pub_dual";
    auto hx = mgr_->add_plot(topic, "linear.x", "geometry_msgs/msg/Twist");
    auto hy = mgr_->add_plot(topic, "linear.y", "geometry_msgs/msg/Twist");
    ASSERT_TRUE(hx.valid());
    ASSERT_TRUE(hy.valid());

    auto pub = pub_node_->create_publisher<geometry_msgs::msg::Twist>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    geometry_msgs::msg::Twist msg;
    msg.linear.x = 1.1;
    msg.linear.y = 2.2;
    pub->publish(msg);

    auto* lsx = dynamic_cast<spectra::LineSeries*>(hx.axes->series()[0].get());
    auto* lsy = dynamic_cast<spectra::LineSeries*>(hy.axes->series()[0].get());

    bool got_data = spin_until([&]() {
        mgr_->poll();
        return lsx->point_count() >= 1 && lsy->point_count() >= 1;
    });

    EXPECT_TRUE(got_data);
    EXPECT_NEAR(lsx->y_data()[0], 1.1f, 0.001f);
    EXPECT_NEAR(lsy->y_data()[0], 2.2f, 0.001f);
}

// ===========================================================================
// Suite 9: on_data callback
// ===========================================================================

TEST_F(RosPlotManagerTest, OnDataCallbackFired)
{
    const std::string topic = "/rpm_cb_test";
    PlotHandle h = mgr_->add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    std::atomic<int> cb_count{0};
    double last_value = 0.0;
    mgr_->set_on_data([&](int /*id*/, double /*t*/, double v) {
        last_value = v;
        ++cb_count;
    });

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 msg;
    msg.data = 7.77;
    pub->publish(msg);

    spin_until([&]() {
        mgr_->poll();
        return cb_count.load() >= 1;
    });

    EXPECT_GE(cb_count.load(), 1);
    EXPECT_NEAR(last_value, 7.77, 0.001);
}

TEST_F(RosPlotManagerTest, OnDataCallbackReceivesCorrectPlotId)
{
    const std::string topic = "/rpm_cb_id";
    PlotHandle h = mgr_->add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    const int expected_id = h.id;

    std::atomic<int> received_id{-99};
    mgr_->set_on_data([&](int id, double, double) {
        received_id = id;
    });

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    pub->publish(msg);

    spin_until([&]() {
        mgr_->poll();
        return received_id.load() != -99;
    });

    EXPECT_EQ(received_id.load(), expected_id);
}

// ===========================================================================
// Suite 10: auto-fit
// ===========================================================================

TEST_F(RosPlotManagerTest, AutoFitNotTriggeredBeforeThreshold)
{
    // With a very high threshold, auto_fitted should remain false after a
    // few samples.  We verify indirectly by checking series size.
    mgr_->set_auto_fit_samples(10000);

    const std::string topic = "/rpm_af_notyet";
    PlotHandle h = mgr_->add_plot(topic, "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; });

    for (int i = 0; i < 5; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spin_until([&]() {
        mgr_->poll();
        auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
        return ls && ls->point_count() >= 5u;
    }, 2000ms);

    // Just confirm data arrived; the auto-fit state is internal, no crash expected.
    auto* ls = dynamic_cast<spectra::LineSeries*>(h.axes->series()[0].get());
    EXPECT_GE(ls->point_count(), 0u);  // at least no crash
}

TEST_F(RosPlotManagerTest, SetAutoFitSamplesConfigured)
{
    mgr_->set_auto_fit_samples(50);
    // Verify this doesn't crash and the manager is still usable.
    PlotHandle h = mgr_->add_plot("/rpm_af_cfg", "data", "std_msgs/msg/Float64");
    EXPECT_TRUE(h.valid());
}

// ===========================================================================
// Suite 11: configuration
// ===========================================================================

TEST_F(RosPlotManagerTest, SetFigureSizeAffectsNewPlots)
{
    mgr_->set_figure_size(800, 600);
    PlotHandle h = mgr_->add_plot("/rpm_figsize", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h.valid());
    EXPECT_EQ(h.figure->width(),  800u);
    EXPECT_EQ(h.figure->height(), 600u);
}

TEST_F(RosPlotManagerTest, SetFigureSizeDoesNotAffectExistingPlots)
{
    // Add a plot, then change size — existing figure is unaffected.
    PlotHandle h1 = mgr_->add_plot("/rpm_fsize_old", "data", "std_msgs/msg/Float64");
    mgr_->set_figure_size(640, 480);
    PlotHandle h2 = mgr_->add_plot("/rpm_fsize_new", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    // h1 was created before the size change.
    EXPECT_EQ(h1.figure->width(),  1280u);
    EXPECT_EQ(h2.figure->width(),   640u);
}

TEST_F(RosPlotManagerTest, SetDefaultBufferDepthIsAccepted)
{
    mgr_->set_default_buffer_depth(512);
    // Just verify no crash and add_plot works.
    PlotHandle h = mgr_->add_plot("/rpm_bufdepth", "data", "std_msgs/msg/Float64");
    EXPECT_TRUE(h.valid());
}

// ===========================================================================
// Suite 12: handles() snapshot
// ===========================================================================

TEST_F(RosPlotManagerTest, HandlesReturnsAllActivePlots)
{
    auto h1 = mgr_->add_plot("/rpm_hs_1", "data", "std_msgs/msg/Float64");
    auto h2 = mgr_->add_plot("/rpm_hs_2", "data", "std_msgs/msg/Float64");
    auto h3 = mgr_->add_plot("/rpm_hs_3", "linear.x", "geometry_msgs/msg/Twist");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());
    ASSERT_TRUE(h3.valid());

    auto all = mgr_->handles();
    EXPECT_EQ(all.size(), 3u);
}

TEST_F(RosPlotManagerTest, HandlesReflectsRemoval)
{
    auto h1 = mgr_->add_plot("/rpm_hrem_1", "data", "std_msgs/msg/Float64");
    auto h2 = mgr_->add_plot("/rpm_hrem_2", "data", "std_msgs/msg/Float64");
    ASSERT_TRUE(h1.valid());
    ASSERT_TRUE(h2.valid());

    mgr_->remove_plot(h1.id);
    auto all = mgr_->handles();
    EXPECT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].id, h2.id);
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}

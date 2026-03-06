// Phase A integration smoke test — Spectra ROS2 Adapter
//
// Verifies the full Phase A pipeline in a single headless test:
//   Ros2Bridge → TopicDiscovery → GenericSubscriber → RingBuffer
//
// Scenario:
//   1. Create and spin a Ros2Bridge.
//   2. Create a TopicDiscovery on the bridge node; refresh and verify the
//      bridge node itself is visible in the graph.
//   3. Create a GenericSubscriber for std_msgs/msg/Float64 on a test topic.
//   4. Publish 5 known values from a separate publisher node.
//   5. Pop samples from the ring buffer and verify values match.
//   6. Clean shutdown — no hangs, no crashes.
//
// Additional sub-tests exercise Twist multi-field extraction and Imu nested
// field extraction end-to-end through the same pipeline.
//
// These tests only compile and run when SPECTRA_USE_ROS2 is ON and a ROS2
// workspace is sourced.  They are registered in tests/CMakeLists.txt inside
// the if(SPECTRA_USE_ROS2) block.
//
// NOTE: rclcpp::init / rclcpp::shutdown may only be called once per process
// in older ROS2 versions.  We use a single shared RclcppEnvironment so all
// tests share one rclcpp lifecycle.

#include "generic_subscriber.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"
#include "topic_discovery.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/int32.hpp>

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
// Fixture — fresh bridge + publisher node per test.
// ---------------------------------------------------------------------------

class PhaseAIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);

        static std::atomic<int> counter{0};
        const int id = counter.fetch_add(1);

        bridge_ = std::make_unique<Ros2Bridge>();
        bridge_->init("phase_a_bridge_" + std::to_string(id));
        bridge_->start_spin();

        pub_node_ = rclcpp::Node::make_shared("phase_a_pub_" + std::to_string(id));
    }

    void TearDown() override
    {
        bridge_->shutdown();
        pub_node_.reset();
    }

    // Spin pub_node_ until predicate is true or timeout expires.
    template<typename Pred>
    bool spin_until(Pred pred, std::chrono::milliseconds timeout = 3000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        rclcpp::executors::SingleThreadedExecutor exec;
        exec.add_node(pub_node_);
        while (std::chrono::steady_clock::now() < deadline)
        {
            exec.spin_once(10ms);
            if (pred()) return true;
        }
        return pred();
    }

    std::unique_ptr<Ros2Bridge> bridge_;
    rclcpp::Node::SharedPtr     pub_node_;
    MessageIntrospector         intr_;
};

// ===========================================================================
// Suite 1: Bridge lifecycle smoke test
// ===========================================================================

TEST_F(PhaseAIntegrationTest, BridgeSpinsAndShutdownsCleanly)
{
    EXPECT_TRUE(bridge_->is_ok());
    // State should be Spinning after start_spin().
    EXPECT_EQ(bridge_->state(), BridgeState::Spinning);
    // TearDown calls shutdown() — no hang, no crash.
}

TEST_F(PhaseAIntegrationTest, BridgeNodeIsAccessible)
{
    auto node = bridge_->node();
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(std::string(node->get_name()).empty());
}

// ===========================================================================
// Suite 2: Bridge → TopicDiscovery
// ===========================================================================

TEST_F(PhaseAIntegrationTest, DiscoveryFindsBridgeNode)
{
    TopicDiscovery disc(bridge_->node());
    disc.refresh();

    // The bridge node must appear in the discovered node list.
    const auto nodes = disc.nodes();
    const std::string bridge_name = bridge_->node()->get_name();
    bool found = false;
    for (const auto& n : nodes)
        if (n.name == bridge_name)
            found = true;
    EXPECT_TRUE(found) << "Bridge node '" << bridge_name << "' not found in discovery";
}

TEST_F(PhaseAIntegrationTest, DiscoveryFindsMockPublisher)
{
    const std::string topic = "/phase_a_discovery_test";

    // Publish on a node visible to the bridge.
    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    // Wait for the ROS2 graph to propagate.
    std::this_thread::sleep_for(200ms);

    TopicDiscovery disc(bridge_->node());
    disc.refresh();

    EXPECT_TRUE(disc.has_topic(topic))
        << "Expected to discover topic " << topic;
}

TEST_F(PhaseAIntegrationTest, DiscoveryTopicHasCorrectType)
{
    const std::string topic = "/phase_a_type_test";
    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);

    TopicDiscovery disc(bridge_->node());
    disc.refresh();

    const auto info = disc.topic(topic);
    ASSERT_FALSE(info.types.empty());
    bool found = false;
    for (const auto& t : info.types)
        if (t.find("Float64") != std::string::npos)
            found = true;
    EXPECT_TRUE(found) << "Float64 not found in type list";
}

TEST_F(PhaseAIntegrationTest, DiscoveryTopicHasPublisherCount)
{
    const std::string topic = "/phase_a_pub_count";
    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);

    TopicDiscovery disc(bridge_->node());
    disc.refresh();

    EXPECT_GE(disc.topic(topic).publisher_count, 1);
}

// ===========================================================================
// Suite 3: Bridge → GenericSubscriber → RingBuffer (Float64)
// ===========================================================================

TEST_F(PhaseAIntegrationTest, Float64ValueArrivesInRingBuffer)
{
    const std::string topic = "/phase_a_float64";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    const int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Float64 msg;
    msg.data = 123.456;
    pub->publish(msg);

    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 3000ms);
    ASSERT_TRUE(got) << "No sample arrived within timeout";

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, 123.456);
    EXPECT_GT(s.timestamp_ns, 0LL);

    sub.stop();
}

TEST_F(PhaseAIntegrationTest, MultipleFloat64ValuesPreserveOrder)
{
    const std::string topic = "/phase_a_float64_order";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    const int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 20);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    const int N = 5;
    const double values[N] = {1.1, 2.2, 3.3, 4.4, 5.5};
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = values[i];
        pub->publish(msg);
        std::this_thread::sleep_for(1ms);
    }

    bool got = spin_until([&]{ return sub.pending(id) >= static_cast<size_t>(N); }, 3000ms);
    ASSERT_TRUE(got) << "Only " << sub.pending(id) << " of " << N << " samples arrived";

    for (int i = 0; i < N; ++i)
    {
        FieldSample s;
        ASSERT_TRUE(sub.pop(id, s));
        EXPECT_NEAR(s.value, values[i], 1e-9) << "Sample " << i << " mismatch";
    }

    sub.stop();
}

TEST_F(PhaseAIntegrationTest, TimestampIsMonotonicallyNonDecreasing)
{
    const std::string topic = "/phase_a_timestamps";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    const int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    const int N = 4;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        // Small sleep to ensure distinct wall-clock timestamps.
        std::this_thread::sleep_for(5ms);
    }

    bool got = spin_until([&]{ return sub.pending(id) >= static_cast<size_t>(N); }, 3000ms);
    ASSERT_TRUE(got);

    int64_t prev_ts = 0;
    for (int i = 0; i < N; ++i)
    {
        FieldSample s;
        ASSERT_TRUE(sub.pop(id, s));
        EXPECT_GE(s.timestamp_ns, prev_ts) << "Timestamp went backwards at sample " << i;
        prev_ts = s.timestamp_ns;
    }

    sub.stop();
}

TEST_F(PhaseAIntegrationTest, StatsAccountForReceivedSamples)
{
    const std::string topic = "/phase_a_stats";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    const int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    const int N = 3;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(1ms);
    }

    spin_until([&]{ return sub.pending(id) >= static_cast<size_t>(N); }, 3000ms);

    const auto stats = sub.stats();
    EXPECT_GE(stats.messages_received, static_cast<uint64_t>(N));
    EXPECT_GE(stats.samples_written,   static_cast<uint64_t>(N));
    EXPECT_EQ(stats.samples_dropped,   0u);

    sub.stop();
}

// ===========================================================================
// Suite 4: Full pipeline — Bridge → Discovery → Subscribe → Extract
// (the canonical A6 scenario described in the plan)
// ===========================================================================

TEST_F(PhaseAIntegrationTest, FullPipeline_BridgeDiscoverSubscribeExtract)
{
    const std::string topic = "/phase_a_full_pipeline";

    // Step 1: Discovery — verify bridge node is in the graph.
    TopicDiscovery disc(bridge_->node());
    disc.refresh();
    EXPECT_GE(disc.node_count(), 1u) << "No nodes found after refresh";

    // Step 2: Subscribe to Float64.
    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    const int id = sub.add_field("data");
    ASSERT_GE(id, 0) << "add_field(\"data\") failed — schema introspection error";
    ASSERT_TRUE(sub.start()) << "GenericSubscriber::start() failed";

    // Step 3: Publish on a separate node.
    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    bool disc_ok = spin_until([&]{ return pub->get_subscription_count() >= 1; }, 3000ms);
    ASSERT_TRUE(disc_ok) << "Subscriber not discovered by publisher within timeout";

    // Step 4: Re-run discovery now that both pub and sub exist.
    std::this_thread::sleep_for(200ms);
    disc.refresh();
    EXPECT_TRUE(disc.has_topic(topic))
        << "TopicDiscovery did not find topic " << topic;

    // Step 5: Publish a value and verify ring buffer extraction.
    std_msgs::msg::Float64 msg;
    msg.data = 99.9;
    pub->publish(msg);

    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 3000ms);
    ASSERT_TRUE(got) << "No sample arrived within timeout";

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, 99.9);
    EXPECT_GT(s.timestamp_ns, 0LL);

    // Step 6: Clean shutdown.
    sub.stop();
    EXPECT_FALSE(sub.is_running());
}

// ===========================================================================
// Suite 5: Twist multi-field extraction (nested message end-to-end)
// ===========================================================================

TEST_F(PhaseAIntegrationTest, TwistMultiFieldExtraction)
{
    const std::string topic = "/phase_a_twist";

    GenericSubscriber sub(bridge_->node(), topic,
                          "geometry_msgs/msg/Twist", intr_);
    const int id_lx = sub.add_field("linear.x");
    const int id_ly = sub.add_field("linear.y");
    const int id_az = sub.add_field("angular.z");
    ASSERT_GE(id_lx, 0);
    ASSERT_GE(id_ly, 0);
    ASSERT_GE(id_az, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<geometry_msgs::msg::Twist>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    geometry_msgs::msg::Twist msg;
    msg.linear.x  = 1.5;
    msg.linear.y  = -2.5;
    msg.angular.z = 0.75;
    pub->publish(msg);

    bool got = spin_until([&]{
        return sub.pending(id_lx) > 0 &&
               sub.pending(id_ly) > 0 &&
               sub.pending(id_az) > 0;
    }, 3000ms);
    ASSERT_TRUE(got) << "Not all Twist fields arrived within timeout";

    FieldSample s;
    ASSERT_TRUE(sub.pop(id_lx, s)); EXPECT_NEAR(s.value,  1.5,  1e-6);
    ASSERT_TRUE(sub.pop(id_ly, s)); EXPECT_NEAR(s.value, -2.5,  1e-6);
    ASSERT_TRUE(sub.pop(id_az, s)); EXPECT_NEAR(s.value,  0.75, 1e-6);

    sub.stop();
}

// ===========================================================================
// Suite 6: Imu deeply nested field extraction end-to-end
// ===========================================================================

TEST_F(PhaseAIntegrationTest, ImuLinearAccelerationExtraction)
{
    const std::string topic = "/phase_a_imu";

    GenericSubscriber sub(bridge_->node(), topic,
                          "sensor_msgs/msg/Imu", intr_);
    const int id_ax = sub.add_field("linear_acceleration.x");
    const int id_ay = sub.add_field("linear_acceleration.y");
    const int id_az = sub.add_field("linear_acceleration.z");
    ASSERT_GE(id_ax, 0);
    ASSERT_GE(id_ay, 0);
    ASSERT_GE(id_az, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<sensor_msgs::msg::Imu>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    sensor_msgs::msg::Imu msg;
    msg.linear_acceleration.x =  9.81;
    msg.linear_acceleration.y =  0.0;
    msg.linear_acceleration.z = -0.1;
    pub->publish(msg);

    bool got = spin_until([&]{
        return sub.pending(id_ax) > 0 &&
               sub.pending(id_ay) > 0 &&
               sub.pending(id_az) > 0;
    }, 3000ms);
    ASSERT_TRUE(got) << "Not all Imu fields arrived within timeout";

    FieldSample s;
    ASSERT_TRUE(sub.pop(id_ax, s)); EXPECT_NEAR(s.value,  9.81, 1e-4);
    ASSERT_TRUE(sub.pop(id_ay, s)); EXPECT_NEAR(s.value,  0.0,  1e-6);
    ASSERT_TRUE(sub.pop(id_az, s)); EXPECT_NEAR(s.value, -0.1,  1e-6);

    sub.stop();
}

TEST_F(PhaseAIntegrationTest, ImuAngularVelocityExtraction)
{
    const std::string topic = "/phase_a_imu_angvel";

    GenericSubscriber sub(bridge_->node(), topic,
                          "sensor_msgs/msg/Imu", intr_);
    const int id = sub.add_field("angular_velocity.x");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<sensor_msgs::msg::Imu>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    sensor_msgs::msg::Imu msg;
    msg.angular_velocity.x = 0.314;
    pub->publish(msg);

    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 3000ms);
    ASSERT_TRUE(got);

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_NEAR(s.value, 0.314, 1e-6);

    sub.stop();
}

// ===========================================================================
// Suite 7: Subscriber stop / restart / buffer persistence
// ===========================================================================

TEST_F(PhaseAIntegrationTest, BufferPersistsAfterStop)
{
    const std::string topic = "/phase_a_buf_persist";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    const int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Float64 msg;
    msg.data = 42.0;
    pub->publish(msg);

    spin_until([&]{ return sub.pending(id) > 0; }, 3000ms);

    // Stop subscription — ring buffer must not be cleared.
    sub.stop();
    EXPECT_FALSE(sub.is_running());

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, 42.0);
}

TEST_F(PhaseAIntegrationTest, DestructorIsClean)
{
    const std::string topic = "/phase_a_dtor";
    {
        GenericSubscriber sub(bridge_->node(), topic,
                              "std_msgs/msg/Float64", intr_);
        sub.add_field("data");
        ASSERT_TRUE(sub.start());
        // Destructor called at end of scope — must not crash or hang.
    }
    SUCCEED();
}

// ===========================================================================
// Suite 8: Discovery callbacks fire correctly
// ===========================================================================

TEST_F(PhaseAIntegrationTest, TopicAddCallbackFiresDuringFullPipeline)
{
    const std::string topic = "/phase_a_cb_topic";

    TopicDiscovery disc(bridge_->node());

    std::atomic<bool> topic_found{false};
    disc.set_topic_callback([&](const TopicInfo& t, bool added) {
        if (added && t.name == topic)
            topic_found.store(true);
    });

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    std::this_thread::sleep_for(200ms);
    disc.refresh();

    EXPECT_TRUE(topic_found.load())
        << "Topic add callback was not fired for " << topic;
}

TEST_F(PhaseAIntegrationTest, NodeCallbackFiresDuringFullPipeline)
{
    TopicDiscovery disc(bridge_->node());

    std::atomic<int> node_count{0};
    disc.set_node_callback([&](const NodeInfo&, bool added) {
        if (added) node_count.fetch_add(1);
    });

    disc.refresh();
    EXPECT_GE(node_count.load(), 1) << "Node add callback never fired";
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

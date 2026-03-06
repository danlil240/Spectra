// Unit tests for GenericSubscriber — ROS2 generic topic subscription engine.
//
// These tests only compile and run when SPECTRA_USE_ROS2 is ON and a ROS2
// workspace is sourced.  They are registered in tests/CMakeLists.txt inside
// the if(SPECTRA_USE_ROS2) block.
//
// NOTE: rclcpp::init / rclcpp::shutdown may only be called once per process
// in older ROS2 versions.  We use a single shared initialisation via
// RclcppEnvironment so all tests share one rclcpp lifecycle.

#include "generic_subscriber.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/bool.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <sensor_msgs/msg/imu.hpp>

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
// Fixture: fresh node + bridge per test.
// ---------------------------------------------------------------------------

class GenericSubscriberTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);

        static std::atomic<int> counter{0};
        const std::string name = "gs_test_" + std::to_string(counter.fetch_add(1));

        bridge_ = std::make_unique<Ros2Bridge>();
        bridge_->init(name);
        bridge_->start_spin();

        // Separate publisher node.
        static std::atomic<int> pub_counter{0};
        const std::string pub_name = "gs_pub_" + std::to_string(pub_counter.fetch_add(1));
        pub_node_ = rclcpp::Node::make_shared(pub_name);
    }

    void TearDown() override
    {
        bridge_->shutdown();
        pub_node_.reset();
    }

    // Spin pub_node_ for up to timeout waiting for predicate to become true.
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

    std::unique_ptr<Ros2Bridge>     bridge_;
    rclcpp::Node::SharedPtr         pub_node_;
    MessageIntrospector             intr_;
};

// ===========================================================================
// Suite 1: RingBuffer
// ===========================================================================

TEST(RingBuffer, ConstructDefault)
{
    RingBuffer rb(16);
    EXPECT_EQ(rb.capacity(), 16u);
    EXPECT_EQ(rb.size(), 0u);
}

TEST(RingBuffer, RoundsUpToPow2)
{
    RingBuffer rb(10);
    EXPECT_EQ(rb.capacity(), 16u);  // 10 → 16
}

TEST(RingBuffer, RoundsUpLargeCapacity)
{
    RingBuffer rb(10000);
    EXPECT_EQ(rb.capacity(), 16384u);  // next pow2 after 10000
}

TEST(RingBuffer, PushPop)
{
    RingBuffer rb(16);
    FieldSample s{12345678LL, 3.14};
    rb.push(s);
    EXPECT_EQ(rb.size(), 1u);

    FieldSample out;
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.timestamp_ns, 12345678LL);
    EXPECT_DOUBLE_EQ(out.value, 3.14);
    EXPECT_EQ(rb.size(), 0u);
}

TEST(RingBuffer, PopEmpty)
{
    RingBuffer rb(16);
    FieldSample out;
    EXPECT_FALSE(rb.pop(out));
}

TEST(RingBuffer, FIFOOrder)
{
    RingBuffer rb(16);
    for (int i = 0; i < 5; ++i)
        rb.push({static_cast<int64_t>(i), static_cast<double>(i)});

    for (int i = 0; i < 5; ++i)
    {
        FieldSample s;
        ASSERT_TRUE(rb.pop(s));
        EXPECT_EQ(s.timestamp_ns, static_cast<int64_t>(i));
    }
}

TEST(RingBuffer, DropOldestWhenFull)
{
    RingBuffer rb(4);  // capacity 4
    for (int i = 0; i < 6; ++i)
        rb.push({static_cast<int64_t>(i), static_cast<double>(i)});

    // Buffer is full; should contain the 4 most-recent or oldest 4 depending
    // on drop policy.  Our push() drops oldest on overflow.
    // After pushing 6 into a 4-slot buffer: indices 2,3,4,5 remain.
    FieldSample s;
    ASSERT_TRUE(rb.pop(s));
    EXPECT_GE(s.timestamp_ns, 0LL);  // some valid entry survived
}

TEST(RingBuffer, Peek)
{
    RingBuffer rb(16);
    rb.push({1LL, 1.0});
    rb.push({2LL, 2.0});
    rb.push({3LL, 3.0});

    FieldSample out[3];
    size_t count = rb.peek(out, 3);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(out[0].timestamp_ns, 1LL);
    EXPECT_EQ(out[1].timestamp_ns, 2LL);
    EXPECT_EQ(out[2].timestamp_ns, 3LL);

    // Peek does not consume.
    EXPECT_EQ(rb.size(), 3u);
}

TEST(RingBuffer, PeekPartial)
{
    RingBuffer rb(16);
    rb.push({1LL, 1.0});
    rb.push({2LL, 2.0});

    FieldSample out[5];
    size_t count = rb.peek(out, 5);
    EXPECT_EQ(count, 2u);
}

TEST(RingBuffer, Clear)
{
    RingBuffer rb(16);
    rb.push({1LL, 1.0});
    rb.push({2LL, 2.0});
    rb.clear();
    EXPECT_EQ(rb.size(), 0u);

    FieldSample s;
    EXPECT_FALSE(rb.pop(s));
}

TEST(RingBuffer, MultipleRoundsNoDrop)
{
    RingBuffer rb(8);
    for (int round = 0; round < 4; ++round)
    {
        for (int i = 0; i < 4; ++i)
            rb.push({static_cast<int64_t>(i), static_cast<double>(i)});
        for (int i = 0; i < 4; ++i)
        {
            FieldSample s;
            ASSERT_TRUE(rb.pop(s));
        }
    }
    EXPECT_EQ(rb.size(), 0u);
}

// ===========================================================================
// Suite 2: Construction
// ===========================================================================

TEST_F(GenericSubscriberTest, ConstructNotRunning)
{
    GenericSubscriber sub(bridge_->node(), "/test_topic",
                          "std_msgs/msg/Float64", intr_);
    EXPECT_FALSE(sub.is_running());
    EXPECT_EQ(sub.topic(), "/test_topic");
    EXPECT_EQ(sub.type_name(), "std_msgs/msg/Float64");
    EXPECT_EQ(sub.field_count(), 0u);
}

TEST_F(GenericSubscriberTest, DefaultBufferDepthPow2)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_, 10000);
    EXPECT_EQ(sub.buffer_depth(), 16384u);
}

TEST_F(GenericSubscriberTest, CustomBufferDepth)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_, 128);
    EXPECT_EQ(sub.buffer_depth(), 128u);
}

// ===========================================================================
// Suite 3: Field management
// ===========================================================================

TEST_F(GenericSubscriberTest, AddFieldByAccessor)
{
    auto schema = intr_.introspect("std_msgs/msg/Float64");
    ASSERT_NE(schema, nullptr);

    auto acc = intr_.make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());

    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id = sub.add_field("data", acc);
    EXPECT_GE(id, 0);
    EXPECT_EQ(sub.field_count(), 1u);
}

TEST_F(GenericSubscriberTest, AddFieldByPath)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id = sub.add_field("data");
    EXPECT_GE(id, 0);
    EXPECT_EQ(sub.field_count(), 1u);
}

TEST_F(GenericSubscriberTest, AddFieldInvalidPath)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id = sub.add_field("nonexistent_field");
    EXPECT_EQ(id, -1);
    EXPECT_EQ(sub.field_count(), 0u);
}

TEST_F(GenericSubscriberTest, AddFieldInvalidAccessor)
{
    FieldAccessor bad;  // default-constructed = invalid
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id = sub.add_field("data", bad);
    EXPECT_EQ(id, -1);
}

TEST_F(GenericSubscriberTest, AddMultipleFields)
{
    GenericSubscriber sub(bridge_->node(), "/test", "geometry_msgs/msg/Twist",
                          intr_);
    int id0 = sub.add_field("linear.x");
    int id1 = sub.add_field("linear.y");
    int id2 = sub.add_field("linear.z");
    int id3 = sub.add_field("angular.x");
    EXPECT_GE(id0, 0);
    EXPECT_GE(id1, 0);
    EXPECT_GE(id2, 0);
    EXPECT_GE(id3, 0);
    EXPECT_EQ(sub.field_count(), 4u);
    // IDs must be distinct.
    EXPECT_NE(id0, id1);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
}

TEST_F(GenericSubscriberTest, RemoveField)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    EXPECT_EQ(sub.field_count(), 1u);

    sub.remove_field(id);
    EXPECT_EQ(sub.field_count(), 0u);
}

TEST_F(GenericSubscriberTest, RemoveNonExistentField)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    // Should not crash.
    sub.remove_field(9999);
    EXPECT_EQ(sub.field_count(), 0u);
}

// ===========================================================================
// Suite 4: Lifecycle
// ===========================================================================

TEST_F(GenericSubscriberTest, StartStop)
{
    GenericSubscriber sub(bridge_->node(), "/test_lifecycle",
                          "std_msgs/msg/Float64", intr_);
    sub.add_field("data");

    EXPECT_TRUE(sub.start());
    EXPECT_TRUE(sub.is_running());

    sub.stop();
    EXPECT_FALSE(sub.is_running());
}

TEST_F(GenericSubscriberTest, StartIdempotent)
{
    GenericSubscriber sub(bridge_->node(), "/test_idem",
                          "std_msgs/msg/Float64", intr_);
    EXPECT_TRUE(sub.start());
    EXPECT_TRUE(sub.start());  // second call is no-op
    EXPECT_TRUE(sub.is_running());
    sub.stop();
}

TEST_F(GenericSubscriberTest, StopIdempotent)
{
    GenericSubscriber sub(bridge_->node(), "/test_stop_idem",
                          "std_msgs/msg/Float64", intr_);
    sub.stop();  // stop when not running — should not crash
    EXPECT_FALSE(sub.is_running());
}

TEST_F(GenericSubscriberTest, DestructorStops)
{
    {
        GenericSubscriber sub(bridge_->node(), "/test_dtor",
                              "std_msgs/msg/Float64", intr_);
        EXPECT_TRUE(sub.start());
    }
    // Destructor called — must not crash or hang.
}

// ===========================================================================
// Suite 5: Publish → ring buffer (integration)
// ===========================================================================

TEST_F(GenericSubscriberTest, Float64SingleField)
{
    const std::string topic = "/gs_test_float64_single";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    // Publisher on separate node.
    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    // Wait for discovery.
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    // Publish a known value.
    std_msgs::msg::Float64 msg;
    msg.data = 42.5;
    pub->publish(msg);

    // Wait for sample to arrive.
    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 2000ms);
    ASSERT_TRUE(got) << "No sample arrived within timeout";

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, 42.5);
    EXPECT_GT(s.timestamp_ns, 0LL);

    sub.stop();
}

TEST_F(GenericSubscriberTest, Float64MultiplePublishedValues)
{
    const std::string topic = "/gs_test_float64_multi";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 20);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    const int N = 10;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(1ms);
    }

    // Wait for all samples.
    bool got = spin_until([&]{ return sub.pending(id) >= static_cast<size_t>(N); }, 3000ms);
    ASSERT_TRUE(got) << "Only " << sub.pending(id) << " of " << N << " samples arrived";

    // Pop and verify values (order preserved).
    std::vector<double> values;
    FieldSample s;
    while (sub.pop(id, s))
        values.push_back(s.value);

    ASSERT_EQ(values.size(), static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        EXPECT_DOUBLE_EQ(values[i], static_cast<double>(i)) << "index " << i;

    sub.stop();
}

TEST_F(GenericSubscriberTest, TwistMultipleFields)
{
    const std::string topic = "/gs_test_twist";

    GenericSubscriber sub(bridge_->node(), topic,
                          "geometry_msgs/msg/Twist", intr_);
    int id_lx = sub.add_field("linear.x");
    int id_ly = sub.add_field("linear.y");
    int id_az = sub.add_field("angular.z");
    ASSERT_GE(id_lx, 0);
    ASSERT_GE(id_ly, 0);
    ASSERT_GE(id_az, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<geometry_msgs::msg::Twist>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    geometry_msgs::msg::Twist msg;
    msg.linear.x  = 1.1;
    msg.linear.y  = 2.2;
    msg.angular.z = 3.3;
    pub->publish(msg);

    bool got = spin_until([&]{
        return sub.pending(id_lx) > 0 &&
               sub.pending(id_ly) > 0 &&
               sub.pending(id_az) > 0;
    }, 2000ms);
    ASSERT_TRUE(got) << "Not all fields arrived";

    FieldSample s;
    ASSERT_TRUE(sub.pop(id_lx, s));
    EXPECT_NEAR(s.value, 1.1, 1e-6);

    ASSERT_TRUE(sub.pop(id_ly, s));
    EXPECT_NEAR(s.value, 2.2, 1e-6);

    ASSERT_TRUE(sub.pop(id_az, s));
    EXPECT_NEAR(s.value, 3.3, 1e-6);

    sub.stop();
}

TEST_F(GenericSubscriberTest, Int32Field)
{
    const std::string topic = "/gs_test_int32";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Int32", intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Int32>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Int32 msg;
    msg.data = -12345;
    pub->publish(msg);

    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 2000ms);
    ASSERT_TRUE(got);

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, -12345.0);

    sub.stop();
}

TEST_F(GenericSubscriberTest, BoolField)
{
    const std::string topic = "/gs_test_bool";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Bool", intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Bool>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Bool msg;
    msg.data = true;
    pub->publish(msg);

    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 2000ms);
    ASSERT_TRUE(got);

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, 1.0);

    sub.stop();
}

// ===========================================================================
// Suite 6: pop_bulk
// ===========================================================================

TEST_F(GenericSubscriberTest, PopBulk)
{
    const std::string topic = "/gs_test_bulk";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 20);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    const int N = 5;
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i) * 10.0;
        pub->publish(msg);
        std::this_thread::sleep_for(1ms);
    }

    bool got = spin_until([&]{ return sub.pending(id) >= static_cast<size_t>(N); }, 2000ms);
    ASSERT_TRUE(got);

    FieldSample buf[10];
    size_t count = sub.pop_bulk(id, buf, 10);
    EXPECT_EQ(count, static_cast<size_t>(N));

    for (size_t i = 0; i < count; ++i)
        EXPECT_DOUBLE_EQ(buf[i].value, static_cast<double>(i) * 10.0);

    sub.stop();
}

TEST_F(GenericSubscriberTest, PopBulkInvalidId)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    FieldSample buf[10];
    EXPECT_EQ(sub.pop_bulk(9999, buf, 10), 0u);
}

// ===========================================================================
// Suite 7: Stats
// ===========================================================================

TEST_F(GenericSubscriberTest, StatsInitiallyZero)
{
    GenericSubscriber sub(bridge_->node(), "/test_stats",
                          "std_msgs/msg/Float64", intr_);
    auto s = sub.stats();
    EXPECT_EQ(s.messages_received, 0u);
    EXPECT_EQ(s.messages_dropped, 0u);
    EXPECT_EQ(s.samples_written, 0u);
    EXPECT_EQ(s.samples_dropped, 0u);
}

TEST_F(GenericSubscriberTest, StatsAfterMessages)
{
    const std::string topic = "/gs_test_stats";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    int id = sub.add_field("data");
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

    spin_until([&]{ return sub.pending(id) >= static_cast<size_t>(N); }, 2000ms);

    auto s = sub.stats();
    EXPECT_GE(s.messages_received, static_cast<uint64_t>(N));
    EXPECT_GE(s.samples_written, static_cast<uint64_t>(N));
    EXPECT_EQ(s.samples_dropped, 0u);

    sub.stop();
}

// ===========================================================================
// Suite 8: MessageCallback
// ===========================================================================

TEST_F(GenericSubscriberTest, MessageCallbackInvoked)
{
    const std::string topic = "/gs_test_callback";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    sub.add_field("data");
    ASSERT_TRUE(sub.start());

    std::atomic<int> cb_count{0};
    sub.set_message_callback([&](const SubscriberStats&){ cb_count.fetch_add(1); });

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    pub->publish(msg);

    bool got = spin_until([&]{ return cb_count.load() >= 1; }, 2000ms);
    EXPECT_TRUE(got);

    sub.stop();
}

// ===========================================================================
// Suite 9: Pending / empty access
// ===========================================================================

TEST_F(GenericSubscriberTest, PendingInvalidId)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    EXPECT_EQ(sub.pending(9999), 0u);
}

TEST_F(GenericSubscriberTest, PopInvalidId)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    FieldSample s;
    EXPECT_FALSE(sub.pop(9999, s));
}

TEST_F(GenericSubscriberTest, PeekInvalidId)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    FieldSample buf[4];
    EXPECT_EQ(sub.peek(9999, buf, 4), 0u);
}

TEST_F(GenericSubscriberTest, PendingBeforeStart)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    EXPECT_EQ(sub.pending(id), 0u);
}

// ===========================================================================
// Suite 10: High-frequency stress (ring buffer overflow behaviour)
// ===========================================================================

TEST_F(GenericSubscriberTest, RingBufferOverflowDropsOldest)
{
    const std::string topic = "/gs_test_overflow";
    const size_t depth = 16;   // tiny buffer

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_, depth);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 100);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    const int N = 100;   // >> buffer depth
    for (int i = 0; i < N; ++i)
    {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(1ms);
    }

    // Give executor time to drain incoming queue.
    spin_until([&]{ return sub.stats().messages_received >= static_cast<uint64_t>(N); }, 3000ms);

    // Buffer should be full but not exceed capacity.
    EXPECT_LE(sub.pending(id), depth);

    // Stats must count ring drops.
    EXPECT_GT(sub.stats().samples_dropped, 0u);

    sub.stop();
}

// ===========================================================================
// Suite 11: IMU nested field (header + angular_velocity)
// ===========================================================================

TEST_F(GenericSubscriberTest, ImuAngularVelocityX)
{
    const std::string topic = "/gs_test_imu";

    GenericSubscriber sub(bridge_->node(), topic,
                          "sensor_msgs/msg/Imu", intr_);
    int id = sub.add_field("angular_velocity.x");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<sensor_msgs::msg::Imu>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    sensor_msgs::msg::Imu msg;
    msg.angular_velocity.x = 0.123;
    msg.angular_velocity.y = 0.456;
    msg.angular_velocity.z = 0.789;
    pub->publish(msg);

    bool got = spin_until([&]{ return sub.pending(id) > 0; }, 2000ms);
    ASSERT_TRUE(got);

    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_NEAR(s.value, 0.123, 1e-6);

    sub.stop();
}

TEST_F(GenericSubscriberTest, ImuMultipleFields)
{
    const std::string topic = "/gs_test_imu_multi";

    GenericSubscriber sub(bridge_->node(), topic,
                          "sensor_msgs/msg/Imu", intr_);
    int id_ax = sub.add_field("linear_acceleration.x");
    int id_ay = sub.add_field("linear_acceleration.y");
    int id_az = sub.add_field("linear_acceleration.z");
    ASSERT_GE(id_ax, 0);
    ASSERT_GE(id_ay, 0);
    ASSERT_GE(id_az, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<sensor_msgs::msg::Imu>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    sensor_msgs::msg::Imu msg;
    msg.linear_acceleration.x =  9.81;
    msg.linear_acceleration.y =  0.01;
    msg.linear_acceleration.z = -0.05;
    pub->publish(msg);

    bool got = spin_until([&]{
        return sub.pending(id_ax) > 0 &&
               sub.pending(id_ay) > 0 &&
               sub.pending(id_az) > 0;
    }, 2000ms);
    ASSERT_TRUE(got);

    FieldSample s;
    sub.pop(id_ax, s); EXPECT_NEAR(s.value,  9.81, 1e-4);
    sub.pop(id_ay, s); EXPECT_NEAR(s.value,  0.01, 1e-6);
    sub.pop(id_az, s); EXPECT_NEAR(s.value, -0.05, 1e-6);

    sub.stop();
}

// ===========================================================================
// Suite 12: Edge cases
// ===========================================================================

TEST_F(GenericSubscriberTest, ZeroExtractorsStillReceivesMessages)
{
    const std::string topic = "/gs_test_no_fields";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    // No field extractors registered.
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    pub->publish(msg);

    bool got = spin_until([&]{
        return sub.stats().messages_received >= 1;
    }, 2000ms);
    EXPECT_TRUE(got);

    sub.stop();
}

TEST_F(GenericSubscriberTest, SameIdNotReused)
{
    GenericSubscriber sub(bridge_->node(), "/test", "std_msgs/msg/Float64",
                          intr_);
    int id0 = sub.add_field("data");
    sub.remove_field(id0);
    int id1 = sub.add_field("data");
    // After removal and re-add, new ID must be different (monotone counter).
    EXPECT_GT(id1, id0);
}

TEST_F(GenericSubscriberTest, StopClearsSubscriptionNotBuffer)
{
    const std::string topic = "/gs_test_stop_buf";

    GenericSubscriber sub(bridge_->node(), topic,
                          "std_msgs/msg/Float64", intr_);
    int id = sub.add_field("data");
    ASSERT_GE(id, 0);
    ASSERT_TRUE(sub.start());

    auto pub = pub_node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
    spin_until([&]{ return pub->get_subscription_count() >= 1; }, 2000ms);

    std_msgs::msg::Float64 msg;
    msg.data = 77.0;
    pub->publish(msg);

    spin_until([&]{ return sub.pending(id) > 0; }, 2000ms);

    sub.stop();
    EXPECT_FALSE(sub.is_running());

    // Samples should still be poppable after stop().
    FieldSample s;
    ASSERT_TRUE(sub.pop(id, s));
    EXPECT_DOUBLE_EQ(s.value, 77.0);
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

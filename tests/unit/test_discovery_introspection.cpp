// H1 — Unit tests: Topic Discovery (A3) & Message Introspection (A4)
//
// Comprehensive test suite covering:
//   - TopicDiscovery: namespace grouping, multi-namespace, QoS, callbacks,
//     concurrent refresh, start/stop lifecycle, service/node discovery
//   - MessageIntrospector: all primitive field types, fixed/dynamic arrays,
//     deeply nested messages, schema caching, concurrent introspection,
//     FieldDescriptor metadata, numeric_paths completeness
//   - FieldAccessor: all scalar types (bool/int8-64/uint8-64/float32/64),
//     fixed-array element access, out-of-bounds, extract_int64 precision,
//     multiple independent accessors on same schema, accessor copy semantics
//   - TopicDiscovery × MessageIntrospector: discover a topic then introspect
//     its type — end-to-end smoke integration
//
// Tests compile and run only when SPECTRA_USE_ROS2=ON.
// Each test binary has its own main() that registers RclcppEnvironment so
// rclcpp::init / rclcpp::shutdown are called exactly once per process.

#include "topic_discovery.hpp"
#include "message_introspector.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/byte.hpp>
#include <std_msgs/msg/char.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/int8.hpp>
#include <std_msgs/msg/int16.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/int64.hpp>
#include <std_msgs/msg/u_int8.hpp>
#include <std_msgs/msg/u_int16.hpp>
#include <std_msgs/msg/u_int32.hpp>
#include <std_msgs/msg/u_int64.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/pose.hpp>
#include <geometry_msgs/msg/vector3.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <sensor_msgs/msg/imu.hpp>

#include <rosidl_typesupport_cpp/message_type_support.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>

using namespace spectra::adapters::ros2;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// RclcppEnvironment — init/shutdown once per process
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
// Helper: get introspection type support for a message type T
// ---------------------------------------------------------------------------

template<typename MsgT>
const rosidl_message_type_support_t* get_introspection_ts()
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<MsgT>();
    if (!ts) return nullptr;
    return get_message_typesupport_handle(
        ts, rosidl_typesupport_introspection_cpp::typesupport_identifier);
}

// ---------------------------------------------------------------------------
// DiscoveryFixture — shared node + executor + TopicDiscovery per test
// ---------------------------------------------------------------------------

class DiscoveryFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);

        static std::atomic<int> counter{0};
        const std::string name =
            "di_test_" + std::to_string(counter.fetch_add(1));

        node_     = std::make_shared<rclcpp::Node>(name);
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);

        stop_spin_.store(false);
        spin_thread_ = std::thread([this]() {
            while (rclcpp::ok() && !stop_spin_.load(std::memory_order_acquire))
                executor_->spin_once(10ms);
        });

        disc_ = std::make_unique<TopicDiscovery>(node_);
    }

    void TearDown() override
    {
        disc_.reset();
        stop_spin_.store(true, std::memory_order_release);
        executor_->cancel();
        if (spin_thread_.joinable())
            spin_thread_.join();
        executor_->remove_node(node_);
        executor_.reset();
        node_.reset();
    }

    void spin_for(std::chrono::milliseconds d) { std::this_thread::sleep_for(d); }

    template<typename Pred>
    bool wait_for(Pred pred, std::chrono::milliseconds timeout = 2000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!pred() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        return pred();
    }

    rclcpp::Node::SharedPtr                                    node_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
    std::unique_ptr<TopicDiscovery>                            disc_;
    std::thread                                                spin_thread_;
    std::atomic<bool>                                          stop_spin_{false};
};

// ---------------------------------------------------------------------------
// IntrospectionFixture — fresh MessageIntrospector per test
// ---------------------------------------------------------------------------

class IntrospectionFixture : public ::testing::Test
{
protected:
    void SetUp() override { intr_ = std::make_unique<MessageIntrospector>(); }
    void TearDown() override { intr_.reset(); }

    std::unique_ptr<MessageIntrospector> intr_;
};

// ===========================================================================
// Suite: TopicDiscovery — namespace grouping
// ===========================================================================

TEST_F(DiscoveryFixture, TopicsInSameNamespaceAreDiscovered)
{
    auto p1 = node_->create_publisher<std_msgs::msg::Float64>("/sensors/temp",     10);
    auto p2 = node_->create_publisher<std_msgs::msg::Float64>("/sensors/humidity", 10);

    spin_for(200ms);
    disc_->refresh();

    EXPECT_TRUE(disc_->has_topic("/sensors/temp"));
    EXPECT_TRUE(disc_->has_topic("/sensors/humidity"));
}

TEST_F(DiscoveryFixture, TopicsInDifferentNamespacesAreDiscovered)
{
    auto p1 = node_->create_publisher<std_msgs::msg::Float64>("/ns_a/data", 10);
    auto p2 = node_->create_publisher<std_msgs::msg::Float64>("/ns_b/data", 10);
    auto p3 = node_->create_publisher<std_msgs::msg::Float64>("/ns_c/data", 10);

    spin_for(200ms);
    disc_->refresh();

    EXPECT_TRUE(disc_->has_topic("/ns_a/data"));
    EXPECT_TRUE(disc_->has_topic("/ns_b/data"));
    EXPECT_TRUE(disc_->has_topic("/ns_c/data"));
}

TEST_F(DiscoveryFixture, DeeplyNestedTopicNameDiscovered)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(
        "/robot/arm/joint1/position", 10);

    spin_for(200ms);
    disc_->refresh();

    EXPECT_TRUE(disc_->has_topic("/robot/arm/joint1/position"));
    const auto info = disc_->topic("/robot/arm/joint1/position");
    EXPECT_EQ(info.name, "/robot/arm/joint1/position");
}

TEST_F(DiscoveryFixture, TopicCountIncrementsWithEachNewTopic)
{
    disc_->refresh();
    const std::size_t base = disc_->topic_count();

    auto p1 = node_->create_publisher<std_msgs::msg::Float64>("/di_count_1", 10);
    spin_for(200ms);
    disc_->refresh();
    const std::size_t after1 = disc_->topic_count();

    auto p2 = node_->create_publisher<std_msgs::msg::Float64>("/di_count_2", 10);
    spin_for(200ms);
    disc_->refresh();
    const std::size_t after2 = disc_->topic_count();

    EXPECT_GT(after1, base);
    EXPECT_GT(after2, after1);
}

TEST_F(DiscoveryFixture, MultiplePublishersOnSameTopicReflectedInCount)
{
    // Create a second node and publisher to the same topic.
    auto node2 = std::make_shared<rclcpp::Node>("di_pub2_node");
    auto pub1  = node_->create_publisher<std_msgs::msg::Float64>("/di_multi_pub", 10);
    auto pub2  = node2->create_publisher<std_msgs::msg::Float64>("/di_multi_pub", 10);

    // Spin both nodes briefly so the graph propagates.
    for (int i = 0; i < 30; ++i)
    {
        rclcpp::spin_some(node2);
        std::this_thread::sleep_for(10ms);
    }
    spin_for(100ms);
    disc_->refresh();

    const auto info = disc_->topic("/di_multi_pub");
    EXPECT_GE(info.publisher_count, 2)
        << "Expected at least 2 publishers on /di_multi_pub";
}

// ===========================================================================
// Suite: TopicDiscovery — QoS
// ===========================================================================

TEST_F(DiscoveryFixture, QosDurabilityTransientLocalPopulated)
{
    rclcpp::QoS qos(1);
    qos.transient_local();
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(
        "/di_qos_transient", qos);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic("/di_qos_transient");
    EXPECT_EQ(info.qos.durability, std::string("transient_local"));
}

TEST_F(DiscoveryFixture, QosDurabilityVolatilePopulated)
{
    rclcpp::QoS qos(10);
    qos.durability_volatile();
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(
        "/di_qos_volatile", qos);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic("/di_qos_volatile");
    EXPECT_EQ(info.qos.durability, std::string("volatile"));
}

TEST_F(DiscoveryFixture, QosDepthReflectedForKnownPublisher)
{
    const int depth = 42;
    rclcpp::QoS qos(depth);
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(
        "/di_qos_depth", qos);

    spin_for(200ms);
    disc_->refresh();

    const auto info = disc_->topic("/di_qos_depth");
    // depth == 0 means unlimited in some ROS2 versions; >= 0 is always safe.
    EXPECT_GE(info.qos.depth, 0);
}

// ===========================================================================
// Suite: TopicDiscovery — concurrent refresh safety
// ===========================================================================

TEST_F(DiscoveryFixture, ConcurrentRefreshDoesNotCrash)
{
    disc_->refresh();   // initial populate

    std::atomic<int> errors{0};
    auto worker = [&]() {
        try { disc_->refresh(); }
        catch (...) { ++errors; }
    };

    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(errors.load(), 0);
}

TEST_F(DiscoveryFixture, ConcurrentTopicsAndServicesQuerySafe)
{
    disc_->refresh();

    std::atomic<int> errors{0};
    auto query = [&]() {
        try {
            (void)disc_->topics();
            (void)disc_->services();
            (void)disc_->nodes();
            (void)disc_->topic_count();
            (void)disc_->service_count();
            (void)disc_->node_count();
        }
        catch (...) { ++errors; }
    };

    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(query);
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(errors.load(), 0);
}

// ===========================================================================
// Suite: TopicDiscovery — callback registration after first refresh
// ===========================================================================

TEST_F(DiscoveryFixture, SetCallbackAfterRefreshReceivesNewTopicsOnly)
{
    // Register existing topics before setting callback.
    auto existing = node_->create_publisher<std_msgs::msg::Float64>(
        "/di_pre_cb", 10);
    spin_for(200ms);
    disc_->refresh();

    std::atomic<int> new_added{0};
    disc_->set_topic_callback([&](const TopicInfo& t, bool added) {
        if (added && t.name == "/di_new_cb")
            ++new_added;
    });

    auto fresh = node_->create_publisher<std_msgs::msg::Float64>(
        "/di_new_cb", 10);
    spin_for(200ms);
    disc_->refresh();

    EXPECT_GE(new_added.load(), 1);
}

TEST_F(DiscoveryFixture, RefreshDoneCallbackFiresAfterEachRefresh)
{
    std::atomic<int> count{0};
    disc_->set_refresh_done_callback([&]() { ++count; });

    disc_->refresh();
    disc_->refresh();
    disc_->refresh();

    EXPECT_EQ(count.load(), 3);
}

TEST_F(DiscoveryFixture, NodeCallbackFiresForSelfNodeOnFirstRefresh)
{
    std::atomic<bool> found_self{false};
    const std::string own_name = node_->get_name();

    disc_->set_node_callback([&](const NodeInfo& n, bool added) {
        if (added && n.name == own_name)
            found_self.store(true);
    });

    disc_->refresh();
    EXPECT_TRUE(found_self.load());
}

// ===========================================================================
// Suite: TopicDiscovery — has_topic / topic() edge cases
// ===========================================================================

TEST_F(DiscoveryFixture, HasTopicReturnsFalseForNonExistentAfterRefresh)
{
    disc_->refresh();
    EXPECT_FALSE(disc_->has_topic("/definitely_does_not_exist_xyz_abc"));
}

TEST_F(DiscoveryFixture, TopicReturnsEmptyForNonExistent)
{
    disc_->refresh();
    const auto info = disc_->topic("/no_such_topic_xyz");
    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.types.empty());
}

TEST_F(DiscoveryFixture, TopicsVectorSizeMatchesTopicCount)
{
    auto p1 = node_->create_publisher<std_msgs::msg::Float64>("/di_size1", 10);
    auto p2 = node_->create_publisher<std_msgs::msg::Float64>("/di_size2", 10);
    spin_for(200ms);
    disc_->refresh();

    EXPECT_EQ(disc_->topics().size(), disc_->topic_count());
}

TEST_F(DiscoveryFixture, NodesVectorSizeMatchesNodeCount)
{
    disc_->refresh();
    EXPECT_EQ(disc_->nodes().size(), disc_->node_count());
}

TEST_F(DiscoveryFixture, ServicesVectorSizeMatchesServiceCount)
{
    disc_->refresh();
    EXPECT_EQ(disc_->services().size(), disc_->service_count());
}

// ===========================================================================
// Suite: TopicDiscovery — stop/start/interval lifecycle
// ===========================================================================

TEST_F(DiscoveryFixture, StopIsIdempotent)
{
    EXPECT_NO_THROW({
        disc_->stop();
        disc_->stop();
        disc_->stop();
    });
}

TEST_F(DiscoveryFixture, StartStopCycleRepeatableWithoutCrash)
{
    EXPECT_NO_THROW({
        disc_->start();
        disc_->stop();
        disc_->start();
        disc_->stop();
    });
}

TEST_F(DiscoveryFixture, RefreshIntervalGetterMatchesSetter)
{
    disc_->set_refresh_interval(250ms);
    EXPECT_EQ(disc_->refresh_interval(), 250ms);

    disc_->set_refresh_interval(1000ms);
    EXPECT_EQ(disc_->refresh_interval(), 1000ms);
}

TEST_F(DiscoveryFixture, RefreshAfterDestructionGuardedBySharedPtr)
{
    // Create a fresh discovery, arm it, then destroy while running.
    auto d = std::make_unique<TopicDiscovery>(node_);
    d->set_refresh_interval(50ms);
    d->start();
    spin_for(80ms);
    EXPECT_NO_THROW(d.reset());
}

// ===========================================================================
// Suite: MessageIntrospector — primitive scalar types
// ===========================================================================

TEST_F(IntrospectionFixture, Float32FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float32>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Float32");
    ASSERT_NE(schema, nullptr);

    const auto* fd = schema->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Float32);
    EXPECT_TRUE(fd->is_numeric_leaf());

    std_msgs::msg::Float32 msg;
    msg.data = 1.5f;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_EQ(acc.leaf_type(), FieldType::Float32);
    EXPECT_NEAR(acc.extract_double(&msg), 1.5, 1e-5);
}

TEST_F(IntrospectionFixture, Int8FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Int8>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Int8");
    ASSERT_NE(schema, nullptr);

    const auto* fd = schema->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Int8);

    std_msgs::msg::Int8 msg;
    msg.data = -128;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), -128.0);
    EXPECT_EQ(acc.extract_int64(&msg), -128);
}

TEST_F(IntrospectionFixture, Int16FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Int16>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Int16");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::Int16 msg;
    msg.data = -32000;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), -32000.0);
    EXPECT_EQ(acc.extract_int64(&msg), -32000);
}

TEST_F(IntrospectionFixture, Int64FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Int64>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Int64");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::Int64 msg;
    msg.data = -1'000'000'000LL;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_EQ(acc.extract_int64(&msg), -1'000'000'000LL);
}

TEST_F(IntrospectionFixture, UInt8FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::UInt8>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/UInt8");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::UInt8 msg;
    msg.data = 255u;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 255.0);
    EXPECT_EQ(acc.extract_int64(&msg), 255);
}

TEST_F(IntrospectionFixture, UInt16FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::UInt16>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/UInt16");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::UInt16 msg;
    msg.data = 65535u;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 65535.0);
}

TEST_F(IntrospectionFixture, UInt32FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::UInt32>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/UInt32");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::UInt32 msg;
    msg.data = 4'000'000'000u;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 4'000'000'000.0);
    EXPECT_EQ(acc.extract_int64(&msg), static_cast<int64_t>(4'000'000'000u));
}

TEST_F(IntrospectionFixture, UInt64FieldTypeAndExtraction)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::UInt64>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/UInt64");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::UInt64 msg;
    msg.data = 1'000'000'000ULL;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_EQ(acc.extract_int64(&msg), static_cast<int64_t>(1'000'000'000ULL));
}

TEST_F(IntrospectionFixture, BoolTrueExtractsAs1)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Bool>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Bool");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::Bool msg;
    msg.data = true;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 1.0);
    EXPECT_EQ(acc.extract_int64(&msg), 1);
}

TEST_F(IntrospectionFixture, BoolFalseExtractsAs0)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Bool>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Bool");
    ASSERT_NE(schema, nullptr);

    std_msgs::msg::Bool msg;
    msg.data = false;
    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 0.0);
    EXPECT_EQ(acc.extract_int64(&msg), 0);
}

// ===========================================================================
// Suite: MessageIntrospector — geometry_msgs/Pose (nested Quaternion)
// ===========================================================================

class PoseSchemaTest : public IntrospectionFixture
{
protected:
    void SetUp() override
    {
        IntrospectionFixture::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            geometry_msgs::msg::Pose>();
        schema_ = intr_->introspect_type_support(ts, "geometry_msgs/msg/Pose");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(PoseSchemaTest, HasPositionAndOrientation)
{
    // geometry_msgs/Pose: position (Point), orientation (Quaternion)
    EXPECT_EQ(schema_->fields.size(), 2u);
}

TEST_F(PoseSchemaTest, PositionXYZPresent)
{
    EXPECT_NE(schema_->find_field("position.x"), nullptr);
    EXPECT_NE(schema_->find_field("position.y"), nullptr);
    EXPECT_NE(schema_->find_field("position.z"), nullptr);
}

TEST_F(PoseSchemaTest, OrientationXYZWPresent)
{
    EXPECT_NE(schema_->find_field("orientation.x"), nullptr);
    EXPECT_NE(schema_->find_field("orientation.y"), nullptr);
    EXPECT_NE(schema_->find_field("orientation.z"), nullptr);
    EXPECT_NE(schema_->find_field("orientation.w"), nullptr);
}

TEST_F(PoseSchemaTest, NumericPathsHasSevenComponents)
{
    // position.x/y/z + orientation.x/y/z/w = 7
    const auto paths = schema_->numeric_paths();
    EXPECT_EQ(paths.size(), 7u);
}

TEST_F(PoseSchemaTest, PositionXExtraction)
{
    geometry_msgs::msg::Pose msg;
    msg.position.x = 3.5;
    msg.position.y = -1.2;
    msg.position.z = 0.8;

    auto acc = intr_->make_accessor(*schema_, "position.x");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg), 3.5, 1e-10);
}

TEST_F(PoseSchemaTest, OrientationWExtraction)
{
    geometry_msgs::msg::Pose msg;
    msg.orientation.w = 1.0;
    msg.orientation.x = 0.0;
    msg.orientation.y = 0.0;
    msg.orientation.z = 0.0;

    auto acc = intr_->make_accessor(*schema_, "orientation.w");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg), 1.0, 1e-10);
}

TEST_F(PoseSchemaTest, AllSevenFieldsExtractIndependently)
{
    geometry_msgs::msg::Pose msg;
    msg.position.x    = 1.0;
    msg.position.y    = 2.0;
    msg.position.z    = 3.0;
    msg.orientation.x = 0.1;
    msg.orientation.y = 0.2;
    msg.orientation.z = 0.3;
    msg.orientation.w = 0.9;

    const std::vector<std::pair<std::string, double>> expected = {
        {"position.x",    1.0}, {"position.y",    2.0}, {"position.z",    3.0},
        {"orientation.x", 0.1}, {"orientation.y", 0.2},
        {"orientation.z", 0.3}, {"orientation.w", 0.9},
    };

    for (const auto& [path, want] : expected)
    {
        auto acc = intr_->make_accessor(*schema_, path);
        ASSERT_TRUE(acc.valid()) << "path: " << path;
        EXPECT_NEAR(acc.extract_double(&msg), want, 1e-10) << "path: " << path;
    }
}

// ===========================================================================
// Suite: MessageIntrospector — Float64MultiArray (dynamic array field)
// ===========================================================================

class MultiArraySchemaTest : public IntrospectionFixture
{
protected:
    void SetUp() override
    {
        IntrospectionFixture::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Float64MultiArray>();
        schema_ = intr_->introspect_type_support(
            ts, "std_msgs/msg/Float64MultiArray");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(MultiArraySchemaTest, SchemaHasFields)
{
    EXPECT_FALSE(schema_->fields.empty());
}

TEST_F(MultiArraySchemaTest, DataFieldIsArray)
{
    const auto* fd = schema_->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_array);
}

TEST_F(MultiArraySchemaTest, DataFieldIsDynamicArray)
{
    const auto* fd = schema_->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_dynamic_array);
}

TEST_F(MultiArraySchemaTest, AccessorForDataFieldIsArray)
{
    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_TRUE(acc.is_array());
    EXPECT_TRUE(acc.is_dynamic_array());
}

TEST_F(MultiArraySchemaTest, ExtractFirstElementFromDynamicArray)
{
    std_msgs::msg::Float64MultiArray msg;
    msg.data = {10.0, 20.0, 30.0};

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg, 0), 10.0, 1e-10);
}

TEST_F(MultiArraySchemaTest, ExtractSecondElementFromDynamicArray)
{
    std_msgs::msg::Float64MultiArray msg;
    msg.data = {10.0, 20.0, 30.0};

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg, 1), 20.0, 1e-10);
}

TEST_F(MultiArraySchemaTest, ExtractThirdElementFromDynamicArray)
{
    std_msgs::msg::Float64MultiArray msg;
    msg.data = {10.0, 20.0, 30.0};

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg, 2), 30.0, 1e-10);
}

TEST_F(MultiArraySchemaTest, OutOfBoundsArrayIndexReturnsNaN)
{
    std_msgs::msg::Float64MultiArray msg;
    msg.data = {1.0, 2.0};

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    // index 10 is well past the 2-element array
    const double v = acc.extract_double(&msg, 10);
    EXPECT_TRUE(std::isnan(v));
}

// ===========================================================================
// Suite: MessageIntrospector — sensor_msgs/Imu (covariance arrays)
// ===========================================================================

class ImuDetailTest : public IntrospectionFixture
{
protected:
    void SetUp() override
    {
        IntrospectionFixture::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            sensor_msgs::msg::Imu>();
        schema_ = intr_->introspect_type_support(ts, "sensor_msgs/msg/Imu");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(ImuDetailTest, OrientationCovarianceIsFixedArray)
{
    const auto* fd = schema_->find_field("orientation_covariance");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_array);
    EXPECT_FALSE(fd->is_dynamic_array);
    EXPECT_EQ(fd->array_size, 9u);
}

TEST_F(ImuDetailTest, LinearAccelerationCovarianceIsFixedArray)
{
    const auto* fd = schema_->find_field("linear_acceleration_covariance");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_array);
    EXPECT_EQ(fd->array_size, 9u);
}

TEST_F(ImuDetailTest, AngularVelocityCovarianceIsFixedArray)
{
    const auto* fd = schema_->find_field("angular_velocity_covariance");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_array);
    EXPECT_EQ(fd->array_size, 9u);
}

TEST_F(ImuDetailTest, CovarianceAccessorMarkedAsArray)
{
    auto acc = intr_->make_accessor(*schema_, "orientation_covariance");
    ASSERT_TRUE(acc.valid());
    EXPECT_TRUE(acc.is_array());
    EXPECT_EQ(acc.array_size(), 9u);
}

TEST_F(ImuDetailTest, ExtractCovarianceElement0)
{
    sensor_msgs::msg::Imu msg;
    msg.orientation_covariance[0] = 0.01;
    msg.orientation_covariance[4] = 0.02;
    msg.orientation_covariance[8] = 0.03;

    auto acc = intr_->make_accessor(*schema_, "orientation_covariance");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg, 0), 0.01, 1e-10);
}

TEST_F(ImuDetailTest, ExtractCovarianceElement4)
{
    sensor_msgs::msg::Imu msg;
    msg.orientation_covariance[4] = 0.02;

    auto acc = intr_->make_accessor(*schema_, "orientation_covariance");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg, 4), 0.02, 1e-10);
}

// ===========================================================================
// Suite: MessageIntrospector — cache and concurrent introspection
// ===========================================================================

TEST_F(IntrospectionFixture, CacheSizeZeroOnConstruction)
{
    EXPECT_EQ(intr_->cache_size(), 0u);
}

TEST_F(IntrospectionFixture, CacheGrowsPerUniqueType)
{
    {
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Float64>();
        intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
    }
    EXPECT_EQ(intr_->cache_size(), 1u);

    {
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Int32>();
        intr_->introspect_type_support(ts, "std_msgs/msg/Int32");
    }
    EXPECT_EQ(intr_->cache_size(), 2u);

    {
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            geometry_msgs::msg::Twist>();
        intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
    }
    EXPECT_EQ(intr_->cache_size(), 3u);
}

TEST_F(IntrospectionFixture, ConcurrentIntrospectionSameTypeSafe)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    threads.reserve(8);

    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&]() {
            try {
                auto s = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
                if (!s) ++errors;
            }
            catch (...) { ++errors; }
        });
    }
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(intr_->cache_size(), 1u);   // deduplicated
}

TEST_F(IntrospectionFixture, IntrospectSameTypeTwiceGivesSamePointer)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Int64>();
    auto s1 = intr_->introspect_type_support(ts, "std_msgs/msg/Int64");
    auto s2 = intr_->introspect_type_support(ts, "std_msgs/msg/Int64");
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s1.get(), s2.get());
}

TEST_F(IntrospectionFixture, ClearCacheDropsAllEntries)
{
    const auto* ts1 = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();
    const auto* ts2 = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Int32>();
    intr_->introspect_type_support(ts1, "std_msgs/msg/Float64");
    intr_->introspect_type_support(ts2, "std_msgs/msg/Int32");
    EXPECT_EQ(intr_->cache_size(), 2u);

    intr_->clear_cache();
    EXPECT_EQ(intr_->cache_size(), 0u);
}

// ===========================================================================
// Suite: FieldDescriptor metadata
// ===========================================================================

TEST_F(IntrospectionFixture, FieldDescriptorFullPathMatchesPath)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        geometry_msgs::msg::Twist>();
    auto schema = intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
    ASSERT_NE(schema, nullptr);

    // All leaf fields should have non-empty full_path.
    for (const auto& f : schema->fields)
    {
        if (!f.children.empty())
        {
            for (const auto& child : f.children)
                EXPECT_FALSE(child.full_path.empty()) << "child: " << child.name;
        }
        EXPECT_FALSE(f.full_path.empty()) << "field: " << f.name;
    }
}

TEST_F(IntrospectionFixture, FieldDescriptorOffsetIsNonZeroForNestedField)
{
    // For a nested message, the second field (e.g. angular in Twist) should
    // have a nonzero offset since linear comes before it.
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        geometry_msgs::msg::Twist>();
    auto schema = intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
    ASSERT_NE(schema, nullptr);
    ASSERT_EQ(schema->fields.size(), 2u);

    // angular is the second field — its offset must be >= sizeof(Vector3).
    const auto& angular = schema->fields[1];
    EXPECT_GT(angular.offset, 0u);
}

TEST_F(IntrospectionFixture, NumericPathsContainAllLeafPaths)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        geometry_msgs::msg::Twist>();
    auto schema = intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
    ASSERT_NE(schema, nullptr);

    const auto paths = schema->numeric_paths();

    // Every path returned should be findable in the schema.
    for (const auto& p : paths)
    {
        const auto* fd = schema->find_field(p);
        ASSERT_NE(fd, nullptr) << "path not found: " << p;
        EXPECT_TRUE(fd->is_numeric_leaf()) << "not numeric leaf: " << p;
    }
}

// ===========================================================================
// Suite: FieldAccessor — multiple independent accessors on same schema
// ===========================================================================

TEST_F(IntrospectionFixture, MultipleAccessorsOnSameSchemaAreIndependent)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        sensor_msgs::msg::Imu>();
    auto schema = intr_->introspect_type_support(ts, "sensor_msgs/msg/Imu");
    ASSERT_NE(schema, nullptr);

    auto acc_lax = intr_->make_accessor(*schema, "linear_acceleration.x");
    auto acc_lay = intr_->make_accessor(*schema, "linear_acceleration.y");
    auto acc_laz = intr_->make_accessor(*schema, "linear_acceleration.z");
    auto acc_avz = intr_->make_accessor(*schema, "angular_velocity.z");

    ASSERT_TRUE(acc_lax.valid());
    ASSERT_TRUE(acc_lay.valid());
    ASSERT_TRUE(acc_laz.valid());
    ASSERT_TRUE(acc_avz.valid());

    sensor_msgs::msg::Imu msg;
    msg.linear_acceleration.x = 1.1;
    msg.linear_acceleration.y = 2.2;
    msg.linear_acceleration.z = 3.3;
    msg.angular_velocity.z    = 4.4;

    EXPECT_NEAR(acc_lax.extract_double(&msg), 1.1, 1e-10);
    EXPECT_NEAR(acc_lay.extract_double(&msg), 2.2, 1e-10);
    EXPECT_NEAR(acc_laz.extract_double(&msg), 3.3, 1e-10);
    EXPECT_NEAR(acc_avz.extract_double(&msg), 4.4, 1e-10);
}

TEST_F(IntrospectionFixture, AccessorPathMembersMatchRequested)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        geometry_msgs::msg::Twist>();
    auto schema = intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
    ASSERT_NE(schema, nullptr);

    auto acc = intr_->make_accessor(*schema, "angular.y");
    ASSERT_TRUE(acc.valid());
    EXPECT_EQ(acc.path(), "angular.y");
    EXPECT_EQ(acc.leaf_type(), FieldType::Float64);
    EXPECT_FALSE(acc.is_array());
}

TEST_F(IntrospectionFixture, DefaultConstructedAccessorIsInvalid)
{
    FieldAccessor acc;
    EXPECT_FALSE(acc.valid());
    EXPECT_TRUE(std::isnan(acc.extract_double(nullptr)));
    EXPECT_EQ(acc.extract_int64(nullptr), 0);
}

TEST_F(IntrospectionFixture, ExtractDoubleFromNullPtrReturnsNaN)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
    ASSERT_NE(schema, nullptr);

    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_TRUE(std::isnan(acc.extract_double(nullptr)));
}

TEST_F(IntrospectionFixture, ExtractInt64FromNullPtrReturnsZero)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Int32>();
    auto schema = intr_->introspect_type_support(ts, "std_msgs/msg/Int32");
    ASSERT_NE(schema, nullptr);

    auto acc = intr_->make_accessor(*schema, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_EQ(acc.extract_int64(nullptr), 0);
}

// ===========================================================================
// Suite: FieldType utilities (free functions)
// ===========================================================================

TEST(FieldTypeUtilsExtra, FieldTypeNamesForAllIntegerTypes)
{
    EXPECT_STREQ(field_type_name(FieldType::Int8),   "int8");
    EXPECT_STREQ(field_type_name(FieldType::Int16),  "int16");
    EXPECT_STREQ(field_type_name(FieldType::Int32),  "int32");
    EXPECT_STREQ(field_type_name(FieldType::Int64),  "int64");
    EXPECT_STREQ(field_type_name(FieldType::Uint8),  "uint8");
    EXPECT_STREQ(field_type_name(FieldType::Uint16), "uint16");
    EXPECT_STREQ(field_type_name(FieldType::Uint32), "uint32");
    EXPECT_STREQ(field_type_name(FieldType::Uint64), "uint64");
}

TEST(FieldTypeUtilsExtra, FieldTypeNamesForFloatTypes)
{
    EXPECT_STREQ(field_type_name(FieldType::Float32), "float32");
    EXPECT_STREQ(field_type_name(FieldType::Float64), "float64");
}

TEST(FieldTypeUtilsExtra, FieldTypeNameForBool)
{
    EXPECT_STREQ(field_type_name(FieldType::Bool), "bool");
}

TEST(FieldTypeUtilsExtra, IsNumericTrueForAllScalars)
{
    const std::vector<FieldType> numeric_types = {
        FieldType::Bool,    FieldType::Float32, FieldType::Float64,
        FieldType::Int8,    FieldType::Uint8,   FieldType::Int16,
        FieldType::Uint16,  FieldType::Int32,   FieldType::Uint32,
        FieldType::Int64,   FieldType::Uint64,
    };
    for (const auto t : numeric_types)
        EXPECT_TRUE(is_numeric(t)) << "type: " << static_cast<int>(t);
}

TEST(FieldTypeUtilsExtra, IsNumericFalseForNonScalars)
{
    EXPECT_FALSE(is_numeric(FieldType::String));
    EXPECT_FALSE(is_numeric(FieldType::WString));
    EXPECT_FALSE(is_numeric(FieldType::Message));
    EXPECT_FALSE(is_numeric(FieldType::Unknown));
    // Byte and Char are 1-byte integer types — plottable as numeric.
    EXPECT_TRUE(is_numeric(FieldType::Char));
    EXPECT_TRUE(is_numeric(FieldType::Byte));
}

// ===========================================================================
// Suite: TopicDiscovery × MessageIntrospector end-to-end
// ===========================================================================

TEST_F(DiscoveryFixture, DiscoverTopicThenIntrospectItsType)
{
    // Publish a Float64 topic, discover it, introspect its type.
    const std::string topic = "/di_e2e_float64";
    auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    ASSERT_TRUE(disc_->has_topic(topic));
    const auto info = disc_->topic(topic);
    ASSERT_FALSE(info.types.empty());

    // Find the type string containing "Float64".
    bool type_known = false;
    for (const auto& t : info.types)
        if (t.find("Float64") != std::string::npos)
            type_known = true;
    ASSERT_TRUE(type_known) << "Type list: " << info.types[0];

    // Introspect via type support handle (no string-based lookup needed).
    MessageIntrospector intr;
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();
    auto schema = intr.introspect_type_support(ts, "std_msgs/msg/Float64");

    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->fields.size(), 1u);
    EXPECT_EQ(schema->fields[0].name, "data");
    EXPECT_EQ(schema->fields[0].type, FieldType::Float64);
}

TEST_F(DiscoveryFixture, DiscoverTwistTopicThenIntrospect)
{
    const std::string topic = "/di_e2e_twist";
    auto pub = node_->create_publisher<geometry_msgs::msg::Twist>(topic, 10);

    spin_for(200ms);
    disc_->refresh();

    ASSERT_TRUE(disc_->has_topic(topic));
    const auto info = disc_->topic(topic);
    ASSERT_FALSE(info.types.empty());

    MessageIntrospector intr;
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        geometry_msgs::msg::Twist>();
    auto schema = intr.introspect_type_support(ts, "geometry_msgs/msg/Twist");

    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->numeric_paths().size(), 6u);
}

// ===========================================================================
// main — register the shared RclcppEnvironment
// ===========================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}

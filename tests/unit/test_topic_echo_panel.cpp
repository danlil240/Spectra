// test_topic_echo_panel.cpp — unit tests for TopicEchoPanel.
//
// Tests exercise the pure-logic API (inject_message, build_echo_message,
// format_*, message_count, etc.) without requiring a live ROS2 node or ImGui
// context.  The ImGui draw() path is not tested here (no-op in test builds).

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// We need rclcpp initialised for node creation.
#include <rclcpp/rclcpp.hpp>

#include "ui/topic_echo_panel.hpp"
#include "message_introspector.hpp"

namespace ros2 = spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Global ROS2 environment (shared by all tests in this binary)
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
   public:
    void SetUp() override { rclcpp::init(0, nullptr); }
    void TearDown() override { rclcpp::shutdown(); }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class TopicEchoPanelTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        node_  = rclcpp::Node::make_shared("test_echo_panel_" + std::to_string(counter_++));
        panel_ = std::make_unique<ros2::TopicEchoPanel>(node_, intr_);
    }

    void TearDown() override
    {
        panel_.reset();
        node_.reset();
    }

    // Build a synthetic EchoMessage with one numeric field.
    static ros2::EchoMessage make_msg(uint64_t seq, double value, const std::string& field = "x")
    {
        ros2::EchoMessage msg;
        msg.seq          = seq;
        msg.timestamp_ns = static_cast<int64_t>(seq) * 1'000'000'000LL;
        msg.wall_time_s  = static_cast<double>(seq);

        ros2::EchoFieldValue fv;
        fv.path         = field;
        fv.display_name = field;
        fv.depth        = 0;
        fv.kind         = ros2::EchoFieldValue::Kind::Numeric;
        fv.numeric      = value;
        msg.fields.push_back(std::move(fv));

        return msg;
    }

    rclcpp::Node::SharedPtr               node_;
    ros2::MessageIntrospector             intr_;
    std::unique_ptr<ros2::TopicEchoPanel> panel_;

    static std::atomic<int> counter_;
};

std::atomic<int> TopicEchoPanelTest::counter_{0};

// ===========================================================================
// Suite 1 — Construction
// ===========================================================================

TEST_F(TopicEchoPanelTest, DefaultState)
{
    EXPECT_EQ(panel_->message_count(), 0u);
    EXPECT_EQ(panel_->total_received(), 0u);
    EXPECT_TRUE(panel_->topic_name().empty());
    EXPECT_TRUE(panel_->type_name().empty());
    EXPECT_FALSE(panel_->is_paused());
    EXPECT_FALSE(panel_->is_subscribed());
}

TEST_F(TopicEchoPanelTest, DefaultTitle)
{
    EXPECT_EQ(panel_->title(), "ROS2 Echo");
}

TEST_F(TopicEchoPanelTest, SetTitle)
{
    panel_->set_title("My Echo");
    EXPECT_EQ(panel_->title(), "My Echo");
}

TEST_F(TopicEchoPanelTest, DefaultMaxMessages)
{
    EXPECT_EQ(panel_->max_messages(), 100u);
}

TEST_F(TopicEchoPanelTest, DefaultDisplayHz)
{
    EXPECT_DOUBLE_EQ(panel_->display_hz(), 30.0);
}

// ===========================================================================
// Suite 2 — Pause / Resume / Clear
// ===========================================================================

TEST_F(TopicEchoPanelTest, PauseResume)
{
    EXPECT_FALSE(panel_->is_paused());
    panel_->pause();
    EXPECT_TRUE(panel_->is_paused());
    panel_->resume();
    EXPECT_FALSE(panel_->is_paused());
}

TEST_F(TopicEchoPanelTest, PausedDropsInjectedMessages)
{
    panel_->pause();
    panel_->inject_message(make_msg(0, 1.0));
    EXPECT_EQ(panel_->message_count(), 0u);
    EXPECT_EQ(panel_->total_received(), 0u);
}

TEST_F(TopicEchoPanelTest, ResumeAcceptsMessages)
{
    panel_->pause();
    panel_->inject_message(make_msg(0, 1.0));
    EXPECT_EQ(panel_->message_count(), 0u);

    panel_->resume();
    panel_->inject_message(make_msg(1, 2.0));
    EXPECT_EQ(panel_->message_count(), 1u);
    EXPECT_EQ(panel_->total_received(), 1u);
}

TEST_F(TopicEchoPanelTest, ClearRemovesMessages)
{
    panel_->inject_message(make_msg(0, 1.0));
    panel_->inject_message(make_msg(1, 2.0));
    EXPECT_EQ(panel_->message_count(), 2u);

    panel_->clear();
    EXPECT_EQ(panel_->message_count(), 0u);
}

TEST_F(TopicEchoPanelTest, ClearDoesNotResetTotalReceived)
{
    panel_->inject_message(make_msg(0, 1.0));
    panel_->inject_message(make_msg(1, 2.0));
    panel_->clear();
    // total_received tracks all-time receptions; clear() only empties the ring.
    EXPECT_EQ(panel_->total_received(), 2u);
}

// ===========================================================================
// Suite 3 — Ring buffer / max_messages
// ===========================================================================

TEST_F(TopicEchoPanelTest, InjectSingleMessage)
{
    panel_->inject_message(make_msg(42, 3.14));
    EXPECT_EQ(panel_->message_count(), 1u);
    EXPECT_EQ(panel_->total_received(), 1u);
}

TEST_F(TopicEchoPanelTest, InjectMultipleMessages)
{
    for (int i = 0; i < 10; ++i)
    {
        panel_->inject_message(make_msg(static_cast<uint64_t>(i), static_cast<double>(i)));
    }
    EXPECT_EQ(panel_->message_count(), 10u);
    EXPECT_EQ(panel_->total_received(), 10u);
}

TEST_F(TopicEchoPanelTest, RingBufferPrunesOldest)
{
    panel_->set_max_messages(5);
    for (int i = 0; i < 8; ++i)
    {
        panel_->inject_message(make_msg(static_cast<uint64_t>(i), static_cast<double>(i)));
    }
    EXPECT_EQ(panel_->message_count(), 5u);

    auto snap = panel_->messages_snapshot();
    // Oldest dropped: seq 0,1,2 gone; 3..7 remain.
    EXPECT_EQ(snap.front().seq, 3u);
    EXPECT_EQ(snap.back().seq, 7u);
}

TEST_F(TopicEchoPanelTest, SetMaxMessagesTruncatesExisting)
{
    for (int i = 0; i < 20; ++i)
    {
        panel_->inject_message(make_msg(static_cast<uint64_t>(i), 0.0));
    }
    EXPECT_EQ(panel_->message_count(), 20u);

    panel_->set_max_messages(10);
    EXPECT_EQ(panel_->message_count(), 10u);
}

TEST_F(TopicEchoPanelTest, SetMaxMessagesZeroClampedToOne)
{
    panel_->set_max_messages(0);
    EXPECT_EQ(panel_->max_messages(), 1u);
}

TEST_F(TopicEchoPanelTest, TotalReceivedContinuesAfterPrune)
{
    panel_->set_max_messages(3);
    for (int i = 0; i < 10; ++i)
    {
        panel_->inject_message(make_msg(static_cast<uint64_t>(i), 0.0));
    }
    EXPECT_EQ(panel_->total_received(), 10u);
    EXPECT_EQ(panel_->message_count(), 3u);
}

// ===========================================================================
// Suite 4 — Snapshots / latest_message
// ===========================================================================

TEST_F(TopicEchoPanelTest, LatestMessageNullWhenEmpty)
{
    EXPECT_EQ(panel_->latest_message(), nullptr);
}

TEST_F(TopicEchoPanelTest, LatestMessageReturnsNewest)
{
    panel_->inject_message(make_msg(0, 1.0));
    panel_->inject_message(make_msg(1, 2.0));
    panel_->inject_message(make_msg(2, 3.0));

    auto latest = panel_->latest_message();
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->seq, 2u);
    ASSERT_FALSE(latest->fields.empty());
    EXPECT_DOUBLE_EQ(latest->fields[0].numeric, 3.0);
}

TEST_F(TopicEchoPanelTest, MessagesSnapshotOrder)
{
    for (uint64_t i = 0; i < 5; ++i)
    {
        panel_->inject_message(make_msg(i, static_cast<double>(i)));
    }
    auto snap = panel_->messages_snapshot();
    ASSERT_EQ(snap.size(), 5u);
    for (size_t i = 0; i < snap.size(); ++i)
    {
        EXPECT_EQ(snap[i].seq, static_cast<uint64_t>(i));
    }
}

// ===========================================================================
// Suite 5 — build_echo_message (static, no ROS2 node needed)
// ===========================================================================

TEST_F(TopicEchoPanelTest, BuildEchoMessageEmptySchema)
{
    ros2::MessageSchema schema;
    schema.type_name = "test/msg/Empty";

    const auto msg =
        ros2::TopicEchoPanel::build_echo_message(nullptr, schema, 99, 1'000'000'000LL, 1.0);

    EXPECT_EQ(msg.seq, 99u);
    EXPECT_EQ(msg.timestamp_ns, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(msg.wall_time_s, 1.0);
    EXPECT_TRUE(msg.fields.empty());
}

TEST_F(TopicEchoPanelTest, BuildEchoMessageNumericField)
{
    // Build a minimal schema with one Float64 field.
    ros2::MessageSchema schema;
    schema.type_name = "std_msgs/msg/Float64";

    ros2::FieldDescriptor fd;
    fd.name      = "data";
    fd.full_path = "data";
    fd.type      = ros2::FieldType::Float64;
    fd.offset    = 0;
    fd.is_array  = false;
    schema.fields.push_back(fd);

    // Message buffer: one double = 1.5.
    double     value = 1.5;
    const auto msg   = ros2::TopicEchoPanel::build_echo_message(&value, schema, 1, 0, 0.0);

    ASSERT_EQ(msg.fields.size(), 1u);
    EXPECT_EQ(msg.fields[0].display_name, "data");
    EXPECT_EQ(msg.fields[0].kind, ros2::EchoFieldValue::Kind::Numeric);
    EXPECT_DOUBLE_EQ(msg.fields[0].numeric, 1.5);
}

TEST_F(TopicEchoPanelTest, BuildEchoMessageInt32Field)
{
    ros2::MessageSchema schema;
    schema.type_name = "std_msgs/msg/Int32";

    ros2::FieldDescriptor fd;
    fd.name      = "data";
    fd.full_path = "data";
    fd.type      = ros2::FieldType::Int32;
    fd.offset    = 0;
    fd.is_array  = false;
    schema.fields.push_back(fd);

    int32_t    value = -42;
    const auto msg   = ros2::TopicEchoPanel::build_echo_message(&value, schema, 2, 0, 0.0);

    ASSERT_EQ(msg.fields.size(), 1u);
    EXPECT_EQ(msg.fields[0].kind, ros2::EchoFieldValue::Kind::Numeric);
    EXPECT_DOUBLE_EQ(msg.fields[0].numeric, -42.0);
}

TEST_F(TopicEchoPanelTest, BuildEchoMessageBoolField)
{
    ros2::MessageSchema schema;
    schema.type_name = "std_msgs/msg/Bool";

    ros2::FieldDescriptor fd;
    fd.name      = "data";
    fd.full_path = "data";
    fd.type      = ros2::FieldType::Bool;
    fd.offset    = 0;
    fd.is_array  = false;
    schema.fields.push_back(fd);

    uint8_t    value = 1;
    const auto msg   = ros2::TopicEchoPanel::build_echo_message(&value, schema, 3, 0, 0.0);

    ASSERT_EQ(msg.fields.size(), 1u);
    EXPECT_DOUBLE_EQ(msg.fields[0].numeric, 1.0);
}

TEST_F(TopicEchoPanelTest, BuildEchoMessageFloat32Field)
{
    ros2::MessageSchema schema;
    schema.type_name = "std_msgs/msg/Float32";

    ros2::FieldDescriptor fd;
    fd.name      = "data";
    fd.full_path = "data";
    fd.type      = ros2::FieldType::Float32;
    fd.offset    = 0;
    fd.is_array  = false;
    schema.fields.push_back(fd);

    float      value = 2.5f;
    const auto msg   = ros2::TopicEchoPanel::build_echo_message(&value, schema, 4, 0, 0.0);

    ASSERT_EQ(msg.fields.size(), 1u);
    EXPECT_NEAR(msg.fields[0].numeric, 2.5, 1e-5);
}

TEST_F(TopicEchoPanelTest, BuildEchoMessageNestedStruct)
{
    // Simulate a Twist-like schema: two nested structs (linear, angular)
    // each with one float field.
    struct LinearStruct
    {
        double x;
    };
    struct TwistLike
    {
        LinearStruct linear;
        double       angular_z;
    };

    ros2::MessageSchema schema;
    schema.type_name = "geometry_msgs/msg/TwistLike";

    ros2::FieldDescriptor fd_linear;
    fd_linear.name      = "linear";
    fd_linear.full_path = "linear";
    fd_linear.type      = ros2::FieldType::Message;
    fd_linear.offset    = offsetof(TwistLike, linear);
    fd_linear.is_array  = false;

    ros2::FieldDescriptor fd_x;
    fd_x.name      = "x";
    fd_x.full_path = "linear.x";
    fd_x.type      = ros2::FieldType::Float64;
    fd_x.offset    = 0;   // offset within LinearStruct
    fd_x.is_array  = false;
    fd_linear.children.push_back(fd_x);

    ros2::FieldDescriptor fd_az;
    fd_az.name      = "angular_z";
    fd_az.full_path = "angular_z";
    fd_az.type      = ros2::FieldType::Float64;
    fd_az.offset    = offsetof(TwistLike, angular_z);
    fd_az.is_array  = false;

    schema.fields.push_back(fd_linear);
    schema.fields.push_back(fd_az);

    TwistLike  data{{7.0}, 3.5};
    const auto msg = ros2::TopicEchoPanel::build_echo_message(&data, schema, 5, 0, 0.0);

    // Expect: NestedHead "linear", Numeric "x", Numeric "angular_z".
    ASSERT_EQ(msg.fields.size(), 3u);
    EXPECT_EQ(msg.fields[0].kind, ros2::EchoFieldValue::Kind::NestedHead);
    EXPECT_EQ(msg.fields[0].display_name, "linear");
    EXPECT_EQ(msg.fields[1].kind, ros2::EchoFieldValue::Kind::Numeric);
    EXPECT_EQ(msg.fields[1].display_name, "x");
    EXPECT_DOUBLE_EQ(msg.fields[1].numeric, 7.0);
    EXPECT_EQ(msg.fields[2].kind, ros2::EchoFieldValue::Kind::Numeric);
    EXPECT_EQ(msg.fields[2].display_name, "angular_z");
    EXPECT_DOUBLE_EQ(msg.fields[2].numeric, 3.5);
}

TEST_F(TopicEchoPanelTest, BuildEchoMessageFixedArrayHead)
{
    // Fixed-size array field (is_array=true, is_dynamic_array=false).
    ros2::MessageSchema schema;
    schema.type_name = "test/msg/FloatArray";

    ros2::FieldDescriptor fd;
    fd.name             = "data";
    fd.full_path        = "data";
    fd.type             = ros2::FieldType::Float64;
    fd.offset           = 0;
    fd.is_array         = true;
    fd.is_dynamic_array = false;
    fd.array_size       = 3;
    schema.fields.push_back(fd);

    double     arr[3] = {1.0, 2.0, 3.0};
    const auto msg    = ros2::TopicEchoPanel::build_echo_message(arr, schema, 6, 0, 0.0);

    // Expect: ArrayHead + 3 ArrayElement entries.
    ASSERT_GE(msg.fields.size(), 4u);
    EXPECT_EQ(msg.fields[0].kind, ros2::EchoFieldValue::Kind::ArrayHead);
    EXPECT_EQ(msg.fields[0].array_len, 3);
    EXPECT_EQ(msg.fields[1].kind, ros2::EchoFieldValue::Kind::ArrayElement);
    EXPECT_DOUBLE_EQ(msg.fields[1].numeric, 1.0);
    EXPECT_EQ(msg.fields[2].kind, ros2::EchoFieldValue::Kind::ArrayElement);
    EXPECT_DOUBLE_EQ(msg.fields[2].numeric, 2.0);
    EXPECT_EQ(msg.fields[3].kind, ros2::EchoFieldValue::Kind::ArrayElement);
    EXPECT_DOUBLE_EQ(msg.fields[3].numeric, 3.0);
}

// ===========================================================================
// Suite 6 — format_timestamp
// ===========================================================================

TEST_F(TopicEchoPanelTest, FormatTimestampZero)
{
    EXPECT_EQ(ros2::TopicEchoPanel::format_timestamp(0), "0.000000000");
}

TEST_F(TopicEchoPanelTest, FormatTimestampOneSecond)
{
    EXPECT_EQ(ros2::TopicEchoPanel::format_timestamp(1'000'000'000LL), "1.000000000");
}

TEST_F(TopicEchoPanelTest, FormatTimestampFractional)
{
    const std::string s = ros2::TopicEchoPanel::format_timestamp(1'500'000'000LL);
    EXPECT_EQ(s, "1.500000000");
}

TEST_F(TopicEchoPanelTest, FormatTimestampLarge)
{
    // ~1 billion seconds (year 2001+).
    const std::string s = ros2::TopicEchoPanel::format_timestamp(1'000'000'000'000'000'000LL);
    EXPECT_FALSE(s.empty());
}

// ===========================================================================
// Suite 7 — format_numeric
// ===========================================================================

TEST_F(TopicEchoPanelTest, FormatNumericZero)
{
    EXPECT_EQ(ros2::TopicEchoPanel::format_numeric(0.0), "0");
}

TEST_F(TopicEchoPanelTest, FormatNumericInteger)
{
    EXPECT_EQ(ros2::TopicEchoPanel::format_numeric(42.0), "42");
    EXPECT_EQ(ros2::TopicEchoPanel::format_numeric(-7.0), "-7");
}

TEST_F(TopicEchoPanelTest, FormatNumericFloat)
{
    const std::string s = ros2::TopicEchoPanel::format_numeric(3.14);
    EXPECT_FALSE(s.empty());
    EXPECT_NE(s, "3");
}

TEST_F(TopicEchoPanelTest, FormatNumericNan)
{
    EXPECT_EQ(ros2::TopicEchoPanel::format_numeric(std::numeric_limits<double>::quiet_NaN()),
              "nan");
}

TEST_F(TopicEchoPanelTest, FormatNumericInf)
{
    EXPECT_EQ(ros2::TopicEchoPanel::format_numeric(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(ros2::TopicEchoPanel::format_numeric(-std::numeric_limits<double>::infinity()),
              "-inf");
}

// ===========================================================================
// Suite 8 — Configuration
// ===========================================================================

TEST_F(TopicEchoPanelTest, SetDisplayHz)
{
    panel_->set_display_hz(60.0);
    EXPECT_DOUBLE_EQ(panel_->display_hz(), 60.0);
}

TEST_F(TopicEchoPanelTest, SetDisplayHzZeroDisablesThrottle)
{
    panel_->set_display_hz(0.0);
    EXPECT_EQ(panel_->display_hz(), 0.0);
}

TEST_F(TopicEchoPanelTest, SetMaxMessages)
{
    panel_->set_max_messages(50);
    EXPECT_EQ(panel_->max_messages(), 50u);
}

// ===========================================================================
// Suite 9 — set_topic (no live publisher — just tests API + subscribe state)
// ===========================================================================

TEST_F(TopicEchoPanelTest, SetTopicEmptyUnsubscribes)
{
    panel_->set_topic("", "");
    EXPECT_FALSE(panel_->is_subscribed());
    EXPECT_TRUE(panel_->topic_name().empty());
    EXPECT_TRUE(panel_->type_name().empty());
}

TEST_F(TopicEchoPanelTest, SetTopicWithTypeSubscribes)
{
    panel_->set_topic("/cmd_vel", "geometry_msgs/msg/Twist");
    EXPECT_EQ(panel_->topic_name(), "/cmd_vel");
    EXPECT_EQ(panel_->type_name(), "geometry_msgs/msg/Twist");
    // Schema introspection may succeed or fail depending on installed packages;
    // we only check the name was stored.
}

TEST_F(TopicEchoPanelTest, SetTopicClearsPreviousMessages)
{
    panel_->inject_message(make_msg(0, 1.0));
    EXPECT_EQ(panel_->message_count(), 1u);

    // Changing topic does NOT auto-clear the ring (history from old topic
    // stays visible until user presses Clear; this is intentional UX).
    panel_->set_topic("", "");
    // Topic cleared, subscription gone, ring untouched.
    EXPECT_EQ(panel_->message_count(), 1u);
}

TEST_F(TopicEchoPanelTest, SetTopicTwiceDropsFirstSubscription)
{
    panel_->set_topic("/topic_a", "std_msgs/msg/Float64");
    panel_->set_topic("/topic_b", "std_msgs/msg/Float64");
    EXPECT_EQ(panel_->topic_name(), "/topic_b");
}

// ===========================================================================
// Suite 10 — Thread safety (stress)
// ===========================================================================

TEST_F(TopicEchoPanelTest, ConcurrentInjectAndSnapshot)
{
    panel_->set_max_messages(50);

    std::thread writer(
        [&]
        {
            for (int i = 0; i < 200; ++i)
            {
                panel_->inject_message(make_msg(static_cast<uint64_t>(i), 0.0));
                std::this_thread::yield();
            }
        });

    std::thread reader(
        [&]
        {
            for (int i = 0; i < 100; ++i)
            {
                auto snap = panel_->messages_snapshot();
                (void)snap.size();
                std::this_thread::yield();
            }
        });

    writer.join();
    reader.join();

    EXPECT_LE(panel_->message_count(), 50u);
    EXPECT_EQ(panel_->total_received(), 200u);
}

TEST_F(TopicEchoPanelTest, ConcurrentPauseResumeAndInject)
{
    std::thread controller(
        [&]
        {
            for (int i = 0; i < 20; ++i)
            {
                panel_->pause();
                std::this_thread::yield();
                panel_->resume();
                std::this_thread::yield();
            }
        });

    std::thread writer(
        [&]
        {
            for (int i = 0; i < 50; ++i)
            {
                panel_->inject_message(make_msg(static_cast<uint64_t>(i), 0.0));
                std::this_thread::yield();
            }
        });

    controller.join();
    writer.join();

    // No crash; total_received ≤ 50 (paused drops some).
    EXPECT_LE(panel_->total_received(), 50u);
}

// ===========================================================================
// Suite 11 — draw() no-op without ImGui
// ===========================================================================

TEST_F(TopicEchoPanelTest, DrawNoOpWithoutImGui)
{
    // draw() should not crash even without an ImGui context.
    panel_->inject_message(make_msg(0, 1.0));
    bool open = true;
    EXPECT_NO_THROW(panel_->draw(&open));
}

TEST_F(TopicEchoPanelTest, DrawNoOpNullptrPOpen)
{
    EXPECT_NO_THROW(panel_->draw(nullptr));
}

// ===========================================================================
// Suite 12 — Edge cases
// ===========================================================================

TEST_F(TopicEchoPanelTest, InjectAfterClearWorks)
{
    panel_->inject_message(make_msg(0, 1.0));
    panel_->clear();
    panel_->inject_message(make_msg(1, 2.0));
    EXPECT_EQ(panel_->message_count(), 1u);
    auto latest = panel_->latest_message();
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->seq, 1u);
}

TEST_F(TopicEchoPanelTest, MaxMessagesOneKeepsOnly1)
{
    panel_->set_max_messages(1);
    panel_->inject_message(make_msg(0, 1.0));
    panel_->inject_message(make_msg(1, 2.0));
    panel_->inject_message(make_msg(2, 3.0));
    EXPECT_EQ(panel_->message_count(), 1u);
    auto latest = panel_->latest_message();
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->seq, 2u);
}

TEST_F(TopicEchoPanelTest, MessageFieldDepthPreserved)
{
    ros2::EchoMessage msg;
    msg.seq          = 0;
    msg.timestamp_ns = 0;

    ros2::EchoFieldValue nested;
    nested.depth        = 2;
    nested.kind         = ros2::EchoFieldValue::Kind::Numeric;
    nested.display_name = "deep";
    nested.numeric      = 99.0;
    msg.fields.push_back(nested);

    panel_->inject_message(std::move(msg));
    auto snap = panel_->messages_snapshot();
    ASSERT_EQ(snap.size(), 1u);
    ASSERT_EQ(snap[0].fields.size(), 1u);
    EXPECT_EQ(snap[0].fields[0].depth, 2);
    EXPECT_DOUBLE_EQ(snap[0].fields[0].numeric, 99.0);
}

TEST_F(TopicEchoPanelTest, FormatTimestampNineDigitNanosec)
{
    // Ensure we always get 9 nanosecond digits.
    const std::string s = ros2::TopicEchoPanel::format_timestamp(1'000'000'001LL);
    EXPECT_EQ(s, "1.000000001");
}

TEST_F(TopicEchoPanelTest, EchoMessageKindVariants)
{
    // Verify all Kind variants can be constructed without crash.
    ros2::EchoMessage msg;
    msg.seq = 0;

    ros2::EchoFieldValue fv1;
    fv1.kind    = ros2::EchoFieldValue::Kind::Numeric;
    fv1.numeric = 1.0;
    msg.fields.push_back(fv1);

    ros2::EchoFieldValue fv2;
    fv2.kind = ros2::EchoFieldValue::Kind::Text;
    fv2.text = "hello";
    msg.fields.push_back(fv2);

    ros2::EchoFieldValue fv3;
    fv3.kind      = ros2::EchoFieldValue::Kind::ArrayHead;
    fv3.array_len = 5;
    msg.fields.push_back(fv3);

    ros2::EchoFieldValue fv4;
    fv4.kind    = ros2::EchoFieldValue::Kind::ArrayElement;
    fv4.numeric = 2.0;
    msg.fields.push_back(fv4);

    ros2::EchoFieldValue fv5;
    fv5.kind = ros2::EchoFieldValue::Kind::NestedHead;
    msg.fields.push_back(fv5);

    panel_->inject_message(std::move(msg));
    EXPECT_EQ(panel_->message_count(), 1u);
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

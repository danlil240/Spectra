// Phase 4 path + pose adapter/display tests.

#include <gtest/gtest.h>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/path.hpp>

#include "display/path_display.hpp"
#include "display/pose_display.hpp"
#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
nav_msgs::msg::Path make_path()
{
    nav_msgs::msg::Path msg;
    msg.header.frame_id = "map";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 700;
    msg.poses.resize(3);
    msg.poses[0].pose.position.x = 0.0;
    msg.poses[1].pose.position.x = 1.0;
    msg.poses[2].pose.position.x = 3.0;
    return msg;
}

geometry_msgs::msg::PoseStamped make_pose()
{
    geometry_msgs::msg::PoseStamped msg;
    msg.header.frame_id = "base_link";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 900;
    msg.pose.position.x = 1.5;
    msg.pose.position.y = -0.5;
    msg.pose.orientation.w = 1.0;
    return msg;
}

TransformStamp make_tf(const std::string& parent,
                       const std::string& child,
                       double tx,
                       uint64_t recv_ns)
{
    TransformStamp stamp;
    stamp.parent_frame = parent;
    stamp.child_frame = child;
    stamp.tx = tx;
    stamp.qw = 1.0;
    stamp.recv_ns = recv_ns;
    return stamp;
}
}   // namespace

TEST(PathAdapter, ComputesLengthAndBounds)
{
    const auto frame = adapt_path_message(make_path(), "/plan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->pose_count, 3u);
    ASSERT_EQ(frame->points.size(), 3u);
    EXPECT_DOUBLE_EQ(frame->start_point.x, 0.0);
    EXPECT_DOUBLE_EQ(frame->end_point.x, 3.0);
    EXPECT_DOUBLE_EQ(frame->path_length_m, 3.0);
    EXPECT_NEAR(frame->centroid.x, 4.0 / 3.0, 1e-6);
}

TEST(PathDisplay, ResolvesPathCentroidIntoFixedFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_tf("world", "map", 10.0, 700));

    PathDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;
    display.on_enable(context);

    auto frame = adapt_path_message(make_path(), "/plan");
    ASSERT_TRUE(frame.has_value());
    display.ingest_path_frame(*frame);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_EQ(scene.entities().front().type, "path");
    ASSERT_TRUE(scene.entities().front().polyline.has_value());
    EXPECT_EQ(scene.entities().front().polyline->points.size(), 3u);
    EXPECT_NEAR(scene.entities().front().polyline->points.front().x, -(4.0 / 3.0), 1e-6);
    EXPECT_NEAR(scene.entities().front().transform.translation.x, 10.0 + (4.0 / 3.0), 1e-6);
}

TEST(PoseDisplay, ComposesPoseWithFixedFrameTransform)
{
    TfBuffer buffer;
    buffer.inject_transform(make_tf("world", "base_link", 4.0, 900));

    PoseDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;
    display.on_enable(context);

    auto frame = adapt_pose_stamped_message(make_pose(), "/pose");
    ASSERT_TRUE(frame.has_value());
    display.ingest_pose_frame(*frame);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_EQ(scene.entities().front().type, "pose");
    ASSERT_TRUE(scene.entities().front().arrow.has_value());
    EXPECT_NEAR(scene.entities().front().arrow->shaft_length, 0.8, 1e-6);
    EXPECT_NEAR(scene.entities().front().transform.translation.x, 5.5, 1e-6);
    EXPECT_NEAR(scene.entities().front().transform.translation.y, -0.5, 1e-6);
}

// --- Phase 6 expanded coverage ---

TEST(PathAdapter, EmptyPathReturnsNullopt)
{
    nav_msgs::msg::Path msg;
    msg.header.frame_id = "map";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.poses.clear();

    const auto frame = adapt_path_message(msg, "/plan");
    EXPECT_FALSE(frame.has_value());
}

TEST(PathAdapter, SinglePosePathHasZeroLength)
{
    nav_msgs::msg::Path msg;
    msg.header.frame_id = "map";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.poses.resize(1);
    msg.poses[0].pose.position.x = 5.0;
    msg.poses[0].pose.position.y = 3.0;

    const auto frame = adapt_path_message(msg, "/plan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->pose_count, 1u);
    EXPECT_DOUBLE_EQ(frame->path_length_m, 0.0);
    EXPECT_DOUBLE_EQ(frame->start_point.x, 5.0);
    EXPECT_DOUBLE_EQ(frame->end_point.x, 5.0);
}

TEST(PathDisplay, ConfigBlobRoundTrip)
{
    PathDisplay display;
    display.deserialize_config_blob(
        "topic=/global_plan;line_width=3.00;alpha=0.80;pose_arrows=1;use_message_stamp=0");

    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("topic=/global_plan"), std::string::npos);
    EXPECT_NE(blob.find("line_width=3.00"), std::string::npos);
    EXPECT_NE(blob.find("alpha=0.80"), std::string::npos);
    EXPECT_NE(blob.find("pose_arrows=1"), std::string::npos);
    EXPECT_NE(blob.find("use_message_stamp=0"), std::string::npos);
}

TEST(PathDisplay, ConfigBlobEmptyNoOp)
{
    PathDisplay display;
    display.deserialize_config_blob("");
    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("line_width=2.00"), std::string::npos);
}

TEST(PathDisplay, LatestFrameReturnsIngested)
{
    PathDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    EXPECT_FALSE(display.latest_frame().has_value());

    auto frame = adapt_path_message(make_path(), "/plan");
    ASSERT_TRUE(frame.has_value());
    display.ingest_path_frame(*frame);

    ASSERT_TRUE(display.latest_frame().has_value());
    EXPECT_EQ(display.latest_frame()->pose_count, 3u);
}

TEST(PoseDisplay, ConfigBlobRoundTrip)
{
    PoseDisplay display;
    display.deserialize_config_blob(
        "topic=/goal_pose;shaft_length=1.20;shaft_width=0.10;head_length=0.30;head_width=0.20;use_message_stamp=0");

    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("topic=/goal_pose"), std::string::npos);
    EXPECT_NE(blob.find("shaft_length=1.20"), std::string::npos);
    EXPECT_NE(blob.find("head_length=0.30"), std::string::npos);
    EXPECT_NE(blob.find("use_message_stamp=0"), std::string::npos);
}

TEST(PoseDisplay, ConfigBlobEmptyNoOp)
{
    PoseDisplay display;
    display.deserialize_config_blob("");
    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("shaft_length=0.80"), std::string::npos);
}

TEST(PoseDisplay, LatestFrameReturnsIngested)
{
    PoseDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    EXPECT_FALSE(display.latest_frame().has_value());

    auto frame = adapt_pose_stamped_message(make_pose(), "/pose");
    ASSERT_TRUE(frame.has_value());
    display.ingest_pose_frame(*frame);

    ASSERT_TRUE(display.latest_frame().has_value());
}

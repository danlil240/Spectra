// Phase 3 laser scan adapter + display tests.

#include <gtest/gtest.h>

#include <cmath>

#include <sensor_msgs/msg/laser_scan.hpp>

#include "display/laserscan_display.hpp"
#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
sensor_msgs::msg::LaserScan make_scan()
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 250;
    msg.angle_min = 0.0f;
    msg.angle_increment = static_cast<float>(M_PI_2);
    msg.range_min = 0.0f;
    msg.range_max = 100.0f;
    msg.ranges = {1.0f, 2.0f};
    msg.intensities = {5.0f, 9.0f};
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

TEST(LaserScanAdapter, ConvertsPolarRangesToBounds)
{
    const auto frame = adapt_laserscan_message(make_scan(), "/scan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 2u);
    ASSERT_EQ(frame->points.size(), 2u);
    EXPECT_NEAR(frame->centroid.x, 0.5, 1e-6);
    EXPECT_NEAR(frame->centroid.y, 1.0, 1e-6);
    EXPECT_FLOAT_EQ(frame->min_range, 1.0f);
    EXPECT_FLOAT_EQ(frame->max_range, 2.0f);
    EXPECT_TRUE(frame->has_intensity);
    EXPECT_FLOAT_EQ(frame->average_intensity, 7.0f);
    EXPECT_NEAR(frame->points[1].position.y, 2.0, 1e-6);
}

TEST(LaserScanDisplay, KeepsTrailAndResolvesFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_tf("world", "laser", 4.0, 250));

    LaserScanDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;
    display.on_enable(context);
    display.deserialize_config_blob(
        "topic=/scan;render_style=0;color_mode=1;trail_size=2;min_range=0.000;max_range=100.000;use_message_stamp=1");

    auto scan = adapt_laserscan_message(make_scan(), "/scan");
    ASSERT_TRUE(scan.has_value());
    display.ingest_scan_frame(*scan);
    display.ingest_scan_frame(*scan);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(display.scan_count(), 2u);
    ASSERT_EQ(scene.entity_count(), 2u);
    EXPECT_EQ(scene.entities().front().type, "laserscan");
    EXPECT_NEAR(scene.entities().front().transform.translation.x, 4.5, 1e-6);
    ASSERT_TRUE(scene.entities().front().point_set.has_value());
    ASSERT_EQ(scene.entities().front().point_set->points.size(), 2u);
    EXPECT_NEAR(scene.entities().front().point_set->points.front().position.x, 0.5, 1e-6);
}

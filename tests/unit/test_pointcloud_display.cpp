// Phase 3 point cloud adapter + display tests.

#include <gtest/gtest.h>

#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/msg/point_field.hpp>

#include "display/pointcloud_display.hpp"
#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
sensor_msgs::msg::PointCloud2 make_cloud_xyz32()
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.height = 1;
    msg.width = 3;
    msg.point_step = 12;
    msg.row_step = msg.point_step * msg.width;
    msg.is_bigendian = false;
    msg.fields.resize(3);
    msg.fields[0].name = "x";
    msg.fields[0].offset = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count = 1;
    msg.fields[1].name = "y";
    msg.fields[1].offset = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count = 1;
    msg.fields[2].name = "z";
    msg.fields[2].offset = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count = 1;
    msg.data.resize(msg.row_step);

    const float points[9] = {
        1.0f, 0.0f, 0.0f,
        3.0f, 0.0f, 0.0f,
        5.0f, 0.0f, 0.0f,
    };
    std::memcpy(msg.data.data(), points, sizeof(points));
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

TEST(PointCloudAdapter, ComputesCentroidAndDecimation)
{
    const auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 2);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->original_point_count, 3u);
    EXPECT_EQ(frame->point_count, 2u);
    EXPECT_DOUBLE_EQ(frame->centroid.x, 3.0);
    EXPECT_DOUBLE_EQ(frame->min_bounds.x, 1.0);
    EXPECT_DOUBLE_EQ(frame->max_bounds.x, 5.0);
}

TEST(PointCloudDisplay, ResolvesIntoFixedFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_tf("world", "lidar", 10.0, 100));

    PointCloudDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;
    display.on_enable(context);

    auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 8);
    ASSERT_TRUE(frame.has_value());
    display.ingest_pointcloud_frame(*frame);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const auto& entity = scene.entities().front();
    EXPECT_EQ(entity.type, "pointcloud");
    EXPECT_DOUBLE_EQ(entity.transform.translation.x, 13.0);
    EXPECT_EQ(entity.frame_id, "lidar");
}

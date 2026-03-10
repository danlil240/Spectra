// Phase 3 point cloud adapter + display tests.

#include <gtest/gtest.h>

#include <cstring>

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
    msg.header.frame_id      = "lidar";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 100;
    msg.height               = 1;
    msg.width                = 3;
    msg.point_step           = 12;
    msg.row_step             = msg.point_step * msg.width;
    msg.is_bigendian         = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(msg.row_step);

    const float points[9] = {
        1.0f,
        0.0f,
        0.0f,
        3.0f,
        0.0f,
        0.0f,
        5.0f,
        0.0f,
        0.0f,
    };
    std::memcpy(msg.data.data(), points, sizeof(points));
    return msg;
}

sensor_msgs::msg::PointCloud2 make_cloud_xyz_rgb_intensity()
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id      = "lidar";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 200;
    msg.height               = 1;
    msg.width                = 2;
    msg.point_step           = 20;
    msg.row_step             = msg.point_step * msg.width;
    msg.is_bigendian         = false;
    msg.fields.resize(5);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.fields[3].name     = "intensity";
    msg.fields[3].offset   = 12;
    msg.fields[3].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[3].count    = 1;
    msg.fields[4].name     = "rgb";
    msg.fields[4].offset   = 16;
    msg.fields[4].datatype = sensor_msgs::msg::PointField::UINT32;
    msg.fields[4].count    = 1;
    msg.data.resize(msg.row_step);

    const float    xyz_i_0[4] = {1.0f, 2.0f, 3.0f, 10.0f};
    const float    xyz_i_1[4] = {4.0f, 5.0f, 6.0f, 20.0f};
    const uint32_t rgb_0      = 0x00112233u;
    const uint32_t rgb_1      = 0x00ABCDEFu;
    std::memcpy(msg.data.data() + 0, xyz_i_0, sizeof(xyz_i_0));
    std::memcpy(msg.data.data() + 16, &rgb_0, sizeof(rgb_0));
    std::memcpy(msg.data.data() + 20, xyz_i_1, sizeof(xyz_i_1));
    std::memcpy(msg.data.data() + 36, &rgb_1, sizeof(rgb_1));
    return msg;
}

TransformStamp make_tf(const std::string& parent,
                       const std::string& child,
                       double             tx,
                       uint64_t           recv_ns)
{
    TransformStamp stamp;
    stamp.parent_frame = parent;
    stamp.child_frame  = child;
    stamp.tx           = tx;
    stamp.qw           = 1.0;
    stamp.recv_ns      = recv_ns;
    return stamp;
}
}   // namespace

TEST(PointCloudAdapter, ComputesCentroidAndDecimation)
{
    const auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 2);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->original_point_count, 3u);
    EXPECT_EQ(frame->point_count, 2u);
    ASSERT_EQ(frame->points.size(), 2u);
    EXPECT_DOUBLE_EQ(frame->centroid.x, 3.0);
    EXPECT_DOUBLE_EQ(frame->min_bounds.x, 1.0);
    EXPECT_DOUBLE_EQ(frame->max_bounds.x, 5.0);
    EXPECT_DOUBLE_EQ(frame->points.front().position.x, 1.0);
    EXPECT_DOUBLE_EQ(frame->points.back().position.x, 5.0);
}

TEST(PointCloudAdapter, ExtractsRgbAndIntensityFields)
{
    const auto frame = adapt_pointcloud_message(make_cloud_xyz_rgb_intensity(), "/points", 16);
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->points.size(), 2u);
    EXPECT_TRUE(frame->has_rgb);
    EXPECT_TRUE(frame->has_intensity);
    EXPECT_FLOAT_EQ(frame->min_intensity, 10.0f);
    EXPECT_FLOAT_EQ(frame->max_intensity, 20.0f);
    EXPECT_TRUE(frame->points.front().has_rgb);
    EXPECT_TRUE(frame->points.front().has_intensity);
    EXPECT_EQ(frame->points.front().rgba, 0xFF332211u);
    EXPECT_FLOAT_EQ(frame->points.front().intensity, 10.0f);
}

TEST(PointCloudDisplay, ResolvesIntoFixedFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_tf("world", "lidar", 10.0, 100));

    PointCloudDisplay display;
    DisplayContext    context;
    context.fixed_frame = "world";
    context.tf_buffer   = &buffer;
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
    ASSERT_TRUE(entity.point_set.has_value());
    ASSERT_EQ(entity.point_set->points.size(), 3u);
    EXPECT_NEAR(entity.point_set->points.front().position.x, -2.0, 1e-6);
    EXPECT_FLOAT_EQ(entity.point_set->point_size, 3.0f);
}

// --- Phase 6 expanded coverage ---

TEST(PointCloudAdapter, EmptyCloudReturnsNullopt)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id      = "lidar";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 100;
    msg.height               = 1;
    msg.width                = 0;
    msg.point_step           = 12;
    msg.row_step             = 0;
    msg.is_bigendian         = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;

    const auto frame = adapt_pointcloud_message(msg, "/points", 100);
    EXPECT_FALSE(frame.has_value());
}

TEST(PointCloudAdapter, NanPointsHandledGracefully)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id      = "lidar";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 100;
    msg.height               = 1;
    msg.width                = 2;
    msg.point_step           = 12;
    msg.row_step             = msg.point_step * msg.width;
    msg.is_bigendian         = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(msg.row_step);

    const float nan       = std::numeric_limits<float>::quiet_NaN();
    const float points[6] = {nan, nan, nan, 1.0f, 2.0f, 3.0f};
    std::memcpy(msg.data.data(), points, sizeof(points));

    const auto frame = adapt_pointcloud_message(msg, "/points", 100);
    ASSERT_TRUE(frame.has_value());
    // Implementation may skip NaN or include them — just verify no crash
    EXPECT_GE(frame->original_point_count, 1u);
}

TEST(PointCloudAdapter, NoDecimationWhenBelowLimit)
{
    const auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 100);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 3u);
    EXPECT_EQ(frame->original_point_count, 3u);
    EXPECT_EQ(frame->points.size(), 3u);
}

TEST(PointCloudDisplay, ConfigBlobRoundTrip)
{
    PointCloudDisplay display;
    display.deserialize_config_blob(
        "topic=/lidar;color_mode=2;point_size=5.00;max_points=50000;use_message_stamp=0");

    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("topic=/lidar"), std::string::npos);
    EXPECT_NE(blob.find("color_mode=2"), std::string::npos);
    EXPECT_NE(blob.find("point_size=5.00"), std::string::npos);
    EXPECT_NE(blob.find("max_points=50000"), std::string::npos);
    EXPECT_NE(blob.find("use_message_stamp=0"), std::string::npos);
}

TEST(PointCloudDisplay, ConfigBlobEmptyNoOp)
{
    PointCloudDisplay display;
    display.deserialize_config_blob("");
    // Should not crash; defaults preserved
    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("color_mode=0"), std::string::npos);
}

TEST(PointCloudDisplay, LatestFrameReturnsIngested)
{
    PointCloudDisplay display;
    DisplayContext    context;
    context.fixed_frame = "world";
    display.on_enable(context);

    EXPECT_FALSE(display.latest_frame().has_value());

    auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 8);
    ASSERT_TRUE(frame.has_value());
    display.ingest_pointcloud_frame(*frame);

    ASSERT_TRUE(display.latest_frame().has_value());
    EXPECT_EQ(display.latest_frame()->point_count, 3u);
}

// --- Phase 6 expanded adapter coverage ---

TEST(PointCloudAdapter, Float64FieldsDecoded)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id      = "lidar";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 300;
    msg.height               = 1;
    msg.width                = 1;
    msg.point_step           = 24;
    msg.row_step             = msg.point_step;
    msg.is_bigendian         = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT64;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 8;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT64;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 16;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT64;
    msg.fields[2].count    = 1;
    msg.data.resize(24);

    const double vals[3] = {10.0, 20.0, 30.0};
    std::memcpy(msg.data.data(), vals, sizeof(vals));

    const auto frame = adapt_pointcloud_message(msg, "/points", 10);
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->point_count, 1u);
    EXPECT_DOUBLE_EQ(frame->points[0].position.x, 10.0);
    EXPECT_DOUBLE_EQ(frame->points[0].position.y, 20.0);
    EXPECT_DOUBLE_EQ(frame->points[0].position.z, 30.0);
}

TEST(PointCloudAdapter, MissingXyzFieldReturnsNullopt)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 1;
    msg.point_step      = 4;
    msg.row_step        = 4;
    msg.is_bigendian    = false;
    msg.fields.resize(1);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.data.resize(4);
    // Missing y and z fields
    const auto frame = adapt_pointcloud_message(msg, "/points", 10);
    EXPECT_FALSE(frame.has_value());
}

TEST(PointCloudAdapter, ZeroPointStepReturnsNullopt)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 1;
    msg.point_step      = 0;
    msg.row_step        = 0;
    msg.is_bigendian    = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(12);

    const auto frame = adapt_pointcloud_message(msg, "/points", 10);
    EXPECT_FALSE(frame.has_value());
}

TEST(PointCloudAdapter, AllNanPointsReturnsNullopt)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 2;
    msg.point_step      = 12;
    msg.row_step        = msg.point_step * msg.width;
    msg.is_bigendian    = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(msg.row_step);

    const float nan       = std::numeric_limits<float>::quiet_NaN();
    const float points[6] = {nan, nan, nan, nan, nan, nan};
    std::memcpy(msg.data.data(), points, sizeof(points));

    const auto frame = adapt_pointcloud_message(msg, "/points", 100);
    EXPECT_FALSE(frame.has_value());
}

TEST(PointCloudAdapter, SinglePointHasCorrectBounds)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 1;
    msg.point_step      = 12;
    msg.row_step        = 12;
    msg.is_bigendian    = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(12);

    const float p[3] = {7.0f, -3.0f, 2.0f};
    std::memcpy(msg.data.data(), p, sizeof(p));

    const auto frame = adapt_pointcloud_message(msg, "/points", 100);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 1u);
    EXPECT_DOUBLE_EQ(frame->min_bounds.x, 7.0);
    EXPECT_DOUBLE_EQ(frame->max_bounds.x, 7.0);
    EXPECT_DOUBLE_EQ(frame->centroid.x, 7.0);
}

TEST(PointCloudAdapter, RgbaFieldExtracted)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 1;
    msg.point_step      = 16;
    msg.row_step        = 16;
    msg.is_bigendian    = false;
    msg.fields.resize(4);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.fields[3].name     = "rgba";
    msg.fields[3].offset   = 12;
    msg.fields[3].datatype = sensor_msgs::msg::PointField::UINT32;
    msg.fields[3].count    = 1;
    msg.data.resize(16);

    const float    xyz[3]      = {1.0f, 2.0f, 3.0f};
    const uint32_t rgba_packed = 0x80112233u;   // ABGR in memory
    std::memcpy(msg.data.data(), xyz, sizeof(xyz));
    std::memcpy(msg.data.data() + 12, &rgba_packed, sizeof(rgba_packed));

    const auto frame = adapt_pointcloud_message(msg, "/points", 10);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->has_rgb);
    EXPECT_TRUE(frame->points.front().has_rgb);
}

TEST(PointCloudAdapter, FieldOffsetBeyondPointStepRejected)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 1;
    msg.point_step      = 8;   // Too small for 3 floats
    msg.row_step        = 8;
    msg.is_bigendian    = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;   // Offset 8 + 4 > point_step 8
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(8);

    const auto frame = adapt_pointcloud_message(msg, "/points", 10);
    EXPECT_FALSE(frame.has_value());
}

TEST(PointCloudAdapter, LargeCloudDecimation)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 1;
    msg.width           = 10'000;
    msg.point_step      = 12;
    msg.row_step        = msg.point_step * msg.width;
    msg.is_bigendian    = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(msg.row_step, 0);

    for (uint32_t i = 0; i < msg.width; ++i)
    {
        float xyz[3] = {static_cast<float>(i), 0.0f, 0.0f};
        std::memcpy(msg.data.data() + i * 12, xyz, sizeof(xyz));
    }

    const auto frame = adapt_pointcloud_message(msg, "/points", 1000);
    ASSERT_TRUE(frame.has_value());
    EXPECT_LE(frame->point_count, 1001u);
    EXPECT_EQ(frame->original_point_count, 10'000u);
}

TEST(PointCloudAdapter, IntensityRangeCorrect)
{
    const auto frame = adapt_pointcloud_message(make_cloud_xyz_rgb_intensity(), "/points", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->has_intensity);
    EXPECT_FLOAT_EQ(frame->min_intensity, 10.0f);
    EXPECT_FLOAT_EQ(frame->max_intensity, 20.0f);
}

TEST(PointCloudAdapter, NoIntensityFieldMeansNoIntensity)
{
    const auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 100);
    ASSERT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->has_intensity);
    EXPECT_FALSE(frame->has_rgb);
}

TEST(PointCloudAdapter, MultiRowCloudParsed)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.height          = 2;
    msg.width           = 2;
    msg.point_step      = 12;
    msg.row_step        = msg.point_step * msg.width;
    msg.is_bigendian    = false;
    msg.fields.resize(3);
    msg.fields[0].name     = "x";
    msg.fields[0].offset   = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count    = 1;
    msg.fields[1].name     = "y";
    msg.fields[1].offset   = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count    = 1;
    msg.fields[2].name     = "z";
    msg.fields[2].offset   = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count    = 1;
    msg.data.resize(msg.row_step * msg.height, 0);

    float pts[12] = {1, 0, 0, 2, 0, 0, 3, 0, 0, 4, 0, 0};
    std::memcpy(msg.data.data(), pts, sizeof(pts));

    const auto frame = adapt_pointcloud_message(msg, "/points", 100);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->original_point_count, 4u);
    EXPECT_EQ(frame->point_count, 4u);
}

TEST(PointCloudAdapter, StampConversion)
{
    sensor_msgs::msg::PointCloud2 msg = make_cloud_xyz32();
    msg.header.stamp.sec              = 5;
    msg.header.stamp.nanosec          = 123;

    const auto frame = adapt_pointcloud_message(msg, "/points", 100);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->stamp_ns, 5'000'000'123ULL);
}

TEST(PointCloudAdapter, FrameIdPreserved)
{
    auto msg            = make_cloud_xyz32();
    msg.header.frame_id = "custom_frame";
    const auto frame    = adapt_pointcloud_message(msg, "/topic", 100);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->frame_id, "custom_frame");
    EXPECT_EQ(frame->topic, "/topic");
}

TEST(PointCloudDisplay, DisabledDisplayDoesNotSubmit)
{
    PointCloudDisplay display;
    DisplayContext    context;
    context.fixed_frame = "world";
    display.on_enable(context);
    display.set_enabled(false);

    auto frame = adapt_pointcloud_message(make_cloud_xyz32(), "/points", 8);
    ASSERT_TRUE(frame.has_value());
    display.ingest_pointcloud_frame(*frame);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 0u);
}

TEST(PointCloudDisplay, TopicSetViaConfig)
{
    PointCloudDisplay display;
    display.deserialize_config_blob(
        "topic=/"
        "velodyne_points;color_mode=1;point_size=4.00;max_points=200000;use_message_stamp=1");
    EXPECT_EQ(display.topic(), "/velodyne_points");
}

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
    msg.header.frame_id      = "laser";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 250;
    msg.angle_min            = 0.0f;
    msg.angle_increment      = static_cast<float>(M_PI_2);
    msg.range_min            = 0.0f;
    msg.range_max            = 100.0f;
    msg.ranges               = {1.0f, 2.0f};
    msg.intensities          = {5.0f, 9.0f};
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
    DisplayContext   context;
    context.fixed_frame = "world";
    context.tf_buffer   = &buffer;
    display.on_enable(context);
    display.deserialize_config_blob("topic=/"
                                    "scan;render_style=0;color_mode=1;trail_size=2;min_range=0.000;"
                                    "max_range=100.000;use_message_stamp=1");

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

// --- Phase 6 expanded coverage ---

TEST(LaserScanAdapter, EmptyScanReturnsNullopt)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id      = "laser";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 100;
    msg.angle_min            = 0.0f;
    msg.angle_increment      = static_cast<float>(M_PI_2);
    msg.range_min            = 0.0f;
    msg.range_max            = 100.0f;
    // No ranges
    msg.ranges.clear();

    const auto frame = adapt_laserscan_message(msg, "/scan");
    EXPECT_FALSE(frame.has_value());
}

TEST(LaserScanAdapter, OutOfRangePointsFiltered)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id      = "laser";
    msg.header.stamp.sec     = 0;
    msg.header.stamp.nanosec = 100;
    msg.angle_min            = 0.0f;
    msg.angle_increment      = static_cast<float>(M_PI_2);
    msg.range_min            = 0.5f;
    msg.range_max            = 3.0f;
    // 0.1 is below range_min, 10.0 is above range_max
    msg.ranges = {0.1f, 1.0f, 10.0f, 2.0f};

    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    // Only ranges within [range_min, range_max] should be included
    EXPECT_LE(frame->point_count, 4u);
    // Valid points should have their ranges in bounds
    for (size_t i = 0; i < frame->points.size(); ++i)
    {
        const auto& p     = frame->points[i];
        const float range = std::sqrt(p.position.x * p.position.x + p.position.y * p.position.y);
        EXPECT_GE(range, msg.range_min - 1e-3f);
        EXPECT_LE(range, msg.range_max + 1e-3f);
    }
}

TEST(LaserScanDisplay, ConfigBlobRoundTrip)
{
    LaserScanDisplay display;
    display.deserialize_config_blob("topic=/"
                                    "laser_scan;render_style=1;color_mode=2;trail_size=5;min_range="
                                    "0.500;max_range=50.000;use_message_stamp=0");

    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("topic=/laser_scan"), std::string::npos);
    EXPECT_NE(blob.find("render_style=1"), std::string::npos);
    EXPECT_NE(blob.find("color_mode=2"), std::string::npos);
    EXPECT_NE(blob.find("trail_size=5"), std::string::npos);
    EXPECT_NE(blob.find("use_message_stamp=0"), std::string::npos);
}

TEST(LaserScanDisplay, ConfigBlobEmptyNoOp)
{
    LaserScanDisplay display;
    display.deserialize_config_blob("");
    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("render_style=0"), std::string::npos);
}

TEST(LaserScanDisplay, ScanCountStartsAtZero)
{
    LaserScanDisplay display;
    DisplayContext   context;
    context.fixed_frame = "world";
    display.on_enable(context);

    EXPECT_EQ(display.scan_count(), 0u);
}

// --- Phase 6 further expanded coverage ---

TEST(LaserScanAdapter, InfiniteRangeSkipped)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.angle_min       = 0.0f;
    msg.angle_increment = static_cast<float>(M_PI_2);
    msg.range_min       = 0.0f;
    msg.range_max       = 100.0f;
    msg.ranges          = {std::numeric_limits<float>::infinity(), 2.0f};

    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 1u);
}

TEST(LaserScanAdapter, NanRangeSkipped)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.angle_min       = 0.0f;
    msg.angle_increment = static_cast<float>(M_PI_2);
    msg.range_min       = 0.0f;
    msg.range_max       = 100.0f;
    msg.ranges          = {std::numeric_limits<float>::quiet_NaN(), 3.0f};

    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 1u);
}

TEST(LaserScanAdapter, CustomRangeFilter)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.angle_min       = 0.0f;
    msg.angle_increment = 0.1f;
    msg.range_min       = 0.0f;
    msg.range_max       = 50.0f;
    msg.ranges          = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};

    // Filter: only keep ranges between 1.0 and 5.0
    const auto frame = adapt_laserscan_message(msg, "/scan", 1.0f, 5.0f);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 3u);
    EXPECT_FLOAT_EQ(frame->min_range, 1.0f);
    EXPECT_FLOAT_EQ(frame->max_range, 5.0f);
}

TEST(LaserScanAdapter, NoIntensitiesStillWorks)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.angle_min       = 0.0f;
    msg.angle_increment = 0.1f;
    msg.range_min       = 0.0f;
    msg.range_max       = 50.0f;
    msg.ranges          = {1.0f, 2.0f};
    // No intensities array

    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->point_count, 2u);
    EXPECT_FALSE(frame->has_intensity);
}

TEST(LaserScanAdapter, StampConversion)
{
    sensor_msgs::msg::LaserScan msg = make_scan();
    msg.header.stamp.sec            = 3;
    msg.header.stamp.nanosec        = 456;

    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->stamp_ns, 3'000'000'456ULL);
}

TEST(LaserScanAdapter, FrameIdPreserved)
{
    auto msg            = make_scan();
    msg.header.frame_id = "custom_laser";
    const auto frame    = adapt_laserscan_message(msg, "/scan2");
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->frame_id, "custom_laser");
    EXPECT_EQ(frame->topic, "/scan2");
}

TEST(LaserScanAdapter, AverageRangeComputed)
{
    auto       msg   = make_scan();
    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    EXPECT_FLOAT_EQ(frame->average_range, 1.5f);
}

TEST(LaserScanAdapter, PolarToCartesianFirstPoint)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.angle_min       = 0.0f;   // angle = 0 -> cos=1, sin=0
    msg.angle_increment = 1.0f;
    msg.range_min       = 0.0f;
    msg.range_max       = 100.0f;
    msg.ranges          = {5.0f};

    const auto frame = adapt_laserscan_message(msg, "/scan");
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->points.size(), 1u);
    EXPECT_NEAR(frame->points[0].position.x, 5.0, 1e-5);
    EXPECT_NEAR(frame->points[0].position.y, 0.0, 1e-5);
}

TEST(LaserScanDisplay, DisabledDisplayDoesNotSubmit)
{
    LaserScanDisplay display;
    DisplayContext   context;
    context.fixed_frame = "world";
    display.on_enable(context);
    display.set_enabled(false);

    auto scan = adapt_laserscan_message(make_scan(), "/scan");
    ASSERT_TRUE(scan.has_value());
    display.ingest_scan_frame(*scan);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 0u);
}

TEST(LaserScanDisplay, TopicSetViaConfig)
{
    LaserScanDisplay display;
    display.deserialize_config_blob("topic=/"
                                    "lidar_scan;render_style=0;color_mode=0;trail_size=1;min_range="
                                    "0.000;max_range=100.000;use_message_stamp=1");
    EXPECT_EQ(display.topic(), "/lidar_scan");
}

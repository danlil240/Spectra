// Phase 4 image adapter + display tests.

#include <gtest/gtest.h>

#include <sensor_msgs/msg/image.hpp>

#include "display/image_display.hpp"
#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
sensor_msgs::msg::Image make_rgb_image()
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 500;
    msg.width = 2;
    msg.height = 1;
    msg.encoding = "rgb8";
    msg.step = 6;
    msg.data = {
        255, 0, 0,
        0, 255, 0,
    };
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

TEST(ImageAdapter, BuildsPreviewAndIntensityStats)
{
    const auto frame = adapt_image_message(make_rgb_image(), "/image", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->supported_encoding);
    EXPECT_EQ(frame->width, 2u);
    EXPECT_EQ(frame->height, 1u);
    EXPECT_EQ(frame->preview_width, 2u);
    EXPECT_EQ(frame->preview_height, 1u);
    ASSERT_EQ(frame->preview_rgba.size(), 8u);
    EXPECT_EQ(frame->preview_rgba[0], 255u);
    EXPECT_EQ(frame->preview_rgba[1], 0u);
    EXPECT_EQ(frame->preview_rgba[4], 0u);
    EXPECT_GT(frame->mean_intensity, 0.0);
}

TEST(ImageDisplay, BillboardEntityResolvesIntoFixedFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_tf("world", "camera", 2.0, 500));

    ImageDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;
    display.on_enable(context);
    display.deserialize_config_blob(
        "topic=/image;mode=1;panel_visible=1;preview_max_dim=32;use_message_stamp=1");

    auto frame = adapt_image_message(make_rgb_image(), "/image", 32);
    ASSERT_TRUE(frame.has_value());
    display.ingest_image_frame(*frame);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const auto& entity = scene.entities().front();
    EXPECT_EQ(entity.type, "image");
    EXPECT_DOUBLE_EQ(entity.transform.translation.x, 2.0);
    EXPECT_EQ(entity.frame_id, "camera");
    ASSERT_TRUE(entity.billboard.has_value());
    EXPECT_DOUBLE_EQ(entity.billboard->width, 2.0);
    EXPECT_DOUBLE_EQ(entity.billboard->height, 1.0);
}

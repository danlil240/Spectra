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

// --- Phase 6 expanded coverage ---

TEST(ImageAdapter, Bgr8EncodingSwizzle)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.width = 1;
    msg.height = 1;
    msg.encoding = "bgr8";
    msg.step = 3;
    msg.data = {100, 150, 200};   // B=100, G=150, R=200

    const auto frame = adapt_image_message(msg, "/image", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->supported_encoding);
    ASSERT_EQ(frame->preview_rgba.size(), 4u);
    // RGBA should be (R=200, G=150, B=100, A=255)
    EXPECT_EQ(frame->preview_rgba[0], 200u);
    EXPECT_EQ(frame->preview_rgba[1], 150u);
    EXPECT_EQ(frame->preview_rgba[2], 100u);
    EXPECT_EQ(frame->preview_rgba[3], 255u);
}

TEST(ImageAdapter, Mono8EncodingGrayscale)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.width = 2;
    msg.height = 1;
    msg.encoding = "mono8";
    msg.step = 2;
    msg.data = {0, 128};

    const auto frame = adapt_image_message(msg, "/image", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->supported_encoding);
    ASSERT_EQ(frame->preview_rgba.size(), 8u);
    // First pixel: gray 0 -> (0, 0, 0, 255)
    EXPECT_EQ(frame->preview_rgba[0], 0u);
    EXPECT_EQ(frame->preview_rgba[1], 0u);
    EXPECT_EQ(frame->preview_rgba[2], 0u);
    EXPECT_EQ(frame->preview_rgba[3], 255u);
    // Second pixel: gray 128 -> (128, 128, 128, 255)
    EXPECT_EQ(frame->preview_rgba[4], 128u);
    EXPECT_EQ(frame->preview_rgba[5], 128u);
    EXPECT_EQ(frame->preview_rgba[6], 128u);
}

TEST(ImageAdapter, Rgba8EncodingPassThrough)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.width = 1;
    msg.height = 1;
    msg.encoding = "rgba8";
    msg.step = 4;
    msg.data = {10, 20, 30, 128};

    const auto frame = adapt_image_message(msg, "/image", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->supported_encoding);
    ASSERT_EQ(frame->preview_rgba.size(), 4u);
    EXPECT_EQ(frame->preview_rgba[0], 10u);
    EXPECT_EQ(frame->preview_rgba[1], 20u);
    EXPECT_EQ(frame->preview_rgba[2], 30u);
    EXPECT_EQ(frame->preview_rgba[3], 128u);
}

TEST(ImageDisplay, ConfigBlobRoundTrip)
{
    ImageDisplay display;
    display.deserialize_config_blob(
        "topic=/cam/image;mode=2;panel_visible=0;preview_max_dim=64;use_message_stamp=0");

    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("topic=/cam/image"), std::string::npos);
    EXPECT_NE(blob.find("mode=2"), std::string::npos);
    EXPECT_NE(blob.find("panel_visible=0"), std::string::npos);
    EXPECT_NE(blob.find("preview_max_dim=64"), std::string::npos);
    EXPECT_NE(blob.find("use_message_stamp=0"), std::string::npos);
}

TEST(ImageDisplay, ConfigBlobEmptyNoOp)
{
    ImageDisplay display;
    display.deserialize_config_blob("");
    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("mode=0"), std::string::npos);
}

TEST(ImageDisplay, LatestFrameReturnsIngested)
{
    ImageDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    EXPECT_FALSE(display.latest_frame().has_value());

    auto frame = adapt_image_message(make_rgb_image(), "/image", 16);
    ASSERT_TRUE(frame.has_value());
    display.ingest_image_frame(*frame);

    ASSERT_TRUE(display.latest_frame().has_value());
    EXPECT_EQ(display.latest_frame()->width, 2u);
}

// --- Phase 6 further expanded coverage ---

TEST(ImageAdapter, Mono16EncodingDecoded)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.width = 2;
    msg.height = 1;
    msg.encoding = "16UC1";
    msg.step = 4;
    msg.is_bigendian = false;
    // 16-bit values: 0 and 65535 (max)
    msg.data = {0, 0, 0xFF, 0xFF};

    const auto frame = adapt_image_message(msg, "/depth", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->supported_encoding);
    ASSERT_EQ(frame->preview_rgba.size(), 8u);
    // first pixel: 0/257 = 0
    EXPECT_EQ(frame->preview_rgba[0], 0u);
    // second pixel: 65535/257 = 255
    EXPECT_EQ(frame->preview_rgba[4], 255u);
}

TEST(ImageAdapter, UnsupportedEncodingReturnsWarning)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.width = 2;
    msg.height = 1;
    msg.encoding = "bayer_rggb8";
    msg.step = 2;
    msg.data = {10, 20};

    const auto frame = adapt_image_message(msg, "/cam", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_FALSE(frame->supported_encoding);
    EXPECT_NE(frame->warning.find("Unsupported"), std::string::npos);
}

TEST(ImageAdapter, EmptyImageReturnsNullopt)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.width = 0;
    msg.height = 0;
    msg.encoding = "rgb8";
    msg.step = 0;

    const auto frame = adapt_image_message(msg, "/cam", 16);
    EXPECT_FALSE(frame.has_value());
}

TEST(ImageAdapter, ZeroWidthReturnsNullopt)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.width = 0;
    msg.height = 1;
    msg.encoding = "rgb8";
    msg.step = 0;
    msg.data = {};

    const auto frame = adapt_image_message(msg, "/cam", 16);
    EXPECT_FALSE(frame.has_value());
}

TEST(ImageAdapter, PreviewDownscalesLargeImage)
{
    sensor_msgs::msg::Image msg;
    msg.header.frame_id = "camera";
    msg.width = 640;
    msg.height = 480;
    msg.encoding = "rgb8";
    msg.step = 640 * 3;
    msg.data.resize(msg.step * msg.height, 128u);

    const auto frame = adapt_image_message(msg, "/cam", 48);
    ASSERT_TRUE(frame.has_value());
    EXPECT_LE(frame->preview_width, 48u);
    EXPECT_LE(frame->preview_height, 48u);
    EXPECT_GT(frame->preview_width, 0u);
    EXPECT_GT(frame->preview_height, 0u);
}

TEST(ImageAdapter, FullImageRetainedWhenRequested)
{
    const auto frame = adapt_image_message(make_rgb_image(), "/image", 16, true);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->full_rgba.size(), 2u * 1u * 4u);
    // First pixel R=255: rgba[0] = 255
    EXPECT_EQ(frame->full_rgba[0], 255u);
}

TEST(ImageAdapter, FullImageNotRetainedByDefault)
{
    const auto frame = adapt_image_message(make_rgb_image(), "/image", 16, false);
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->full_rgba.empty());
}

TEST(ImageAdapter, IntensityStatsComputed)
{
    const auto frame = adapt_image_message(make_rgb_image(), "/image", 16);
    ASSERT_TRUE(frame.has_value());
    // Red pixel: (255+0+0)/3 = 85, Green pixel: (0+255+0)/3 = 85
    EXPECT_NEAR(frame->min_intensity, 85.0, 0.5);
    EXPECT_NEAR(frame->max_intensity, 85.0, 0.5);
    EXPECT_NEAR(frame->mean_intensity, 85.0, 0.5);
}

TEST(ImageAdapter, StampConversion)
{
    auto msg = make_rgb_image();
    msg.header.stamp.sec = 7;
    msg.header.stamp.nanosec = 999;

    const auto frame = adapt_image_message(msg, "/cam", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->stamp_ns, 7'000'000'999ULL);
}

TEST(ImageAdapter, FrameIdPreserved)
{
    auto msg = make_rgb_image();
    msg.header.frame_id = "front_camera";
    const auto frame = adapt_image_message(msg, "/front", 16);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->frame_id, "front_camera");
    EXPECT_EQ(frame->topic, "/front");
}

TEST(ImageAdapter, IsColorFlagCorrect)
{
    auto msg = make_rgb_image();
    const auto frame_rgb = adapt_image_message(msg, "/cam", 16);
    ASSERT_TRUE(frame_rgb.has_value());
    EXPECT_TRUE(frame_rgb->is_color);

    msg.encoding = "mono8";
    msg.step = 2;
    msg.data = {128, 64};
    const auto frame_mono = adapt_image_message(msg, "/cam", 16);
    ASSERT_TRUE(frame_mono.has_value());
    EXPECT_FALSE(frame_mono->is_color);
}

TEST(ImageDisplay, DisabledDisplayDoesNotSubmit)
{
    ImageDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);
    display.set_enabled(false);

    auto frame = adapt_image_message(make_rgb_image(), "/image", 16);
    ASSERT_TRUE(frame.has_value());
    display.ingest_image_frame(*frame);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 0u);
}

TEST(ImageDisplay, TopicSetViaConfig)
{
    ImageDisplay display;
    display.deserialize_config_blob("topic=/webcam/image;mode=0;panel_visible=1;preview_max_dim=48;use_message_stamp=0");
    EXPECT_EQ(display.topic(), "/webcam/image");
}

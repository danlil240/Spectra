// Phase 2 display tests — TF and marker scene submission.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <visualization_msgs/msg/marker.hpp>

#include "display/marker_display.hpp"
#include "display/tf_display.hpp"
#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
TransformStamp make_ts(const std::string& parent,
                       const std::string& child,
                       double tx = 0.0,
                       double ty = 0.0,
                       double tz = 0.0,
                       bool is_static = false,
                       uint64_t recv_ns = 1'000)
{
    TransformStamp ts;
    ts.parent_frame = parent;
    ts.child_frame = child;
    ts.tx = tx;
    ts.ty = ty;
    ts.tz = tz;
    ts.qw = 1.0;
    ts.is_static = is_static;
    ts.recv_ns = recv_ns;
    return ts;
}
}   // namespace

TEST(TfDisplay, SubmitsTfFramesIntoScene)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "base_link", 1.0, 2.0, 3.0, false, 100));

    TfDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;

    display.on_enable(context);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 2u);
    bool found_base = false;
    for (const auto& entity : scene.entities())
    {
        if (entity.label == "base_link")
        {
            found_base = true;
            EXPECT_DOUBLE_EQ(entity.transform.translation.x, 1.0);
            EXPECT_DOUBLE_EQ(entity.transform.translation.y, 2.0);
            EXPECT_DOUBLE_EQ(entity.transform.translation.z, 3.0);
        }
    }
    EXPECT_TRUE(found_base);
}

TEST(MarkerDisplay, MarkerPoseIsResolvedIntoFixedFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "base_link", 1.0, 0.0, 0.0, false, 100));

    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer = &buffer;

    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/visualization_marker";
    marker.ns = "demo";
    marker.id = 7;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cube;
    marker.frame_id = "base_link";
    marker.stamp_ns = 100;
    marker.pose.translation = {2.0, 0.0, 0.0};
    marker.scale = {0.5, 0.5, 0.5};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const SceneEntity& entity = scene.entities()[0];
    EXPECT_EQ(entity.type, "marker");
    EXPECT_EQ(entity.frame_id, "base_link");
    EXPECT_DOUBLE_EQ(entity.transform.translation.x, 3.0);
}

TEST(MarkerDisplay, MarkerLifetimeExpires)
{
    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/visualization_marker";
    marker.ns = "demo";
    marker.id = 1;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Sphere;
    marker.frame_id = "world";
    marker.lifetime_ns = 1;

    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    display.on_update(0.016f);

    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, DeleteActionRemovesMarker)
{
    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/visualization_marker";
    marker.ns = "demo";
    marker.id = 3;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Arrow;
    marker.frame_id = "world";
    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 1u);

    marker.action = visualization_msgs::msg::Marker::DELETE;
    display.ingest_marker_data(marker);
    EXPECT_EQ(display.marker_count(), 0u);
}

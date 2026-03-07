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

TEST(MarkerDisplay, LineStripMarkerCreatesPolylineEntity)
{
    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/markers";
    marker.ns = "lines";
    marker.id = 1;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::LineStrip;
    marker.frame_id = "world";
    marker.scale = {0.01, 0.0, 0.0};
    marker.color = {0.0f, 1.0f, 0.0f, 1.0f};
    marker.points = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const SceneEntity& entity = scene.entities()[0];
    ASSERT_TRUE(entity.polyline.has_value());
    EXPECT_EQ(entity.polyline->points.size(), 3u);
    EXPECT_DOUBLE_EQ(entity.polyline->points[0].x, 0.0);
    EXPECT_DOUBLE_EQ(entity.polyline->points[1].x, 1.0);
    EXPECT_DOUBLE_EQ(entity.polyline->points[2].y, 1.0);
}

TEST(MarkerDisplay, LineListMarkerCreatesPolylineEntities)
{
    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/markers";
    marker.ns = "lines";
    marker.id = 2;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::LineList;
    marker.frame_id = "world";
    marker.scale = {0.02, 0.0, 0.0};
    marker.color = {1.0f, 0.0f, 0.0f, 1.0f};
    marker.points = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    // 4 points = 2 line segments = 2 entities
    ASSERT_EQ(scene.entity_count(), 2u);
    for (const auto& entity : scene.entities())
    {
        ASSERT_TRUE(entity.polyline.has_value());
        EXPECT_EQ(entity.polyline->points.size(), 2u);
    }
    // First segment: (0,0,0) -> (1,0,0)
    EXPECT_DOUBLE_EQ(scene.entities()[0].polyline->points[0].x, 0.0);
    EXPECT_DOUBLE_EQ(scene.entities()[0].polyline->points[1].x, 1.0);
    // Second segment: (0,1,0) -> (1,1,0)
    EXPECT_DOUBLE_EQ(scene.entities()[1].polyline->points[0].y, 1.0);
    EXPECT_DOUBLE_EQ(scene.entities()[1].polyline->points[1].y, 1.0);
}

TEST(MarkerDisplay, PointsMarkerCreatesPointSetEntity)
{
    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/markers";
    marker.ns = "pts";
    marker.id = 3;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Points;
    marker.frame_id = "world";
    marker.scale = {0.05, 0.05, 0.0};
    marker.color = {0.0f, 0.0f, 1.0f, 0.8f};
    marker.points = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const SceneEntity& entity = scene.entities()[0];
    ASSERT_TRUE(entity.point_set.has_value());
    EXPECT_EQ(entity.point_set->points.size(), 3u);
    EXPECT_TRUE(entity.point_set->transparent);
    EXPECT_DOUBLE_EQ(entity.point_set->points[0].position.x, 1.0);
    EXPECT_DOUBLE_EQ(entity.point_set->points[1].position.x, 4.0);
    EXPECT_DOUBLE_EQ(entity.point_set->points[2].position.x, 7.0);
}

TEST(MarkerDisplay, TextViewFacingMarkerSubmitsEntity)
{
    MarkerDisplay display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic = "/markers";
    marker.ns = "text";
    marker.id = 4;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::TextViewFacing;
    marker.frame_id = "world";
    marker.scale = {0.0, 0.0, 0.3};
    marker.text = "Hello";
    marker.color = {1.0f, 1.0f, 1.0f, 1.0f};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    const SceneEntity& entity = scene.entities()[0];
    EXPECT_EQ(entity.type, "marker");
    // Check that the text property is preserved
    bool has_text = false;
    for (const auto& prop : entity.properties)
    {
        if (prop.key == "text" && prop.value == "Hello")
            has_text = true;
    }
    EXPECT_TRUE(has_text);
}

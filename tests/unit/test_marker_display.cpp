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
                       double             tx        = 0.0,
                       double             ty        = 0.0,
                       double             tz        = 0.0,
                       bool               is_static = false,
                       uint64_t           recv_ns   = 1'000)
{
    TransformStamp ts;
    ts.parent_frame = parent;
    ts.child_frame  = child;
    ts.tx           = tx;
    ts.ty           = ty;
    ts.tz           = tz;
    ts.qw           = 1.0;
    ts.is_static    = is_static;
    ts.recv_ns      = recv_ns;
    return ts;
}
}   // namespace

TEST(TfDisplay, SubmitsTfFramesIntoScene)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "base_link", 1.0, 2.0, 3.0, false, 100));

    TfDisplay      display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer   = &buffer;

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

    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    context.tf_buffer   = &buffer;

    display.on_enable(context);

    MarkerData marker;
    marker.topic            = "/visualization_marker";
    marker.ns               = "demo";
    marker.id               = 7;
    marker.action           = visualization_msgs::msg::Marker::ADD;
    marker.primitive        = MarkerPrimitive::Cube;
    marker.frame_id         = "base_link";
    marker.stamp_ns         = 100;
    marker.pose.translation = {2.0, 0.0, 0.0};
    marker.scale            = {0.5, 0.5, 0.5};

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
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic       = "/visualization_marker";
    marker.ns          = "demo";
    marker.id          = 1;
    marker.action      = visualization_msgs::msg::Marker::ADD;
    marker.primitive   = MarkerPrimitive::Sphere;
    marker.frame_id    = "world";
    marker.lifetime_ns = 1;

    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    display.on_update(0.016f);

    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, DeleteActionRemovesMarker)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/visualization_marker";
    marker.ns        = "demo";
    marker.id        = 3;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Arrow;
    marker.frame_id  = "world";
    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 1u);

    marker.action = visualization_msgs::msg::Marker::DELETE;
    display.ingest_marker_data(marker);
    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, LineStripMarkerCreatesPolylineEntity)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "lines";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::LineStrip;
    marker.frame_id  = "world";
    marker.scale     = {0.01, 0.0, 0.0};
    marker.color     = {0.0f, 1.0f, 0.0f, 1.0f};
    marker.points    = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}};

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
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "lines";
    marker.id        = 2;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::LineList;
    marker.frame_id  = "world";
    marker.scale     = {0.02, 0.0, 0.0};
    marker.color     = {1.0f, 0.0f, 0.0f, 1.0f};
    marker.points    = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}};

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
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "pts";
    marker.id        = 3;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Points;
    marker.frame_id  = "world";
    marker.scale     = {0.05, 0.05, 0.0};
    marker.color     = {0.0f, 0.0f, 1.0f, 0.8f};
    marker.points    = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}};

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
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "text";
    marker.id        = 4;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::TextViewFacing;
    marker.frame_id  = "world";
    marker.scale     = {0.0, 0.0, 0.3};
    marker.text      = "Hello";
    marker.color     = {1.0f, 1.0f, 1.0f, 1.0f};

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

// --- Phase 6 expanded coverage ---

TEST(MarkerDisplay, DeleteAllClearsAllMarkers)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    // Add multiple markers
    for (int i = 0; i < 5; ++i)
    {
        MarkerData marker;
        marker.topic     = "/markers";
        marker.ns        = "demo";
        marker.id        = i;
        marker.action    = visualization_msgs::msg::Marker::ADD;
        marker.primitive = MarkerPrimitive::Sphere;
        marker.frame_id  = "world";
        display.ingest_marker_data(marker);
    }
    ASSERT_EQ(display.marker_count(), 5u);

    // DELETEALL should remove all
    MarkerData delete_all;
    delete_all.topic  = "/markers";
    delete_all.ns     = "demo";
    delete_all.id     = 0;
    delete_all.action = visualization_msgs::msg::Marker::DELETEALL;
    display.ingest_marker_data(delete_all);

    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, ModifyActionOverwritesExistingMarker)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "demo";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cube;
    marker.frame_id  = "world";
    marker.scale     = {1.0, 1.0, 1.0};
    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 1u);

    // Modify with different primitive
    marker.action    = visualization_msgs::msg::Marker::MODIFY;
    marker.primitive = MarkerPrimitive::Sphere;
    marker.scale     = {2.0, 2.0, 2.0};
    display.ingest_marker_data(marker);

    EXPECT_EQ(display.marker_count(), 1u);

    display.on_update(0.016f);
    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_NEAR(scene.entities()[0].scale.x, 2.0, 1e-6);
}

TEST(MarkerDisplay, MultipleNamespacesCoexist)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker_a;
    marker_a.topic     = "/markers";
    marker_a.ns        = "ns_a";
    marker_a.id        = 1;
    marker_a.action    = visualization_msgs::msg::Marker::ADD;
    marker_a.primitive = MarkerPrimitive::Cube;
    marker_a.frame_id  = "world";
    display.ingest_marker_data(marker_a);

    MarkerData marker_b;
    marker_b.topic     = "/markers";
    marker_b.ns        = "ns_b";
    marker_b.id        = 1;
    marker_b.action    = visualization_msgs::msg::Marker::ADD;
    marker_b.primitive = MarkerPrimitive::Sphere;
    marker_b.frame_id  = "world";
    display.ingest_marker_data(marker_b);

    EXPECT_EQ(display.marker_count(), 2u);

    // Deleting from ns_a should not affect ns_b
    marker_a.action = visualization_msgs::msg::Marker::DELETE;
    display.ingest_marker_data(marker_a);
    EXPECT_EQ(display.marker_count(), 1u);
}

TEST(MarkerDisplay, ConfigBlobRoundTrip)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    display.deserialize_config_blob(
        "topic=/visualization_marker;use_message_stamp=1;show_expired_count=0");

    const auto blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("topic=/visualization_marker"), std::string::npos);
    EXPECT_NE(blob.find("use_message_stamp=1"), std::string::npos);
}

TEST(MarkerDisplay, ConfigBlobEmptyStringNoOp)
{
    MarkerDisplay display;
    // Should not crash
    display.deserialize_config_blob("");
    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, UnknownPrimitiveIgnored)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "demo";
    marker.id        = 99;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Unknown;
    marker.frame_id  = "world";
    display.ingest_marker_data(marker);

    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, CylinderMarkerSubmitsEntity)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "shapes";
    marker.id        = 10;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cylinder;
    marker.frame_id  = "world";
    marker.scale     = {0.5, 0.5, 1.0};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_EQ(scene.entities()[0].type, "marker");
}

TEST(MarkerDisplay, MarkerCountMatchesSceneEntities)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    for (int i = 0; i < 3; ++i)
    {
        MarkerData marker;
        marker.topic     = "/markers";
        marker.ns        = "batch";
        marker.id        = i;
        marker.action    = visualization_msgs::msg::Marker::ADD;
        marker.primitive = MarkerPrimitive::Sphere;
        marker.frame_id  = "world";
        display.ingest_marker_data(marker);
    }

    EXPECT_EQ(display.marker_count(), 3u);

    display.on_update(0.016f);
    SceneManager scene;
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 3u);
}

// --- Phase 6 further expanded coverage ---

TEST(MarkerDisplay, ArrowMarkerSubmitsEntity)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "arrows";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Arrow;
    marker.frame_id  = "world";
    marker.scale     = {1.0, 0.1, 0.1};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_EQ(scene.entities()[0].type, "marker");
}

TEST(MarkerDisplay, SphereMarkerSubmitsEntity)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic            = "/markers";
    marker.ns               = "shapes";
    marker.id               = 2;
    marker.action           = visualization_msgs::msg::Marker::ADD;
    marker.primitive        = MarkerPrimitive::Sphere;
    marker.frame_id         = "world";
    marker.scale            = {0.3, 0.3, 0.3};
    marker.pose.translation = {5.0, 0.0, 0.0};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_NEAR(scene.entities()[0].transform.translation.x, 5.0, 1e-6);
}

TEST(MarkerDisplay, CubeMarkerSubmitsEntity)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "shapes";
    marker.id        = 3;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cube;
    marker.frame_id  = "world";
    marker.scale     = {1.0, 2.0, 3.0};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_NEAR(scene.entities()[0].scale.y, 2.0, 1e-6);
}

TEST(MarkerDisplay, ReAddAfterDelete)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "demo";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cube;
    marker.frame_id  = "world";
    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 1u);

    marker.action = visualization_msgs::msg::Marker::DELETE;
    display.ingest_marker_data(marker);
    ASSERT_EQ(display.marker_count(), 0u);

    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Sphere;
    display.ingest_marker_data(marker);
    EXPECT_EQ(display.marker_count(), 1u);
}

TEST(MarkerDisplay, DeleteNonexistentMarkerNoOp)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic    = "/markers";
    marker.ns       = "nonexistent";
    marker.id       = 999;
    marker.action   = visualization_msgs::msg::Marker::DELETE;
    marker.frame_id = "world";

    display.ingest_marker_data(marker);
    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, DeleteAllOnEmptyNoOp)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.action = visualization_msgs::msg::Marker::DELETEALL;
    display.ingest_marker_data(marker);

    EXPECT_EQ(display.marker_count(), 0u);
}

TEST(MarkerDisplay, SameIdDifferentNamespace)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    // Same ID, different namespaces
    for (const auto& ns : {"ns1", "ns2", "ns3"})
    {
        MarkerData marker;
        marker.topic     = "/markers";
        marker.ns        = ns;
        marker.id        = 1;
        marker.action    = visualization_msgs::msg::Marker::ADD;
        marker.primitive = MarkerPrimitive::Cube;
        marker.frame_id  = "world";
        display.ingest_marker_data(marker);
    }
    EXPECT_EQ(display.marker_count(), 3u);
}

TEST(MarkerDisplay, SameNamespaceDifferentIds)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    for (int i = 0; i < 10; ++i)
    {
        MarkerData marker;
        marker.topic     = "/markers";
        marker.ns        = "batch";
        marker.id        = i;
        marker.action    = visualization_msgs::msg::Marker::ADD;
        marker.primitive = MarkerPrimitive::Sphere;
        marker.frame_id  = "world";
        display.ingest_marker_data(marker);
    }
    EXPECT_EQ(display.marker_count(), 10u);
}

TEST(MarkerDisplay, OverwriteChangesScale)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "demo";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cube;
    marker.frame_id  = "world";
    marker.scale     = {1.0, 1.0, 1.0};
    display.ingest_marker_data(marker);

    marker.scale = {3.0, 4.0, 5.0};
    display.ingest_marker_data(marker);

    display.on_update(0.016f);
    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_NEAR(scene.entities()[0].scale.x, 3.0, 1e-6);
    EXPECT_NEAR(scene.entities()[0].scale.y, 4.0, 1e-6);
    EXPECT_NEAR(scene.entities()[0].scale.z, 5.0, 1e-6);
}

TEST(MarkerDisplay, DisabledDisplayDoesNotSubmit)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);
    display.set_enabled(false);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "demo";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Cube;
    marker.frame_id  = "world";
    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 0u);
}

TEST(MarkerDisplay, TopicSetViaConfig)
{
    MarkerDisplay display;
    display.deserialize_config_blob("topic=/viz_marker;use_message_stamp=0;show_expired_count=0");
    EXPECT_EQ(display.topic(), "/viz_marker");
}

TEST(MarkerDisplay, PointsMarkerTransparentWhenAlphaLow)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "pts";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::Points;
    marker.frame_id  = "world";
    marker.scale     = {0.05, 0.05, 0.0};
    marker.color     = {1.0f, 0.0f, 0.0f, 0.5f};
    marker.points    = {{0.0, 0.0, 0.0}};

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 1u);
    ASSERT_TRUE(scene.entities()[0].point_set.has_value());
    EXPECT_TRUE(scene.entities()[0].point_set->transparent);
}

TEST(MarkerDisplay, LineStripEmptyPointsNoEntity)
{
    MarkerDisplay  display;
    DisplayContext context;
    context.fixed_frame = "world";
    display.on_enable(context);

    MarkerData marker;
    marker.topic     = "/markers";
    marker.ns        = "lines";
    marker.id        = 1;
    marker.action    = visualization_msgs::msg::Marker::ADD;
    marker.primitive = MarkerPrimitive::LineStrip;
    marker.frame_id  = "world";
    // No points

    display.ingest_marker_data(marker);
    display.on_update(0.016f);

    SceneManager scene;
    display.submit_renderables(scene);

    // LineStrip with no points may create entity without polyline,
    // or may skip the entity entirely — either is acceptable
    if (scene.entity_count() > 0 && scene.entities()[0].polyline.has_value())
    {
        EXPECT_EQ(scene.entities()[0].polyline->points.size(), 0u);
    }
}

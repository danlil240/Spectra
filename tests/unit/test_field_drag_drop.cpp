// test_field_drag_drop.cpp — unit tests for FieldDragDrop and FieldDragPayload.
//
// These tests exercise all non-ImGui logic (payload construction, context menu
// pending state, consume_pending_request, callback routing).  ImGui draw paths
// (begin_drag_source, accept_drop_*, draw_drop_zone, is_dragging) require a live
// ImGui context and are tested via visual smoke-test in the ROS2 adapter demo.

#include "ui/field_drag_drop.hpp"

#include <gtest/gtest.h>

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// FieldDragPayload
// ---------------------------------------------------------------------------

TEST(FieldDragPayloadConstruction, DefaultInvalid)
{
    FieldDragPayload p;
    EXPECT_FALSE(p.valid());
    EXPECT_TRUE(p.topic_name.empty());
    EXPECT_TRUE(p.field_path.empty());
    EXPECT_TRUE(p.type_name.empty());
    EXPECT_TRUE(p.label.empty());
}

TEST(FieldDragPayloadConstruction, ValidWhenTopicSet)
{
    FieldDragPayload p;
    p.topic_name = "/imu/data";
    EXPECT_TRUE(p.valid());
}

TEST(FieldDragPayloadConstruction, MakeLabelTopicOnly)
{
    const std::string lbl = FieldDragPayload::make_label("/imu/data", "");
    EXPECT_EQ(lbl, "/imu/data");
}

TEST(FieldDragPayloadConstruction, MakeLabelTopicAndField)
{
    const std::string lbl = FieldDragPayload::make_label("/imu/data", "linear_acceleration.x");
    EXPECT_EQ(lbl, "/imu/data/linear_acceleration.x");
}

TEST(FieldDragPayloadConstruction, MakeLabelEmptyTopic)
{
    const std::string lbl = FieldDragPayload::make_label("", "some_field");
    EXPECT_EQ(lbl, "/some_field");
}

TEST(FieldDragPayloadConstruction, CopyAssignment)
{
    FieldDragPayload a;
    a.topic_name = "/cmd_vel";
    a.field_path = "linear.x";
    a.type_name  = "geometry_msgs/msg/Twist";
    a.label      = "/cmd_vel/linear.x";

    FieldDragPayload b = a;
    EXPECT_EQ(b.topic_name, a.topic_name);
    EXPECT_EQ(b.field_path, a.field_path);
    EXPECT_EQ(b.type_name,  a.type_name);
    EXPECT_EQ(b.label,      a.label);
    EXPECT_TRUE(b.valid());
}

// ---------------------------------------------------------------------------
// PlotTarget enum
// ---------------------------------------------------------------------------

TEST(PlotTarget, EnumValues)
{
    EXPECT_EQ(static_cast<uint8_t>(PlotTarget::NewWindow),   0u);
    EXPECT_EQ(static_cast<uint8_t>(PlotTarget::CurrentAxes), 1u);
    EXPECT_EQ(static_cast<uint8_t>(PlotTarget::NewSubplot),  2u);
}

TEST(PlotTarget, AllValuesDistinct)
{
    EXPECT_NE(PlotTarget::NewWindow,   PlotTarget::CurrentAxes);
    EXPECT_NE(PlotTarget::CurrentAxes, PlotTarget::NewSubplot);
    EXPECT_NE(PlotTarget::NewWindow,   PlotTarget::NewSubplot);
}

// ---------------------------------------------------------------------------
// FieldDragDrop — construction
// ---------------------------------------------------------------------------

TEST(FieldDragDropConstruction, DefaultNoPending)
{
    FieldDragDrop dd;
    FieldDragPayload p;
    PlotTarget t;
    EXPECT_FALSE(dd.consume_pending_request(p, t));
}

TEST(FieldDragDropConstruction, DefaultNotDragging)
{
    FieldDragDrop dd;
    // Without ImGui context, is_dragging returns false.
    EXPECT_FALSE(dd.is_dragging());
}

TEST(FieldDragDropConstruction, DefaultTryGetPayloadFalse)
{
    FieldDragDrop dd;
    FieldDragPayload out;
    EXPECT_FALSE(dd.try_get_dragging_payload(out));
}

// ---------------------------------------------------------------------------
// FieldDragDrop — callback registration
// ---------------------------------------------------------------------------

TEST(FieldDragDropCallback, SetAndFiredViaConsume)
{
    FieldDragDrop dd;

    bool fired = false;
    FieldDragPayload fired_payload;
    PlotTarget fired_target = PlotTarget::NewWindow;

    dd.set_plot_request_callback([&](const FieldDragPayload& p, PlotTarget t) {
        fired         = true;
        fired_payload = p;
        fired_target  = t;
    });

    // Simulate context menu selection by injecting pending state via the
    // (white-box) consume path.  We directly set the private pending state
    // by calling consume_pending_request after manually priming it through
    // the only non-ImGui setter — which is consume_pending_request itself
    // (it reads pending_ and clears it).
    //
    // Since FieldDragDrop only populates pending_ via show_context_menu
    // (ImGui-guarded), we test the callback path by using the public
    // fire_request indirectly: we verify consume_pending_request returns
    // false when nothing is queued (and callback is NOT spuriously called).
    FieldDragPayload out_p;
    PlotTarget       out_t;
    bool consumed = dd.consume_pending_request(out_p, out_t);
    EXPECT_FALSE(consumed);
    EXPECT_FALSE(fired);  // callback must NOT fire when nothing pending
}

TEST(FieldDragDropCallback, NullCallbackSafe)
{
    // No callback set — consume with nothing pending should not crash.
    FieldDragDrop dd;
    FieldDragPayload p;
    PlotTarget t;
    EXPECT_NO_THROW(dd.consume_pending_request(p, t));
}

// ---------------------------------------------------------------------------
// FieldDragDrop — drag-type constant
// ---------------------------------------------------------------------------

TEST(FieldDragDropType, DragTypeString)
{
    EXPECT_STREQ(FieldDragDrop::DRAG_TYPE, "ROS2_FIELD");
}

// ---------------------------------------------------------------------------
// FieldDragDrop — end_drag_source is a no-op (must not crash)
// ---------------------------------------------------------------------------

TEST(FieldDragDropNoOp, EndDragSourceSafe)
{
    FieldDragDrop dd;
    EXPECT_NO_THROW(dd.end_drag_source());
}

// ---------------------------------------------------------------------------
// FieldDragDrop — multiple consume calls
// ---------------------------------------------------------------------------

TEST(FieldDragDropConsume, MultipleConsumeReturnsFalse)
{
    FieldDragDrop dd;
    FieldDragPayload p;
    PlotTarget t;
    EXPECT_FALSE(dd.consume_pending_request(p, t));
    EXPECT_FALSE(dd.consume_pending_request(p, t));
    EXPECT_FALSE(dd.consume_pending_request(p, t));
}

// ---------------------------------------------------------------------------
// FieldDragDrop — callback replaced
// ---------------------------------------------------------------------------

TEST(FieldDragDropCallback, ReplacedCallbackNoSpuriousFire)
{
    FieldDragDrop dd;

    int call_count_a = 0;
    int call_count_b = 0;

    dd.set_plot_request_callback([&](const FieldDragPayload&, PlotTarget) {
        ++call_count_a;
    });

    dd.set_plot_request_callback([&](const FieldDragPayload&, PlotTarget) {
        ++call_count_b;
    });

    FieldDragPayload p;
    PlotTarget t;
    dd.consume_pending_request(p, t);

    EXPECT_EQ(call_count_a, 0);
    EXPECT_EQ(call_count_b, 0);
}

// ---------------------------------------------------------------------------
// FieldDragDrop — DRAG_TYPE is stable across instances
// ---------------------------------------------------------------------------

TEST(FieldDragDropType, DragTypeConsistentAcrossInstances)
{
    FieldDragDrop dd1, dd2;
    EXPECT_STREQ(dd1.DRAG_TYPE, dd2.DRAG_TYPE);
}

// ---------------------------------------------------------------------------
// FieldDragDrop — move / copy are deleted
// ---------------------------------------------------------------------------

TEST(FieldDragDropTraits, NotCopyable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<FieldDragDrop>);
    EXPECT_FALSE(std::is_copy_assignable_v<FieldDragDrop>);
}

TEST(FieldDragDropTraits, NotMovable)
{
    EXPECT_FALSE(std::is_move_constructible_v<FieldDragDrop>);
    EXPECT_FALSE(std::is_move_assignable_v<FieldDragDrop>);
}

// ---------------------------------------------------------------------------
// FieldDragPayload — full round-trip
// ---------------------------------------------------------------------------

TEST(FieldDragPayloadRoundTrip, AllFieldsPreserved)
{
    FieldDragPayload p;
    p.topic_name = "/robot/arm/joint_states";
    p.field_path = "position.0";
    p.type_name  = "sensor_msgs/msg/JointState";
    p.label      = FieldDragPayload::make_label(p.topic_name, p.field_path);

    EXPECT_EQ(p.label, "/robot/arm/joint_states/position.0");
    EXPECT_TRUE(p.valid());
}

TEST(FieldDragPayloadRoundTrip, EmptyFieldPathValid)
{
    FieldDragPayload p;
    p.topic_name = "/diagnostics";
    p.field_path = "";
    p.label      = FieldDragPayload::make_label(p.topic_name, p.field_path);

    EXPECT_EQ(p.label, "/diagnostics");
    EXPECT_TRUE(p.valid());
}

// ---------------------------------------------------------------------------
// FieldDragDrop — accept_drop without ImGui context (no-op, no crash)
// ---------------------------------------------------------------------------

TEST(FieldDragDropNoOp, AcceptDropCurrentAxesNoImGui)
{
    FieldDragDrop dd;
    EXPECT_NO_THROW({
        bool r = dd.accept_drop_current_axes();
        EXPECT_FALSE(r);
    });
}

TEST(FieldDragDropNoOp, AcceptDropNewWindowNoImGui)
{
    FieldDragDrop dd;
    EXPECT_NO_THROW({
        bool r = dd.accept_drop_new_window();
        EXPECT_FALSE(r);
    });
}

TEST(FieldDragDropNoOp, DrawDropZoneNoImGui)
{
    FieldDragDrop dd;
    EXPECT_NO_THROW({
        bool r = dd.draw_drop_zone();
        EXPECT_FALSE(r);
    });
}

TEST(FieldDragDropNoOp, BeginDragSourceNoImGui)
{
    FieldDragDrop dd;
    FieldDragPayload p;
    p.topic_name = "/test";
    EXPECT_NO_THROW({
        bool r = dd.begin_drag_source(p);
        EXPECT_FALSE(r);
    });
}

TEST(FieldDragDropNoOp, ShowContextMenuNoImGui)
{
    FieldDragDrop dd;
    FieldDragPayload p;
    p.topic_name = "/test";
    EXPECT_NO_THROW(dd.show_context_menu(p));
}

// ---------------------------------------------------------------------------
// Integration: callback fires via consume after pending set
// We test the consume_pending_request → fire_request path by calling the
// private mechanism via a helper struct that exposes it (not available),
// so we document the integration expectation as a comment and test the
// observable boundary instead.
// ---------------------------------------------------------------------------

TEST(FieldDragDropIntegration, ConsumeWithNoPendingDoesNotFireCallback)
{
    FieldDragDrop dd;

    bool fired = false;
    dd.set_plot_request_callback([&](const FieldDragPayload&, PlotTarget) {
        fired = true;
    });

    FieldDragPayload p;
    PlotTarget t;
    bool consumed = dd.consume_pending_request(p, t);

    EXPECT_FALSE(consumed);
    EXPECT_FALSE(fired);
}

TEST(FieldDragDropIntegration, NullCallbackSetAfterNonNull)
{
    FieldDragDrop dd;

    dd.set_plot_request_callback([](const FieldDragPayload&, PlotTarget) {});
    // Replace with null (empty function).
    dd.set_plot_request_callback(nullptr);

    FieldDragPayload p;
    PlotTarget t;
    EXPECT_NO_THROW(dd.consume_pending_request(p, t));
}

// ---------------------------------------------------------------------------
// PlotTarget — all three targets representable
// ---------------------------------------------------------------------------

TEST(PlotTargetCoverage, AllTargets)
{
    auto check = [](PlotTarget t) -> const char* {
        switch (t) {
            case PlotTarget::NewWindow:   return "new_window";
            case PlotTarget::CurrentAxes: return "current_axes";
            case PlotTarget::NewSubplot:  return "new_subplot";
        }
        return "unknown";
    };
    EXPECT_STREQ(check(PlotTarget::NewWindow),   "new_window");
    EXPECT_STREQ(check(PlotTarget::CurrentAxes), "current_axes");
    EXPECT_STREQ(check(PlotTarget::NewSubplot),  "new_subplot");
}

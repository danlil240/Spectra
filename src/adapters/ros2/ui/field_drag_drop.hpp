#pragma once

// FieldDragDrop — Drag-and-drop + right-click context menu for ROS2 field-to-plot.
//
// Provides two interaction modes to add a (topic, field_path) pair to a Spectra plot:
//
//   1. Drag source  — any ImGui widget (e.g. a numeric row in TopicEchoPanel or a topic
//      row in TopicListPanel) calls begin_drag_source() / end_drag_source() around its
//      Selectable / TreeNode call.  ImGui carries the FieldDragPayload while the mouse is
//      held.
//
//   2. Drop target  — a plot canvas or empty area calls accept_drop() each frame; if a
//      payload is released, the appropriate PlotTarget action fires.
//
//   3. Right-click context menu — any widget calls show_context_menu() on the item under
//      the mouse; a popup offers three actions:
//        • "Plot in new window"
//        • "Plot in current axes"
//        • "Plot in new subplot"
//
// The three target actions are routed through a PlotRequestCallback so that the caller
// (e.g. the spectra-ros application shell) owns the actual RosPlotManager interaction.
//
// Thread-safety:
//   All methods must be called from the ImGui render thread only.
//   No internal locking.
//
// ImGui guard:
//   All draw methods are no-ops when SPECTRA_USE_IMGUI is not defined.
//   The pure data types (FieldDragPayload, PlotTarget) are always available.

#include <cstdint>
#include <functional>
#include <string>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// FieldDragPayload — data carried by an in-flight ImGui drag operation.
// ---------------------------------------------------------------------------

struct FieldDragPayload
{
    // Fully-qualified topic name, e.g. "/imu/data"
    std::string topic_name;

    // Dot-separated field path within the message, e.g. "linear_acceleration.x"
    // Empty string means "plot the first numeric field" (used when dragging a
    // whole topic from the list panel without choosing a specific field).
    std::string field_path;

    // ROS2 message type, e.g. "sensor_msgs/msg/Imu"
    // May be empty — callers should tolerate this (RosPlotManager auto-detects).
    std::string type_name;

    // Human-readable label for the drag tooltip, e.g. "/imu/data/linear_acceleration.x"
    std::string label;

    bool valid() const { return !topic_name.empty(); }

    // Build a canonical label from topic + field.
    static std::string make_label(const std::string& topic, const std::string& field)
    {
        if (field.empty())
            return topic;
        return topic + "/" + field;
    }
};

// ---------------------------------------------------------------------------
// PlotTarget — where should the new series land?
// ---------------------------------------------------------------------------

enum class PlotTarget : uint8_t
{
    NewWindow   = 0,   // create a new OS window / Figure
    CurrentAxes = 1,   // overlay onto the currently active Axes
    NewSubplot  = 2,   // add a new subplot row to the current Figure
};

// ---------------------------------------------------------------------------
// PlotRequestCallback — fired when the user commits a drop or menu action.
// ---------------------------------------------------------------------------
using PlotRequestCallback = std::function<void(const FieldDragPayload& payload, PlotTarget target)>;

// ---------------------------------------------------------------------------
// FieldDragDrop — stateful controller for C3 interaction.
// ---------------------------------------------------------------------------

class FieldDragDrop
{
   public:
    // ImGui type-id string used for the drag payload.
    static constexpr const char* DRAG_TYPE = "ROS2_FIELD";

    FieldDragDrop()  = default;
    ~FieldDragDrop() = default;

    FieldDragDrop(const FieldDragDrop&)            = delete;
    FieldDragDrop& operator=(const FieldDragDrop&) = delete;
    FieldDragDrop(FieldDragDrop&&)                 = delete;
    FieldDragDrop& operator=(FieldDragDrop&&)      = delete;

    // ---------- wiring -------------------------------------------------------

    // Set the callback that is invoked whenever the user requests a plot action.
    void set_plot_request_callback(PlotRequestCallback cb) { request_cb_ = std::move(cb); }

    // ---------- drag source helpers ------------------------------------------

    // Call this immediately AFTER an ImGui::Selectable / TreeNode etc.
    // The call site must already know the payload it wants to emit.
    //
    // On the first frame of a drag this registers the payload with ImGui.
    // Renders a semi-transparent tooltip showing the field label.
    //
    // Returns true if a drag is currently in progress from this source.
    bool begin_drag_source(const FieldDragPayload& payload);

    // No-op counterpart (reserved for symmetry; currently empty).
    void end_drag_source() {}

    // ---------- drop target helpers ------------------------------------------

    // Call this over any region that should accept drops (e.g. a plot canvas).
    // If a FieldDragPayload is released over this region, fires request_cb_
    // with PlotTarget::CurrentAxes.
    //
    // Returns true if a drop was accepted this frame.
    bool accept_drop_current_axes();

    // Same but fires with PlotTarget::NewWindow.
    bool accept_drop_new_window();

    // ---------- context menu -------------------------------------------------

    // Call this after ImGui::Selectable / TreeNode etc. to attach a right-click
    // context menu popup.
    //
    // When any of the three menu items is chosen, fires request_cb_ with the
    // corresponding target.
    //
    // `popup_id` must be unique per call site (used as ImGui popup label).
    void show_context_menu(const FieldDragPayload& payload, const char* popup_id = "##field_ctx");

    // ---------- empty-area drop zone -----------------------------------------

    // Render an invisible drop zone that covers the full content region.
    // Useful for plot canvas areas.  If a payload drops here, fires
    // request_cb_ with PlotTarget::CurrentAxes.
    //
    // Returns true if something was dropped.
    bool draw_drop_zone();

    // ---------- state queries ------------------------------------------------

    // True if a ROS2_FIELD drag is currently in flight (any source).
    bool is_dragging() const;

    // The payload currently in flight, if any.  Only valid while is_dragging().
    bool try_get_dragging_payload(FieldDragPayload& out) const;

    // ---------- pending action (populated by context menu, consumed by frame) --

    // Returns true if a pending plot request was queued by the context menu
    // and clears the pending state.  Call once per frame.
    bool consume_pending_request(FieldDragPayload& payload_out, PlotTarget& target_out);

   private:
    void fire_request(const FieldDragPayload& payload, PlotTarget target);

    PlotRequestCallback request_cb_;

    // Pending action from context menu (fires next frame, avoids reentrant call
    // inside the ImGui popup stack).
    bool             pending_{false};
    FieldDragPayload pending_payload_;
    PlotTarget       pending_target_{PlotTarget::NewWindow};
};

}   // namespace spectra::adapters::ros2

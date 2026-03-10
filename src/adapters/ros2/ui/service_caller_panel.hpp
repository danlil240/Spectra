#pragma once

// ServiceCallerPanel — ImGui UI panel for ROS2 service calling (F4).
//
// Provides a dockable window with three panes:
//   1. Service list — all known services, filterable, with type badges.
//   2. Request form — auto-generated editable fields from the request schema.
//   3. History — scrollable call log with state, latency, and response viewer.
//
// Features:
//   - Service list: filter bar, type column, click to select
//   - Auto-generated input form: one widget per leaf field in the request schema
//     (checkbox for bool, drag-float for numeric, text input for string)
//   - Nested struct fields shown with indented separators
//   - Call button + configurable timeout slider
//   - History panel: per-entry state badge (Pending/Done/Error/TimedOut),
//     latency, expandable response JSON viewer
//   - History controls: Clear, Copy JSON to clipboard, Import JSON from clipboard
//   - Replay: click a history entry to re-populate the request form
//
// Thread-safety:
//   draw() must be called from the ImGui render thread.
//   ServiceCaller callbacks fire on the executor thread; ServiceCaller's own
//   mutex protects shared state.
//
// Typical usage:
//   ServiceCaller caller(node, &introspector, &discovery);
//   ServiceCallerPanel panel(&caller);
//   // In render loop:
//   panel.draw();

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "service_caller.hpp"

namespace spectra::adapters::ros2
{

class ServiceCallerPanel
{
   public:
    // Construct with a non-owning pointer to the ServiceCaller backend.
    // The pointer must outlive this panel.
    explicit ServiceCallerPanel(ServiceCaller* caller = nullptr);
    ~ServiceCallerPanel() = default;

    ServiceCallerPanel(const ServiceCallerPanel&)            = delete;
    ServiceCallerPanel& operator=(const ServiceCallerPanel&) = delete;
    ServiceCallerPanel(ServiceCallerPanel&&)                 = delete;
    ServiceCallerPanel& operator=(ServiceCallerPanel&&)      = delete;

    // ---------- wiring -------------------------------------------------------

    void           set_caller(ServiceCaller* caller) { caller_ = caller; }
    ServiceCaller* caller() const { return caller_; }

    // ---------- ImGui rendering ----------------------------------------------

    // Render the panel into the current ImGui context.
    // p_open: if non-null a close button is shown; set to false to close.
    void draw(bool* p_open = nullptr);

    // ---------- state (render-thread only) -----------------------------------

    // Currently selected service name ("" if none).
    const std::string& selected_service() const { return selected_service_; }

    // Programmatically select a service and reload the request form.
    void select_service(const std::string& name);

    // Number of history entries currently displayed.
    std::size_t history_display_count() const { return history_snap_.size(); }

    // ---------- configuration ------------------------------------------------

    void               set_title(const std::string& t) { title_ = t; }
    const std::string& title() const { return title_; }

    // Trigger a service refresh on the next draw() call.
    void request_refresh() { refresh_requested_ = true; }

    // ---------- testing helpers (no ImGui dependency) ------------------------

    // Force-select a service for testing (skips ImGui click).
    // Does NOT load the schema; caller must ensure the schema is already loaded.
    void set_selected_service_for_test(const std::string& name) { selected_service_ = name; }

    // Expose current request fields (for testing).
    const std::vector<ServiceFieldValue>& request_fields() const { return request_fields_; }

    // Set request fields directly (for testing).
    void set_request_fields(std::vector<ServiceFieldValue> fields)
    {
        request_fields_ = std::move(fields);
    }

    // Build the request JSON from current fields (for testing).
    std::string build_request_json() const
    {
        return ServiceCaller::fields_to_json(request_fields_);
    }

    // Current timeout value (seconds).
    float timeout_s() const { return timeout_s_; }
    void  set_timeout_s(float t) { timeout_s_ = t; }

    // Last dispatched call handle (INVALID_CALL_HANDLE if none).
    CallHandle last_call_handle() const { return last_call_handle_; }

   private:
    // -----------------------------------------------------------------------
    // Internal draw helpers
    // -----------------------------------------------------------------------

    // Left pane: service list.
    void draw_service_list(float pane_width);

    // Middle pane: request form + call button.
    void draw_request_form(float pane_width);

    // Right pane: call history.
    void draw_history_pane();

    // Draw one editable field row.
    void draw_field_row(ServiceFieldValue& fv, int idx);

    // Draw one history entry row.
    void draw_history_entry(const CallRecord& rec, bool selected);

    // Reload the request form for the currently selected service.
    void reload_request_form();

    // Populate request fields from a history record (replay).
    void populate_from_record(const CallRecord& rec);

    // State badge: colored text label for CallState.
    static const char* state_badge(CallState s);
    static void        push_state_color(CallState s);
    static void        pop_state_color();

    // Format latency for display.
    static std::string format_latency(double ms);

    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------

    ServiceCaller* caller_{nullptr};
    std::string    title_{"ROS2 Services"};

    // Service list state (render-thread only).
    std::vector<ServiceEntry> services_snap_;   // refreshed each draw
    std::string               selected_service_;
    char                      filter_buf_[256]{};
    std::string               filter_str_;
    bool                      refresh_requested_{true};

    // Request form state (render-thread only).
    std::vector<ServiceFieldValue> request_fields_;
    float                          timeout_s_{5.0f};
    std::string                    form_error_;   // shown below the call button

    // History state (render-thread only).
    std::vector<std::shared_ptr<CallRecord>> history_snap_;   // refreshed each draw
    CallHandle                               selected_history_{INVALID_CALL_HANDLE};
    std::string response_json_preview_;   // expanded JSON for selected entry

    // Last dispatched call.
    CallHandle last_call_handle_{INVALID_CALL_HANDLE};

    // Response/JSON viewer scroll position.
    float json_scroll_{0.0f};

    // Layout ratios (normalized 0-1).
    float list_ratio_{0.28f};
    float form_ratio_{0.42f};
    // history gets the rest
};

}   // namespace spectra::adapters::ros2

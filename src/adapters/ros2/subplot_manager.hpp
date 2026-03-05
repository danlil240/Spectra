#pragma once

// SubplotManager — multi-subplot layout for ROS2 live plotting (C4).
//
// Manages a configurable NxM grid of ROS2 field subscriptions inside a single
// Spectra Figure.  All subplot axes share a common X-axis range via
// AxisLinkManager (LinkAxis::X), and a shared cursor is broadcast across
// subplots whenever the user hovers over any one of them.
//
// Relationship to RosPlotManager:
//   SubplotManager owns its own Figure, its own set of GenericSubscribers (one
//   per slot), and its own ScrollControllers.  It is entirely independent of
//   RosPlotManager — both can coexist in the same application.
//
// Typical usage:
//   Ros2Bridge bridge;  bridge.init(...);  bridge.start_spin();
//   MessageIntrospector intr;
//   SubplotManager mgr(bridge, intr, 3, 1);    // 3 rows, 1 column
//
//   auto h = mgr.add_plot(0, "/imu", "linear_acceleration.x");
//   auto h = mgr.add_plot(1, "/imu", "linear_acceleration.y");
//   auto h = mgr.add_plot(2, "/cmd_vel", "linear.x");
//
//   // In render loop:
//   mgr.poll();     // drain ring buffers, append to series, advance scrollers
//   mgr.figure();   // pass to renderer
//
// Grid layout:
//   Subplots are 1-indexed (slot 1 … rows*cols) following the Spectra
//   Figure::subplot() convention.  Helper index_of(row, col) converts
//   (1-based row, 1-based col) → 1-based slot index.
//
// Shared cursor:
//   The caller forwards hovered-axes cursor data via
//   notify_cursor(source_axes*, data_x, data_y).  SubplotManager broadcasts
//   to AxisLinkManager so the shared cursor is visible on all linked subplots.
//
// Thread-safety:
//   add_plot() / remove_plot() / clear() must be called from the render thread.
//   poll() is render-thread-only (SPSC ring buffer contract).
//   notify_cursor() is render-thread-only.
//   All configuration setters are render-thread-only.
//   AxisLinkManager is internally mutex-protected; all other state is not.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "generic_subscriber.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"
#include "scroll_controller.hpp"

// AxisLinkManager lives under src/ui/data/ — not a public include header.
// We forward-declare here and include the .hpp from subplot_manager.cpp.
namespace spectra { class AxisLinkManager; }

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// SubplotHandle — returned by add_plot(); allows direct access to axes/series.
// ---------------------------------------------------------------------------

struct SubplotHandle
{
    // 1-based slot index within the grid (same as Figure::subplot index).
    int slot{-1};

    // Topic and field that this slot tracks.
    std::string topic;
    std::string field_path;

    // Non-owning pointers into SubplotManager-owned storage.
    spectra::Axes*       axes{nullptr};
    spectra::LineSeries* series{nullptr};

    bool valid() const { return slot >= 1 && axes != nullptr && series != nullptr; }
};

class TopicDiscovery;

// ---------------------------------------------------------------------------
// SubplotManager — main class.
// ---------------------------------------------------------------------------

class SubplotManager
{
public:
    // Number of samples after which the first Y auto-fit fires.
    static constexpr size_t AUTO_FIT_SAMPLES  = 100;

    // Maximum samples drained per slot per poll() call.
    static constexpr size_t MAX_DRAIN_PER_POLL = 4096;

    // -----------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------

    // Create a SubplotManager with a rows×cols grid.
    // rows and cols must each be >= 1.
    SubplotManager(Ros2Bridge&          bridge,
                   MessageIntrospector& intr,
                   int                  rows = 1,
                   int                  cols = 1,
                   spectra::Figure*     external_figure = nullptr);

    ~SubplotManager();

    // Non-copyable, non-movable.
    SubplotManager(const SubplotManager&)            = delete;
    SubplotManager& operator=(const SubplotManager&) = delete;
    SubplotManager(SubplotManager&&)                 = delete;
    SubplotManager& operator=(SubplotManager&&)      = delete;

    // -----------------------------------------------------------------
    // Grid size
    // -----------------------------------------------------------------

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int capacity() const { return rows_ * cols_; }

    // Convert (1-based row, 1-based col) → 1-based slot index.
    // Returns -1 if out of range.
    int index_of(int row, int col) const;

    // -----------------------------------------------------------------
    // Plot management
    // -----------------------------------------------------------------

    // Subscribe to (topic, field_path) and assign it to slot (1-based).
    // If the slot already has a plot, it is replaced.
    // type_name — ROS2 message type (e.g. "std_msgs/msg/Float64").
    //             If empty, auto-discovered from the ROS2 graph.
    // buffer_depth — ring buffer capacity for the GenericSubscriber.
    // Returns a valid SubplotHandle on success; slot == -1 on failure.
    SubplotHandle add_plot(int                slot,
                           const std::string& topic,
                           const std::string& field_path,
                           const std::string& type_name   = "",
                           size_t             buffer_depth = 10000);

    // Convenience: add_plot(row, col, topic, field_path, ...).
    SubplotHandle add_plot(int                row,
                           int                col,
                           const std::string& topic,
                           const std::string& field_path,
                           const std::string& type_name   = "",
                           size_t             buffer_depth = 10000);

    // Remove the plot in a slot.  Returns false if slot is empty.
    bool remove_plot(int slot);

    // Clear all slots (removes all subscriptions, clears series data).
    void clear();

    // Is slot active (has a subscription)?
    bool has_plot(int slot) const;

    // Number of active (non-empty) slots.
    int active_count() const;

    // Retrieve handle for a slot.  Returns invalid handle if slot is empty.
    SubplotHandle handle(int slot) const;

    // All active handles (snapshot).
    std::vector<SubplotHandle> handles() const;

    // -----------------------------------------------------------------
    // Frame update (render thread)
    // -----------------------------------------------------------------

    // Drain ring buffers and append new points to all active series.
    // Also advances all ScrollControllers.
    // Call once per frame from the render thread.
    void poll();

    // -----------------------------------------------------------------
    // Figure access
    // -----------------------------------------------------------------

    spectra::Figure& figure() { return *figure_; }
    const spectra::Figure& figure() const { return *figure_; }

    // -----------------------------------------------------------------
    // Axis linking
    // -----------------------------------------------------------------

    // The AxisLinkManager shared across all subplot axes (X-axis linked).
    spectra::AxisLinkManager& link_manager() { return *link_manager_; }
    const spectra::AxisLinkManager& link_manager() const { return *link_manager_; }

    // -----------------------------------------------------------------
    // Shared cursor
    // -----------------------------------------------------------------

    // Notify the manager that the cursor is at (data_x, data_y) on
    // source_axes.  Broadcasts via AxisLinkManager::update_shared_cursor().
    // Pass nullptr to clear the cursor.
    void notify_cursor(spectra::Axes* source_axes, float data_x, float data_y,
                       double screen_x = 0.0, double screen_y = 0.0);

    // Clear the shared cursor (e.g. mouse left the window).
    void clear_cursor();

    // -----------------------------------------------------------------
    // Auto-scroll (C2)
    // -----------------------------------------------------------------

    // Set the sliding time window (seconds) for all scroll controllers.
    void set_time_window(double seconds);
    double time_window() const { return scroll_window_s_; }

    // Advance "now" for all scroll controllers (call once per frame
    // before poll(), or let poll() call it automatically with wall clock).
    void set_now(double wall_time_s);

    // Pause / resume auto-scroll for a specific slot.
    void pause_scroll(int slot);
    void resume_scroll(int slot);
    bool is_scroll_paused(int slot) const;

    // Pause / resume all slots.
    void pause_all_scroll();
    void resume_all_scroll();

    // Total estimated memory across all active series.
    size_t total_memory_bytes() const;

    // -----------------------------------------------------------------
    // Per-slot time-window override
    // -----------------------------------------------------------------

    // Override the scroll time-window for a specific slot.
    // Pass seconds <= 0 to revert the slot to the global window.
    void set_slot_time_window(int slot, double seconds);

    // Returns the effective window for a slot (per-slot override or global).
    double slot_time_window(int slot) const;

    // Clear any per-slot override, reverting to the global window.
    void clear_slot_time_window(int slot);

    // -----------------------------------------------------------------
    // Subplot context menu helpers (ImGui, render-thread only)
    // -----------------------------------------------------------------

    // Possible actions returned from draw_slot_context_menu().
    enum class SubplotAction : uint8_t
    {
        None      = 0,
        Rename    = 1,
        Clear     = 2,
        Duplicate = 3,
        Detach    = 4,
    };

    // Draw an ImGui right-click popup for a subplot slot.
    // Call this after drawing the subplot's axes/canvas widget.
    // Returns the action the user selected (None if popup not open or no action).
    // `popup_id` must be unique per slot — callers typically pass e.g. "##sctx_1".
    SubplotAction draw_slot_context_menu(int slot, const char* popup_id);

    // -----------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------

    void set_figure_size(uint32_t w, uint32_t h);
    void set_auto_fit_samples(size_t n) { auto_fit_samples_ = n; }
    size_t auto_fit_samples() const { return auto_fit_samples_; }
    void set_topic_discovery(TopicDiscovery* disc) { discovery_ = disc; }

    // -----------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------

    // Called (render thread, inside poll()) when a new point is appended.
    using OnDataCallback = std::function<void(int slot, double t_sec, double value)>;
    void set_on_data(OnDataCallback cb) { on_data_cb_ = std::move(cb); }

private:
    // ------------------------------------------------------------------
    // SlotEntry — per-slot state.
    // ------------------------------------------------------------------
    struct SlotEntry
    {
        int         slot{-1};   // 1-based
        std::string topic;
        std::string field_path;
        std::string type_name;

        // Non-owning Spectra pointers — owned by figure_.
        spectra::Axes*       axes{nullptr};
        spectra::LineSeries* series{nullptr};

        // ROS2 subscription.
        std::unique_ptr<GenericSubscriber> subscriber;
        int                                extractor_id{-1};

        // Auto-fit state.
        size_t samples_received{0};
        bool   auto_fitted{false};

        // Color index in the palette.
        size_t color_index{0};

        // Per-slot scratch drain buffer.
        std::vector<FieldSample> drain_buf;

        // Per-slot auto-scroll controller.
        ScrollController scroll;

        // Per-slot time-window override (seconds). <= 0 means use global.
        double time_window_override_s{-1.0};

        bool active() const { return slot >= 1 && axes != nullptr; }
    };

    // Retrieve entry by 1-based slot (creates if needed up to capacity).
    SlotEntry* slot_entry(int slot);
    const SlotEntry* slot_entry(int slot) const;

    // Auto-detect type name for topic.
    std::string detect_type(const std::string& topic) const;

    // Assign next palette color.
    spectra::Color next_color();

    // Re-link all active axes (called after add_plot / remove_plot).
    void rebuild_x_links();

    // -----------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------

    Ros2Bridge&          bridge_;
    MessageIntrospector& intr_;

    int rows_{1};
    int cols_{1};

    std::unique_ptr<spectra::Figure>          owned_figure_;
    spectra::Figure*                          figure_{nullptr};
    std::unique_ptr<spectra::AxisLinkManager> link_manager_;

    // Slots vector indexed [slot-1] (0-based internally).
    // Size is always rows_ * cols_ after construction.
    std::vector<SlotEntry> slots_;

    // Color assignment cursor.
    size_t color_cursor_{0};

    // Configuration.
    double   scroll_window_s_    = ScrollController::DEFAULT_WINDOW_S;
    size_t   auto_fit_samples_   = AUTO_FIT_SAMPLES;

    OnDataCallback on_data_cb_;

    TopicDiscovery*      discovery_{nullptr};
};

}   // namespace spectra::adapters::ros2

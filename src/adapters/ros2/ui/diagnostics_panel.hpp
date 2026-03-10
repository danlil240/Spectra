#pragma once

// DiagnosticsPanel — ROS2 /diagnostics dashboard panel (F6).
//
// Subscribes to /diagnostics (diagnostic_msgs/msg/DiagnosticArray), groups
// results by component name, displays status badges and expandable key/value
// tables.  A short sparkline history tracks status changes over time, and
// alert callbacks fire on every OK→WARN/ERROR transition.
//
// ROS2 message layout (CDR, parsed without codegen):
//   DiagnosticArray:
//     std_msgs/Header header
//     DiagnosticStatus[] status
//       byte level          (0=OK, 1=WARN, 2=ERROR, 3=STALE)
//       string name
//       string message
//       string hardware_id
//       KeyValue[] values
//         string key
//         string value
//
// Thread model:
//   push_message() — safe from ROS2 executor thread (lock-free ring buffer)
//   poll() / draw() — render thread only (acquires mutex)
//   All shared state behind mutex_
//
// CDR parsing:
//   Uses raw byte arithmetic identical to the pattern in bag_player.cpp.
//   String: 4-byte LE length + data (no null terminator in length).
//   Sequence: 4-byte LE count + elements.
//   No dependency on rosidl_typesupport at runtime (avoids schema lookup
//   for this well-known fixed message type).
//
// Typical usage:
//   DiagnosticsPanel panel;
//   panel.set_node(&node);          // wire ROS2 node before calling start()
//   panel.start();                  // creates GenericSubscription
//
//   // In render loop:
//   panel.poll();                   // drain ring buffer into model
//   panel.draw();                   // render ImGui window

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef SPECTRA_USE_ROS2
    #include <diagnostic_msgs/msg/diagnostic_array.hpp>
    #include <rclcpp/rclcpp.hpp>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// DiagLevel — mirrors diagnostic_msgs/msg/DiagnosticStatus level constants.
// ---------------------------------------------------------------------------

enum class DiagLevel : uint8_t
{
    OK    = 0,
    Warn  = 1,
    Error = 2,
    Stale = 3,
};

const char* diag_level_name(DiagLevel l);

// ---------------------------------------------------------------------------
// DiagKeyValue — one key/value pair from a DiagnosticStatus.
// ---------------------------------------------------------------------------

struct DiagKeyValue
{
    std::string key;
    std::string value;
};

// ---------------------------------------------------------------------------
// DiagStatus — one parsed DiagnosticStatus message entry.
// ---------------------------------------------------------------------------

struct DiagStatus
{
    DiagLevel                 level{DiagLevel::Stale};
    std::string               name;
    std::string               message;
    std::string               hardware_id;
    std::vector<DiagKeyValue> values;

    // Wall-clock arrival timestamp (nanoseconds since epoch).
    int64_t arrival_ns{0};
};

// ---------------------------------------------------------------------------
// DiagSparkEntry — one entry in the per-component sparkline history.
//   level   — DiagLevel at that sample
//   time_ns — wall-clock timestamp
// ---------------------------------------------------------------------------

struct DiagSparkEntry
{
    DiagLevel level{DiagLevel::Stale};
    int64_t   time_ns{0};
};

// ---------------------------------------------------------------------------
// DiagComponent — aggregated state for one hardware component (by name).
// ---------------------------------------------------------------------------

struct DiagComponent
{
    std::string name;          // e.g. "battery", "/driver/motor"
    std::string hardware_id;   // last seen hardware_id

    // Current (most-recent) status.
    DiagLevel                 level{DiagLevel::Stale};
    std::string               message;
    std::vector<DiagKeyValue> values;

    // Arrival timestamp of the most-recent status update.
    int64_t last_update_ns{0};

    // Sparkline history: up to MAX_SPARK entries, newest at back.
    static constexpr size_t    MAX_SPARK = 60;
    std::deque<DiagSparkEntry> history;

    // True if this component has ever transitioned to WARN or ERROR.
    bool ever_alerted{false};

    // Count of transitions (OK→WARN, OK→ERROR, WARN→ERROR, etc.).
    uint32_t transition_count{0};

    // Seconds elapsed since the last update (computed from now_ns at draw time).
    // Returns 0.0 if never updated.
    double seconds_since_update(int64_t now_ns) const
    {
        if (last_update_ns <= 0)
            return 0.0;
        const double diff = static_cast<double>(now_ns - last_update_ns) * 1e-9;
        return diff > 0.0 ? diff : 0.0;
    }

    // Push a new status update; appends to history, detects transitions.
    // Returns true if the level changed (i.e. a transition occurred).
    bool update(const DiagStatus& s);
};

// ---------------------------------------------------------------------------
// DiagnosticsModel — the parsed, aggregated view of all diagnostics.
// ---------------------------------------------------------------------------

struct DiagnosticsModel
{
    // Component map: name → DiagComponent.
    std::unordered_map<std::string, DiagComponent> components;

    // Ordered list of component names (insertion order preserved).
    std::vector<std::string> order;

    // Counts by level (recomputed after each poll batch).
    int count_ok{0};
    int count_warn{0};
    int count_error{0};
    int count_stale{0};

    // Total messages received.
    uint64_t total_messages{0};

    // Apply one DiagStatus.  Creates component if new, updates if existing.
    // Returns the component name on a level transition, "" otherwise.
    std::string apply(const DiagStatus& s);

    // Recount level badges.
    void recount();

    // Remove components not updated within stale_ns nanoseconds.
    void prune_stale(int64_t now_ns, int64_t stale_ns);
};

// ---------------------------------------------------------------------------
// DiagRawMessage — raw CDR bytes of one DiagnosticArray, queued via ring.
// ---------------------------------------------------------------------------

struct DiagRawMessage
{
    std::vector<uint8_t> data;
    int64_t              arrival_ns{0};
};

// ---------------------------------------------------------------------------
// DiagnosticsPanel
// ---------------------------------------------------------------------------

class DiagnosticsPanel
{
   public:
    DiagnosticsPanel();
    ~DiagnosticsPanel();

    DiagnosticsPanel(const DiagnosticsPanel&)            = delete;
    DiagnosticsPanel& operator=(const DiagnosticsPanel&) = delete;
    DiagnosticsPanel(DiagnosticsPanel&&)                 = delete;
    DiagnosticsPanel& operator=(DiagnosticsPanel&&)      = delete;

    // -----------------------------------------------------------------------
    // Wiring / lifecycle
    // -----------------------------------------------------------------------

#ifdef SPECTRA_USE_ROS2
    // Set the ROS2 node to create the subscription on.
    // Must be called before start().  Pointer must outlive this panel.
    void          set_node(rclcpp::Node* node) { node_ = node; }
    rclcpp::Node* node() const { return node_; }
#endif

    // Create the /diagnostics subscription and start receiving.
    // Idempotent — does nothing if already started.
    // Returns false if no node is set or subscription creation fails.
    bool start();

    // Destroy the subscription.  Idempotent.
    void stop();

    bool is_running() const { return running_.load(std::memory_order_acquire); }

    // -----------------------------------------------------------------------
    // Per-frame update (render thread)
    // -----------------------------------------------------------------------

    // Drain ring buffer, parse CDR, update model, fire alert callbacks.
    // Call once per frame before draw().
    void poll();

    // -----------------------------------------------------------------------
    // ImGui rendering (render thread)
    // -----------------------------------------------------------------------

    // Render the diagnostics dashboard as a standalone ImGui window.
    // p_open — if non-null, a close button is shown.
    void draw(bool* p_open = nullptr);

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    // Topic to subscribe (default "/diagnostics").
    void               set_topic(const std::string& topic) { topic_ = topic; }
    const std::string& topic() const { return topic_; }

    // Window title (default "Diagnostics").
    void               set_title(const std::string& t) { title_ = t; }
    const std::string& title() const { return title_; }

    // Components not updated within stale_threshold_s seconds are
    // automatically marked STALE (default 5.0 s).
    void   set_stale_threshold_s(double s) { stale_threshold_s_ = s; }
    double stale_threshold_s() const { return stale_threshold_s_; }

    // Maximum raw messages buffered from the executor thread (ring, default 256).
    // Must be set before start().
    void   set_ring_depth(size_t d) { ring_depth_ = d; }
    size_t ring_depth() const { return ring_depth_; }

    // -----------------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------------

    // Fired from poll() when a component transitions to WARN or ERROR.
    // Signature: void(const std::string& component_name, DiagLevel new_level)
    using AlertCallback = std::function<void(const std::string&, DiagLevel)>;
    void set_alert_callback(AlertCallback cb) { alert_cb_ = std::move(cb); }

    // -----------------------------------------------------------------------
    // Testing helpers (no ImGui, no ROS2 runtime required)
    // -----------------------------------------------------------------------

    // Inject a pre-built DiagStatus directly into the model (bypasses CDR +
    // ring buffer).  Fires alert callback on transitions.  Render-thread only.
    void inject_status(const DiagStatus& s);

    // Inject a full DiagnosticArray (vector of statuses).
    void inject_array(const std::vector<DiagStatus>& statuses, int64_t arrival_ns = 0);

    // Access the model (render-thread only, after poll()).
    const DiagnosticsModel& model() const { return model_; }

    // Number of raw messages waiting in the ring buffer.
    size_t pending_raw() const;

    // Parse a DiagnosticArray from raw CDR bytes.
    // Static so it can be unit-tested independently.
    // Returns parsed statuses; empty on parse failure.
    static std::vector<DiagStatus> parse_diag_array(const uint8_t* data,
                                                    size_t         len,
                                                    int64_t        arrival_ns);

    // Compute RGBA badge colour for a DiagLevel.
    // Returns {r, g, b, a} in [0, 1] float range.
    static void level_color(DiagLevel l, float& r, float& g, float& b, float& a);

    // Short label for a DiagLevel: "OK", "WARN", "ERR", "STALE".
    static const char* level_short(DiagLevel l);

   private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    // Low-level CDR helper: read a 4-byte LE uint32 from buf[offset].
    // Returns 0 and sets offset beyond buf length on underflow.
    static uint32_t read_u32(const uint8_t* buf, size_t len, size_t& offset);

    // Read a CDR string (4-byte length + bytes, no null in length).
    // Returns "" on underflow.
    static std::string read_string(const uint8_t* buf, size_t len, size_t& offset);

    // Read a single DiagnosticStatus from CDR at offset.
    // Returns false on underflow.
    static bool read_diag_status(const uint8_t* buf,
                                 size_t         len,
                                 size_t&        offset,
                                 DiagStatus&    out,
                                 int64_t        arrival_ns);

#ifdef SPECTRA_USE_IMGUI
    // Draw the summary badge bar (OK/WARN/ERR/STALE counts).
    void draw_summary_bar();

    // Draw the filter / search bar.  Returns true if filter changed.
    bool draw_filter_bar();

    // Draw the table of components.
    void draw_component_table();

    // Draw one component row (may expand key/value sub-rows).
    void draw_component_row(DiagComponent& comp);

    // Draw a mini sparkline for one component inline.
    // width/height in pixels.
    void draw_sparkline(const DiagComponent& comp, float width, float height);
#endif

    // -----------------------------------------------------------------------
    // Ring buffer (lock-free SPSC: executor = producer, poll = consumer)
    // -----------------------------------------------------------------------

    std::vector<DiagRawMessage> ring_;
    size_t                      ring_mask_{0};
    std::atomic<size_t>         ring_head_{0};   // producer
    std::atomic<size_t>         ring_tail_{0};   // consumer

    // Push a raw message from the executor thread.
    void ring_push(DiagRawMessage msg);

    // Pop one raw message on the render thread.
    bool ring_pop(DiagRawMessage& out);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

#ifdef SPECTRA_USE_ROS2
    rclcpp::Node*                                                          node_{nullptr};
    rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr subscription_;
#endif

    std::atomic<bool> running_{false};

    std::string topic_{"diagnostics"};   // without leading slash (rclcpp resolves)
    std::string title_{"Diagnostics"};
    double      stale_threshold_s_{5.0};
    size_t      ring_depth_{256};

    // Model (render-thread only after construction).
    DiagnosticsModel model_;

    // UI state (render-thread only).
    char        filter_buf_[256]{};
    std::string filter_str_;
    std::string expanded_component_;   // name of currently expanded component
    bool        show_ok_{true};
    bool        show_warn_{true};
    bool        show_error_{true};
    bool        show_stale_{true};
    bool        show_kv_all_{false};   // expand all key/value tables

    // Alert callback.
    AlertCallback alert_cb_;

    // Typed ROS2 diagnostic callbacks stage parsed status arrays here so the
    // render thread can apply them during poll().
    std::mutex                           pending_mutex_;
    std::vector<std::vector<DiagStatus>> pending_status_batches_;
};

}   // namespace spectra::adapters::ros2

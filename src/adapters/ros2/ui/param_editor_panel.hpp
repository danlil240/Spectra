#pragma once

// ParamEditorPanel — ROS2 parameter editor panel (F3).
//
// Discovers parameters on a selected ROS2 node via standard rcl_interfaces
// service calls (ListParameters, DescribeParameters, GetParameters,
// SetParameters).  Renders an ImGui panel with per-type widgets:
//
//   bool        → checkbox
//   int64       → drag_int / input_int with range hints
//   double      → drag_float / input_float with range hints
//   string      → input_text
//   byte_array  → hex display (read-only)
//   int_array / double_array / string_array / bool_array → compact lists
//
// Modes:
//   Live-edit   — sends SetParameters on every widget change (default)
//   Apply-mode  — stages changes locally; "Apply" button sends all at once
//
// Undo:
//   Single-level undo of last applied change (stores before/after values per
//   parameter).  "Undo" button restores last changed parameter to its
//   previous value.
//
// YAML presets:
//   Save current node's parameters to a YAML file; load a YAML file to
//   apply a stored set of values to the current node.
//   Format is ROS2-compatible param YAML:
//     <node_name>:
//       ros__parameters:
//         <param>: <value>
//
// Thread-safety:
//   set_node() / refresh() may be called from any thread.
//   draw() must be called from the ImGui render thread only.
//   Internal state is protected by mutex_.
//
// Non-ImGui build:
//   All draw*() methods compile to no-ops when SPECTRA_USE_IMGUI is not
//   defined.  All logic methods (set_node, refresh, param accessors) remain
//   available for testing.
//
// Typical usage:
//   ParamEditorPanel panel(bridge.node());
//   panel.set_target_node("/my_node");
//   panel.refresh();      // fetch parameters
//   // In render loop:
//   panel.draw();         // renders ImGui window

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rcl_interfaces/msg/parameter.hpp>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>
#include <rcl_interfaces/msg/parameter_type.hpp>
#include <rcl_interfaces/msg/parameter_value.hpp>
#include <rcl_interfaces/srv/describe_parameters.hpp>
#include <rcl_interfaces/srv/get_parameters.hpp>
#include <rcl_interfaces/srv/list_parameters.hpp>
#include <rcl_interfaces/srv/set_parameters.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// ParamType — mirrors rcl_interfaces ParameterType for our model layer
// ---------------------------------------------------------------------------

enum class ParamType : uint8_t
{
    NotSet       = 0,
    Bool         = 1,
    Integer      = 2,
    Double       = 3,
    String       = 4,
    ByteArray    = 5,
    BoolArray    = 6,
    IntegerArray = 7,
    DoubleArray  = 8,
    StringArray  = 9,
};

// Human-readable name for a ParamType.
const char* param_type_name(ParamType t);

// ---------------------------------------------------------------------------
// ParamValue — discriminated union (no std::variant to keep C++17/20 compat)
// ---------------------------------------------------------------------------

struct ParamValue
{
    ParamType type{ParamType::NotSet};

    // Scalar fields
    bool        bool_val{false};
    int64_t     int_val{0};
    double      double_val{0.0};
    std::string string_val;

    // Array fields
    std::vector<uint8_t>     byte_array;
    std::vector<bool>        bool_array;
    std::vector<int64_t>     int_array;
    std::vector<double>      double_array;
    std::vector<std::string> string_array;

    // Construct from rcl_interfaces ParameterValue
    static ParamValue from_msg(const rcl_interfaces::msg::ParameterValue& msg);

    // Convert to rcl_interfaces ParameterValue
    rcl_interfaces::msg::ParameterValue to_msg() const;

    // Human-readable value string (truncated for arrays / long strings).
    std::string to_display_string(size_t max_len = 64) const;

    // Equality check (used to detect staged changes vs current).
    bool operator==(const ParamValue& o) const;
    bool operator!=(const ParamValue& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// ParamDescriptor — range hints + read-only flag
// ---------------------------------------------------------------------------

struct ParamDescriptor
{
    std::string description;
    bool        read_only{false};
    bool        dynamic_typing{false};

    // Range hints for numeric types (all zero = no constraint)
    double float_range_min{0.0};
    double float_range_max{0.0};
    double float_range_step{0.0};

    int64_t integer_range_min{0};
    int64_t integer_range_max{0};
    int64_t integer_range_step{0};

    bool has_float_range()   const { return float_range_min != 0.0 || float_range_max != 0.0; }
    bool has_integer_range() const { return integer_range_min != 0 || integer_range_max != 0; }

    // Construct from rcl_interfaces ParameterDescriptor
    static ParamDescriptor from_msg(const rcl_interfaces::msg::ParameterDescriptor& msg);
};

// ---------------------------------------------------------------------------
// ParamEntry — one parameter as known to the editor
// ---------------------------------------------------------------------------

struct ParamEntry
{
    std::string    name;
    ParamType      type{ParamType::NotSet};
    ParamValue     current;       // last fetched value from node
    ParamValue     staged;        // edited-but-not-yet-applied value (apply mode)
    ParamDescriptor descriptor;

    bool staged_dirty{false};     // staged != current
    bool set_error{false};        // last SetParameters call rejected this param
    std::string set_error_reason; // reason string from rcl_interfaces
};

// ---------------------------------------------------------------------------
// UndoEntry — stores one param's before/after for single-level undo
// ---------------------------------------------------------------------------

struct UndoEntry
{
    std::string node_name;
    std::string param_name;
    ParamValue  before;
    ParamValue  after;
    bool        valid{false};
};

// ---------------------------------------------------------------------------
// ParamSetResult — outcome of a SetParameters call
// ---------------------------------------------------------------------------

struct ParamSetResult
{
    bool        ok{false};
    std::string reason;  // on failure: reason from rcl_interfaces
};

// ---------------------------------------------------------------------------
// PresetEntry — one saved YAML preset
// ---------------------------------------------------------------------------

struct PresetEntry
{
    std::string name;        // display name
    std::string node_name;   // target node at save time
    std::string yaml_path;   // absolute file path

    bool valid() const { return !name.empty() && !yaml_path.empty(); }
};

// ---------------------------------------------------------------------------
// ParamEditorPanel
// ---------------------------------------------------------------------------

class ParamEditorPanel
{
public:
    // Construct with the shared node used for service calls.
    explicit ParamEditorPanel(rclcpp::Node::SharedPtr node);
    ~ParamEditorPanel();

    // Non-copyable, non-movable (owns service clients).
    ParamEditorPanel(const ParamEditorPanel&)            = delete;
    ParamEditorPanel& operator=(const ParamEditorPanel&) = delete;
    ParamEditorPanel(ParamEditorPanel&&)                 = delete;
    ParamEditorPanel& operator=(ParamEditorPanel&&)      = delete;

    // ---------- wiring / configuration -----------------------------------

    // Set the fully-qualified node name to edit (e.g. "/my_ns/my_node").
    // Clears cached parameters and re-creates service clients.
    // Thread-safe.
    void set_target_node(const std::string& node_name);
    std::string target_node() const;

    // Window title shown in ImGui title bar.
    void        set_title(const std::string& t);
    std::string title() const;

    // Live-edit mode: if true, every widget change calls SetParameters
    // immediately.  If false (apply mode), changes are staged until Apply.
    void set_live_edit(bool live);
    bool live_edit() const;

    // ---------- lifecycle ------------------------------------------------

    // Fetch parameters from the current target node (async — fires
    // on_refresh_done when complete).  May be called from any thread.
    void refresh();

    // Discard all staged edits (apply mode only).
    void discard_staged();

    // Apply all staged edits (sends SetParameters for dirty params).
    // Live-edit: no-op (changes are sent immediately).
    // Returns true if all SetParameters calls succeeded.
    bool apply_staged();

    // Undo the last successful set operation (restores previous value).
    // Returns false if there is nothing to undo.
    bool undo_last();
    bool can_undo() const;

    // ---------- accessors (thread-safe, snapshot) -----------------------

    // List of known parameter names (after last refresh).
    std::vector<std::string> param_names() const;

    // Snapshot of a single parameter entry ("" name if not found).
    ParamEntry param_entry(const std::string& name) const;

    // True once the first successful refresh has completed.
    bool is_loaded() const;

    // True if a refresh is currently in progress.
    bool is_refreshing() const;

    // Name of the last error (empty if no error).
    std::string last_error() const;
    void        clear_error();

    // Number of staged-dirty parameters (apply mode).
    size_t staged_count() const;

    // ---------- YAML preset API ------------------------------------------

    // Save the current parameter values to a YAML file.
    // Returns true on success.
    bool save_preset(const std::string& display_name,
                     const std::string& file_path);

    // Load parameters from a YAML file and apply them to the target node.
    // Returns true if all parameters were set successfully.
    bool load_preset(const std::string& file_path);

    // In-memory preset list (not persisted — session only).
    void               add_preset(PresetEntry e);
    void               remove_preset(size_t idx);
    const std::vector<PresetEntry>& presets() const;

    // ---------- callbacks ------------------------------------------------

    // Called (from any thread) when a refresh completes.
    using RefreshDoneCallback = std::function<void(bool success)>;
    void set_on_refresh_done(RefreshDoneCallback cb);

    // Called (from the render thread) when a parameter is set.
    using ParamSetCallback = std::function<void(const std::string& param,
                                                const ParamValue& value,
                                                bool success)>;
    void set_on_param_set(ParamSetCallback cb);

    // ---------- ImGui rendering ------------------------------------------

    // Draw the panel as a dockable ImGui window.
    void draw(bool* p_open = nullptr);

    // ---------- helpers (public for testing) -----------------------------

    // Parse a ROS2-compatible YAML string into a name→value map.
    // Returns false if parsing fails; populates error_out on failure.
    static bool parse_yaml(const std::string& yaml_text,
                           const std::string& node_name,
                           std::unordered_map<std::string, ParamValue>& out,
                           std::string& error_out);

    // Serialise a name→value map to ROS2-compatible YAML text.
    static std::string serialize_yaml(
        const std::string& node_name,
        const std::unordered_map<std::string, ParamValue>& params);

    // Convert rcl_interfaces ParameterType integer to ParamType.
    static ParamType from_rcl_type(uint8_t rcl_type);

private:
    // Service client management (must be called with mutex_ held or from
    // refresh() which handles locking).
    void create_clients(const std::string& node_name);
    void destroy_clients();

    // Internal refresh (called from refresh() on a detached thread).
    void do_refresh();

    // Internal set one parameter (live-edit path + apply_staged path).
    // WARNING: blocks for up to 7 s — never call from the render thread.
    ParamSetResult set_param_internal(const std::string& param_name,
                                      const ParamValue& value);

    // Queue a set operation for the background worker thread (non-blocking).
    // Safe to call from the render thread.
    void queue_set_param(const std::string& param_name,
                         const ParamValue& value);

    // Background worker that drains set_queue_ and calls set_param_internal.
    void set_worker_func();

    // Poll completed async set results (called from draw() each frame).
    void poll_set_results();

    // ImGui helpers (compiled only with SPECTRA_USE_IMGUI)
    void draw_toolbar();
    void draw_param_table();
    void draw_param_row(ParamEntry& entry);
    void draw_bool_widget(ParamEntry& entry);
    void draw_int_widget(ParamEntry& entry);
    void draw_double_widget(ParamEntry& entry);
    void draw_string_widget(ParamEntry& entry);
    void draw_array_widget(ParamEntry& entry);
    void draw_preset_popup();
    void draw_error_banner();

    // YAML helpers
    static std::string yaml_scalar(const ParamValue& v);
    static bool        parse_yaml_scalar(const std::string& raw,
                                         ParamType type,
                                         ParamValue& out);

    // ---------- data (all protected by mutex_ unless noted) ---------------

    mutable std::mutex mutex_;

    rclcpp::Node::SharedPtr node_;

    std::string target_node_;
    std::string title_{"Parameter Editor"};
    bool        live_edit_{true};

    // Service clients (recreated on set_target_node)
    rclcpp::Client<rcl_interfaces::srv::ListParameters>::SharedPtr    list_client_;
    rclcpp::Client<rcl_interfaces::srv::DescribeParameters>::SharedPtr desc_client_;
    rclcpp::Client<rcl_interfaces::srv::GetParameters>::SharedPtr      get_client_;
    rclcpp::Client<rcl_interfaces::srv::SetParameters>::SharedPtr      set_client_;

    // Parameter model
    std::vector<std::string>                    param_names_;  // ordered
    std::unordered_map<std::string, ParamEntry> param_map_;

    // State flags
    std::atomic<bool> loaded_{false};
    std::atomic<bool> refreshing_{false};

    std::string last_error_;

    // Undo slot (single level)
    UndoEntry undo_slot_;

    // Preset list
    std::vector<PresetEntry> presets_;

    // Callbacks
    RefreshDoneCallback refresh_done_cb_;
    ParamSetCallback    param_set_cb_;

    // ---------- async set worker (separate lock domain) -------------------

    struct PendingSetOp
    {
        std::string name;
        ParamValue  value;
    };

    struct SetResultEntry
    {
        std::string    name;
        ParamValue     value;
        ParamSetResult result;
    };

    std::mutex              set_queue_mutex_;
    std::condition_variable set_cv_;
    std::deque<PendingSetOp> set_queue_;          // pending operations
    std::deque<SetResultEntry> set_results_;      // completed results
    std::thread             set_worker_;
    std::atomic<bool>       stop_worker_{false};

    // ImGui transient state (render thread only — no lock needed)
    char   node_input_buf_[256]{};   // node name text box
    char   search_buf_[128]{};        // search/filter text
    char   preset_name_buf_[128]{};   // save-preset name box
    char   preset_path_buf_[512]{};   // save-preset path box
    bool   show_preset_popup_{false};
    bool   show_read_only_{true};
    int    sort_mode_{0};             // 0=name, 1=type, 2=none
};

}   // namespace spectra::adapters::ros2

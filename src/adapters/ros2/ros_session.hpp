#pragma once

// RosSession — save/load of spectra-ros session state (G3).
//
// A "session" captures everything needed to reconstruct the user's current
// workspace when spectra-ros is next launched:
//
//   - Active subscriptions (RosPlotManager / SubplotManager plots):
//       topic, field_path, message type, subplot slot, time-window
//   - Expression plots (ExpressionPlot entries):
//       expression string, variable→topic/field bindings, label
//   - ExpressionEngine presets (saved expressions)
//   - Global time window (seconds)
//   - Layout mode ("default" / "plot-only" / "monitor")
//   - Subplot grid dimensions (rows × cols)
//   - Panel visibility flags
//   - CLI node_name / node_ns
//
// Format: UTF-8 JSON with extension `.spectra-ros-session`.
// Version field allows future forward-compatibility checks.
//
// Recent sessions list:
//   - Stored at ~/.config/spectra/ros_recent.json
//   - Up to MAX_RECENT entries (MRU order)
//   - Each entry: { "path": "...", "node": "...", "saved_at": "<ISO8601>" }
//
// Auto-save:
//   Call RosSessionManager::auto_save() from RosAppShell destructor /
//   shutdown path. The manager saves to the last-used path if one exists.
//
// Thread-safety:
//   All RosSessionManager methods must be called from the render thread.
//   No internal locking.

#include <string>
#include <vector>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Session format version
// ---------------------------------------------------------------------------

inline constexpr int SESSION_FORMAT_VERSION = 1;

// ---------------------------------------------------------------------------
// SubscriptionEntry — one active plot (RosPlotManager or SubplotManager slot)
// ---------------------------------------------------------------------------

struct SubscriptionEntry
{
    // ROS2 topic and field to subscribe.
    std::string topic;
    std::string field_path;

    // Optional: cached message type (e.g. "std_msgs/msg/Float64").
    // Empty = auto-detect on restore.
    std::string type_name;

    // 0 = RosPlotManager standalone plot (no subplot grid)
    // >0 = 1-based SubplotManager slot index
    int subplot_slot{0};

    // Per-plot scroll/time-window override (seconds).
    // 0 = inherit from global time_window_s.
    double time_window_s{0.0};

    // Whether auto-scroll is paused for this plot.
    bool scroll_paused{false};
};

// ---------------------------------------------------------------------------
// ExpressionEntry — one expression plot (ExpressionPlot / C5)
// ---------------------------------------------------------------------------

struct ExpressionEntry
{
    // The compiled expression string (e.g. "sqrt($a.x^2 + $a.y^2)").
    std::string expression;

    // Human-readable label for the series.  If empty, expression is used.
    std::string label;

    // Variable bindings: variable name → "topic:field_path"
    // (e.g. "$imu.linear_acceleration.x" → "/imu:linear_acceleration.x")
    struct VarBinding
    {
        std::string variable;   // "$imu.linear_acceleration.x"
        std::string topic;
        std::string field_path;
    };
    std::vector<VarBinding> bindings;

    // 0 = standalone; >0 = SubplotManager slot.
    int subplot_slot{0};
};

// ---------------------------------------------------------------------------
// ExpressionPresetEntry — a saved expression preset (not necessarily active)
// ---------------------------------------------------------------------------

struct ExpressionPresetEntry
{
    std::string              name;
    std::string              expression;
    std::vector<std::string> variables;
};

// ---------------------------------------------------------------------------
// PanelVisibility — which panels are shown
// ---------------------------------------------------------------------------

struct PanelVisibility
{
    bool topic_list     = true;
    bool topic_echo     = true;
    bool topic_stats    = true;
    bool plot_area      = true;
    bool bag_info       = false;
    bool bag_playback   = false;
    bool log_viewer     = false;
    bool diagnostics    = false;
    bool node_graph     = false;
    bool tf_tree        = false;
    bool param_editor   = false;
    bool service_caller = false;
    bool nav_rail       = true;
};

// ---------------------------------------------------------------------------
// RosSession — the full in-memory session data model
// ---------------------------------------------------------------------------

struct RosSession
{
    // Format version — always SESSION_FORMAT_VERSION when writing.
    int version{SESSION_FORMAT_VERSION};

    // ROS2 identity saved with the session (informational; not re-applied).
    std::string node_name;
    std::string node_ns;

    // Layout mode string: "default" / "plot-only" / "monitor".
    std::string layout{"default"};

    // Subplot grid.
    int subplot_rows{4};
    int subplot_cols{1};

    // Global auto-scroll time window (seconds).
    double time_window_s{30.0};

    // Active subscriptions.
    std::vector<SubscriptionEntry> subscriptions;

    // Active expression plots.
    std::vector<ExpressionEntry> expressions;

    // Saved expression presets (from ExpressionEngine).
    std::vector<ExpressionPresetEntry> expression_presets;

    // Panel visibility.
    PanelVisibility panels;

    // Spectra-ROS nav rail state.
    bool   nav_rail_expanded{false};
    double nav_rail_width{220.0};

    // ISO-8601 UTC timestamp written at save time (informational).
    std::string saved_at;

    // Optional user-visible description / notes.
    std::string description;
};

// ---------------------------------------------------------------------------
// RecentEntry — one item in the recent-sessions list
// ---------------------------------------------------------------------------

struct RecentEntry
{
    std::string path;      // Absolute path to .spectra-ros-session file
    std::string node;      // node_name stored in the session
    std::string saved_at;  // ISO-8601 timestamp from the session file
};

// ---------------------------------------------------------------------------
// SaveResult / LoadResult
// ---------------------------------------------------------------------------

struct SaveResult
{
    bool        ok{false};
    std::string error;      // empty when ok == true
    std::string path;       // path written (same as input, resolved)

    explicit operator bool() const { return ok; }
};

struct LoadResult
{
    bool        ok{false};
    std::string error;
    std::string path;
    RosSession  session;

    explicit operator bool() const { return ok; }
};

// ---------------------------------------------------------------------------
// RosSessionManager — save / load / recent-list management
// ---------------------------------------------------------------------------

class RosSessionManager
{
public:
    // Maximum number of entries kept in the recent list.
    static constexpr int MAX_RECENT = 10;

    // Default file extension (without dot).
    static constexpr const char* SESSION_EXT = "spectra-ros-session";

    // ------------------------------------------------------------------
    // Construction
    // ------------------------------------------------------------------

    RosSessionManager();
    ~RosSessionManager() = default;

    // Non-copyable, non-movable.
    RosSessionManager(const RosSessionManager&)            = delete;
    RosSessionManager& operator=(const RosSessionManager&) = delete;
    RosSessionManager(RosSessionManager&&)                 = delete;
    RosSessionManager& operator=(RosSessionManager&&)      = delete;

    // ------------------------------------------------------------------
    // Save
    // ------------------------------------------------------------------

    // Serialize `session` to JSON and write to `path`.
    // Creates parent directories if they don't exist.
    // Updates the recent-sessions list on success.
    // Returns SaveResult with ok == true on success.
    SaveResult save(const RosSession& session, const std::string& path);

    // ------------------------------------------------------------------
    // Load
    // ------------------------------------------------------------------

    // Read and parse a .spectra-ros-session JSON file from `path`.
    // Returns LoadResult; on success result.session is populated.
    // Also updates the recent-sessions list to promote the entry.
    LoadResult load(const std::string& path);

    // ------------------------------------------------------------------
    // Auto-save
    // ------------------------------------------------------------------

    // Save `session` to the last-used path (set by previous save() or load()).
    // No-op if no last_path is set (returns ok=false, no error set).
    SaveResult auto_save(const RosSession& session);

    // Set / get the path used for the next auto_save().
    void        set_last_path(const std::string& path) { last_path_ = path; }
    const std::string& last_path() const               { return last_path_; }

    // ------------------------------------------------------------------
    // Recent sessions list
    // ------------------------------------------------------------------

    // Load the recent-sessions list from disk (~/.config/spectra/ros_recent.json).
    // Returns the list (or empty on I/O error).
    std::vector<RecentEntry> load_recent();

    // Save the current in-memory recent list to disk.
    // Called automatically after save() and load(); rarely needed directly.
    bool save_recent(const std::vector<RecentEntry>& entries);

    // Add or promote a path/node/saved_at entry to the front of the list.
    // Trims to MAX_RECENT. Saves to disk.
    void push_recent(const std::string& path,
                     const std::string& node,
                     const std::string& saved_at);

    // Remove an entry from the recent list by path.
    void remove_recent(const std::string& path);

    // Clear the entire recent list and remove from disk.
    void clear_recent();

    // ------------------------------------------------------------------
    // Paths
    // ------------------------------------------------------------------

    // Default recent-sessions list path: ~/.config/spectra/ros_recent.json
    static std::string default_recent_path();

    // Build a suggested save path for `node_name` in the default config dir.
    // E.g. ~/.config/spectra/sessions/<node_name>.spectra-ros-session
    static std::string default_session_path(const std::string& node_name);

    // ------------------------------------------------------------------
    // JSON helpers (public for testing)
    // ------------------------------------------------------------------

    // Serialize a RosSession to a JSON string (pretty-printed).
    static std::string serialize(const RosSession& session);

    // Deserialize a RosSession from a JSON string.
    // Returns false and sets error_out on failure.
    static bool deserialize(const std::string& json,
                            RosSession&        session_out,
                            std::string&       error_out);

    // Serialize / deserialize the recent list.
    static std::string  serialize_recent(const std::vector<RecentEntry>& entries);
    static std::vector<RecentEntry> deserialize_recent(const std::string& json);

    // Current UTC time as ISO-8601 string (e.g. "2026-03-05T20:30:00Z").
    static std::string current_iso8601();

    // Escape a string for embedding in JSON (handles \, ", control chars).
    static std::string json_escape(const std::string& s);

    // Parse a string value from a flat JSON object for a given key.
    // Returns empty string if key not found.
    static std::string json_get_string(const std::string& json,
                                       const std::string& key);

    // Parse an int value from a flat JSON object.
    // Returns default_val if key not found or not parseable.
    static int json_get_int(const std::string& json,
                            const std::string& key,
                            int                default_val = 0);

    // Parse a double value from a flat JSON object.
    static double json_get_double(const std::string& json,
                                  const std::string& key,
                                  double             default_val = 0.0);

    // Parse a bool value from a flat JSON object.
    static bool json_get_bool(const std::string& json,
                              const std::string& key,
                              bool               default_val = false);

private:
    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    // Create all parent directories for `path`.
    // Returns true on success (or if they already exist).
    static bool ensure_directory(const std::string& path);

    // Write `content` to `path` atomically (write to temp, rename).
    static bool write_file(const std::string& path, const std::string& content);

    // Read entire file into string. Returns false on error.
    static bool read_file(const std::string& path, std::string& content_out);

    // Build a JSON object string from key→value pairs.
    // Values already JSON-encoded (caller must quote strings).
    static std::string build_object(
        std::initializer_list<std::pair<std::string, std::string>> kv);

    // Serialize / deserialize individual nested structs.
    static std::string serialize_subscription(const SubscriptionEntry& e);
    static std::string serialize_expression(const ExpressionEntry& e);
    static std::string serialize_preset(const ExpressionPresetEntry& e);
    static std::string serialize_panels(const PanelVisibility& p);

    static SubscriptionEntry    deserialize_subscription(const std::string& json);
    static ExpressionEntry      deserialize_expression(const std::string& json);
    static ExpressionPresetEntry deserialize_preset(const std::string& json);
    static PanelVisibility      deserialize_panels(const std::string& json);

    // Extract a JSON array body (the content between the first '[' and last ']').
    // Returns empty string if not found.
    static std::string extract_array(const std::string& json, const std::string& key);

    // Split a JSON array body into individual object strings (balanced-brace).
    static std::vector<std::string> split_objects(const std::string& array_body);

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    std::string last_path_;   // last save() or load() path for auto_save()
};

}  // namespace spectra::adapters::ros2

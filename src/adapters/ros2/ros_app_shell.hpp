#pragma once

// RosAppShell — application shell for spectra-ros standalone executable (G1).
//
// Owns and wires together all ROS2 adapter panels into the default layout:
//
//   +--------------------+-----------------------------+------------------+
//   | TopicListPanel     | Plot area (SubplotManager)  | TopicStatsOverlay|
//   | (left dock)        | (center)                    | (right dock)     |
//   +--------------------+-----------------------------+------------------+
//   | TopicEchoPanel     (bottom dock)                                    |
//   +--------------------------------------------------------------------+
//
// The shell also owns:
//   - Ros2Bridge           — node lifecycle + executor thread
//   - TopicDiscovery       — periodic graph discovery
//   - MessageIntrospector  — schema cache (shared across panels)
//   - RosPlotManager       — per-topic single-figure plots
//   - SubplotManager       — NxM grid plots
//   - TopicListPanel       — topic monitor (B1)
//   - TopicEchoPanel       — message echo (B2)
//   - TopicStatsOverlay    — per-topic statistics (B3)
//
// Layout config (from CLI --layout option):
//   "default"   — topic list left, plot center, stats right, echo bottom
//   "plot-only" — plot center, no panels
//   "monitor"   — topic list left, echo center, stats right (no plot)
//
// Thread model:
//   - ROS2 executor runs on background thread (owned by Ros2Bridge)
//   - All draw*() methods must be called from the ImGui / render thread
//   - poll() must be called from the render thread once per frame
//   - TopicListPanel::notify_message() + TopicStatsOverlay::notify_message()
//     are forwarded from the GenericSubscriber message callback (executor thread)
//
// SIGINT:
//   The caller (main.cpp) installs a SIGINT handler that calls
//   RosAppShell::request_shutdown().  The render loop checks
//   shutdown_requested() each frame and exits cleanly.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "message_introspector.hpp"
#include "ros2_bridge.hpp"
#include "ros_plot_manager.hpp"
#include "subplot_manager.hpp"
#include "topic_discovery.hpp"
#include "ui/bag_info_panel.hpp"
#include "ui/diagnostics_panel.hpp"
#include "ros_screenshot_export.hpp"
#include "ros_session.hpp"
#include "ui/field_drag_drop.hpp"
#include "ui/log_viewer_panel.hpp"
#include "ui/topic_echo_panel.hpp"
#include "ui/topic_list_panel.hpp"
#include "ui/topic_stats_overlay.hpp"

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// LayoutMode
// ---------------------------------------------------------------------------

enum class LayoutMode
{
    Default,    // topic list left, plot center, stats right, echo bottom
    PlotOnly,   // plot center only
    Monitor,    // topic list left, echo center, stats right, no plot
};

// Parse a layout mode from a string ("default", "plot-only", "monitor").
// Returns LayoutMode::Default if the string is unrecognised.
LayoutMode parse_layout_mode(const std::string& s);
const char* layout_mode_name(LayoutMode m);

// ---------------------------------------------------------------------------
// RosAppConfig — parsed CLI configuration
// ---------------------------------------------------------------------------

struct RosAppConfig
{
    // Node / ROS2 identity.
    std::string node_name  = "spectra_ros";
    std::string node_ns    = "";

    // Initial topics to subscribe and plot immediately (--topics flag).
    // Each element is "topic_name" or "topic_name:field_path".
    std::vector<std::string> initial_topics;

    // Bag file to open immediately (--bag flag).  Empty = no bag.
    std::string bag_file;

    // Panel layout (--layout flag).
    LayoutMode layout = LayoutMode::Default;

    // Time window for auto-scroll (seconds).
    double time_window_s = 30.0;

    // Subplot grid dimensions for the plot area.
    int subplot_rows = 4;
    int subplot_cols = 1;

    // Application window dimensions (pixels).
    uint32_t window_width  = 1600;
    uint32_t window_height =  900;
};

// Parse CLI arguments into RosAppConfig.
// argc / argv should be the original main() arguments.
// Returns false and writes an error message to error_out if parsing fails.
// Unrecognised flags are silently forwarded to rclcpp::init() (argc/argv).
RosAppConfig parse_args(int argc, char** argv, std::string& error_out);

// ---------------------------------------------------------------------------
// RosAppShell — the application shell
// ---------------------------------------------------------------------------

class RosAppShell
{
public:
    // Construct with parsed configuration.
    // Does not initialise ROS2 or open any windows — call init() for that.
    explicit RosAppShell(const RosAppConfig& cfg);
    ~RosAppShell();

    // Non-copyable, non-movable.
    RosAppShell(const RosAppShell&)            = delete;
    RosAppShell& operator=(const RosAppShell&) = delete;
    RosAppShell(RosAppShell&&)                 = delete;
    RosAppShell& operator=(RosAppShell&&)      = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Initialise ROS2 bridge, start discovery, subscribe initial topics.
    // Returns false on failure (e.g. rclcpp::init error).
    bool init(int argc, char** argv);

    // Shutdown ROS2 bridge and all subscriptions.
    void shutdown();

    // Request shutdown from any thread (e.g. SIGINT handler).
    void request_shutdown() { shutdown_requested_.store(true, std::memory_order_relaxed); }

    // True once request_shutdown() has been called.
    bool shutdown_requested() const { return shutdown_requested_.load(std::memory_order_relaxed); }

    // -----------------------------------------------------------------------
    // Per-frame update (render thread)
    // -----------------------------------------------------------------------

    // Drain ring buffers, advance scroll controllers, refresh discovery.
    // Call once per frame before any draw*() methods.
    void poll();

    // -----------------------------------------------------------------------
    // ImGui draw methods (render thread, within valid ImGui frame)
    // -----------------------------------------------------------------------

    // Draw the full default layout.
    // Creates dockable ImGui windows for each panel.
    void draw();

    // Draw individual panels (for custom integration / testing).
    void draw_topic_list(bool* p_open = nullptr);
    void draw_topic_echo(bool* p_open = nullptr);
    void draw_topic_stats(bool* p_open = nullptr);
    void draw_plot_area(bool* p_open = nullptr);
    void draw_bag_info(bool* p_open = nullptr);
    void draw_log_viewer(bool* p_open = nullptr);
    void draw_diagnostics(bool* p_open = nullptr);
    void draw_status_bar();
    void draw_menu_bar();

    // -----------------------------------------------------------------------
    // Window title
    // -----------------------------------------------------------------------

    // Returns "Spectra ROS2 — <node_name>"
    std::string window_title() const;

    // -----------------------------------------------------------------------
    // Panel visibility toggles
    // -----------------------------------------------------------------------

    bool topic_list_visible() const  { return show_topic_list_; }
    bool topic_echo_visible() const  { return show_topic_echo_; }
    bool topic_stats_visible() const { return show_topic_stats_; }
    bool plot_area_visible() const   { return show_plot_area_; }
    bool bag_info_visible() const    { return show_bag_info_; }
    bool log_viewer_visible() const  { return show_log_viewer_; }
    bool diagnostics_visible() const { return show_diagnostics_; }

    void set_topic_list_visible(bool v)  { show_topic_list_  = v; }
    void set_topic_echo_visible(bool v)  { show_topic_echo_  = v; }
    void set_topic_stats_visible(bool v) { show_topic_stats_ = v; }
    void set_plot_area_visible(bool v)   { show_plot_area_   = v; }
    void set_bag_info_visible(bool v)    { show_bag_info_    = v; }
    void set_log_viewer_visible(bool v)  { show_log_viewer_  = v; }
    void set_diagnostics_visible(bool v) { show_diagnostics_ = v; }

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    Ros2Bridge&          bridge()          { return *bridge_; }
    TopicDiscovery&      discovery()       { return *discovery_; }
    MessageIntrospector& introspector()    { return *intr_; }
    RosPlotManager&      plot_manager()    { return *plot_mgr_; }
    SubplotManager&      subplot_manager() { return *subplot_mgr_; }
    TopicListPanel&      topic_list()      { return *topic_list_; }
    TopicEchoPanel&      topic_echo()      { return *topic_echo_; }
    TopicStatsOverlay&   topic_stats()     { return *topic_stats_; }
    BagInfoPanel&        bag_info()        { return *bag_info_; }
    RosLogViewer&        log_viewer()      { return *log_viewer_; }
    LogViewerPanel&      log_viewer_panel(){ return *log_viewer_panel_; }
    DiagnosticsPanel&    diagnostics()     { return *diag_panel_; }

    const RosAppConfig&  config() const    { return cfg_; }

    // Total number of messages received across all topics since init.
    uint64_t total_messages() const { return total_messages_.load(std::memory_order_relaxed); }

    // Total active subscriptions (plots).
    int active_plot_count() const;

    // -----------------------------------------------------------------------
    // Plot management helpers
    // -----------------------------------------------------------------------

    // Subscribe topic+field and add to the subplot grid.
    // topic_field may be "topic_name" or "topic_name:field_path".
    // Returns true on success.
    bool add_topic_plot(const std::string& topic_field);

    // Remove all plots.
    void clear_plots();

    // -----------------------------------------------------------------------
    // Session save / load (G3)
    // -----------------------------------------------------------------------

    // Build a RosSession snapshot from current shell state.
    RosSession capture_session() const;

    // Apply a loaded RosSession (re-subscribes topics, sets time window, etc.).
    // Safe to call before init() returns — will no-op on missing managers.
    void apply_session(const RosSession& session);

    // Save current session to `path`.  Updates last_path for auto-save.
    SaveResult save_session(const std::string& path);

    // Load session from `path` and apply it.
    LoadResult load_session(const std::string& path);

    // Return the session manager (for advanced access / testing).
    RosSessionManager& session_manager() { return *session_mgr_; }
    const RosSessionManager& session_manager() const { return *session_mgr_; }

    // -----------------------------------------------------------------------
    // Callbacks for inter-panel communication
    // -----------------------------------------------------------------------

    // Called when the user selects a topic in TopicListPanel.
    // Wires the selected topic into TopicEchoPanel and TopicStatsOverlay.
    // Type is resolved via TopicDiscovery internally.
    void on_topic_selected(const std::string& topic);

    // Called when the user double-clicks (plot) a topic in TopicListPanel.
    void on_topic_plot(const std::string& topic);

private:
    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    void subscribe_initial_topics();
    void wire_panel_callbacks();
    void handle_plot_request(const FieldDragPayload& payload, PlotTarget target);
    void setup_layout_visibility();

    // G3: session dialog helpers (ImGui, only called from draw()).
    void draw_session_save_dialog();
    void draw_session_load_dialog();

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    RosAppConfig cfg_;

    // Core ROS2 infrastructure.
    std::unique_ptr<Ros2Bridge>          bridge_;
    std::unique_ptr<TopicDiscovery>      discovery_;
    std::unique_ptr<MessageIntrospector> intr_;

    // Plot engines.
    std::unique_ptr<RosPlotManager>  plot_mgr_;
    std::unique_ptr<SubplotManager>  subplot_mgr_;

    // UI panels.
    std::unique_ptr<TopicListPanel>    topic_list_;
    std::unique_ptr<TopicEchoPanel>    topic_echo_;
    std::unique_ptr<TopicStatsOverlay> topic_stats_;
    std::unique_ptr<BagInfoPanel>      bag_info_;
    std::unique_ptr<RosLogViewer>      log_viewer_;
    std::unique_ptr<LogViewerPanel>    log_viewer_panel_;
    std::unique_ptr<DiagnosticsPanel>  diag_panel_;

    // Drag-and-drop controller (wired to topic_list_ and topic_echo_).
    std::unique_ptr<FieldDragDrop>         drag_drop_;

    // Screenshot / video export (E3).
    std::unique_ptr<RosScreenshotExport>   screenshot_export_;
    bool                                   show_record_dialog_ = false;

    // Session manager (G3).
    std::unique_ptr<RosSessionManager>     session_mgr_;
    bool                                   show_session_save_dialog_ = false;
    bool                                   show_session_load_dialog_ = false;
    std::string                            session_save_path_buf_;   // imgui input buffer
    std::string                            session_status_msg_;      // toast text
    float                                  session_status_timer_{0.0f};

    // Shutdown flag.
    std::atomic<bool> shutdown_requested_{false};

    // Panel visibility.
    bool show_topic_list_  = true;
    bool show_topic_echo_  = true;
    bool show_topic_stats_ = true;
    bool show_plot_area_   = true;
    bool show_bag_info_    = false;
    bool show_log_viewer_  = false;
    bool show_diagnostics_ = false;

    // Statistics.
    std::atomic<uint64_t> total_messages_{0};

    // Currently selected topic (for echo + stats).
    std::string selected_topic_;
    std::string selected_type_;
};

}   // namespace spectra::adapters::ros2

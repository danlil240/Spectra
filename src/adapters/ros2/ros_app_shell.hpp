#pragma once

// RosAppShell — application shell for spectra-ros standalone executable (G1).
//
// Owns and wires ROS2 adapter backends + panels into a dockspace-driven UI
// with a customizable optional nav rail. The shell is render-thread driven:
// poll() once per frame, then draw() from inside an active ImGui frame.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "bag_player.hpp"
#include "display/display_registry.hpp"
#include "message_introspector.hpp"
#include "ros2_bridge.hpp"
#include "ros_plot_manager.hpp"
#include "ros_screenshot_export.hpp"
#include "ros_session.hpp"
#include "service_caller.hpp"
#include "scene/scene_manager.hpp"
#include "subplot_manager.hpp"
#include "topic_discovery.hpp"

#include <ui/input/input.hpp>
#include "ui/bag_info_panel.hpp"
#include "ui/bag_playback_panel.hpp"
#include "ui/diagnostics_panel.hpp"
#include "ui/displays_panel.hpp"
#include "ui/field_drag_drop.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/log_viewer_panel.hpp"
#include "ui/node_graph_panel.hpp"
#include "ui/param_editor_panel.hpp"
#include "ui/scene_viewport.hpp"
#include "ui/service_caller_panel.hpp"
#include "ui/tf_tree_panel.hpp"
#include "ui/topic_echo_panel.hpp"
#include "ui/topic_list_panel.hpp"
#include "ui/topic_stats_overlay.hpp"

namespace spectra
{
class Figure;
class LayoutManager;
}

namespace spectra::adapters::ros2
{

enum class LayoutMode
{
    Default,    // monitor + plot + right stats/tools
    PlotOnly,   // central plot area only
    Monitor,    // monitor-focused panels, plot hidden
    RViz,       // scene viewport + displays panels
    RVizPlot,   // scene viewport + displays + plot area
};

LayoutMode parse_layout_mode(const std::string& s);
const char* layout_mode_name(LayoutMode m);
bool should_skip_debug_validation_for_ros_app(const char* no_validation_env,
                                              const char* enable_validation_env);
bool should_trim_vulkan_loader_environment_for_ros_app(const char* preserve_loader_env);

struct RosAppConfig
{
    std::string node_name = "spectra_ros";
    std::string node_ns   = "";

    // "topic" or "topic:field_path"
    std::vector<std::string> initial_topics;

    std::string bag_file;

    LayoutMode layout = LayoutMode::Default;

    double time_window_s = 30.0;

    int subplot_rows = 1;
    int subplot_cols = 1;

    uint32_t window_width  = 1600;
    uint32_t window_height = 900;
};

RosAppConfig parse_args(int argc, char** argv, std::string& error_out);

// ---------------------------------------------------------------------------
// RosWorkspaceState — global selection context shared across all panels.
//
// Owned by RosAppShell and reset once per frame.  Panels read this struct
// to react to user selection changes without coupling to each other.
// ---------------------------------------------------------------------------

struct RosWorkspaceState
{
    // Currently selected topic / type / field.
    std::string selected_topic;
    std::string selected_type;
    std::string selected_field;   // fully-qualified field path within the topic

    // Index of the subplot slot that should receive new series (-1 = auto).
    int active_subplot_idx = -1;

    // Current TF fixed frame for scene-space displays.
    std::string fixed_frame;

    // Per-frame event flags — set by shell actions, consumed during draw().
    bool selection_changed = false;   // topic or field changed this frame
    bool plot_requested    = false;   // user asked to add selected_field to plot

    // Select a topic; type is resolved by the shell via TopicDiscovery.
    // Marks selection_changed = true and resets selected_field.
    void select_topic(const std::string& topic, const std::string& type)
    {
        if (selected_topic == topic && selected_type == type)
            return;
        selected_topic    = topic;
        selected_type     = type;
        selected_field    = "";
        selection_changed = true;
    }

    void select_field(const std::string& field)
    {
        selected_field    = field;
        selection_changed = true;
    }

    void request_plot() { plot_requested = true; }

    void reset_events()
    {
        selection_changed = false;
        plot_requested    = false;
    }
};

class RosAppShell
{
public:
    explicit RosAppShell(const RosAppConfig& cfg);
    ~RosAppShell();

    RosAppShell(const RosAppShell&)            = delete;
    RosAppShell& operator=(const RosAppShell&) = delete;
    RosAppShell(RosAppShell&&)                 = delete;
    RosAppShell& operator=(RosAppShell&&)      = delete;

    // Optional: bind SubplotManager to the application's render figure.
    // Must be called before init().
    void set_canvas_figure(spectra::Figure* fig) { canvas_figure_ = fig; }

    // Optional: bind to the Spectra LayoutManager so we can override canvas_rect
    // to match the Plot Area docked panel's position.
    void set_layout_manager(spectra::LayoutManager* lm) { layout_manager_ = lm; }

    bool init(int argc, char** argv);
    void shutdown();

    void request_shutdown() { shutdown_requested_.store(true, std::memory_order_relaxed); }
    bool shutdown_requested() const { return shutdown_requested_.load(std::memory_order_relaxed); }

    void poll();

    void draw();

    void draw_topic_list(bool* p_open = nullptr);
    void draw_topic_echo(bool* p_open = nullptr);
    void draw_topic_stats(bool* p_open = nullptr);
    void draw_plot_area(bool* p_open = nullptr);
    void draw_bag_info(bool* p_open = nullptr);
    void draw_bag_playback(bool* p_open = nullptr);
    void draw_log_viewer(bool* p_open = nullptr);
    void draw_diagnostics(bool* p_open = nullptr);
    void draw_displays_panel(bool* p_open = nullptr);
    void draw_node_graph(bool* p_open = nullptr);
    void draw_scene_viewport(bool* p_open = nullptr);
    void draw_inspector_panel(bool* p_open = nullptr);
    void draw_tf_tree(bool* p_open = nullptr);
    void draw_param_editor(bool* p_open = nullptr);
    void draw_service_caller(bool* p_open = nullptr);
    void draw_status_bar();
    void draw_menu_bar();

    std::string window_title() const;

    bool topic_list_visible() const    { return show_topic_list_; }
    bool topic_echo_visible() const    { return show_topic_echo_; }
    bool topic_stats_visible() const   { return show_topic_stats_; }
    bool plot_area_visible() const     { return show_plot_area_; }
    bool bag_info_visible() const      { return show_bag_info_; }
    bool bag_playback_visible() const  { return show_bag_playback_; }
    bool log_viewer_visible() const    { return show_log_viewer_; }
    bool diagnostics_visible() const   { return show_diagnostics_; }
    bool displays_panel_visible() const { return show_displays_panel_; }
    bool node_graph_visible() const    { return show_node_graph_; }
    bool scene_viewport_visible() const { return show_scene_viewport_; }
    bool inspector_panel_visible() const { return show_inspector_panel_; }
    bool tf_tree_visible() const       { return show_tf_tree_; }
    bool param_editor_visible() const  { return show_param_editor_; }
    bool service_caller_visible() const { return show_service_caller_; }

    void set_topic_list_visible(bool v)     { show_topic_list_ = v; }
    void set_topic_echo_visible(bool v)     { show_topic_echo_ = v; }
    void set_topic_stats_visible(bool v)    { show_topic_stats_ = v; }
    void set_plot_area_visible(bool v)      { show_plot_area_ = v; }
    void set_bag_info_visible(bool v)       { show_bag_info_ = v; }
    void set_bag_playback_visible(bool v)   { show_bag_playback_ = v; }
    void set_log_viewer_visible(bool v)     { show_log_viewer_ = v; }
    void set_diagnostics_visible(bool v)    { show_diagnostics_ = v; }
    void set_displays_panel_visible(bool v) { show_displays_panel_ = v; }
    void set_node_graph_visible(bool v)     { show_node_graph_ = v; }
    void set_scene_viewport_visible(bool v) { show_scene_viewport_ = v; }
    void set_inspector_panel_visible(bool v) { show_inspector_panel_ = v; }
    void set_tf_tree_visible(bool v)        { show_tf_tree_ = v; }
    void set_param_editor_visible(bool v)   { show_param_editor_ = v; }
    void set_service_caller_visible(bool v) { show_service_caller_ = v; }

    bool nav_rail_visible() const { return show_nav_rail_; }
    bool nav_rail_expanded() const { return nav_rail_expanded_; }
    float nav_rail_width() const { return nav_rail_width_; }
    void set_nav_rail_visible(bool v) { show_nav_rail_ = v; }
    void set_nav_rail_expanded(bool v) { nav_rail_expanded_ = v; }
    void set_nav_rail_width(float px);

    Ros2Bridge&          bridge()          { return *bridge_; }
    TopicDiscovery&      discovery()       { return *discovery_; }
    MessageIntrospector& introspector()    { return *intr_; }
    RosPlotManager&      plot_manager()    { return *plot_mgr_; }
    SubplotManager&      subplot_manager() { return *subplot_mgr_; }

    // Automation / QA accessors. These expose non-owning pointers to shell-managed
    // components so external harnesses can verify end-to-end state without duplicating
    // the shell wiring. Callers must handle nullptr before init() / after shutdown().
    TopicListPanel*       topic_list_panel()       const { return topic_list_.get(); }
    TopicEchoPanel*       topic_echo_panel()       const { return topic_echo_.get(); }
    TopicStatsOverlay*    topic_stats_panel()      const { return topic_stats_.get(); }
    BagInfoPanel*         bag_info_panel()         const { return bag_info_.get(); }
    BagPlayer*            bag_player()             const { return bag_player_.get(); }
    RosLogViewer*         log_viewer()             const { return log_viewer_.get(); }
    DiagnosticsPanel*     diagnostics_panel()      const { return diag_panel_.get(); }
    DisplaysPanel*        displays_panel()         const { return displays_panel_.get(); }
    NodeGraphPanel*       node_graph_panel()       const { return node_graph_panel_.get(); }
    SceneViewport*        scene_viewport()         const { return scene_viewport_.get(); }
    InspectorPanel*       inspector_panel()        const { return inspector_panel_.get(); }
    TfTreePanel*          tf_tree_panel()          const { return tf_tree_panel_.get(); }
    ParamEditorPanel*     param_editor_panel()     const { return param_editor_.get(); }
    ServiceCaller*        service_caller()         const { return service_caller_.get(); }
    ServiceCallerPanel*   service_caller_panel()   const { return service_caller_panel_.get(); }
    RosScreenshotExport*  screenshot_export()      const { return screenshot_export_.get(); }

    const RosAppConfig& config() const { return cfg_; }

    uint64_t total_messages() const { return total_messages_.load(std::memory_order_relaxed); }

    int active_plot_count() const;

    bool add_topic_plot(const std::string& topic_field);
    void clear_plots();

    RosSession capture_session() const;
    void apply_session(const RosSession& session);
    SaveResult save_session(const std::string& path);
    LoadResult load_session(const std::string& path);

    // ------------------------------------------------------------------
    // Named layout presets
    // ------------------------------------------------------------------

    enum class LayoutPreset : uint8_t
    {
        Default   = 0,  // Topic List + Echo + Subplots + Stats
        Debug     = 1,  // Topic List + Echo + Log Viewer
        Monitor   = 2,  // 4x1 subplots + Diagnostics + Stats
        BagReview = 3,  // Bag Playback + Subplots
        RViz      = 4,  // Scene viewport + displays + TF tools
        RVizPlot  = 5,  // Scene viewport + displays + plots
    };

    // Apply a preset: adjusts panel visibility and calls setup_layout_visibility().
    // Resets dock layout so it rebuilds for the new panel set.
    void apply_layout_preset(LayoutPreset preset);

    LayoutPreset current_preset() const { return current_preset_; }

    static const char* layout_preset_name(LayoutPreset p);

    RosSessionManager& session_manager() { return *session_mgr_; }
    const RosSessionManager& session_manager() const { return *session_mgr_; }

    // topic_hint allows callers (e.g. BagInfoPanel) to supply the ROS type
    // directly when discovery may not have it yet.  Empty = auto-discover.
    void on_topic_selected(const std::string& topic,
                           const std::string& type_hint = "");
    void on_topic_plot(const std::string& topic);

    // Read-only access to the shared workspace selection context.
    const RosWorkspaceState& workspace() const { return workspace_state_; }
    const std::vector<std::unique_ptr<DisplayPlugin>>& displays() const { return displays_; }
    const std::string& fixed_frame() const { return workspace_state_.fixed_frame; }
    void set_fixed_frame(const std::string& frame_id) { workspace_state_.fixed_frame = frame_id; }
    SceneManager& scene_manager() { return scene_manager_; }

private:
    void subscribe_initial_topics();
    void wire_panel_callbacks();
    void handle_plot_request(const FieldDragPayload& payload, PlotTarget target);
    void setup_layout_visibility();
    void register_builtin_displays();
    void seed_default_rviz_displays_if_needed();
    void refresh_scene_displays(float dt);
    void draw_display_auxiliary_windows();

    void draw_dockspace();
    void apply_default_dock_layout();
    void draw_nav_rail();

    void draw_session_save_dialog();
    void draw_session_load_dialog();
    void draw_recent_sessions_menu();   // inline submenu items for "Recent"
    void draw_layout_preset_menu();     // inline submenu items for "Layout"
    void handle_plot_shortcuts();
    void reset_plot_display(int slot = -1);
    void restore_plot_autofit(int slot = -1);
    void remember_active_subplot(int slot);
    void begin_manual_y_tracking(int slot);
    void finish_manual_y_tracking(int slot);

    // Bridge ImGui mouse/scroll events to the core InputHandler.
    // Handles ROS2-specific live-mode time-window scroll zoom.
    void bridge_imgui_to_input_handler();

    // Find which subplot slot the mouse cursor is over (1-based, -1 if none).
    int hit_test_subplot_slot(float mx, float my, bool include_y_gutter = false) const;

    std::string detect_topic_type(const std::string& topic) const;
    std::string default_numeric_field(const std::string& topic,
                                      const std::string& type_hint) const;

    RosAppConfig cfg_;

    std::unique_ptr<Ros2Bridge>          bridge_;
    std::unique_ptr<TopicDiscovery>      discovery_;
    std::unique_ptr<MessageIntrospector> intr_;

    std::unique_ptr<RosPlotManager>  plot_mgr_;
    std::unique_ptr<SubplotManager>  subplot_mgr_;

    std::unique_ptr<TopicListPanel>      topic_list_;
    std::unique_ptr<TopicEchoPanel>      topic_echo_;
    std::unique_ptr<TopicStatsOverlay>   topic_stats_;
    std::unique_ptr<BagInfoPanel>        bag_info_;
    std::unique_ptr<BagPlayer>           bag_player_;
    std::unique_ptr<BagPlaybackPanel>    bag_playback_panel_;
    std::unique_ptr<RosLogViewer>        log_viewer_;
    std::unique_ptr<LogViewerPanel>      log_viewer_panel_;
    std::unique_ptr<DiagnosticsPanel>    diag_panel_;
    std::unique_ptr<DisplaysPanel>       displays_panel_;
    std::unique_ptr<NodeGraphPanel>      node_graph_panel_;
    std::unique_ptr<SceneViewport>       scene_viewport_;
    std::unique_ptr<InspectorPanel>      inspector_panel_;
    std::unique_ptr<TfTreePanel>         tf_tree_panel_;
    std::unique_ptr<ParamEditorPanel>    param_editor_;
    std::unique_ptr<ServiceCaller>       service_caller_;
    std::unique_ptr<ServiceCallerPanel>  service_caller_panel_;

    std::unique_ptr<FieldDragDrop>       drag_drop_;

    std::unique_ptr<RosScreenshotExport> screenshot_export_;
    bool                                 show_record_dialog_ = false;

    std::unique_ptr<RosSessionManager> session_mgr_;
    bool                               show_session_save_dialog_ = false;
    bool                               show_session_load_dialog_ = false;
    std::string                        session_save_path_buf_;
    std::string                        session_status_msg_;
    float                              session_status_timer_{0.0f};

    std::atomic<bool> shutdown_requested_{false};

    bool show_topic_list_      = true;
    bool show_topic_echo_      = false;
    bool show_topic_stats_     = true;
    bool show_plot_area_       = true;
    bool plot_area_was_visible_= true;  // tracks previous frame for close detection
    bool show_bag_info_        = false;
    bool show_bag_playback_    = false;
    bool show_log_viewer_      = false;
    bool show_diagnostics_     = false;
    bool show_displays_panel_  = false;
    bool show_node_graph_      = false;
    bool show_scene_viewport_  = false;
    bool show_inspector_panel_ = false;
    bool show_tf_tree_         = false;
    bool show_param_editor_    = false;
    bool show_service_caller_  = false;

    // Shell nav rail (Spectra-ROS specific, independent from core Spectra rail).
    bool  show_nav_rail_       = true;
    bool  nav_rail_expanded_   = false;
    float nav_rail_width_      = 220.0f;
    float nav_rail_collapsed_w_ = 52.0f;

    // Dockspace state.
    bool dock_layout_initialized_ = false;

    // Optional external render figure for subplot manager integration.
    spectra::Figure* canvas_figure_ = nullptr;

    // Optional layout manager (owned by ImGuiIntegration; lifetime >= shell).
    spectra::LayoutManager* layout_manager_ = nullptr;

    // Lightweight per-topic subscriptions for Hz/BW monitoring.
    // Created automatically for every discovered topic so the Topic Monitor
    // shows Hz and bandwidth columns for all topics, not just plotted ones.
    std::mutex monitor_subs_mutex_;
    std::unordered_map<std::string,
                       rclcpp::GenericSubscription::SharedPtr> monitor_subs_;

    std::atomic<uint64_t> total_messages_{0};

    // Centralised selection context — reset each frame in draw().
    RosWorkspaceState workspace_state_;

    DisplayRegistry display_registry_;
    SceneManager scene_manager_;
    std::vector<std::unique_ptr<DisplayPlugin>> displays_;
    std::unordered_map<DisplayPlugin*, bool> display_activation_state_;
    std::string last_display_context_fixed_frame_;

    int next_replace_slot_ = 1;

    // Core input handler — replaces reimplemented pan/zoom/scroll handlers.
    InputHandler input_handler_;

    // Tracks previous ImGui mouse button state for edge detection.
    bool prev_mouse_left_  = false;
    bool prev_mouse_right_ = false;

    // Track drag-start Y limits so manual zoom/pan disables live auto-fit only
    // when the user actually changed Y.
    int                tracked_manual_y_slot_ = -1;
    spectra::AxisLimits tracked_manual_y_limits_{};
    bool               tracked_manual_y_valid_ = false;

    // Active layout preset.
    LayoutPreset current_preset_ = LayoutPreset::Default;

    // For per-frame dt in poll()
    double last_poll_time_s_ = 0.0;

    // Layout persistence: cached ini updated each frame via WantSaveIniSettings;
    // pending ini is applied before the next DockSpace() call after a load.
    std::string cached_imgui_ini_;
    std::string pending_imgui_ini_;

#ifdef SPECTRA_USE_IMGUI
    unsigned int dockspace_id_ = 0;
#endif
};

}   // namespace spectra::adapters::ros2

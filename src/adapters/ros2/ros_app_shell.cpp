#include "ros_app_shell.hpp"

#include <spectra/logger.hpp>

#include "ros_app_shell_drop_targets.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include "display/grid_display.hpp"
#include "display/image_display.hpp"
#include "display/laserscan_display.hpp"
#include "display/marker_display.hpp"
#include "display/occupancy_grid_display.hpp"
#include "display/path_display.hpp"
#include "display/pointcloud_display.hpp"
#include "display/pose_display.hpp"
#include "display/robot_model_display.hpp"
#include "display/tf_display.hpp"
#include "ui/layout/layout_manager.hpp"
#include "ui/theme/icons.hpp"

// AxisLinkManager — needed for wiring InputHandler to subplot link manager.
#include "ui/data/axis_link.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #ifdef IMGUI_HAS_DOCK
        #include <imgui_internal.h>
    #endif
#endif

namespace spectra::adapters::ros2
{

namespace
{
bool axis_limits_changed(const spectra::AxisLimits& a,
                         const spectra::AxisLimits& b,
                         double                     eps = 1e-6)
{
    return std::abs(a.min - b.min) > eps || std::abs(a.max - b.max) > eps;
}

bool env_flag_is_truthy(const char* value)
{
    if (!value || value[0] == '\0')
        return false;

    const std::string_view text(value);
    return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "YES"
           || text == "on" || text == "ON";
}

constexpr float kPlotAreaMinWindowWidth     = 900.0f;
constexpr float kPlotAreaMinWindowHeight    = 200.0f;
constexpr float kPlotAreaTimeSliderMinWidth = 120.0f;
constexpr float kPlotAreaTimeSliderMaxWidth = 220.0f;
constexpr float kPlotAreaMinViewportHeight  = 120.0f;
constexpr float kPlotAreaButtonSpacing      = 4.0f;
}   // namespace

LayoutMode parse_layout_mode(const std::string& s)
{
    if (s == "plot-only")
        return LayoutMode::PlotOnly;
    if (s == "monitor")
        return LayoutMode::Monitor;
    if (s == "rviz")
        return LayoutMode::RViz;
    if (s == "rviz-plot")
        return LayoutMode::RVizPlot;
    return LayoutMode::Default;
}

const char* layout_mode_name(LayoutMode m)
{
    switch (m)
    {
        case LayoutMode::Default:
            return "default";
        case LayoutMode::PlotOnly:
            return "plot-only";
        case LayoutMode::Monitor:
            return "monitor";
        case LayoutMode::RViz:
            return "rviz";
        case LayoutMode::RVizPlot:
            return "rviz-plot";
    }
    return "default";
}

bool should_skip_debug_validation_for_ros_app(const char* no_validation_env,
                                              const char* enable_validation_env)
{
    if (no_validation_env != nullptr)
        return env_flag_is_truthy(no_validation_env);

    if (enable_validation_env != nullptr)
        return !env_flag_is_truthy(enable_validation_env);

    return true;
}

bool should_trim_vulkan_loader_environment_for_ros_app(const char* preserve_loader_env)
{
    return !env_flag_is_truthy(preserve_loader_env);
}

bool is_rviz_layout(LayoutMode layout)
{
    return layout == LayoutMode::RViz || layout == LayoutMode::RVizPlot;
}

RosAppConfig parse_args(int argc, char** argv, std::string& error_out)
{
    RosAppConfig cfg;
    error_out.clear();

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);

        if (arg == "--help" || arg == "-h")
        {
            error_out =
                "Usage: spectra-ros [--topics TOPIC[:FIELD] ...] [--bag BAG_FILE] "
                "[--session FILE.spectra-ros-session] "
                "[--layout default|plot-only|monitor|rviz|rviz-plot] [--window-s SECONDS] "
                "[--rate R] [--loop true|false] "
                "[--node-name NAME] [--rows N] [--cols N]\n";
            return cfg;
        }

        if (arg == "--topics" || arg == "-t")
        {
            while (i + 1 < argc && argv[i + 1][0] != '-')
            {
                ++i;
                cfg.initial_topics.emplace_back(argv[i]);
            }
            continue;
        }

        if ((arg == "--bag" || arg == "-b") && i + 1 < argc)
        {
            cfg.bag_file = argv[++i];
            continue;
        }

        if (arg == "--rate" && i + 1 < argc)
        {
            try
            {
                cfg.bag_rate = std::stod(argv[++i]);
            }
            catch (...)
            {
                error_out = "Invalid --rate value";
                return cfg;
            }
            continue;
        }

        if (arg == "--loop" && i + 1 < argc)
        {
            const std::string v = argv[++i];
            cfg.bag_loop        = (v == "1" || v == "true" || v == "TRUE" || v == "yes");
            continue;
        }

        if ((arg == "--session" || arg == "-s") && i + 1 < argc)
        {
            cfg.session_file = argv[++i];
            continue;
        }

        if ((arg == "--layout" || arg == "-l") && i + 1 < argc)
        {
            cfg.layout = parse_layout_mode(argv[++i]);
            continue;
        }

        if ((arg == "--window-s" || arg == "-w") && i + 1 < argc)
        {
            try
            {
                cfg.time_window_s = std::stod(argv[++i]);
            }
            catch (...)
            {
                error_out = "Invalid --window-s value";
                return cfg;
            }
            continue;
        }

        if ((arg == "--node-name" || arg == "-n") && i + 1 < argc)
        {
            cfg.node_name = argv[++i];
            continue;
        }

        if (arg == "--rows" && i + 1 < argc)
        {
            try
            {
                cfg.subplot_rows = std::stoi(argv[++i]);
            }
            catch (...)
            {
                error_out = "Invalid --rows value";
                return cfg;
            }
            continue;
        }

        if (arg == "--cols" && i + 1 < argc)
        {
            try
            {
                cfg.subplot_cols = std::stoi(argv[++i]);
            }
            catch (...)
            {
                error_out = "Invalid --cols value";
                return cfg;
            }
            continue;
        }
    }

    if (cfg.subplot_rows < 1)
        cfg.subplot_rows = 1;
    if (cfg.subplot_cols < 1)
        cfg.subplot_cols = 1;
    if (cfg.time_window_s < RosPlotManager::MIN_WINDOW_S)
        cfg.time_window_s = RosPlotManager::MIN_WINDOW_S;
    if (cfg.time_window_s > RosPlotManager::MAX_WINDOW_S)
        cfg.time_window_s = RosPlotManager::MAX_WINDOW_S;

    return cfg;
}

RosAppShell::RosAppShell(const RosAppConfig& cfg) : cfg_(cfg)
{
    screenshot_export_     = std::make_unique<RosScreenshotExport>();
    session_mgr_           = std::make_unique<RosSessionManager>();
    session_save_path_buf_ = RosSessionManager::default_session_path(cfg_.node_name);
    session_mgr_->set_last_path(session_save_path_buf_);
}

RosAppShell::~RosAppShell()
{
    shutdown();
}

void RosAppShell::set_nav_rail_width(float px)
{
    nav_rail_width_ = std::clamp(px, 180.0f, 360.0f);
}

bool RosAppShell::init(int argc, char** argv)
{
    bridge_ = std::make_unique<Ros2Bridge>();
    intr_   = std::make_unique<MessageIntrospector>();

    if (!bridge_->init(cfg_.node_name, cfg_.node_ns, argc, argv))
        return false;

    discovery_ = std::make_unique<TopicDiscovery>(bridge_->node());
    discovery_->set_self_node_name(bridge_->node()->get_fully_qualified_name());

    plot_mgr_    = std::make_unique<RosPlotManager>(*bridge_, *intr_);
    subplot_mgr_ = std::make_unique<SubplotManager>(*bridge_,
                                                    *intr_,
                                                    cfg_.subplot_rows,
                                                    cfg_.subplot_cols,
                                                    canvas_figure_);
    subplot_mgr_->set_time_window(cfg_.time_window_s);
    plot_mgr_->set_time_window(cfg_.time_window_s);

    plot_mgr_->set_topic_discovery(discovery_.get());
    subplot_mgr_->set_topic_discovery(discovery_.get());

    // Wire the core InputHandler to the SubplotManager's figure so all
    // pan/zoom/scroll interactions use the existing, well-tested code path
    // (with animations, inertia, undo, axis linking, etc.) instead of the
    // manual reimplementations that were here before.
    input_handler_.set_figure(&subplot_mgr_->figure());
    input_handler_.set_axis_link_manager(&subplot_mgr_->link_manager());

    topic_list_  = std::make_unique<TopicListPanel>();
    topic_stats_ = std::make_unique<TopicStatsOverlay>();
    topic_echo_  = std::make_unique<TopicEchoPanel>(bridge_->node(), *intr_);

    topic_list_->set_title("Topic Monitor");
    topic_stats_->set_title("Topic Statistics");
    topic_stats_->set_subplot_manager(subplot_mgr_.get());
    topic_echo_->set_title("Topic Echo");

    bag_info_ = std::make_unique<BagInfoPanel>();
    bag_info_->set_title("Bag Info");

    bag_player_        = std::make_unique<BagPlayer>(*plot_mgr_, *intr_);
    bag_display_sync_  = std::make_unique<BagDisplaySync>();
    bag_player_->set_subplot_manager(subplot_mgr_.get());
    bag_playback_panel_ = std::make_unique<BagPlaybackPanel>(bag_player_.get());
    bag_playback_panel_->set_title("Bag Playback");

    log_viewer_ = std::make_unique<RosLogViewer>(bridge_->node());
    log_viewer_->subscribe();
    log_viewer_panel_ = std::make_unique<LogViewerPanel>(*log_viewer_);
    log_viewer_panel_->set_title("ROS2 Log");

    diag_panel_ = std::make_unique<DiagnosticsPanel>();
    diag_panel_->set_title("Diagnostics");
    diag_panel_->set_node(bridge_->node().get());
    diag_panel_->start();

    displays_panel_ = std::make_unique<DisplaysPanel>();
    displays_panel_->set_title("Displays");

    node_graph_panel_ = std::make_unique<NodeGraphPanel>();
    node_graph_panel_->set_title("Node Graph");
    node_graph_panel_->set_topic_discovery(discovery_.get());

    scene_viewport_ = std::make_unique<SceneViewport>();
    scene_viewport_->set_title("Scene Viewport");
    inspector_panel_ = std::make_unique<InspectorPanel>();
    inspector_panel_->set_title("Inspector");

    tf_tree_panel_ = std::make_unique<TfTreePanel>();
    tf_tree_panel_->set_title("TF Tree");
    tf_tree_panel_->set_node(bridge_->node());
    tf_tree_panel_->start();

    param_editor_ = std::make_unique<ParamEditorPanel>(bridge_->node());
    param_editor_->set_title("Parameter Editor");
    param_editor_->set_live_edit(true);

    service_caller_ =
        std::make_unique<ServiceCaller>(bridge_->node(), intr_.get(), discovery_.get());
    service_caller_panel_ = std::make_unique<ServiceCallerPanel>(service_caller_.get());
    service_caller_panel_->set_title("Service Caller");

    register_builtin_displays();

    bag_info_->set_topic_select_callback(
        [this](const std::string& topic, const std::string& type)
        {
            // Pass the bag-provided type directly so discovery is not needed.
            on_topic_selected(topic, type);
        });
    bag_info_->set_topic_plot_callback([this](const std::string& topic, const std::string& type)
                                       { add_topic_plot(topic, type); });
    bag_info_->set_bag_opened_callback(
        [this](const std::string& path)
        {
            if (bag_player_ && bag_player_->open(path))
            {
                on_bag_opened(path);

                // Auto-create subplots for the first few bag topics so the
                // user sees data immediately when pressing play.
                if (bag_info_)
                {
                    const auto&      topics   = bag_info_->topics();
                    constexpr size_t MAX_AUTO = 4;
                    size_t           added    = 0;
                    for (const auto& row : topics)
                    {
                        if (added >= MAX_AUTO)
                            break;
                        if (add_topic_plot(row.name, row.type))
                            ++added;
                    }
                }
            }
            else if (bag_player_)
            {
                session_status_msg_ = std::string("Bag open failed: ") + bag_player_->last_error();
                session_status_timer_ = 3.0f;
            }
        });

    topic_list_->set_topic_discovery(discovery_.get());

    discovery_->set_topic_callback(
        [this](const TopicInfo& info, bool added)
        {
            std::lock_guard<std::mutex> lk(pending_monitor_subs_mutex_);
            pending_monitor_subs_.emplace_back(info, added);
        });

    plot_mgr_->set_on_data(
        [this](int id, double /*t*/, double /*v*/)
        {
            const PlotHandle h = plot_mgr_->handle(id);
            if (!h.valid())
                return;
            bool has_monitor = false;
            {
                std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                has_monitor = monitor_subs_.contains(h.topic);
            }
            if (!has_monitor)
            {
                ++total_messages_;
                if (topic_list_)
                    topic_list_->notify_message(h.topic, sizeof(double));
                if (topic_stats_)
                    topic_stats_->notify_message(h.topic, sizeof(double), -1);
            }
        });

    subplot_mgr_->set_on_data(
        [this](int slot, double /*t*/, double /*v*/)
        {
            const SubplotHandle h = subplot_mgr_->handle(slot);
            if (!h.valid())
                return;
            bool has_monitor = false;
            {
                std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                has_monitor = monitor_subs_.contains(h.topic);
            }
            if (!has_monitor)
            {
                ++total_messages_;
                if (topic_list_)
                    topic_list_->notify_message(h.topic, sizeof(double));
                if (topic_stats_)
                    topic_stats_->notify_message(h.topic, sizeof(double), -1);
            }
        });

    wire_panel_callbacks();
    wire_bag_time_clock();
    setup_layout_visibility();
    seed_default_rviz_displays_if_needed();

    bridge_->start_spin();
    discovery_->start();

    subscribe_initial_topics();

    if (!cfg_.bag_file.empty())
    {
        bag_info_->open_bag(cfg_.bag_file);
        show_bag_info_ = true;
        if (bag_player_ && bag_player_->open(cfg_.bag_file))
            on_bag_opened(cfg_.bag_file);
    }

    if (!cfg_.session_file.empty())
    {
        const LoadResult lr = load_session(cfg_.session_file);
        if (!lr.ok)
        {
            SPECTRA_LOG_WARN("ros",
                             "failed to load session '{}': {}",
                             cfg_.session_file,
                             lr.error);
        }
    }

    return true;
}

void RosAppShell::detach_canvas_figure()
{
    if (subplot_mgr_)
        subplot_mgr_->detach_external_figure();
    canvas_figure_ = nullptr;
}

void RosAppShell::shutdown()
{
    if (!bridge_)
        return;

    if (session_mgr_ && !session_mgr_->last_path().empty())
        session_mgr_->auto_save(capture_session());

    if (discovery_)
    {
        discovery_->set_topic_callback(nullptr);
        discovery_->stop();
    }

    // Tear down ROS subscriptions while the executor is still running.
    clear_monitor_subs();
    if (diag_panel_)
        diag_panel_->stop();
    if (tf_tree_panel_)
        tf_tree_panel_->stop();
    if (log_viewer_)
        log_viewer_->unsubscribe();
    for (auto& display : displays_)
    {
        if (display)
            display->on_destroy();
    }
    display_activation_state_.clear();
    displays_.clear();
    scene_manager_.clear();
    subplot_mgr_.reset();
    plot_mgr_.reset();
    topic_echo_.reset();

    bridge_->stop_spin();

    bag_playback_panel_.reset();
    bag_player_.reset();
    service_caller_panel_.reset();
    service_caller_.reset();
    param_editor_.reset();
    tf_tree_panel_.reset();
    inspector_panel_.reset();
    node_graph_panel_.reset();
    diag_panel_.reset();
    log_viewer_panel_.reset();
    log_viewer_.reset();
    bag_info_.reset();
    scene_viewport_.reset();
    displays_panel_.reset();

    topic_stats_.reset();
    topic_list_.reset();
    drag_drop_.reset();

    discovery_.reset();

    bridge_->shutdown();
    bridge_.reset();
    intr_.reset();
}

void RosAppShell::drain_pending_monitor_subs()
{
    std::vector<std::pair<TopicInfo, bool>> pending;
    {
        std::lock_guard<std::mutex> lk(pending_monitor_subs_mutex_);
        pending.swap(pending_monitor_subs_);
    }

    for (const auto& [info, added] : pending)
        apply_monitor_sub_change(info, added);
}

void RosAppShell::apply_monitor_sub_change(const TopicInfo& info, bool added)
{
    if (added)
    {
        if (info.types.empty())
            return;

        auto node = bridge_->node();
        if (!node)
            return;

        auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
        const std::string topic = info.name;
        const std::string type  = info.types.front();
        try
        {
            auto sub = node->create_generic_subscription(
                info.name,
                type,
                qos,
                [this, topic](const std::shared_ptr<rclcpp::SerializedMessage>& msg)
                {
                    const size_t bytes = msg ? msg->size() : 0;
                    ++total_messages_;
                    if (topic_list_)
                        topic_list_->notify_message(topic, bytes);
                    if (topic_stats_)
                        topic_stats_->notify_message(topic, bytes, -1);
                });
            std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
            monitor_subs_[info.name] = std::move(sub);
        }
        catch (const std::exception&)
        {
        }
    }
    else
    {
        std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
        monitor_subs_.erase(info.name);
    }
}

void RosAppShell::clear_monitor_subs()
{
    {
        std::lock_guard<std::mutex> lk(pending_monitor_subs_mutex_);
        pending_monitor_subs_.clear();
    }

    std::vector<rclcpp::GenericSubscription::SharedPtr> subs;
    {
        std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
        subs.reserve(monitor_subs_.size());
        for (auto& [_, sub] : monitor_subs_)
            subs.push_back(std::move(sub));
        monitor_subs_.clear();
    }

    for (auto& sub : subs)
        sub.reset();
}

void RosAppShell::poll()
{
    if (!bridge_)
        return;

    if (!rclcpp::ok())
    {
        request_shutdown();
        return;
    }

    drain_pending_monitor_subs();

    const double now_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    // Wall-clock time for scroll controllers (must be system_clock to match
    // ROS2 message timestamps which use wall time or header.stamp).
    const double wall_now_s =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();

    double dt = 1.0 / 60.0;
    if (last_poll_time_s_ > 0.0)
        dt = std::clamp(now_s - last_poll_time_s_, 1.0 / 300.0, 0.25);
    last_poll_time_s_ = now_s;

    if (subplot_mgr_)
    {
        if (bag_player_ && bag_player_->is_open())
        {
            if (!workspace_state_.clock.is_bag_mode())
                on_bag_opened(bag_player_->bag_path());

            const double ph = bag_player_->playhead_sec();
            workspace_state_.clock.update_bag_transport(ph,
                                                        bag_player_->is_playing(),
                                                        bag_player_->rate());
            subplot_mgr_->set_now(ph);
            if (topic_echo_)
                topic_echo_->set_bag_playhead(ph);
        }
        else
        {
            if (!subplot_mgr_->has_shared_time_origin())
                subplot_mgr_->set_shared_time_origin(wall_now_s);
            subplot_mgr_->set_now(wall_now_s);
        }
    }

    if (plot_mgr_)
        plot_mgr_->poll();

    if (subplot_mgr_)
        subplot_mgr_->poll();

    if (bag_player_)
        bag_player_->advance(dt);

    if (node_graph_panel_)
        node_graph_panel_->tick(static_cast<float>(dt));

    if (diag_panel_)
        diag_panel_->poll();

    if (screenshot_export_)
        screenshot_export_->tick(static_cast<float>(dt));

    refresh_scene_displays(static_cast<float>(dt));

    // Advance InputHandler animations (inertial pan, animated zoom, etc.).
    input_handler_.update(static_cast<float>(dt));

    if (session_status_timer_ > 0.0f)
        session_status_timer_ = std::max(0.0f, session_status_timer_ - static_cast<float>(dt));
}

void RosAppShell::draw()
{
#ifdef SPECTRA_USE_IMGUI
    if (shutdown_requested())
        return;

    if (scene_viewport_)
        scene_viewport_->invalidate_canvas_rect();

    // Reset per-frame workspace events so panels see a clean state.
    workspace_state_.reset_events();

    #ifdef IMGUI_HAS_DOCK
    // Cache ImGui layout whenever ImGui requests a settings save.
    // This lets capture_session() include layout data without calling
    // SaveIniSettingsToMemory at shutdown (when ImGui may already be torn down).
    if (ImGui::GetIO().WantSaveIniSettings)
    {
        size_t      ini_sz  = 0;
        const char* ini_ptr = ImGui::SaveIniSettingsToMemory(&ini_sz);
        if (ini_ptr && ini_sz > 0)
            cached_imgui_ini_.assign(ini_ptr, ini_sz);
        if (layout_change_tracking_enabled_)
            layout_unsaved_ = true;
        ImGui::GetIO().WantSaveIniSettings = false;
    }

    if (dock_layout_initialized_ && !layout_change_tracking_enabled_)
    {
        if (++layout_settle_frames_ >= 3)
            layout_change_tracking_enabled_ = true;
    }
    #endif

    draw_menu_bar();
    draw_nav_rail();
    draw_dockspace();

    if (show_topic_list_)
    {
        draw_topic_list(&show_topic_list_);
    }
    if (show_plot_area_)
    {
        draw_plot_area(&show_plot_area_);
    }
    else if (layout_manager_)
    {
        layout_manager_->clear_canvas_override();
    }

    // Issue 1: When the Plot Area panel is closed (X button), clear all subplots
    // so they don't remain active in the background.
    if (plot_area_was_visible_ && !show_plot_area_)
    {
        if (subplot_mgr_)
            subplot_mgr_->clear();
    }
    plot_area_was_visible_ = show_plot_area_;
    if (show_topic_stats_)
    {
        draw_topic_stats(&show_topic_stats_);
    }
    if (show_node_graph_)
    {
        draw_node_graph(&show_node_graph_);
    }
    if (show_topic_echo_)
    {
        draw_topic_echo(&show_topic_echo_);
    }
    if (show_bag_info_)
    {
        draw_bag_info(&show_bag_info_);
    }
    if (show_log_viewer_)
    {
        draw_log_viewer(&show_log_viewer_);
    }

    if (show_bag_playback_)
    {
        draw_bag_playback(&show_bag_playback_);
    }
    if (show_diagnostics_)
    {
        draw_diagnostics(&show_diagnostics_);
    }
    if (show_displays_panel_)
    {
        draw_displays_panel(&show_displays_panel_);
    }
    if (show_scene_viewport_)
    {
        draw_scene_viewport(&show_scene_viewport_);
    }
    if (show_inspector_panel_)
    {
        draw_inspector_panel(&show_inspector_panel_);
    }
    if (show_tf_tree_)
    {
        draw_tf_tree(&show_tf_tree_);
    }
    if (show_param_editor_)
    {
        draw_param_editor(&show_param_editor_);
    }
    if (show_service_caller_)
    {
        draw_service_caller(&show_service_caller_);
    }

    if (bag_player_ && bag_player_->is_open())
        draw_unified_transport_bar();

    if (drag_drop_)
    {
        FieldDragPayload p;
        PlotTarget       t = PlotTarget::CurrentAxes;
        if (drag_drop_->consume_pending_request(p, t) && p.valid())
            handle_plot_request(p, t);
    }

    // Handle "Add to Plot" requests raised from the Plot Area or menu.
    if (workspace_state_.plot_requested && !workspace_state_.selected_topic.empty())
    {
        std::string topic_field = workspace_state_.selected_topic;
        if (!workspace_state_.selected_field.empty())
            topic_field += ':' + workspace_state_.selected_field;
        add_topic_plot(topic_field);
    }

    draw_display_auxiliary_windows();
    draw_status_bar();

    if (show_record_dialog_ && screenshot_export_)
        screenshot_export_->draw_record_dialog(&show_record_dialog_);

    if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
        && (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift))
        && ImGui::IsKeyPressed(ImGuiKey_S, false) && screenshot_export_)
    {
        const std::string path = RosScreenshotExport::make_screenshot_path("/tmp", "spectra_ros");
        screenshot_export_->take_screenshot(path);
    }

    draw_session_save_dialog();
    draw_session_load_dialog();
    draw_session_merge_dialog();

    if (session_status_timer_ > 0.0f && !session_status_msg_.empty())
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + vp->WorkSize.y - 52.0f),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.82f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
    #ifdef IMGUI_HAS_DOCK
            ImGuiWindowFlags_NoDocking |
    #endif
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ros_session_toast", nullptr, flags))
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f),
                               "%s",
                               session_status_msg_.c_str());
        ImGui::End();
    }

    if (screenshot_export_ && screenshot_export_->screenshot_toast_active())
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + vp->WorkSize.y - 12.0f),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.78f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
    #ifdef IMGUI_HAS_DOCK
            ImGuiWindowFlags_NoDocking |
    #endif
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ros_ss_toast", nullptr, flags))
        {
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "Screenshot saved");
            const auto& p = screenshot_export_->last_screenshot_path();
            if (!p.empty())
            {
                const auto  sep  = p.rfind('/');
                const char* name = (sep != std::string::npos) ? p.c_str() + sep + 1 : p.c_str();
                ImGui::TextDisabled("%s", name);
            }
        }
        ImGui::End();
    }
#endif
}

void RosAppShell::draw_dockspace()
{
#ifdef SPECTRA_USE_IMGUI
    #ifdef IMGUI_HAS_DOCK
    // Restore a pending ImGui layout BEFORE the DockSpace() call; ImGui
    // requires LoadIniSettingsFromMemory to be called before the first
    // DockSpace frame on which the layout should take effect.
    if (!pending_imgui_ini_.empty())
    {
        ImGui::LoadIniSettingsFromMemory(pending_imgui_ini_.c_str(), pending_imgui_ini_.size());
        pending_imgui_ini_.clear();
    }

    const ImGuiViewport* vp       = ImGui::GetMainViewport();
    const float          status_h = ImGui::GetFrameHeight();
    const float          rail_w =
        show_nav_rail_ ? (nav_rail_expanded_ ? nav_rail_width_ : nav_rail_collapsed_w_) : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + rail_w, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(320.0f, vp->WorkSize.x - rail_w),
                                    std::max(240.0f, vp->WorkSize.y - status_h)),
                             ImGuiCond_Always);

    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
                                  | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                                  | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
                                  | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    const ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    host_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##ros_dockspace_host", nullptr, host_flags))
    {
        dockspace_id_ = ImGui::GetID("##ros_dockspace");

        // Push transparent WindowBg so dock node backgrounds don't cover the
        // Vulkan-rendered figure content beneath the "Plot Area" panel.
        // Each docked panel still draws its own window background (Topic
        // Monitor, Topic Echo, etc.), so only the central plot area becomes
        // truly see-through to the Vulkan render pass.
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::DockSpace(dockspace_id_, ImVec2(0.0f, 0.0f), dock_flags);
        ImGui::PopStyleColor();

        if (!dock_layout_initialized_)
            apply_default_dock_layout();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    #else
    dock_layout_initialized_ = true;
    #endif
#endif
}

void RosAppShell::apply_default_dock_layout()
{
#ifdef SPECTRA_USE_IMGUI
    #ifdef IMGUI_HAS_DOCK
    if (dockspace_id_ == 0)
        return;

    ImGui::DockBuilderRemoveNode(dockspace_id_);
    ImGui::DockBuilderAddNode(dockspace_id_, ImGuiDockNodeFlags_DockSpace);

    const ImGuiViewport* vp       = ImGui::GetMainViewport();
    const float          status_h = ImGui::GetFrameHeight();
    const float          rail_w =
        show_nav_rail_ ? (nav_rail_expanded_ ? nav_rail_width_ : nav_rail_collapsed_w_) : 0.0f;
    ImGui::DockBuilderSetNodeSize(dockspace_id_,
                                  ImVec2(std::max(320.0f, vp->WorkSize.x - rail_w),
                                         std::max(240.0f, vp->WorkSize.y - status_h)));

    ImGuiID dock_main = dockspace_id_;
    ImGuiID dock_left =
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.18f, nullptr, &dock_main);
    ImGuiID dock_right =
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, nullptr, &dock_main);
    ImGuiID dock_bottom =
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);
    ImGuiID dock_right_bottom =
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.42f, nullptr, &dock_right);
    ImGuiID dock_bottom_left =
        ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Left, 0.28f, nullptr, &dock_bottom);
    ImGuiID dock_bottom_right =
        ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Right, 0.42f, nullptr, &dock_bottom);

    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Displays", dock_left);
    ImGui::DockBuilderDockWindow("Plot Area", dock_main);
    ImGui::DockBuilderDockWindow("Scene Viewport", dock_main);
    ImGui::DockBuilderDockWindow("Inspector", dock_right);
    ImGui::DockBuilderDockWindow("Topic Statistics", dock_right);
    ImGui::DockBuilderDockWindow("Node Graph###NodeGraphPanel", dock_right_bottom);

    ImGui::DockBuilderDockWindow("Topic Echo", dock_bottom);
    ImGui::DockBuilderDockWindow("ROS2 Log", dock_bottom);
    ImGui::DockBuilderDockWindow("Bag Info", dock_bottom_left);

    ImGui::DockBuilderDockWindow("Bag Playback", dock_bottom);
    ImGui::DockBuilderDockWindow("Diagnostics", dock_right);
    ImGui::DockBuilderDockWindow("TF Tree", dock_right_bottom);
    ImGui::DockBuilderDockWindow("Parameter Editor", dock_right);
    ImGui::DockBuilderDockWindow("Service Caller", dock_bottom);

    ImGui::DockBuilderFinish(dockspace_id_);
    dock_layout_initialized_          = true;
    layout_change_tracking_enabled_   = false;
    layout_settle_frames_             = 0;
    layout_unsaved_                   = false;
    #else
    dock_layout_initialized_ = false;
    #endif
#else
    dock_layout_initialized_ = false;
#endif
}

void RosAppShell::draw_nav_rail()
{
#ifdef SPECTRA_USE_IMGUI
    if (!show_nav_rail_)
        return;

    const ImGuiViewport* vp       = ImGui::GetMainViewport();
    const float          status_h = ImGui::GetFrameHeight();
    const float          rail_w   = nav_rail_expanded_ ? nav_rail_width_ : nav_rail_collapsed_w_;
    const auto&          th       = ui::theme();

    // Helper to make ImU32 from theme Color with optional alpha override.
    auto to_u32 = [](const ui::Color& c, float a = -1.0f) -> ImU32
    {
        float alpha = a >= 0.0f ? a : c.a;
        return IM_COL32(static_cast<uint8_t>(c.r * 255),
                        static_cast<uint8_t>(c.g * 255),
                        static_cast<uint8_t>(c.b * 255),
                        static_cast<uint8_t>(alpha * 255));
    };

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, std::max(1.0f, vp->WorkSize.y - status_h)),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);

    // Use a slightly darker shade than bg_secondary for the rail to create depth.
    ui::Color rail_bg = th.bg_primary.lerp(th.bg_secondary, 0.15f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(rail_bg.r, rail_bg.g, rail_bg.b, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
    #ifdef IMGUI_HAS_DOCK
                                   ImGuiWindowFlags_NoDocking |
    #endif
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("##ros_nav_rail", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        return;
    }

    auto* dl = ImGui::GetWindowDrawList();

    // Right-edge: border line + inner shadow gradient for depth.
    {
        float rx = vp->WorkPos.x + rail_w;
        float ry = vp->WorkPos.y;
        float rh = vp->WorkSize.y;
        // Crisp 1px border.
        dl->AddLine(ImVec2(rx - 1.0f, ry),
                    ImVec2(rx - 1.0f, ry + rh),
                    to_u32(th.border_subtle, 0.5f));
        // Soft inner shadow (gradient from dark to transparent, 6px wide).
        dl->AddRectFilledMultiColor(ImVec2(rx - 7.0f, ry),
                                    ImVec2(rx - 1.0f, ry + rh),
                                    IM_COL32(0, 0, 0, 0),
                                    IM_COL32(0, 0, 0, 28),
                                    IM_COL32(0, 0, 0, 28),
                                    IM_COL32(0, 0, 0, 0));
    }

    // Expand / collapse toggle — styled as an icon-only button.
    {
        const char* toggle_icon =
            nav_rail_expanded_ ? ui::icon_str(ui::Icon::Back) : ui::icon_str(ui::Icon::Forward);
        ImVec2 btn_sz = nav_rail_expanded_ ? ImVec2(-1.0f, 24.0f) : ImVec2(rail_w - 14.0f, 24.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(th.bg_tertiary.r, th.bg_tertiary.g, th.bg_tertiary.b, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(th.bg_elevated.r, th.bg_elevated.g, th.bg_elevated.b, 0.6f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(th.text_tertiary.r, th.text_tertiary.g, th.text_tertiary.b, 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button(toggle_icon, btn_sz))
            nav_rail_expanded_ = !nav_rail_expanded_;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
    }

    ImGui::Spacing();

    // Button height.
    const float btn_h = nav_rail_expanded_ ? 28.0f : 32.0f;

    // ── nav_button: custom-drawn icon button with depth ──────────────────
    auto nav_button = [&](ui::Icon icon, const char* label, bool& panel_flag)
    {
        const bool active = panel_flag;
        ImGui::PushID(label);

        // Reserve space for the button via an invisible ImGui button.
        ImVec2 btn_sz = nav_rail_expanded_ ? ImVec2(ImGui::GetContentRegionAvail().x, btn_h)
                                           : ImVec2(rail_w - 14.0f, btn_h);
        ImVec2 cursor = ImGui::GetCursorScreenPos();

        // Invisible button for hit-testing and interaction.
        ImGui::InvisibleButton("##btn", btn_sz);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();
        if (clicked)
            panel_flag = !panel_flag;

        ImVec2 tl(cursor.x, cursor.y);
        ImVec2 br(cursor.x + btn_sz.x, cursor.y + btn_sz.y);

        // ── Background ──
        if (active)
        {
            // Active: gradient fill from accent-tinted bg_tertiary.
            ui::Color bg_top = th.bg_tertiary.lerp(th.accent, 0.12f);
            ui::Color bg_bot = th.bg_tertiary.lerp(th.accent, 0.06f);
            float     alpha  = hovered ? 1.0f : 0.85f;
            dl->AddRectFilled(tl, br, to_u32(bg_top, alpha), 5.0f);
            // Subtle bottom-half darkening for gradient feel.
            float mid_y = (tl.y + br.y) * 0.5f;
            dl->AddRectFilledMultiColor(ImVec2(tl.x, mid_y),
                                        br,
                                        to_u32(bg_bot, 0.0f),
                                        to_u32(bg_bot, 0.0f),
                                        to_u32(bg_bot, alpha * 0.5f),
                                        to_u32(bg_bot, alpha * 0.5f));

            // Inner highlight at top edge (1px bright line).
            dl->AddLine(ImVec2(tl.x + 4.0f, tl.y + 0.5f),
                        ImVec2(br.x - 4.0f, tl.y + 0.5f),
                        to_u32(th.accent, 0.15f),
                        1.0f);
        }
        else if (hovered)
        {
            // Hover: subtle elevated background.
            dl->AddRectFilled(tl, br, to_u32(th.bg_tertiary, 0.55f), 5.0f);
        }

        // ── Accent bar (left edge, glowing for active) ──
        if (active)
        {
            // Glow layer (wider, softer).
            dl->AddRectFilled(ImVec2(tl.x - 1.0f, tl.y + 3.0f),
                              ImVec2(tl.x + 4.0f, br.y - 3.0f),
                              to_u32(th.accent, 0.25f),
                              2.0f);
            // Core bar.
            dl->AddRectFilled(ImVec2(tl.x, tl.y + 4.0f),
                              ImVec2(tl.x + 3.0f, br.y - 4.0f),
                              to_u32(th.accent, 0.95f),
                              1.5f);
        }

        // ── Border (subtle, only for active) ──
        if (active)
        {
            dl->AddRect(tl, br, to_u32(th.border_subtle, 0.3f), 5.0f);
        }

        // ── Icon + text ──
        if (nav_rail_expanded_)
        {
            // Icon + label.
            const std::string buf =
                std::format("{}  {}", ui::icon_str(icon), label);
            ImU32  text_col = active    ? to_u32(th.text_primary, 1.0f)
                              : hovered ? to_u32(th.text_primary, 0.9f)
                                        : to_u32(th.text_secondary, 0.8f);
            ImVec2 tsz      = ImGui::CalcTextSize(buf.c_str());
            ImVec2 tpos(tl.x + 10.0f, tl.y + (btn_h - tsz.y) * 0.5f);
            dl->AddText(tpos, text_col, buf.c_str());
        }
        else
        {
            // Icon only, centered.
            const char* icon_s   = ui::icon_str(icon);
            ImU32       text_col = active    ? to_u32(th.accent, 1.0f)
                                   : hovered ? to_u32(th.text_primary, 0.85f)
                                             : to_u32(th.text_secondary, 0.6f);
            ImVec2      isz      = ImGui::CalcTextSize(icon_s);
            ImVec2      ipos(tl.x + (btn_sz.x - isz.x) * 0.5f, tl.y + (btn_h - isz.y) * 0.5f);
            dl->AddText(ipos, text_col, icon_s);
        }

        if (hovered)
            ImGui::SetTooltip("%s", label);

        ImGui::PopID();
    };

    // ── section_header: label + subtle divider ───────────────────────────
    auto section_header = [&](const char* text)
    {
        ImGui::Spacing();
        ImGui::Spacing();
        if (nav_rail_expanded_)
        {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            float  avail  = ImGui::GetContentRegionAvail().x;
            // Small-caps style label.
            ImVec2 tsz = ImGui::CalcTextSize(text);
            dl->AddText(ImVec2(cursor.x + 10.0f, cursor.y), to_u32(th.text_tertiary, 0.5f), text);
            // Subtle line after text.
            float lx = cursor.x + 10.0f + tsz.x + 6.0f;
            float ly = cursor.y + tsz.y * 0.5f;
            dl->AddLine(ImVec2(lx, ly),
                        ImVec2(cursor.x + avail - 4.0f, ly),
                        to_u32(th.border_subtle, 0.2f));
            ImGui::Dummy(ImVec2(0.0f, tsz.y + 2.0f));
        }
        else
        {
            float cx = ImGui::GetCursorScreenPos().x;
            float cy = ImGui::GetCursorScreenPos().y + 2.0f;
            dl->AddLine(ImVec2(cx + 8.0f, cy),
                        ImVec2(cx + rail_w - 22.0f, cy),
                        to_u32(th.border_subtle, 0.25f));
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        }
    };

    section_header("CORE");
    nav_button(ui::Icon::List, "Topic Monitor", show_topic_list_);
    nav_button(ui::Icon::ChartLine, "Plot Area", show_plot_area_);
    nav_button(ui::Icon::Command, "Topic Echo", show_topic_echo_);
    nav_button(ui::Icon::BarChart, "Topic Statistics", show_topic_stats_);

    section_header("TOOLS");
    nav_button(ui::Icon::Database, "Bag Info", show_bag_info_);
    nav_button(ui::Icon::Play, "Bag Playback", show_bag_playback_);
    nav_button(ui::Icon::FileText, "Log Viewer", show_log_viewer_);
    nav_button(ui::Icon::Warning, "Diagnostics", show_diagnostics_);

    section_header("ADVANCED");
    nav_button(ui::Icon::Eye, "Displays", show_displays_panel_);
    nav_button(ui::Icon::Maximize, "Scene Viewport", show_scene_viewport_);
    nav_button(ui::Icon::Search, "Inspector", show_inspector_panel_);
    nav_button(ui::Icon::Link, "Node Graph", show_node_graph_);
    nav_button(ui::Icon::Timeline, "TF Tree", show_tf_tree_);
    nav_button(ui::Icon::Settings, "Parameter Editor", show_param_editor_);
    nav_button(ui::Icon::Wrench, "Service Caller", show_service_caller_);

    if (nav_rail_expanded_)
    {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        {
            float cx = ImGui::GetCursorScreenPos().x;
            float cy = ImGui::GetCursorScreenPos().y;
            float aw = ImGui::GetContentRegionAvail().x;
            dl->AddLine(ImVec2(cx + 8.0f, cy),
                        ImVec2(cx + aw - 4.0f, cy),
                        to_u32(th.border_subtle, 0.2f));
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        dl->AddText(ImGui::GetCursorScreenPos(), to_u32(th.text_tertiary, 0.45f), "Rail Width");
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() + 2.0f));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##ros_nav_width", &nav_rail_width_, 180.0f, 360.0f, "%.0f px");

        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(th.bg_tertiary.r, th.bg_tertiary.g, th.bg_tertiary.b, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(th.bg_elevated.r, th.bg_elevated.g, th.bg_elevated.b, 0.8f));
        if (ImGui::Button("Reset Layout", ImVec2(-1.0f, 0.0f)))
            dock_layout_initialized_ = false;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
#endif
}

void RosAppShell::draw_topic_list(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (topic_list_)
        topic_list_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_topic_echo(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (topic_echo_)
        topic_echo_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_topic_stats(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (topic_stats_)
    {
        const int slot = workspace_state_.active_subplot_idx >= 1 ? workspace_state_.active_subplot_idx
                                                                  : 1;
        topic_stats_->draw(p_open, slot);
    }
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_plot_area(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    // Transparent background so the Vulkan-rendered figure canvas shows through.
    ImGui::SetNextWindowBgAlpha(0.0f);
    // Enforce a minimum window size so toolbar buttons are always visible.
    ImGui::SetNextWindowSizeConstraints(ImVec2(kPlotAreaMinWindowWidth, kPlotAreaMinWindowHeight),
                                        ImVec2(FLT_MAX, FLT_MAX));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

    const RosPlotDropTargetContext drop_ctx{
        .drag_drop             = drag_drop_.get(),
        .subplot_mgr           = subplot_mgr_.get(),
        .default_numeric_field = [this](const std::string& topic, const std::string& type)
        { return default_numeric_field(topic, type); },
    };

    if (ImGui::Begin("Plot Area", p_open, flags))
    {
        float      canvas_top       = ImGui::GetCursorScreenPos().y;
        float      canvas_bottom    = canvas_top;
        const auto same_line_button = []() { ImGui::SameLine(0.0f, kPlotAreaButtonSpacing); };

        if (subplot_mgr_)
        {
            // Synchronize the slider with the actual visible X extent.
            // When the user right-click-drags (box zoom) or scrolls while paused,
            // the axes X limits change but subplot_mgr_->time_window() doesn't
            // update.  Read the actual extent from the active subplot.
            auto tw = static_cast<float>(subplot_mgr_->time_window());
            {
                int active_slot = workspace_state_.active_subplot_idx;
                if (active_slot >= 1 && active_slot <= subplot_mgr_->capacity())
                {
                    const auto* se = subplot_mgr_->slot_entry_pub(active_slot);
                    if (se && se->axes)
                    {
                        auto xl     = se->axes->x_limits();
                        auto actual = static_cast<float>(xl.max - xl.min);
                        if (actual > 0.5f && actual < 86400.0f)
                            tw = actual;
                    }
                }
            }
            ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x,
                                               kPlotAreaTimeSliderMinWidth,
                                               kPlotAreaTimeSliderMaxWidth));
            if (ImGui::SliderFloat("##TimeWindow", &tw, 1.0f, 3600.0f, "%.1f s"))
            {
                const auto new_tw = static_cast<double>(tw);
                subplot_mgr_->set_time_window(new_tw);
                if (plot_mgr_)
                    plot_mgr_->set_time_window(new_tw);

                // When any subplot is paused, apply the new time window as
                // a zoom centred on the current view so the user sees an
                // actual zoom instead of a no-op slider change.
                for (int s = 1; s <= subplot_mgr_->capacity(); ++s)
                {
                    if (!subplot_mgr_->is_scroll_paused(s))
                        continue;
                    auto* se = subplot_mgr_->slot_entry_pub(s);
                    if (!se || !se->axes)
                        continue;
                    auto   xl  = se->axes->x_limits();
                    double mid = (xl.min + xl.max) * 0.5;
                    se->axes->xlim(mid - new_tw * 0.5, mid + new_tw * 0.5);
                }
            }
            same_line_button();

            const bool has_plots = active_plot_count() > 0;
            if (!has_plots)
            {
                ImGui::TextDisabled("Expand a topic and double-click a field to plot");
            }

            auto draw_plot_toolbar_extras = [&]()
            {
                bool prune_enabled = subplot_mgr_->pruning_enabled();
                if (ImGui::Checkbox("Prune Old", &prune_enabled))
                {
                    subplot_mgr_->set_pruning_enabled(prune_enabled);
                    if (plot_mgr_)
                        plot_mgr_->set_pruning_enabled(prune_enabled);
                }
                same_line_button();
                auto prune_buf = static_cast<float>(subplot_mgr_->prune_buffer());
                if (!prune_enabled)
                    ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::SliderFloat("##PruneBuffer", &prune_buf, 0.0f, 600.0f, "%.0f s"))
                {
                    subplot_mgr_->set_prune_buffer(static_cast<double>(prune_buf));
                    if (plot_mgr_)
                        plot_mgr_->set_prune_buffer(static_cast<double>(prune_buf));
                }
                if (!prune_enabled)
                    ImGui::EndDisabled();
                same_line_button();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                if (ImGui::SmallButton("Live All"))
                    subplot_mgr_->resume_all_scroll();
                ImGui::PopStyleColor(2);
                same_line_button();
                if (ImGui::SmallButton("Pause All"))
                    subplot_mgr_->pause_all_scroll();
                same_line_button();
                if (ImGui::SmallButton("Reset"))
                    reset_plot_display();
                same_line_button();
                if (ImGui::SmallButton("Auto Y"))
                    restore_plot_autofit(workspace_state_.active_subplot_idx);
                same_line_button();
                if (ImGui::SmallButton("+Sub"))
                    subplot_mgr_->add_row();
                same_line_button();
                ImGui::BeginDisabled(subplot_mgr_->rows() <= 1);
                {
                    if (ImGui::SmallButton("-Sub"))
                        subplot_mgr_->remove_last_row();
                }
                ImGui::EndDisabled();
            };

            if (has_plots)
            {
                draw_plot_toolbar_extras();
            }
            else
            {
                if (ImGui::SmallButton("More..."))
                    ImGui::OpenPopup("##plot_toolbar_more");
                if (ImGui::BeginPopup("##plot_toolbar_more"))
                {
                    draw_plot_toolbar_extras();
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::Separator();
        canvas_top = ImGui::GetCursorScreenPos().y;

        // Per-subplot controls and targeted drop zones.
        if (subplot_mgr_)
        {
            const int   total_slots     = subplot_mgr_->capacity();
            const auto& fig_style       = subplot_mgr_->figure().style();
            const float min_slot_height = std::max(
                120.0f,
                fig_style.margin_top + fig_style.margin_bottom + kPlotAreaMinViewportHeight);
            const float avail_h = ImGui::GetContentRegionAvail().y;
            // Reserve space for the global drop zone at the bottom.
            const float reserved_bottom =
                kPlotAreaGlobalDropHeight + ImGui::GetStyle().ItemSpacing.y;
            const float usable_h = std::max(0.0f, avail_h - reserved_bottom);
            const float slot_h =
                (total_slots > 0)
                    ? std::max(min_slot_height, usable_h / static_cast<float>(total_slots))
                    : usable_h;

            for (int s = 1; s <= total_slots; ++s)
            {
                ImGui::PushID(s);
                const float slot_start_y = ImGui::GetCursorPosY();

                const bool has      = subplot_mgr_->has_plot(s);
                const int  n_series = subplot_mgr_->slot_series_count(s);

                // Slot header — compact single line with controls.
                ImGui::BeginGroup();

                if (has)
                {
                    // First line: plot title + action buttons, no series text.
                    const bool selected = (workspace_state_.active_subplot_idx == s);
                    ImGui::TextColored(selected ? ImVec4(0.6f, 0.95f, 0.55f, 1.0f)
                                                : ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                                       "Plot %d",
                                       s);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d series)", n_series);
                    if (selected)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("[active]");
                    }

                    // Scroll control per-slot.
                    ImGui::SameLine(0.0f, 12.0f);
                    if (subplot_mgr_->is_scroll_paused(s))
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                              ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("Live##scroll"))
                        {
                            remember_active_subplot(s);
                            subplot_mgr_->resume_scroll(s);
                        }
                        ImGui::PopStyleColor(2);
                    }
                    else
                    {
                        if (ImGui::SmallButton("Pause##scroll"))
                        {
                            remember_active_subplot(s);
                            subplot_mgr_->pause_scroll(s);
                        }
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    if (ImGui::SmallButton("Auto Y"))
                        restore_plot_autofit(s);

                    ImGui::SameLine();
                    const std::string ylim_popup_id = std::format("ylim_{}", s);
                    if (ImGui::SmallButton("Y Limits"))
                    {
                        remember_active_subplot(s);
                        ImGui::OpenPopup(ylim_popup_id.c_str());
                    }

                    if (ImGui::BeginPopup(ylim_popup_id.c_str()))
                    {
                        ImGui::TextDisabled("Y-Axis Limits for Plot %d", s);
                        ImGui::Separator();

                        auto* slot_entry = subplot_mgr_->slot_entry_pub(s);
                        if (slot_entry && slot_entry->axes)
                        {
                            auto yl     = slot_entry->axes->y_limits();
                            auto ymin_f = static_cast<float>(yl.min);
                            auto ymax_f = static_cast<float>(yl.max);

                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::DragFloat("Y Min", &ymin_f, 0.01f);
                            ImGui::SetNextItemWidth(120.0f);
                            ImGui::DragFloat("Y Max", &ymax_f, 0.01f);

                            if (ymin_f < ymax_f)
                            {
                                subplot_mgr_->set_slot_ylim(s,
                                                            static_cast<double>(ymin_f),
                                                            static_cast<double>(ymax_f));
                            }

                            ImGui::Separator();
                            if (ImGui::Button("Reset to Auto"))
                            {
                                subplot_mgr_->auto_fit_slot_y(s);
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    const std::string style_popup_id = std::format("style_{}", s);
                    if (ImGui::SmallButton("Style"))
                    {
                        remember_active_subplot(s);
                        ImGui::OpenPopup(style_popup_id.c_str());
                    }

                    if (ImGui::BeginPopup(style_popup_id.c_str()))
                    {
                        auto* slot_entry = subplot_mgr_->slot_entry_pub(s);
                        if (slot_entry && slot_entry->axes)
                        {
                            auto* axes = slot_entry->axes;

                            auto show_axis_config_error = [&](const std::string& error)
                            {
                                session_status_msg_   = "Plot update failed: " + error;
                                session_status_timer_ = 4.0f;
                            };

                            const auto  numeric_fields = subplot_mgr_->slot_numeric_fields(s);
                            std::string custom_axes_unavailable;
                            const bool  supports_custom_axes =
                                subplot_mgr_->slot_supports_custom_axes(s,
                                                                        &custom_axes_unavailable);

                            char title_buf[128];
                            std::strncpy(title_buf, axes->title().c_str(), sizeof(title_buf) - 1);
                            title_buf[sizeof(title_buf) - 1] = '\0';
                            if (ImGui::InputText("Title", title_buf, sizeof(title_buf)))
                                axes->title(title_buf);

                            char xlabel_buf[128];
                            std::strncpy(xlabel_buf,
                                         axes->xlabel().c_str(),
                                         sizeof(xlabel_buf) - 1);
                            xlabel_buf[sizeof(xlabel_buf) - 1] = '\0';
                            if (ImGui::InputText("X Label", xlabel_buf, sizeof(xlabel_buf)))
                                axes->xlabel(xlabel_buf);

                            char ylabel_buf[128];
                            std::strncpy(ylabel_buf,
                                         axes->ylabel().c_str(),
                                         sizeof(ylabel_buf) - 1);
                            ylabel_buf[sizeof(ylabel_buf) - 1] = '\0';
                            if (ImGui::InputText("Y Label", ylabel_buf, sizeof(ylabel_buf)))
                                axes->ylabel(ylabel_buf);

                            bool show_grid = axes->grid_enabled();
                            if (ImGui::Checkbox("Show Grid", &show_grid))
                                axes->grid(show_grid);

                            bool show_border = axes->border_enabled();
                            if (ImGui::Checkbox("Show Border", &show_border))
                                axes->show_border(show_border);

                            bool live_auto_y = !slot_entry->manual_ylim.has_value();
                            if (ImGui::Checkbox("Live Auto-Fit Y", &live_auto_y))
                            {
                                if (live_auto_y)
                                {
                                    subplot_mgr_->auto_fit_slot_y(s);
                                }
                                else
                                {
                                    auto yl = axes->y_limits();
                                    subplot_mgr_->set_slot_ylim(s, yl.min, yl.max);
                                }
                            }

                            ImGui::Separator();

                            const char* axis_mode_preview =
                                (slot_entry->axis_mode == AxisMode::CustomAxes) ? "Custom Axes"
                                                                                : "Time Series";
                            if (ImGui::BeginCombo("Axis Mode", axis_mode_preview))
                            {
                                const std::string current_y = slot_entry->y_field_path.empty()
                                                                  ? slot_entry->field_path
                                                                  : slot_entry->y_field_path;
                                const std::string current_x =
                                    (slot_entry->axis_mode == AxisMode::CustomAxes
                                     && !slot_entry->x_field_path.empty())
                                        ? slot_entry->x_field_path
                                        : std::string(AXIS_SOURCE_TIME);

                                if (ImGui::Selectable(
                                        "Time Series",
                                        slot_entry->axis_mode == AxisMode::TimeSeries))
                                {
                                    std::string error;
                                    if (!subplot_mgr_->configure_slot_axes(s,
                                                                           AxisMode::TimeSeries,
                                                                           AXIS_SOURCE_TIME,
                                                                           current_y,
                                                                           &error))
                                    {
                                        show_axis_config_error(error);
                                    }
                                }

                                if (!supports_custom_axes)
                                    ImGui::BeginDisabled();
                                if (ImGui::Selectable(
                                        "Custom Axes",
                                        slot_entry->axis_mode == AxisMode::CustomAxes))
                                {
                                    std::string error;
                                    if (!subplot_mgr_->configure_slot_axes(s,
                                                                           AxisMode::CustomAxes,
                                                                           current_x,
                                                                           current_y,
                                                                           &error))
                                    {
                                        show_axis_config_error(error);
                                    }
                                }
                                if (!supports_custom_axes)
                                    ImGui::EndDisabled();

                                ImGui::EndCombo();
                            }

                            if (slot_entry->axis_mode == AxisMode::CustomAxes)
                            {
                                std::vector<std::string> x_sources;
                                x_sources.reserve(numeric_fields.size() + 1);
                                x_sources.emplace_back(AXIS_SOURCE_TIME);
                                x_sources.insert(x_sources.end(),
                                                 numeric_fields.begin(),
                                                 numeric_fields.end());

                                const std::string current_x = slot_entry->x_field_path.empty()
                                                                  ? std::string(AXIS_SOURCE_TIME)
                                                                  : slot_entry->x_field_path;
                                const std::string current_y = slot_entry->y_field_path.empty()
                                                                  ? slot_entry->field_path
                                                                  : slot_entry->y_field_path;

                                const char* x_preview = (current_x == AXIS_SOURCE_TIME)
                                                            ? "time (s)"
                                                            : current_x.c_str();
                                if (ImGui::BeginCombo("X Source", x_preview))
                                {
                                    for (const auto& option : x_sources)
                                    {
                                        const bool  selected = (option == current_x);
                                        const char* label    = (option == AXIS_SOURCE_TIME)
                                                                   ? "time (s)"
                                                                   : option.c_str();
                                        if (ImGui::Selectable(label, selected))
                                        {
                                            std::string error;
                                            if (!subplot_mgr_->configure_slot_axes(
                                                    s,
                                                    AxisMode::CustomAxes,
                                                    option,
                                                    current_y,
                                                    &error))
                                            {
                                                show_axis_config_error(error);
                                            }
                                        }
                                    }
                                    ImGui::EndCombo();
                                }

                                const char* y_preview =
                                    current_y.empty() ? "<select field>" : current_y.c_str();
                                if (ImGui::BeginCombo("Y Source", y_preview))
                                {
                                    for (const auto& option : numeric_fields)
                                    {
                                        const bool selected = (option == current_y);
                                        if (ImGui::Selectable(option.c_str(), selected))
                                        {
                                            std::string error;
                                            if (!subplot_mgr_->configure_slot_axes(
                                                    s,
                                                    AxisMode::CustomAxes,
                                                    current_x,
                                                    option,
                                                    &error))
                                            {
                                                show_axis_config_error(error);
                                            }
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            else if (!supports_custom_axes && !custom_axes_unavailable.empty())
                            {
                                ImGui::TextDisabled("%s", custom_axes_unavailable.c_str());
                            }
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    if (ImGui::SmallButton("Clear##plot"))
                    {
                        remember_active_subplot(s);
                        subplot_mgr_->clear_slot_data(s);
                    }

                    // Series dropdown — compact combo with hide/delete per series.
                    {
                        // Build summary label for the combo preview.
                        const auto* first_se = subplot_mgr_->slot_series(s, 0);
                        const std::string combo_label =
                            (n_series == 1 && first_se)
                                ? std::format("{}/{}", first_se->topic, first_se->field_path)
                                : std::format("{} series", n_series);

                        const std::string combo_id = std::format("##series_{}", s);
                        ImGui::SetNextItemWidth(
                            std::max(200.0f, ImGui::GetContentRegionAvail().x * 0.5f));

                        if (ImGui::BeginCombo(combo_id.c_str(),
                                              combo_label.c_str(),
                                              ImGuiComboFlags_HeightLarge))
                        {
                            int remove_si = -1;
                            for (int si = 0; si < n_series; ++si)
                            {
                                const auto* se = subplot_mgr_->slot_series(s, si);
                                if (!se)
                                    continue;
                                ImGui::PushID(si);

                                // Eye icon — toggle series visibility.
                                bool        vis      = se->series ? se->series->visible() : true;
                                const char* eye_icon = vis ? ui::icon_str(ui::Icon::Eye)
                                                           : ui::icon_str(ui::Icon::EyeOff);
                                ImGui::PushStyleColor(ImGuiCol_Text,
                                                      vis ? ImVec4(0.3f, 0.85f, 0.4f, 1.0f)
                                                          : ImVec4(0.5f, 0.5f, 0.5f, 0.7f));
                                const std::string eye_id = std::format("{}##eye", eye_icon);
                                if (ImGui::SmallButton(eye_id.c_str()))
                                {
                                    if (se->series)
                                        se->series->visible(!vis);
                                }
                                ImGui::PopStyleColor();
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(vis ? "Hide series" : "Show series");

                                // Trash icon — remove series.
                                ImGui::SameLine(0.0f, 6.0f);
                                ImGui::PushStyleColor(ImGuiCol_Text,
                                                      ImVec4(0.9f, 0.3f, 0.3f, 0.85f));
                                const std::string trash_id =
                                    std::format("{}##del", ui::icon_str(ui::Icon::Trash));
                                if (ImGui::SmallButton(trash_id.c_str()))
                                    remove_si = si;
                                ImGui::PopStyleColor();
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("Remove series");

                                // Series label.
                                ImGui::SameLine(0.0f, 8.0f);
                                ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f),
                                                   "%s/%s",
                                                   se->topic.c_str(),
                                                   se->field_path.c_str());

                                ImGui::PopID();
                            }
                            ImGui::EndCombo();

                            // Deferred removal (after EndCombo to avoid iterator invalidation).
                            if (remove_si >= 0)
                            {
                                const auto* rse = subplot_mgr_->slot_series(s, remove_si);
                                if (rse)
                                {
                                    subplot_mgr_->remove_series_from_slot(s,
                                                                          rse->topic,
                                                                          rse->field_path);
                                    if (!subplot_mgr_->has_plot(s))
                                        subplot_mgr_->compact();
                                }
                            }
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Plot %d — drop a topic field here", s);
                }

                ImGui::EndGroup();

                // Drop zone — fills remaining slot height so subplots
                // expand to use all available vertical space.
                {
                    const float used_h    = ImGui::GetCursorPosY() - slot_start_y;
                    const float remaining = std::max(8.0f, slot_h - used_h);
                    const float drop_w    = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                    draw_subplot_slot_drop_target(drop_ctx, s, drop_w, remaining);
                }

                if (s < total_slots)
                    ImGui::Separator();

                ImGui::PopID();
            }
        }

        canvas_bottom = ImGui::GetCursorScreenPos().y;

        // Global drop zone at the bottom — drops to first empty slot or adds a new row.
        ImGui::Separator();
        ImGui::TextDisabled("Drop a topic/field here to add to a new subplot");

        {
            const float drop_w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            draw_global_plot_drop_target(drop_ctx, drop_w, kPlotAreaGlobalDropHeight);
        }

        // Forward mouse/scroll events to the core InputHandler which provides
        // animated zoom, inertial pan, box zoom, undo, axis linking, and more.
        handle_plot_shortcuts();
        bridge_imgui_to_input_handler();

        // Canvas override for Vulkan rendering.
        if (layout_manager_)
        {
            const ImVec2 wpos        = ImGui::GetWindowPos();
            const ImVec2 content_min = ImGui::GetWindowContentRegionMin();
            const ImVec2 content_max = ImGui::GetWindowContentRegionMax();
            const float  canvas_x    = wpos.x + content_min.x;
            const float  canvas_w    = std::max(0.0f, content_max.x - content_min.x);
            const float  plot_top    = canvas_top;
            const float  plot_bottom = std::max(canvas_top, canvas_bottom);
            const float  canvas_h    = plot_bottom - plot_top;
            if (canvas_w > 0.0f && canvas_h > 0.0f)
            {
                layout_manager_->set_canvas_override(
                    spectra::Rect{canvas_x, plot_top, canvas_w, canvas_h});
            }
        }
    }
    ImGui::End();
#else
    (void)p_open;
#endif
}

// ─── Input bridging ──────────────────────────────────────────────────────────
//
// Translates ImGui mouse/scroll events into core InputHandler calls.
// The InputHandler provides animated zoom, inertial pan, axis-lock detection,
// box zoom, undo, and axis linking — all features that were previously
// reimplemented (without animation or undo) in the old handle_plot_*() methods.
//
// ROS2-specific behaviours preserved:
//   - Left-click pan pauses auto-scroll on all subplots.
//   - Zoom and manual Y changes do not pause follow mode on their own.
//   - Scroll-wheel zoom in live (following) mode adjusts the global time
//     window instead of zooming the X axis, keeping the "spread/compress"
//     UX that lets users see more or less history without pausing.
//   - Scroll-wheel zoom in paused mode forwards to InputHandler for normal
//     anchor-based zoom (with animation).
// ──────────────────────────────────────────────────────────────────────────────

int RosAppShell::hit_test_subplot_slot(float mx, float my, bool include_y_gutter) const
{
    if (!subplot_mgr_)
        return -1;
    const float y_gutter = include_y_gutter ? subplot_mgr_->figure().style().margin_left : 0.0f;
    const int   cap      = subplot_mgr_->capacity();
    for (int s = 1; s <= cap; ++s)
    {
        const auto* se = subplot_mgr_->slot_entry_pub(s);
        if (!se || !se->axes)
            continue;
        const auto& vp    = se->axes->viewport();
        const float min_x = std::max(0.0f, vp.x - y_gutter);
        if (mx >= min_x && mx <= vp.x + vp.w && my >= vp.y && my <= vp.y + vp.h)
            return s;
    }
    return -1;
}

void RosAppShell::remember_active_subplot(int slot)
{
    if (!subplot_mgr_ || slot < 1 || slot > subplot_mgr_->capacity())
        return;
    workspace_state_.active_subplot_idx = slot;
}

void RosAppShell::begin_manual_y_tracking(int slot)
{
    tracked_manual_y_slot_  = -1;
    tracked_manual_y_valid_ = false;

    if (!subplot_mgr_ || slot < 1 || slot > subplot_mgr_->capacity())
        return;

    auto* se = subplot_mgr_->slot_entry_pub(slot);
    if (!se || !se->axes)
        return;

    tracked_manual_y_slot_   = slot;
    tracked_manual_y_limits_ = se->axes->y_limits();
    tracked_manual_y_valid_  = true;
    remember_active_subplot(slot);
}

void RosAppShell::finish_manual_y_tracking(int slot)
{
    if (!subplot_mgr_ || !tracked_manual_y_valid_)
        return;

    const int tracked_slot = (slot > 0) ? slot : tracked_manual_y_slot_;
    auto*     se           = subplot_mgr_->slot_entry_pub(tracked_slot);
    if (se && se->axes)
    {
        const auto current_y = se->axes->y_limits();
        if (axis_limits_changed(current_y, tracked_manual_y_limits_))
            subplot_mgr_->set_slot_ylim(tracked_slot, current_y.min, current_y.max);
    }

    tracked_manual_y_slot_  = -1;
    tracked_manual_y_valid_ = false;
}

void RosAppShell::restore_plot_autofit(int slot)
{
    if (!subplot_mgr_)
        return;

    if (slot >= 1 && slot <= subplot_mgr_->capacity())
    {
        subplot_mgr_->auto_fit_slot_y(slot);
        remember_active_subplot(slot);
        return;
    }

    for (int s = 1; s <= subplot_mgr_->capacity(); ++s)
        subplot_mgr_->auto_fit_slot_y(s);
}

void RosAppShell::reset_plot_display(int slot)
{
    const double default_window = cfg_.time_window_s;

    if (subplot_mgr_)
    {
        if (slot >= 1 && slot <= subplot_mgr_->capacity())
        {
            subplot_mgr_->clear_slot_time_window(slot);
            subplot_mgr_->clear_slot_ylim(slot);
            subplot_mgr_->resume_scroll(slot);
            remember_active_subplot(slot);
        }
        else
        {
            subplot_mgr_->set_time_window(default_window);
            for (int s = 1; s <= subplot_mgr_->capacity(); ++s)
            {
                subplot_mgr_->clear_slot_time_window(s);
                subplot_mgr_->clear_slot_ylim(s);
            }
            subplot_mgr_->resume_all_scroll();
        }
    }

    if (plot_mgr_)
    {
        plot_mgr_->set_time_window(default_window);
        plot_mgr_->resume_all_scroll();
    }
}

void RosAppShell::handle_plot_shortcuts()
{
#ifdef SPECTRA_USE_IMGUI
    if (!subplot_mgr_)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper || io.WantTextInput || ImGui::IsAnyItemActive())
        return;

    int target_slot = workspace_state_.active_subplot_idx;
    if (target_slot < 1 || target_slot > subplot_mgr_->capacity())
        target_slot = -1;

    // R — Reset basic display: resume live, reset time window, clear Y limits.
    if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        reset_plot_display();

    // A — Auto-fit Y on active subplot (or all if none active).
    if (ImGui::IsKeyPressed(ImGuiKey_A, false))
        restore_plot_autofit(target_slot);

    // Space — Toggle pause/resume on active subplot (or all if none active).
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        if (target_slot > 0)
        {
            if (subplot_mgr_->is_scroll_paused(target_slot))
                subplot_mgr_->resume_scroll(target_slot);
            else
                subplot_mgr_->pause_scroll(target_slot);
        }
        else
        {
            // Toggle all: if any is live, pause all; otherwise resume all.
            bool any_live = false;
            for (int s = 1; s <= subplot_mgr_->capacity(); ++s)
            {
                if (subplot_mgr_->has_plot(s) && !subplot_mgr_->is_scroll_paused(s))
                {
                    any_live = true;
                    break;
                }
            }
            if (any_live)
                subplot_mgr_->pause_all_scroll();
            else
                subplot_mgr_->resume_all_scroll();
        }
    }

    // Home — Resume live mode (like pressing "Live All").
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
    {
        if (target_slot > 0)
            subplot_mgr_->resume_scroll(target_slot);
        else
            subplot_mgr_->resume_all_scroll();
    }

    // G — Toggle grid on active subplot.
    if (ImGui::IsKeyPressed(ImGuiKey_G, false))
    {
        if (target_slot > 0)
        {
            auto* se = subplot_mgr_->slot_entry_pub(target_slot);
            if (se && se->axes)
                se->axes->grid(!se->axes->grid_enabled());
        }
    }

    // Escape — Forward to InputHandler for box zoom cancellation.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        input_handler_.on_key(256 /* KEY_ESCAPE */, 1 /* ACTION_PRESS */, 0);
    }
#endif
}

void RosAppShell::bridge_imgui_to_input_handler()
{
#ifdef SPECTRA_USE_IMGUI
    if (!subplot_mgr_)
        return;

    const ImGuiIO& io                  = ImGui::GetIO();
    const float    mx                  = io.MousePos.x;
    const float    my                  = io.MousePos.y;
    const int      hovered_slot        = hit_test_subplot_slot(mx, my);
    const int      y_drag_slot         = hit_test_subplot_slot(mx, my, true);
    const bool     plot_window_hovered = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    if (!plot_window_hovered && hovered_slot <= 0 && y_drag_slot <= 0 && !prev_mouse_left_
        && !prev_mouse_right_)
        return;

    // GLFW-compatible constants expected by InputHandler.
    constexpr int BTN_LEFT       = 0;
    constexpr int BTN_RIGHT      = 1;
    constexpr int ACTION_PRESS   = 1;
    constexpr int ACTION_RELEASE = 0;
    constexpr int MOD_CTRL       = 0x0002;
    constexpr int MOD_SHIFT      = 0x0001;

    int mods = 0;
    if (io.KeyCtrl)
        mods |= MOD_CTRL;
    if (io.KeyShift)
        mods |= MOD_SHIFT;

    const auto sync_input_handler_slot = [&](int slot)
    {
        if (slot < 1)
            return;
        const auto* se = subplot_mgr_->slot_entry_pub(slot);
        if (!se || !se->axes)
            return;
        input_handler_.set_active_axes(se->axes);
        const auto& vp = se->axes->viewport();
        input_handler_.set_viewport(vp.x, vp.y, vp.w, vp.h);
    };
    const auto sync_tracked_manual_y = [&]()
    {
        if (!tracked_manual_y_valid_ || tracked_manual_y_slot_ < 1)
            return;
        auto* se = subplot_mgr_->slot_entry_pub(tracked_manual_y_slot_);
        if (!se || !se->axes)
            return;
        const auto current_y = se->axes->y_limits();
        if (axis_limits_changed(current_y, tracked_manual_y_limits_))
            subplot_mgr_->set_slot_ylim(tracked_manual_y_slot_, current_y.min, current_y.max);
    };

    // ── Scroll-wheel zoom ────────────────────────────────────────────────
    if (io.MouseWheel != 0.0f)
    {
        const int slot = hovered_slot;
        if (slot > 0)
        {
            remember_active_subplot(slot);
            sync_input_handler_slot(slot);
            // Live mode: adjust the global time window (spread/compress UX).
            if (!subplot_mgr_->is_scroll_paused(slot))
            {
                constexpr double SCROLL_SENSITIVITY = 0.1;
                const double factor = 1.0 - static_cast<double>(io.MouseWheel) * SCROLL_SENSITIVITY;
                const double clamped = std::clamp(factor, 0.8, 1.25);
                const double new_w =
                    std::clamp(subplot_mgr_->time_window() * clamped, 0.5, 86400.0);
                subplot_mgr_->set_time_window(new_w);

                // Also zoom Y axis for the hovered slot via InputHandler.
                // We forward the scroll so the Y-axis gets the normal
                // anchor-based animated zoom while X stays time-window-managed.
                // But to avoid InputHandler also zooming X, we pause auto-scroll
                // first so subsequent InputHandler X zoom has visible effect,
                // then restore.  Simpler: just zoom Y directly.
                const auto* se = subplot_mgr_->slot_entry_pub(slot);
                if (se && se->axes)
                {
                    const auto&  vp       = se->axes->viewport();
                    const auto   ylim     = se->axes->y_limits();
                    const float  norm_y   = (vp.h > 0.0f) ? 1.0f - (my - vp.y) / vp.h : 0.5f;
                    const double anchor_y = ylim.min + norm_y * (ylim.max - ylim.min);
                    const double new_ymin = anchor_y - (anchor_y - ylim.min) * clamped;
                    const double new_ymax = anchor_y + (ylim.max - anchor_y) * clamped;
                    se->axes->ylim(new_ymin, new_ymax);
                    subplot_mgr_->set_slot_ylim(slot, new_ymin, new_ymax);
                }
            }
            else
            {
                // Paused mode: forward to InputHandler for full anchor-based
                // animated zoom on both axes.
                const auto* se = subplot_mgr_->slot_entry_pub(slot);
                const auto  y_before =
                    (se && se->axes) ? se->axes->y_limits() : spectra::AxisLimits{};
                input_handler_.on_scroll(0.0,
                                         static_cast<double>(io.MouseWheel),
                                         static_cast<double>(mx),
                                         static_cast<double>(my));
                se = subplot_mgr_->slot_entry_pub(slot);
                if (se && se->axes)
                {
                    const auto y_after = se->axes->y_limits();
                    if (axis_limits_changed(y_after, y_before))
                        subplot_mgr_->set_slot_ylim(slot, y_after.min, y_after.max);
                }
            }
        }
    }

    // ── Left mouse button (pan) ──────────────────────────────────────────
    const bool left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (left_down && !prev_mouse_left_)
    {
        // Press — pause auto-scroll so the user can inspect previous data,
        // then forward to InputHandler for inertial pan with undo.
        const int slot = hovered_slot;
        if (slot > 0)
        {
            subplot_mgr_->pause_all_scroll();
            begin_manual_y_tracking(slot);
            sync_input_handler_slot(slot);
            input_handler_.on_mouse_button(BTN_LEFT,
                                           ACTION_PRESS,
                                           mods,
                                           static_cast<double>(mx),
                                           static_cast<double>(my));
        }
    }
    else if (!left_down && prev_mouse_left_)
    {
        // Release
        input_handler_.on_mouse_button(BTN_LEFT,
                                       ACTION_RELEASE,
                                       mods,
                                       static_cast<double>(mx),
                                       static_cast<double>(my));
        finish_manual_y_tracking(-1);
    }
    prev_mouse_left_ = left_down;

    // ── Right mouse button (drag zoom) ───────────────────────────────────
    const bool right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (right_down && !prev_mouse_right_)
    {
        // Press — allow zoom while live; only pan or explicit pause exits follow mode.
        const int slot = y_drag_slot;
        if (slot > 0)
        {
            begin_manual_y_tracking(slot);
            sync_input_handler_slot(slot);
            input_handler_.on_mouse_button(BTN_RIGHT,
                                           ACTION_PRESS,
                                           mods,
                                           static_cast<double>(mx),
                                           static_cast<double>(my));
        }
    }
    else if (!right_down && prev_mouse_right_)
    {
        // Release
        input_handler_.on_mouse_button(BTN_RIGHT,
                                       ACTION_RELEASE,
                                       mods,
                                       static_cast<double>(mx),
                                       static_cast<double>(my));
        finish_manual_y_tracking(-1);
    }
    prev_mouse_right_ = right_down;

    // ── Mouse move (always forward for cursor readout + drag updates) ────
    const int interaction_slot = (hovered_slot > 0) ? hovered_slot : y_drag_slot;
    if (interaction_slot > 0)
    {
        remember_active_subplot(interaction_slot);
        sync_input_handler_slot(interaction_slot);
    }
    if (interaction_slot > 0 || prev_mouse_left_ || prev_mouse_right_)
    {
        input_handler_.on_mouse_move(static_cast<double>(mx), static_cast<double>(my));
        sync_tracked_manual_y();
    }
#endif
}

void RosAppShell::draw_bag_info(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (bag_info_)
        bag_info_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_bag_playback(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (bag_playback_panel_)
        bag_playback_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_log_viewer(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (log_viewer_panel_)
        log_viewer_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_diagnostics(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (diag_panel_)
        diag_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_displays_panel(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (displays_panel_)
    {
        DisplayContext context;
        context.fixed_frame     = workspace_state_.fixed_frame;
        context.tf_buffer       = tf_tree_panel_ ? &tf_tree_panel_->buffer() : nullptr;
        context.topic_discovery = discovery_.get();
    #ifdef SPECTRA_USE_ROS2
        context.node = bridge_ ? bridge_->node() : nullptr;
    #endif
        displays_panel_->draw(p_open, display_registry_, context, displays_);
    }
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_node_graph(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (node_graph_panel_)
        node_graph_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_scene_viewport(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (scene_viewport_)
        scene_viewport_->draw(p_open,
                              workspace_state_.fixed_frame,
                              displays_.size(),
                              scene_manager_);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_inspector_panel(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (inspector_panel_)
        inspector_panel_->draw(
            p_open, scene_manager_, workspace_state_.fixed_frame, displays_.size());
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_tf_tree(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (tf_tree_panel_)
        tf_tree_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_param_editor(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (param_editor_)
        param_editor_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_service_caller(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (service_caller_panel_)
        service_caller_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_status_bar()
{
#ifdef SPECTRA_USE_IMGUI
    const ImGuiViewport* vp    = ImGui::GetMainViewport();
    const float          bar_h = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - bar_h),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, bar_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.86f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
    #ifdef IMGUI_HAS_DOCK
                                   ImGuiWindowFlags_NoDocking |
    #endif
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
                                   | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##ros_status_bar", nullptr, flags))
    {
        const uint64_t total = total_messages_.load(std::memory_order_relaxed);
        const int      plots = active_plot_count();
        const size_t   mem   = subplot_mgr_ ? subplot_mgr_->total_memory_bytes() : 0;
        const size_t   pts   = subplot_mgr_ ? subplot_mgr_->total_point_count() : 0;
        const char* fixed_frame =
            workspace_state_.fixed_frame.empty() ? "(auto)" : workspace_state_.fixed_frame.c_str();
        const char* layout_note = layout_unsaved_ ? "  |  Layout: unsaved" : "";

        std::string pts_label;
        if (pts >= 10'000)
            pts_label = std::format("{:.1f}k", static_cast<double>(pts) / 1000.0);
        else
            pts_label = std::to_string(pts);

        ImGui::Text("Node: %s  |  Fixed frame: %s  |  Displays: %zu  |  Messages: %" PRIu64
                    "  |  Active plots: %d  |  Points: %s  |  Buffer: %.1f KB%s",
                    cfg_.node_name.c_str(),
                    fixed_frame,
                    displays_.size(),
                    total,
                    plots,
                    pts_label.c_str(),
                    static_cast<double>(mem) / 1024.0,
                    layout_note);
    }
    ImGui::End();
#endif
}

std::string RosAppShell::window_title() const
{
    return "Spectra ROS2 \xe2\x80\x94 " + cfg_.node_name;
}

bool RosAppShell::add_topic_plot(const std::string& topic_field, const std::string& type_hint)
{
    if (!subplot_mgr_)
        return false;

    std::string topic;
    std::string field_path;

    const auto colon = topic_field.find(':');
    if (colon != std::string::npos)
    {
        topic      = topic_field.substr(0, colon);
        field_path = topic_field.substr(colon + 1);
    }
    else
    {
        topic = topic_field;
    }

    if (topic.empty())
        return false;

    std::string type_name = type_hint;
    if (type_name.empty())
        type_name = detect_topic_type(topic);

    if (field_path.empty())
        field_path = default_numeric_field(topic, type_name);

    if (field_path.empty())
    {
        session_status_msg_   = "No plottable numeric field found for " + topic;
        session_status_timer_ = 3.0f;
        return false;
    }

    int       slot = -1;
    const int cap  = subplot_mgr_->capacity();
    for (int s = 1; s <= cap; ++s)
    {
        if (!subplot_mgr_->has_plot(s))
        {
            slot = s;
            break;
        }
    }

    // If all slots are full, add a new row instead of replacing.
    if (slot < 0)
    {
        slot = subplot_mgr_->add_row();
    }

    const SubplotHandle h = subplot_mgr_->add_plot(slot, topic, field_path, type_name);
    if (!h.valid())
        return false;

    show_plot_area_ = true;
    return true;
}

void RosAppShell::clear_plots()
{
    if (subplot_mgr_)
        subplot_mgr_->clear();
    if (plot_mgr_)
        plot_mgr_->clear();
    next_replace_slot_ = 1;
}

int RosAppShell::active_plot_count() const
{
    int n = 0;
    if (subplot_mgr_)
        n += subplot_mgr_->active_count();
    if (plot_mgr_)
        n += static_cast<int>(plot_mgr_->plot_count());
    return n;
}

void RosAppShell::on_topic_selected(const std::string& topic, const std::string& type_hint)
{
    // Resolve the ROS type: prefer the caller-supplied hint (e.g. from a bag
    // file), falling back to live TopicDiscovery.
    std::string type = type_hint;
    if (type.empty() && discovery_)
    {
        TopicInfo info = discovery_->topic(topic);
        if (!info.types.empty())
            type = info.types.front();
    }

    workspace_state_.select_topic(topic, type);

    if (topic_echo_)
    {
        topic_echo_->set_manually_pinned(false);
        topic_echo_->set_topic(topic, type);
    }
    if (topic_stats_)
        topic_stats_->set_topic(topic);
}

void RosAppShell::on_topic_plot(const std::string& topic)
{
    add_topic_plot(topic);
}

void RosAppShell::subscribe_initial_topics()
{
    for (const auto& t : cfg_.initial_topics)
        add_topic_plot(t);
}

void RosAppShell::refresh_scene_displays(float dt)
{
    scene_manager_.clear();

    if (tf_tree_panel_ && workspace_state_.fixed_frame.empty())
    {
        const TfTreeSnapshot snapshot = tf_tree_panel_->snapshot();
        if (!snapshot.roots.empty())
            workspace_state_.fixed_frame = snapshot.roots.front();
        else if (!snapshot.frames.empty())
            workspace_state_.fixed_frame = snapshot.frames.front().frame_id;
    }

    const bool fixed_frame_changed =
        workspace_state_.fixed_frame != last_display_context_fixed_frame_;
    DisplayContext context;
    context.fixed_frame     = workspace_state_.fixed_frame;
    context.tf_buffer       = tf_tree_panel_ ? &tf_tree_panel_->buffer() : nullptr;
    context.topic_discovery = discovery_.get();
    context.bag_mode        = workspace_state_.clock.is_bag_mode();
    context.bag_playhead_sec = workspace_state_.clock.playhead_sec;
#ifdef SPECTRA_USE_ROS2
    context.node = bridge_ ? bridge_->node() : nullptr;
#endif

    if (bag_display_sync_ && bag_display_sync_->is_open() && bag_player_
        && bag_player_->is_open() && tf_tree_panel_)
    {
        const int64_t start_ns = bag_player_->metadata().start_time_ns;
        context.bag_lookup_time_ns =
            start_ns + static_cast<int64_t>(workspace_state_.clock.playhead_sec * 1e9);

        std::vector<DisplayPlugin*> display_ptrs;
        display_ptrs.reserve(displays_.size());
        for (auto& display : displays_)
        {
            if (display)
                display_ptrs.push_back(display.get());
        }

        bag_display_sync_->sync_to_playhead(workspace_state_.clock.playhead_sec,
                                             start_ns,
                                             tf_tree_panel_->buffer(),
                                             display_ptrs);
    }

    std::unordered_map<DisplayPlugin*, bool> next_activation_state;
    next_activation_state.reserve(displays_.size());

    for (auto& display : displays_)
    {
        if (!display)
            continue;

        auto it     = display_activation_state_.find(display.get());
        bool active = (it != display_activation_state_.end()) ? it->second : display->enabled();

        if (fixed_frame_changed && active)
        {
            display->on_disable();
            active = false;
        }

        if (display->enabled() && !active)
        {
            display->on_enable(context);
            active = true;
        }
        else if (!display->enabled() && active)
        {
            display->on_disable();
            active = false;
        }

        if (display->enabled())
        {
            display->on_update(dt);
            display->submit_renderables(scene_manager_);
        }

        next_activation_state[display.get()] = active;
    }

    display_activation_state_.swap(next_activation_state);
    last_display_context_fixed_frame_ = workspace_state_.fixed_frame;
}

void RosAppShell::draw_display_auxiliary_windows()
{
    for (auto& display : displays_)
    {
        if (display)
            display->draw_auxiliary_ui();
    }
}

void RosAppShell::setup_layout_visibility()
{
    switch (cfg_.layout)
    {
        case LayoutMode::Default:
            current_preset_       = LayoutPreset::Default;
            show_topic_list_      = true;
            show_topic_echo_      = true;
            show_topic_stats_     = true;
            show_plot_area_       = true;
            show_node_graph_      = true;
            show_inspector_panel_ = false;
            show_log_viewer_      = true;
            break;

        case LayoutMode::PlotOnly:
            current_preset_       = LayoutPreset::Default;
            show_topic_list_      = false;
            show_topic_echo_      = false;
            show_topic_stats_     = false;
            show_plot_area_       = true;
            show_inspector_panel_ = false;
            show_log_viewer_      = false;
            break;

        case LayoutMode::Monitor:
            current_preset_       = LayoutPreset::Monitor;
            show_topic_list_      = true;
            show_topic_echo_      = false;
            show_topic_stats_     = true;
            show_plot_area_       = false;
            show_inspector_panel_ = false;
            show_log_viewer_      = true;
            break;

        case LayoutMode::RViz:
            current_preset_       = LayoutPreset::RViz;
            show_displays_panel_  = true;
            show_scene_viewport_  = true;
            show_inspector_panel_ = true;
            show_tf_tree_         = true;
            show_topic_list_      = false;
            show_topic_echo_      = false;
            show_topic_stats_     = false;
            show_plot_area_       = false;
            show_log_viewer_      = false;
            break;

        case LayoutMode::RVizPlot:
            current_preset_       = LayoutPreset::RVizPlot;
            show_displays_panel_  = true;
            show_scene_viewport_  = true;
            show_inspector_panel_ = true;
            show_tf_tree_         = true;
            show_plot_area_       = true;
            show_topic_list_      = true;
            show_topic_echo_      = false;
            show_topic_stats_     = true;
            show_log_viewer_      = false;
            break;
    }
}

void RosAppShell::register_builtin_displays()
{
    display_registry_.register_display<GridDisplay>();
    display_registry_.register_display<TfDisplay>();
    display_registry_.register_display<MarkerDisplay>();
    display_registry_.register_display<PointCloudDisplay>();
    display_registry_.register_display<LaserScanDisplay>();
    display_registry_.register_display<ImageDisplay>();
    display_registry_.register_display<PathDisplay>();
    display_registry_.register_display<PoseDisplay>();
    display_registry_.register_display<RobotModelDisplay>();
    display_registry_.register_display<OccupancyGridDisplay>();
}

void RosAppShell::seed_default_rviz_displays_if_needed()
{
    if (!is_rviz_layout(cfg_.layout) || !displays_.empty())
        return;

    auto grid = display_registry_.create("grid");
    if (!grid)
        return;

    DisplayContext context;
    context.fixed_frame     = workspace_state_.fixed_frame;
    context.tf_buffer       = tf_tree_panel_ ? &tf_tree_panel_->buffer() : nullptr;
    context.topic_discovery = discovery_.get();
#ifdef SPECTRA_USE_ROS2
    context.node = bridge_ ? bridge_->node() : nullptr;
#endif

    if (grid->enabled())
    {
        grid->on_enable(context);
        display_activation_state_[grid.get()] = true;
    }

    displays_.push_back(std::move(grid));
}

// ---------------------------------------------------------------------------
// Layout presets
// ---------------------------------------------------------------------------

/*static*/ const char* RosAppShell::layout_preset_name(LayoutPreset p)
{
    switch (p)
    {
        case LayoutPreset::Default:
            return "Default";
        case LayoutPreset::Debug:
            return "Debug";
        case LayoutPreset::Monitor:
            return "Monitor";
        case LayoutPreset::BagReview:
            return "Bag Review";
        case LayoutPreset::RViz:
            return "RViz";
        case LayoutPreset::RVizPlot:
            return "RViz + Plot";
    }
    return "Default";
}

void RosAppShell::apply_layout_preset(LayoutPreset preset)
{
    current_preset_ = preset;

    // Hide everything first; each preset enables what it needs.
    show_topic_list_      = false;
    show_topic_echo_      = false;
    show_topic_stats_     = false;
    show_plot_area_       = false;
    show_bag_info_        = false;
    show_bag_playback_    = false;
    show_log_viewer_      = false;
    show_diagnostics_     = false;
    show_displays_panel_  = false;
    show_node_graph_      = false;
    show_scene_viewport_  = false;
    show_inspector_panel_ = false;
    show_tf_tree_         = false;
    show_param_editor_    = false;
    show_service_caller_  = false;

    switch (preset)
    {
        case LayoutPreset::Default:
            show_topic_list_  = true;
            show_topic_echo_  = true;
            show_topic_stats_ = true;
            show_plot_area_   = true;
            show_node_graph_  = true;
            show_log_viewer_  = true;
            break;

        case LayoutPreset::Debug:
            // Topic List + Echo + Log Viewer — ideal for live debugging.
            show_topic_list_  = true;
            show_topic_echo_  = true;
            show_log_viewer_  = true;
            show_topic_stats_ = true;
            break;

        case LayoutPreset::Monitor:
            // Multi-subplot + Diagnostics + Stats — ops dashboard.
            show_plot_area_   = true;
            show_diagnostics_ = true;
            show_topic_stats_ = true;
            show_topic_list_  = true;
            break;

        case LayoutPreset::BagReview:
            // Bag controls + subplots — offline data review.
            show_bag_playback_ = true;
            show_bag_info_     = true;
            show_plot_area_    = true;
            break;

        case LayoutPreset::RViz:
            show_displays_panel_  = true;
            show_scene_viewport_  = true;
            show_inspector_panel_ = true;
            show_tf_tree_         = true;
            show_topic_list_      = true;
            break;

        case LayoutPreset::RVizPlot:
            show_displays_panel_  = true;
            show_scene_viewport_  = true;
            show_inspector_panel_ = true;
            show_tf_tree_         = true;
            show_plot_area_       = true;
            show_topic_list_      = true;
            show_topic_stats_     = true;
            break;
    }

    // Force a dock-layout rebuild on the next frame.
    dock_layout_initialized_           = false;
    layout_change_tracking_enabled_      = false;
    layout_settle_frames_                = 0;
    layout_unsaved_                      = false;
}

RosSession RosAppShell::capture_session() const
{
    RosSession s;
    s.node_name       = cfg_.node_name;
    s.node_ns         = cfg_.node_ns;
    s.layout          = layout_mode_name(cfg_.layout);
    s.subplot_rows    = cfg_.subplot_rows;
    s.subplot_cols    = cfg_.subplot_cols;
    s.time_window_s   = subplot_mgr_ ? subplot_mgr_->time_window() : cfg_.time_window_s;
    s.pruning_enabled = subplot_mgr_ ? subplot_mgr_->pruning_enabled()
                                     : (plot_mgr_ ? plot_mgr_->pruning_enabled() : true);
    s.prune_buffer_s  = subplot_mgr_ ? subplot_mgr_->prune_buffer()
                                     : (plot_mgr_ ? plot_mgr_->prune_buffer() : 20.0);
    s.fixed_frame     = workspace_state_.fixed_frame;

    s.panels.topic_list      = show_topic_list_;
    s.panels.topic_echo      = show_topic_echo_;
    s.panels.topic_stats     = show_topic_stats_;
    s.panels.plot_area       = show_plot_area_;
    s.panels.bag_info        = show_bag_info_;
    s.panels.bag_playback    = show_bag_playback_;
    s.panels.log_viewer      = show_log_viewer_;
    s.panels.diagnostics     = show_diagnostics_;
    s.panels.displays_panel  = show_displays_panel_;
    s.panels.node_graph      = show_node_graph_;
    s.panels.scene_viewport  = show_scene_viewport_;
    s.panels.inspector_panel = show_inspector_panel_;
    s.panels.tf_tree         = show_tf_tree_;
    s.panels.param_editor    = show_param_editor_;
    s.panels.service_caller  = show_service_caller_;
    s.panels.nav_rail        = show_nav_rail_;

    s.nav_rail_expanded = nav_rail_expanded_;
    s.nav_rail_width    = nav_rail_width_;

    if (topic_list_)
    {
        const auto columns        = topic_list_->column_visibility();
        s.topic_monitor.show_type = columns.show_type;
        s.topic_monitor.show_hz   = columns.show_hz;
        s.topic_monitor.show_pubs = columns.show_pubs;
        s.topic_monitor.show_subs = columns.show_subs;
        s.topic_monitor.show_bw   = columns.show_bw;
    }

    if (scene_viewport_)
    {
        const auto& camera      = scene_viewport_->camera();
        s.camera_pose.azimuth   = camera.azimuth;
        s.camera_pose.elevation = camera.elevation;
        s.camera_pose.distance  = camera.distance;
        s.camera_pose.target    = {
            camera.target.x,
            camera.target.y,
            camera.target.z,
        };
        s.camera_pose.projection =
            camera.projection_mode == spectra::Camera::ProjectionMode::Orthographic ? "orthographic"
                                                                                    : "perspective";
        s.camera_pose.fov = camera.fov;

        const auto& background   = scene_viewport_->background_rgba();
        s.scene_background_color = {
            background[0],
            background[1],
            background[2],
            background[3],
        };
    }

    // Persist the most recently cached ImGui docking layout.
    // The cache is updated in draw() whenever ImGui sets WantSaveIniSettings.
    s.imgui_ini_data = cached_imgui_ini_;

    if (subplot_mgr_)
    {
        for (const auto& h : subplot_mgr_->handles())
        {
            if (!h.valid())
                continue;

            const auto* slot_entry = subplot_mgr_->slot_entry_pub(h.slot);
            const bool  is_primary = (slot_entry != nullptr) && slot_entry->series == h.series;
            const SeriesEntry* matched_extra = nullptr;
            if (slot_entry && !is_primary)
            {
                for (const auto& es : slot_entry->extra_series)
                {
                    if (es && es->series == h.series)
                    {
                        matched_extra = es.get();
                        break;
                    }
                }
            }

            SubscriptionEntry e;
            e.topic         = h.topic;
            e.field_path    = h.field_path;
            e.type_name     = is_primary ? slot_entry->type_name
                                         : (matched_extra ? matched_extra->type_name : "");
            e.subplot_slot  = h.slot;
            e.time_window_s = subplot_mgr_->time_window();
            e.scroll_paused = subplot_mgr_->is_scroll_paused(h.slot);
            e.axis_mode = is_primary && slot_entry ? slot_entry->axis_mode : AxisMode::TimeSeries;
            if (is_primary && slot_entry)
            {
                e.x_field_path = slot_entry->x_field_path;
                e.y_field_path = slot_entry->y_field_path;
            }
            s.subscriptions.push_back(std::move(e));
        }
    }

    if (plot_mgr_)
    {
        for (const auto& h : plot_mgr_->handles())
        {
            if (!h.valid())
                continue;
            SubscriptionEntry e;
            e.topic         = h.topic;
            e.field_path    = h.field_path;
            e.subplot_slot  = 0;
            e.time_window_s = plot_mgr_->time_window();
            e.scroll_paused = plot_mgr_->is_scroll_paused(h.id);
            s.subscriptions.push_back(std::move(e));
        }
    }

    for (const auto& display : displays_)
    {
        if (!display)
            continue;
        DisplaySessionEntry entry;
        entry.type_id     = display->type_id();
        entry.topic       = display->topic();
        entry.enabled     = display->enabled();
        entry.config_blob = display->serialize_config_blob();
        s.displays.push_back(std::move(entry));
    }

    return s;
}

void RosAppShell::apply_subscription_entry(const SubscriptionEntry& e)
{
    if (e.topic.empty())
        return;

    if (e.subplot_slot > 0 && subplot_mgr_)
    {
        const auto h = subplot_mgr_->add_plot(e.subplot_slot, e.topic, e.field_path, e.type_name);
        if (!h.valid())
            return;

        const bool has_axis_restore = (e.axis_mode != AxisMode::TimeSeries)
                                      || !e.x_field_path.empty() || !e.y_field_path.empty();
        if (has_axis_restore)
        {
            const std::string restored_x =
                (e.axis_mode == AxisMode::CustomAxes && !e.x_field_path.empty())
                    ? e.x_field_path
                    : std::string(AXIS_SOURCE_TIME);
            const std::string restored_y = e.y_field_path.empty() ? e.field_path : e.y_field_path;

            std::string error;
            if (!subplot_mgr_->configure_slot_axes(e.subplot_slot,
                                                   e.axis_mode,
                                                   restored_x,
                                                   restored_y,
                                                   &error))
            {
                session_status_msg_ =
                    "Plot restore fallback for slot " + std::to_string(e.subplot_slot) + ": "
                    + error;
                session_status_timer_ = 4.0f;
            }
        }

        if (e.scroll_paused)
            subplot_mgr_->pause_scroll(e.subplot_slot);
    }
    else if (plot_mgr_)
    {
        const auto h = plot_mgr_->add_plot(e.topic, e.field_path, e.type_name);
        if (h.valid() && e.scroll_paused)
            plot_mgr_->pause_scroll(h.id);
    }
}

void RosAppShell::apply_session(const RosSession& session)
{
    if (!session.layout.empty())
        cfg_.layout = parse_layout_mode(session.layout);

    if (session.time_window_s > 0.0)
    {
        if (subplot_mgr_)
            subplot_mgr_->set_time_window(session.time_window_s);
        if (plot_mgr_)
            plot_mgr_->set_time_window(session.time_window_s);
    }
    if (subplot_mgr_)
    {
        subplot_mgr_->set_pruning_enabled(session.pruning_enabled);
        subplot_mgr_->set_prune_buffer(session.prune_buffer_s);
    }
    if (plot_mgr_)
    {
        plot_mgr_->set_pruning_enabled(session.pruning_enabled);
        plot_mgr_->set_prune_buffer(session.prune_buffer_s);
    }

    show_topic_list_             = session.panels.topic_list;
    show_topic_echo_             = session.panels.topic_echo;
    show_topic_stats_            = session.panels.topic_stats;
    show_plot_area_              = session.panels.plot_area;
    show_bag_info_               = session.panels.bag_info;
    show_bag_playback_           = session.panels.bag_playback;
    show_log_viewer_             = session.panels.log_viewer;
    show_diagnostics_            = session.panels.diagnostics;
    show_displays_panel_         = session.panels.displays_panel;
    show_node_graph_             = session.panels.node_graph;
    show_scene_viewport_         = session.panels.scene_viewport;
    show_inspector_panel_        = session.panels.inspector_panel;
    show_tf_tree_                = session.panels.tf_tree;
    show_param_editor_           = session.panels.param_editor;
    show_service_caller_         = session.panels.service_caller;
    show_nav_rail_               = session.panels.nav_rail;
    workspace_state_.fixed_frame = session.fixed_frame;

    nav_rail_expanded_ = session.nav_rail_expanded;
    set_nav_rail_width(static_cast<float>(session.nav_rail_width));

    if (topic_list_)
    {
        topic_list_->set_column_visibility({
            .show_type = session.topic_monitor.show_type,
            .show_hz   = session.topic_monitor.show_hz,
            .show_pubs = session.topic_monitor.show_pubs,
            .show_subs = session.topic_monitor.show_subs,
            .show_bw   = session.topic_monitor.show_bw,
        });
    }

    if (scene_viewport_)
    {
        spectra::Camera camera;
        camera.reset();
        camera.set_azimuth(static_cast<float>(session.camera_pose.azimuth));
        camera.set_elevation(static_cast<float>(session.camera_pose.elevation));
        camera.set_distance(static_cast<float>(session.camera_pose.distance));
        camera.set_target({
            session.camera_pose.target[0],
            session.camera_pose.target[1],
            session.camera_pose.target[2],
        });
        camera.set_fov(static_cast<float>(session.camera_pose.fov));
        camera.set_projection(session.camera_pose.projection == "orthographic"
                                  ? spectra::Camera::ProjectionMode::Orthographic
                                  : spectra::Camera::ProjectionMode::Perspective);
        scene_viewport_->set_camera(camera);
        scene_viewport_->set_background_rgba({
            static_cast<float>(session.scene_background_color[0]),
            static_cast<float>(session.scene_background_color[1]),
            static_cast<float>(session.scene_background_color[2]),
            static_cast<float>(session.scene_background_color[3]),
        });
    }

    // Queue the ImGui layout for restoration before the next DockSpace() call.
    // If the session was saved before layout persistence was added, this is
    // empty and apply_default_dock_layout() will run instead.
    if (!session.imgui_ini_data.empty())
    {
        pending_imgui_ini_       = session.imgui_ini_data;
        dock_layout_initialized_ = true;   // skip default layout; ini will apply
    }
    else
    {
        dock_layout_initialized_ = false;   // rebuild default layout
    }

    if (subplot_mgr_)
        subplot_mgr_->clear();
    if (plot_mgr_)
        plot_mgr_->clear();
    for (auto& display : displays_)
    {
        if (display)
            display->on_destroy();
    }
    display_activation_state_.clear();
    displays_.clear();
    scene_manager_.clear();

    for (const auto& e : session.subscriptions)
        apply_subscription_entry(e);

    DisplayContext context;
    context.fixed_frame     = workspace_state_.fixed_frame;
    context.tf_buffer       = tf_tree_panel_ ? &tf_tree_panel_->buffer() : nullptr;
    context.topic_discovery = discovery_.get();
#ifdef SPECTRA_USE_ROS2
    context.node = bridge_ ? bridge_->node() : nullptr;
#endif
    for (const auto& e : session.displays)
    {
        auto display = display_registry_.create(e.type_id);
        if (!display)
            continue;
        display->set_topic(e.topic);
        display->deserialize_config_blob(e.config_blob);
        display->set_enabled(e.enabled);
        if (display->enabled())
        {
            display->on_enable(context);
            display_activation_state_[display.get()] = true;
        }
        displays_.push_back(std::move(display));
    }

    last_display_context_fixed_frame_ = workspace_state_.fixed_frame;
}

SaveResult RosAppShell::save_session(const std::string& path)
{
    if (!session_mgr_)
    {
        SaveResult r;
        r.error = "session manager not initialised";
        return r;
    }
    return session_mgr_->save(capture_session(), path);
}

LoadResult RosAppShell::load_session(const std::string& path)
{
    if (!session_mgr_)
    {
        LoadResult r;
        r.error = "session manager not initialised";
        return r;
    }

    LoadResult lr = session_mgr_->load(path);
    if (lr.ok)
    {
        apply_session(lr.session);
        session_status_msg_   = "Session loaded";
        session_status_timer_ = 3.0f;
    }
    return lr;
}

LoadResult RosAppShell::merge_session(const std::string& path)
{
    if (!session_mgr_)
    {
        LoadResult r;
        r.error = "session manager not initialised";
        return r;
    }

    LoadResult lr = session_mgr_->load(path);
    if (!lr.ok)
        return lr;

    const RosSession current = capture_session();
    const RosSession merged  = merge_sessions(current, lr.session);

    size_t added_subs = 0;
    for (const auto& e : merged.subscriptions)
    {
        const bool existed = std::any_of(current.subscriptions.begin(),
                                         current.subscriptions.end(),
                                         [&](const SubscriptionEntry& existing)
                                         {
                                             return existing.topic == e.topic
                                                    && existing.field_path == e.field_path
                                                    && existing.subplot_slot == e.subplot_slot;
                                         });
        if (!existed)
        {
            apply_subscription_entry(e);
            ++added_subs;
        }
    }

    session_status_msg_   = "Imported " + std::to_string(added_subs) + " plot subscription(s)";
    session_status_timer_ = 3.0f;
    return lr;
}

void RosAppShell::draw_session_save_dialog()
{
#ifdef SPECTRA_USE_IMGUI
    if (!show_session_save_dialog_)
        return;

    ImGui::SetNextWindowSize(ImVec2(520, 130), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::Begin("Save Session##g3",
                     &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::Text("Path:");
        ImGui::SameLine();

        static char path_buf[512];
        if (ImGui::IsWindowAppearing())
        {
            session_save_path_buf_.copy(path_buf, sizeof(path_buf) - 1);
            path_buf[std::min(session_save_path_buf_.size(), sizeof(path_buf) - 1)] = '\0';
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##session_path", path_buf, sizeof(path_buf));
        session_save_path_buf_ = path_buf;

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(80, 0)))
        {
            SaveResult sr             = save_session(session_save_path_buf_);
            session_status_msg_       = sr.ok ? "Session saved" : ("Save failed: " + sr.error);
            session_status_timer_     = 3.0f;
            if (sr.ok)
                layout_unsaved_ = false;
            show_session_save_dialog_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            show_session_save_dialog_ = false;
    }
    ImGui::End();

    if (!open)
        show_session_save_dialog_ = false;
#endif
}

void RosAppShell::draw_session_load_dialog()
{
#ifdef SPECTRA_USE_IMGUI
    if (!show_session_load_dialog_)
        return;

    ImGui::SetNextWindowSize(ImVec2(520, 130), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::Begin("Load Session##g3",
                     &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::Text("Path:");
        ImGui::SameLine();

        static char load_buf[512];
        if (ImGui::IsWindowAppearing())
        {
            session_save_path_buf_.copy(load_buf, sizeof(load_buf) - 1);
            load_buf[std::min(session_save_path_buf_.size(), sizeof(load_buf) - 1)] = '\0';
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##session_load_path", load_buf, sizeof(load_buf));

        ImGui::Spacing();
        if (ImGui::Button("Load", ImVec2(80, 0)))
        {
            LoadResult lr = load_session(load_buf);
            if (!lr.ok)
            {
                session_status_msg_   = "Load failed: " + lr.error;
                session_status_timer_ = 3.0f;
            }
            show_session_load_dialog_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            show_session_load_dialog_ = false;
    }
    ImGui::End();

    if (!open)
        show_session_load_dialog_ = false;
#endif
}

void RosAppShell::draw_session_merge_dialog()
{
#ifdef SPECTRA_USE_IMGUI
    if (!show_session_merge_dialog_)
        return;

    ImGui::SetNextWindowSize(ImVec2(520, 150), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::Begin("Import Session##merge",
                     &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::TextWrapped(
            "Import plot subscriptions from another session without replacing the current layout.");
        ImGui::Text("Path:");
        ImGui::SameLine();

        static char merge_buf[512];
        if (ImGui::IsWindowAppearing())
        {
            session_save_path_buf_.copy(merge_buf, sizeof(merge_buf) - 1);
            merge_buf[std::min(session_save_path_buf_.size(), sizeof(merge_buf) - 1)] = '\0';
        }

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##session_merge_path", merge_buf, sizeof(merge_buf));

        ImGui::Spacing();
        if (ImGui::Button("Import", ImVec2(80, 0)))
        {
            LoadResult lr = merge_session(merge_buf);
            if (!lr.ok)
            {
                session_status_msg_   = "Import failed: " + lr.error;
                session_status_timer_ = 3.0f;
            }
            show_session_merge_dialog_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            show_session_merge_dialog_ = false;
    }
    ImGui::End();

    if (!open)
        show_session_merge_dialog_ = false;
#endif
}

std::string RosAppShell::detect_topic_type(const std::string& topic) const
{
    if (discovery_ && discovery_->has_topic(topic))
    {
        const TopicInfo ti = discovery_->topic(topic);
        if (!ti.types.empty())
            return ti.types.front();
    }

    // Do NOT fall back to node_->get_topic_names_and_types() here — that
    // call goes through the DDS graph layer and can block the render thread
    // for hundreds of milliseconds (or indefinitely during discovery).
    // The TopicDiscovery cache is refreshed periodically and is sufficient.

    return {};
}

std::string RosAppShell::default_numeric_field(const std::string& topic,
                                               const std::string& type_hint) const
{
    std::string type_name = type_hint;
    if (type_name.empty())
        type_name = detect_topic_type(topic);

    if (type_name.empty() || !intr_)
        return {};

    auto schema = intr_->introspect(type_name);
    if (!schema)
        return {};

    const auto numeric = schema->numeric_paths();
    if (numeric.empty())
        return {};

    return numeric.front();
}

}   // namespace spectra::adapters::ros2

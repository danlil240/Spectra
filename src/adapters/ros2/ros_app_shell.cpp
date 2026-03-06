#include "ros_app_shell.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <string_view>

#include "ui/layout/layout_manager.hpp"

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
    return text == "1" || text == "true" || text == "TRUE"
        || text == "yes" || text == "YES"
        || text == "on" || text == "ON";
}

constexpr float kPlotAreaMinWindowWidth    = 900.0f;
constexpr float kPlotAreaMinWindowHeight   = 200.0f;
constexpr float kPlotAreaTimeSliderMinWidth = 180.0f;
constexpr float kPlotAreaTimeSliderMaxWidth = 320.0f;
constexpr float kPlotAreaMinViewportHeight = 120.0f;
constexpr float kPlotAreaGlobalDropHeight  = 56.0f;
constexpr float kPlotAreaButtonSpacing     = 8.0f;
}   // namespace

LayoutMode parse_layout_mode(const std::string& s)
{
    if (s == "plot-only") return LayoutMode::PlotOnly;
    if (s == "monitor")   return LayoutMode::Monitor;
    return LayoutMode::Default;
}

const char* layout_mode_name(LayoutMode m)
{
    switch (m)
    {
        case LayoutMode::Default:  return "default";
        case LayoutMode::PlotOnly: return "plot-only";
        case LayoutMode::Monitor:  return "monitor";
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
                "[--layout default|plot-only|monitor] [--window-s SECONDS] "
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

        if ((arg == "--layout" || arg == "-l") && i + 1 < argc)
        {
            cfg.layout = parse_layout_mode(argv[++i]);
            continue;
        }

        if ((arg == "--window-s" || arg == "-w") && i + 1 < argc)
        {
            try { cfg.time_window_s = std::stod(argv[++i]); }
            catch (...) { error_out = "Invalid --window-s value"; return cfg; }
            continue;
        }

        if ((arg == "--node-name" || arg == "-n") && i + 1 < argc)
        {
            cfg.node_name = argv[++i];
            continue;
        }

        if (arg == "--rows" && i + 1 < argc)
        {
            try { cfg.subplot_rows = std::stoi(argv[++i]); }
            catch (...) { error_out = "Invalid --rows value"; return cfg; }
            continue;
        }

        if (arg == "--cols" && i + 1 < argc)
        {
            try { cfg.subplot_cols = std::stoi(argv[++i]); }
            catch (...) { error_out = "Invalid --cols value"; return cfg; }
            continue;
        }
    }

    if (cfg.subplot_rows < 1) cfg.subplot_rows = 1;
    if (cfg.subplot_cols < 1) cfg.subplot_cols = 1;
    if (cfg.time_window_s < RosPlotManager::MIN_WINDOW_S)
        cfg.time_window_s = RosPlotManager::MIN_WINDOW_S;
    if (cfg.time_window_s > RosPlotManager::MAX_WINDOW_S)
        cfg.time_window_s = RosPlotManager::MAX_WINDOW_S;

    return cfg;
}

RosAppShell::RosAppShell(const RosAppConfig& cfg)
    : cfg_(cfg)
{
    screenshot_export_ = std::make_unique<RosScreenshotExport>();
    session_mgr_       = std::make_unique<RosSessionManager>();
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

    plot_mgr_ = std::make_unique<RosPlotManager>(*bridge_, *intr_);
    subplot_mgr_ = std::make_unique<SubplotManager>(
        *bridge_, *intr_, cfg_.subplot_rows, cfg_.subplot_cols, canvas_figure_);
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
    topic_echo_->set_title("Topic Echo");

    bag_info_ = std::make_unique<BagInfoPanel>();
    bag_info_->set_title("Bag Info");

    bag_player_ = std::make_unique<BagPlayer>(*plot_mgr_, *intr_);
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

    node_graph_panel_ = std::make_unique<NodeGraphPanel>();
    node_graph_panel_->set_title("Node Graph");
    node_graph_panel_->set_topic_discovery(discovery_.get());

    tf_tree_panel_ = std::make_unique<TfTreePanel>();
    tf_tree_panel_->set_title("TF Tree");
    tf_tree_panel_->set_node(bridge_->node());
    tf_tree_panel_->start();

    param_editor_ = std::make_unique<ParamEditorPanel>(bridge_->node());
    param_editor_->set_title("Parameter Editor");
    param_editor_->set_live_edit(true);
    param_editor_->set_target_node(bridge_->node()->get_fully_qualified_name());

    service_caller_ =
        std::make_unique<ServiceCaller>(bridge_->node(), intr_.get(), discovery_.get());
    service_caller_panel_ = std::make_unique<ServiceCallerPanel>(service_caller_.get());
    service_caller_panel_->set_title("Service Caller");

    bag_info_->set_topic_select_callback(
        [this](const std::string& topic, const std::string& type)
        {
            // Pass the bag-provided type directly so discovery is not needed.
            on_topic_selected(topic, type);
        });
    bag_info_->set_topic_plot_callback(
        [this](const std::string& topic, const std::string& /*type*/)
        {
            add_topic_plot(topic);
        });
    bag_info_->set_bag_opened_callback(
        [this](const std::string& path)
        {
            if (bag_player_ && bag_player_->open(path))
            {
                show_bag_playback_ = true;
            }
            else if (bag_player_)
            {
                session_status_msg_ = std::string("Bag open failed: ") + bag_player_->last_error();
                session_status_timer_ = 3.0f;
            }
        });

    topic_list_->set_topic_discovery(discovery_.get());

    // Automatically create lightweight subscriptions for every discovered
    // topic so the Topic Monitor can show Hz and BW for all topics, not just
    // the ones that are plotted or currently selected in the echo panel.
    discovery_->set_topic_callback(
        [this](const TopicInfo& info, bool added)
        {
            if (added)
            {
                // Create a monitoring subscription for Hz/BW tracking.
                if (info.types.empty())
                    return;
                auto node = bridge_->node();
                auto qos  = rclcpp::QoS(rclcpp::KeepLast(1))
                                .best_effort()
                                .durability_volatile();
                const std::string topic = info.name;
                const std::string type  = info.types.front();
                try
                {
                    auto sub = node->create_generic_subscription(
                        info.name,
                        type,
                        qos,
                        [this, topic](std::shared_ptr<rclcpp::SerializedMessage> msg)
                        {
                            const size_t bytes = msg ? msg->size() : 0;
                            if (topic_list_)
                                topic_list_->notify_message(topic, bytes);
                            if (topic_stats_)
                                topic_stats_->notify_message(topic, bytes, -1);
                        });
                    std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                    monitor_subs_[info.name] = std::move(sub);
                }
                catch (const std::exception& e)
                {
                    // Subscription creation can fail for topics with
                    // unavailable type support — silently ignore.
                    (void)e;
                }
            }
            else
            {
                // Topic removed — drop the monitoring subscription.
                std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                monitor_subs_.erase(info.name);
            }
        });

    plot_mgr_->set_on_data(
        [this](int id, double /*t*/, double /*v*/)
        {
            ++total_messages_;
            const PlotHandle h = plot_mgr_->handle(id);
            if (!h.valid()) return;
            // Skip if a monitor subscription already handles notify.
            bool has_monitor = false;
            {
                std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                has_monitor = monitor_subs_.count(h.topic) > 0;
            }
            if (!has_monitor)
            {
                if (topic_list_)
                    topic_list_->notify_message(h.topic, sizeof(double));
                if (topic_stats_)
                    topic_stats_->notify_message(h.topic, sizeof(double), -1);
            }
        });

    subplot_mgr_->set_on_data(
        [this](int slot, double /*t*/, double /*v*/)
        {
            ++total_messages_;
            const SubplotHandle h = subplot_mgr_->handle(slot);
            if (!h.valid())
                return;
            // Skip if a monitor subscription already handles notify.
            bool has_monitor = false;
            {
                std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                has_monitor = monitor_subs_.count(h.topic) > 0;
            }
            if (!has_monitor)
            {
                if (topic_list_)
                    topic_list_->notify_message(h.topic, sizeof(double));
                if (topic_stats_)
                    topic_stats_->notify_message(h.topic, sizeof(double), -1);
            }
        });

    wire_panel_callbacks();
    setup_layout_visibility();

    bridge_->start_spin();
    discovery_->start();

    subscribe_initial_topics();

    if (!cfg_.bag_file.empty())
    {
        bag_info_->open_bag(cfg_.bag_file);
        show_bag_info_ = true;
        if (bag_player_ && bag_player_->open(cfg_.bag_file))
            show_bag_playback_ = true;
    }

    return true;
}

void RosAppShell::shutdown()
{
    if (!bridge_) return;

    if (session_mgr_ && !session_mgr_->last_path().empty())
        session_mgr_->auto_save(capture_session());

    if (diag_panel_)
        diag_panel_->stop();
    if (tf_tree_panel_)
        tf_tree_panel_->stop();

    bag_playback_panel_.reset();
    bag_player_.reset();
    service_caller_panel_.reset();
    service_caller_.reset();
    param_editor_.reset();
    tf_tree_panel_.reset();
    node_graph_panel_.reset();
    diag_panel_.reset();
    log_viewer_panel_.reset();
    log_viewer_.reset();
    bag_info_.reset();

    subplot_mgr_.reset();
    plot_mgr_.reset();
    topic_echo_.reset();
    topic_stats_.reset();
    topic_list_.reset();
    drag_drop_.reset();

    // Drop all monitoring subscriptions before destroying the node.
    {
        std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
        monitor_subs_.clear();
    }

    if (discovery_)
    {
        discovery_->stop();
        discovery_.reset();
    }

    bridge_->shutdown();
    bridge_.reset();
    intr_.reset();
}

void RosAppShell::poll()
{
    if (!bridge_) return;

    if (!rclcpp::ok())
    {
        request_shutdown();
        return;
    }

    const double now_s = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Wall-clock time for scroll controllers (must be system_clock to match
    // ROS2 message timestamps which use wall time or header.stamp).
    const double wall_now_s = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    double dt = 1.0 / 60.0;
    if (last_poll_time_s_ > 0.0)
        dt = std::clamp(now_s - last_poll_time_s_, 1.0 / 300.0, 0.25);
    last_poll_time_s_ = now_s;

    if (subplot_mgr_)
    {
        // Establish a shared time origin on first poll so all subplots sync.
        if (!subplot_mgr_->has_shared_time_origin())
            subplot_mgr_->set_shared_time_origin(wall_now_s);
        subplot_mgr_->set_now(wall_now_s);
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

    // Advance InputHandler animations (inertial pan, animated zoom, etc.).
    input_handler_.update(static_cast<float>(dt));

    if (session_status_timer_ > 0.0f)
        session_status_timer_ = std::max(0.0f, session_status_timer_ - static_cast<float>(dt));
}

void RosAppShell::draw()
{
#ifdef SPECTRA_USE_IMGUI
    if (shutdown_requested()) return;

    // Reset per-frame workspace events so panels see a clean state.
    workspace_state_.reset_events();

#ifdef IMGUI_HAS_DOCK
    // Cache ImGui layout whenever ImGui requests a settings save.
    // This lets capture_session() include layout data without calling
    // SaveIniSettingsToMemory at shutdown (when ImGui may already be torn down).
    if (ImGui::GetIO().WantSaveIniSettings)
    {
        size_t ini_sz = 0;
        const char* ini_ptr = ImGui::SaveIniSettingsToMemory(&ini_sz);
        if (ini_ptr && ini_sz > 0)
            cached_imgui_ini_.assign(ini_ptr, ini_sz);
        ImGui::GetIO().WantSaveIniSettings = false;
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

    draw_status_bar();

    if (show_record_dialog_ && screenshot_export_)
        screenshot_export_->draw_record_dialog(&show_record_dialog_);

    if ((ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) &&
        (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) &&
        ImGui::IsKeyPressed(ImGuiKey_S, false) && screenshot_export_)
    {
        const std::string path =
            RosScreenshotExport::make_screenshot_path("/tmp", "spectra_ros");
        screenshot_export_->take_screenshot(path);
    }

    draw_session_save_dialog();
    draw_session_load_dialog();

    if (session_status_timer_ > 0.0f && !session_status_msg_.empty())
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f,
                   vp->WorkPos.y + vp->WorkSize.y - 52.0f),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.82f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
#ifdef IMGUI_HAS_DOCK
            ImGuiWindowFlags_NoDocking |
#endif
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ros_session_toast", nullptr, flags))
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s", session_status_msg_.c_str());
        ImGui::End();
    }

    if (screenshot_export_ && screenshot_export_->screenshot_toast_active())
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f,
                   vp->WorkPos.y + vp->WorkSize.y - 12.0f),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.78f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
#ifdef IMGUI_HAS_DOCK
            ImGuiWindowFlags_NoDocking |
#endif
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ros_ss_toast", nullptr, flags))
        {
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "Screenshot saved");
            const auto& p = screenshot_export_->last_screenshot_path();
            if (!p.empty())
            {
                const auto sep = p.rfind('/');
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
        ImGui::LoadIniSettingsFromMemory(pending_imgui_ini_.c_str(),
                                         pending_imgui_ini_.size());
        pending_imgui_ini_.clear();
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menu_h = ImGui::GetFrameHeightWithSpacing();
    const float status_h = ImGui::GetFrameHeight();
    const float rail_w = show_nav_rail_ ? (nav_rail_expanded_ ? nav_rail_width_ : nav_rail_collapsed_w_)
                                        : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + rail_w, vp->WorkPos.y + menu_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(std::max(320.0f, vp->WorkSize.x - rail_w),
               std::max(240.0f, vp->WorkSize.y - menu_h - status_h)),
        ImGuiCond_Always);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    const ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    host_flags |= ImGuiWindowFlags_NoBackground;

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

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menu_h = ImGui::GetFrameHeightWithSpacing();
    const float status_h = ImGui::GetFrameHeight();
    const float rail_w = show_nav_rail_ ? (nav_rail_expanded_ ? nav_rail_width_ : nav_rail_collapsed_w_)
                                        : 0.0f;
    ImGui::DockBuilderSetNodeSize(
        dockspace_id_,
        ImVec2(std::max(320.0f, vp->WorkSize.x - rail_w),
               std::max(240.0f, vp->WorkSize.y - menu_h - status_h)));

    ImGuiID dock_main         = dockspace_id_;
    ImGuiID dock_left         = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.22f, nullptr, &dock_main);
    ImGuiID dock_right        = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.31f, nullptr, &dock_main);
    ImGuiID dock_bottom       = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.38f, nullptr, &dock_main);
    ImGuiID dock_right_bottom = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.42f, nullptr, &dock_right);
    ImGuiID dock_bottom_left  = ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Left, 0.28f, nullptr, &dock_bottom);
    ImGuiID dock_bottom_right = ImGui::DockBuilderSplitNode(dock_bottom, ImGuiDir_Right, 0.42f, nullptr, &dock_bottom);

    ImGui::DockBuilderDockWindow("Topic Monitor", dock_left);
    ImGui::DockBuilderDockWindow("Plot Area", dock_main);
    ImGui::DockBuilderDockWindow("Topic Statistics", dock_right);
    ImGui::DockBuilderDockWindow("Node Graph###NodeGraphPanel", dock_right_bottom);

    ImGui::DockBuilderDockWindow("Topic Echo", dock_bottom);
    ImGui::DockBuilderDockWindow("Bag Info", dock_bottom_left);
    ImGui::DockBuilderDockWindow("ROS2 Log", dock_bottom_right);

    ImGui::DockBuilderDockWindow("Bag Playback", dock_bottom);
    ImGui::DockBuilderDockWindow("Diagnostics", dock_right);
    ImGui::DockBuilderDockWindow("TF Tree", dock_right_bottom);
    ImGui::DockBuilderDockWindow("Parameter Editor", dock_right);
    ImGui::DockBuilderDockWindow("Service Caller", dock_bottom);

    ImGui::DockBuilderFinish(dockspace_id_);
    dock_layout_initialized_ = true;
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

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menu_h = ImGui::GetFrameHeightWithSpacing();
    const float status_h = ImGui::GetFrameHeight();
    const float rail_w = nav_rail_expanded_ ? nav_rail_width_ : nav_rail_collapsed_w_;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + menu_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(rail_w, std::max(1.0f, vp->WorkSize.y - menu_h - status_h)),
        ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
#ifdef IMGUI_HAS_DOCK
        ImGuiWindowFlags_NoDocking |
#endif
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##ros_nav_rail", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button(nav_rail_expanded_ ? "<<" : ">>"))
        nav_rail_expanded_ = !nav_rail_expanded_;

    if (nav_rail_expanded_)
    {
        ImGui::SameLine();
        if (ImGui::Button("Hide Rail"))
            show_nav_rail_ = false;
    }

    auto toggle_btn = [&](const char* compact,
                          const char* full,
                          bool& panel_flag)
    {
        const bool was_active = panel_flag;
        if (was_active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

        const char* lbl = nav_rail_expanded_ ? full : compact;
        ImVec2 btn_size = nav_rail_expanded_ ? ImVec2(-1.0f, 0.0f)
                                             : ImVec2(rail_w - 14.0f, 0.0f);
        if (ImGui::Button(lbl, btn_size))
            panel_flag = !panel_flag;

        if (was_active)
            ImGui::PopStyleColor();

        if (!nav_rail_expanded_ && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", full);
    };

    ImGui::SeparatorText("Core");
    toggle_btn("TP", "Topic Monitor", show_topic_list_);
    toggle_btn("PL", "Plot Area", show_plot_area_);
    toggle_btn("EC", "Topic Echo", show_topic_echo_);
    toggle_btn("ST", "Topic Statistics", show_topic_stats_);

    ImGui::SeparatorText("Tools");
    toggle_btn("BI", "Bag Info", show_bag_info_);
    toggle_btn("BP", "Bag Playback", show_bag_playback_);
    toggle_btn("LG", "Log Viewer", show_log_viewer_);
    toggle_btn("DG", "Diagnostics", show_diagnostics_);

    ImGui::SeparatorText("Advanced");
    toggle_btn("NG", "Node Graph", show_node_graph_);
    toggle_btn("TF", "TF Tree", show_tf_tree_);
    toggle_btn("PE", "Parameter Editor", show_param_editor_);
    toggle_btn("SV", "Service Caller", show_service_caller_);

    if (nav_rail_expanded_)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Rail Width");
        ImGui::SliderFloat("##ros_nav_width", &nav_rail_width_, 180.0f, 360.0f, "%.0f px");

        if (ImGui::Button("Reset Layout", ImVec2(-1.0f, 0.0f)))
            dock_layout_initialized_ = false;
    }

    ImGui::End();
#endif
}

void RosAppShell::draw_topic_list(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (topic_list_) topic_list_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_topic_echo(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (topic_echo_) topic_echo_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_topic_stats(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (topic_stats_) topic_stats_->draw(p_open);
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

    // Fixed-size raw payload for ImGui drag-drop (mirrors field_drag_drop.cpp).
    struct RawPayload {
        char topic_name[256]{};
        char field_path[256]{};
        char type_name[128]{};
        char label[320]{};
    };

    if (ImGui::Begin("Plot Area", p_open, flags))
    {
        const int active = subplot_mgr_ ? subplot_mgr_->active_count() : 0;
        const int cap    = subplot_mgr_ ? subplot_mgr_->capacity() : 0;
        ImGui::Text("Active plots: %d / %d", active, cap);
        float canvas_top    = ImGui::GetCursorScreenPos().y;
        float canvas_bottom = canvas_top;
        const auto same_line_button = []() {
            ImGui::SameLine(0.0f, kPlotAreaButtonSpacing);
        };

        if (subplot_mgr_)
        {
            float tw = static_cast<float>(subplot_mgr_->time_window());
            ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x,
                                               kPlotAreaTimeSliderMinWidth,
                                               kPlotAreaTimeSliderMaxWidth));
            if (ImGui::SliderFloat("##TimeWindow", &tw, 1.0f, 3600.0f, "%.1f s"))
            {
                const double new_tw = static_cast<double>(tw);
                subplot_mgr_->set_time_window(new_tw);
                if (plot_mgr_) plot_mgr_->set_time_window(new_tw);

                // When any subplot is paused, apply the new time window as
                // a zoom centred on the current view so the user sees an
                // actual zoom instead of a no-op slider change.
                for (int s = 1; s <= subplot_mgr_->capacity(); ++s)
                {
                    if (!subplot_mgr_->is_scroll_paused(s)) continue;
                    auto* se = subplot_mgr_->slot_entry_pub(s);
                    if (!se || !se->axes) continue;
                    auto xl = se->axes->x_limits();
                    double mid = (xl.min + xl.max) * 0.5;
                    se->axes->xlim(mid - new_tw * 0.5, mid + new_tw * 0.5);
                }
            }
            ImGui::TextDisabled("Time Window");

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Live All")) subplot_mgr_->resume_all_scroll();
            ImGui::PopStyleColor(2);
            same_line_button();
            if (ImGui::Button("Pause All")) subplot_mgr_->pause_all_scroll();
            same_line_button();
            if (ImGui::Button("Reset (R)"))
                reset_plot_display();
            same_line_button();
            if (ImGui::Button("Auto Y (A)"))
                restore_plot_autofit(workspace_state_.active_subplot_idx);

            same_line_button();
            if (ImGui::Button("+ Add Subplot"))
            {
                subplot_mgr_->add_row();
            }
            same_line_button();
            ImGui::BeginDisabled(subplot_mgr_->rows() <= 1);
            {
                if (ImGui::Button("- Remove Last"))
                    subplot_mgr_->remove_last_row();
            }
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        canvas_top = ImGui::GetCursorScreenPos().y;

        // Per-subplot controls and targeted drop zones.
        if (subplot_mgr_)
        {
            const int total_slots = subplot_mgr_->capacity();
            const auto& fig_style = subplot_mgr_->figure().style();
            const float min_slot_height =
                std::max(120.0f,
                         fig_style.margin_top + fig_style.margin_bottom
                             + kPlotAreaMinViewportHeight);
            const float avail_h = ImGui::GetContentRegionAvail().y;
            // Reserve space for the global drop zone at the bottom.
            const float reserved_bottom =
                kPlotAreaGlobalDropHeight + ImGui::GetTextLineHeightWithSpacing()
                + ImGui::GetStyle().ItemSpacing.y + 12.0f;
            const float usable_h = std::max(0.0f, avail_h - reserved_bottom);
            const float slot_h = (total_slots > 0)
                ? std::max(min_slot_height, usable_h / static_cast<float>(total_slots))
                : usable_h;

            for (int s = 1; s <= total_slots; ++s)
            {
                ImGui::PushID(s);
                const float slot_start_y = ImGui::GetCursorPosY();

                const bool has = subplot_mgr_->has_plot(s);
                const int  n_series = subplot_mgr_->slot_series_count(s);

                // Slot header — compact single line with controls.
                ImGui::BeginGroup();

                if (has)
                {
                    // First line: plot title + action buttons, no series text.
                    const bool selected = (workspace_state_.active_subplot_idx == s);
                    ImGui::TextColored(selected ? ImVec4(0.6f, 0.95f, 0.55f, 1.0f)
                                                : ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                                       "Plot %d", s);
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
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
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
                    char ylim_popup_id[32];
                    std::snprintf(ylim_popup_id, sizeof(ylim_popup_id), "ylim_%d", s);
                    if (ImGui::SmallButton("Y Limits"))
                    {
                        remember_active_subplot(s);
                        ImGui::OpenPopup(ylim_popup_id);
                    }

                    if (ImGui::BeginPopup(ylim_popup_id))
                    {
                        ImGui::TextDisabled("Y-Axis Limits for Plot %d", s);
                        ImGui::Separator();

                        auto* slot_entry = subplot_mgr_->slot_entry_pub(s);
                        if (slot_entry && slot_entry->axes)
                        {
                            auto yl = slot_entry->axes->y_limits();
                            float ymin_f = static_cast<float>(yl.min);
                            float ymax_f = static_cast<float>(yl.max);

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
                    char style_popup_id[32];
                    std::snprintf(style_popup_id, sizeof(style_popup_id), "style_%d", s);
                    if (ImGui::SmallButton("Style"))
                    {
                        remember_active_subplot(s);
                        ImGui::OpenPopup(style_popup_id);
                    }

                    if (ImGui::BeginPopup(style_popup_id))
                    {
                        auto* slot_entry = subplot_mgr_->slot_entry_pub(s);
                        if (slot_entry && slot_entry->axes)
                        {
                            auto* axes = slot_entry->axes;

                            char title_buf[128];
                            std::strncpy(title_buf, axes->title().c_str(), sizeof(title_buf) - 1);
                            title_buf[sizeof(title_buf) - 1] = '\0';
                            if (ImGui::InputText("Title", title_buf, sizeof(title_buf)))
                                axes->title(title_buf);

                            char xlabel_buf[128];
                            std::strncpy(xlabel_buf, axes->xlabel().c_str(), sizeof(xlabel_buf) - 1);
                            xlabel_buf[sizeof(xlabel_buf) - 1] = '\0';
                            if (ImGui::InputText("X Label", xlabel_buf, sizeof(xlabel_buf)))
                                axes->xlabel(xlabel_buf);

                            char ylabel_buf[128];
                            std::strncpy(ylabel_buf, axes->ylabel().c_str(), sizeof(ylabel_buf) - 1);
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
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    if (ImGui::SmallButton("Clear##plot"))
                    {
                        remember_active_subplot(s);
                        subplot_mgr_->clear_slot_data(s);
                    }

                    // Series details on a second line, indented.
                    for (int si = 0; si < n_series; ++si)
                    {
                        const auto* se = subplot_mgr_->slot_series(s, si);
                        if (!se) continue;
                        ImGui::Indent(16.0f);
                        ImGui::TextColored(
                            ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                            "%s/%s", se->topic.c_str(), se->field_path.c_str());
                        ImGui::SameLine();
                        char rm_id[32];
                        std::snprintf(rm_id, sizeof(rm_id), "x##rm_%d_%d", s, si);
                        if (ImGui::SmallButton(rm_id))
                        {
                            subplot_mgr_->remove_series_from_slot(
                                s, se->topic, se->field_path);
                            if (!subplot_mgr_->has_plot(s))
                                subplot_mgr_->compact();
                        }
                        ImGui::Unindent(16.0f);
                    }
                }
                else
                {
                    ImGui::TextDisabled("Plot %d (empty)  - Drop a topic/field here", s);
                }

                ImGui::EndGroup();

                // Drop zone — fills remaining slot height so subplots
                // expand to use all available vertical space.
                {
                    const float used_h = ImGui::GetCursorPosY() - slot_start_y;
                    const float remaining = std::max(8.0f, slot_h - used_h);
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const float drop_w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                    ImGui::InvisibleButton("##slot_drop", ImVec2(drop_w, remaining));

                    // Accept drag-drop payloads targeted at this slot.
                    if (drag_drop_ && drag_drop_->is_dragging())
                    {
                        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const ImVec2 pmax = {pos.x + drop_w, pos.y + std::min(remaining, 8.0f)};

                        if (hovered)
                        {
                            dl->AddRectFilled(pos, pmax, IM_COL32(60, 180, 255, 80), 3.0f);
                            dl->AddRect(pos, pmax, IM_COL32(60, 180, 255, 200), 3.0f, 0, 2.0f);
                        }

                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* imgui_payload =
                                    ImGui::AcceptDragDropPayload(FieldDragDrop::DRAG_TYPE))
                            {
                                if (imgui_payload->DataSize < static_cast<int>(sizeof(RawPayload)))
                                {
                                    ImGui::EndDragDropTarget();
                                    continue;
                                }
                                const auto* raw = static_cast<const RawPayload*>(imgui_payload->Data);
                                std::string topic = raw->topic_name;
                                std::string field = raw->field_path;
                                std::string type  = raw->type_name;

                                if (!topic.empty())
                                {
                                    if (field.empty())
                                        field = default_numeric_field(topic, type);
                                    if (!field.empty())
                                        subplot_mgr_->add_plot(s, topic, field, type);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                    }

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
            ImVec2 avail = ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x),
                                  kPlotAreaGlobalDropHeight);

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##global_drop", avail);

            if (drag_drop_ && drag_drop_->is_dragging())
            {
                const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 pmax = {pos.x + avail.x, pos.y + avail.y};
                dl->AddRectFilled(pos, pmax,
                                  hovered ? IM_COL32(60, 200, 100, 55) : IM_COL32(60, 200, 100, 22),
                                  4.0f);
                if (hovered)
                {
                    dl->AddRect(pos, pmax, IM_COL32(60, 200, 100, 220), 4.0f, 0, 2.0f);
                    const char* lbl = "Drop to create new subplot";
                    const ImVec2 text_sz = ImGui::CalcTextSize(lbl);
                    dl->AddText({pos.x + (avail.x - text_sz.x) * 0.5f,
                                 pos.y + (avail.y - text_sz.y) * 0.5f},
                                IM_COL32(60, 200, 100, 240), lbl);
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* imgui_payload =
                            ImGui::AcceptDragDropPayload(FieldDragDrop::DRAG_TYPE))
                    {
                        if (imgui_payload->DataSize >= static_cast<int>(sizeof(RawPayload)))
                        {
                            const auto* raw = static_cast<const RawPayload*>(imgui_payload->Data);
                            std::string topic = raw->topic_name;
                            std::string field = raw->field_path;
                            std::string type  = raw->type_name;

                            if (!topic.empty())
                            {
                                if (field.empty())
                                    field = default_numeric_field(topic, type);
                                if (!field.empty())
                                {
                                    // Find first empty slot, or add a new row.
                                    int target_slot = -1;
                                    if (subplot_mgr_)
                                    {
                                        for (int s = 1; s <= subplot_mgr_->capacity(); ++s)
                                        {
                                            if (!subplot_mgr_->has_plot(s))
                                            {
                                                target_slot = s;
                                                break;
                                            }
                                        }
                                        if (target_slot < 0)
                                            target_slot = subplot_mgr_->add_row();
                                    }
                                    if (target_slot > 0)
                                        subplot_mgr_->add_plot(target_slot, topic, field, type);
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }

        // Forward mouse/scroll events to the core InputHandler which provides
        // animated zoom, inertial pan, box zoom, undo, axis linking, and more.
        handle_plot_shortcuts();
        bridge_imgui_to_input_handler();

        // Canvas override for Vulkan rendering.
        if (layout_manager_)
        {
            const ImVec2 wpos         = ImGui::GetWindowPos();
            const ImVec2 content_min  = ImGui::GetWindowContentRegionMin();
            const ImVec2 content_max  = ImGui::GetWindowContentRegionMax();
            const float  canvas_x     = wpos.x + content_min.x;
            const float  canvas_w     = std::max(0.0f, content_max.x - content_min.x);
            const float  plot_top     = canvas_top;
            const float  plot_bottom  = std::max(canvas_top, canvas_bottom);
            const float  canvas_h     = plot_bottom - plot_top;
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
    const int cap = subplot_mgr_->capacity();
    for (int s = 1; s <= cap; ++s)
    {
        const auto* se = subplot_mgr_->slot_entry_pub(s);
        if (!se || !se->axes)
            continue;
        const auto& vp = se->axes->viewport();
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
    tracked_manual_y_slot_   = -1;
    tracked_manual_y_valid_  = false;

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
    auto* se = subplot_mgr_->slot_entry_pub(tracked_slot);
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

    const ImGuiIO& io = ImGui::GetIO();
    const float mx = io.MousePos.x;
    const float my = io.MousePos.y;
    const int hovered_slot = hit_test_subplot_slot(mx, my);
    const int y_drag_slot = hit_test_subplot_slot(mx, my, true);
    const bool plot_window_hovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
                               | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    if (!plot_window_hovered && hovered_slot <= 0 && y_drag_slot <= 0
        && !prev_mouse_left_ && !prev_mouse_right_)
        return;

    // GLFW-compatible constants expected by InputHandler.
    constexpr int BTN_LEFT    = 0;
    constexpr int BTN_RIGHT   = 1;
    constexpr int ACTION_PRESS   = 1;
    constexpr int ACTION_RELEASE = 0;
    constexpr int MOD_CTRL  = 0x0002;
    constexpr int MOD_SHIFT = 0x0001;

    int mods = 0;
    if (io.KeyCtrl)  mods |= MOD_CTRL;
    if (io.KeyShift) mods |= MOD_SHIFT;

    const auto sync_input_handler_slot = [&](int slot) {
        if (slot < 1)
            return;
        const auto* se = subplot_mgr_->slot_entry_pub(slot);
        if (!se || !se->axes)
            return;
        input_handler_.set_active_axes(se->axes);
        const auto& vp = se->axes->viewport();
        input_handler_.set_viewport(vp.x, vp.y, vp.w, vp.h);
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
                const double factor =
                    1.0 - static_cast<double>(io.MouseWheel) * SCROLL_SENSITIVITY;
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
                    const auto& vp = se->axes->viewport();
                    const auto ylim = se->axes->y_limits();
                    const float norm_y = (vp.h > 0.0f) ? 1.0f - (my - vp.y) / vp.h : 0.5f;
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
                const auto y_before = (se && se->axes) ? se->axes->y_limits()
                                                       : spectra::AxisLimits{};
                input_handler_.on_scroll(0.0, static_cast<double>(io.MouseWheel),
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
            input_handler_.on_mouse_button(BTN_LEFT, ACTION_PRESS, mods,
                                           static_cast<double>(mx),
                                           static_cast<double>(my));
        }
    }
    else if (!left_down && prev_mouse_left_)
    {
        // Release
        input_handler_.on_mouse_button(BTN_LEFT, ACTION_RELEASE, mods,
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
            input_handler_.on_mouse_button(BTN_RIGHT, ACTION_PRESS, mods,
                                           static_cast<double>(mx),
                                           static_cast<double>(my));
        }
    }
    else if (!right_down && prev_mouse_right_)
    {
        // Release
        input_handler_.on_mouse_button(BTN_RIGHT, ACTION_RELEASE, mods,
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
        input_handler_.on_mouse_move(static_cast<double>(mx),
                                     static_cast<double>(my));
    }
#endif
}

void RosAppShell::draw_bag_info(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (bag_info_) bag_info_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_bag_playback(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (bag_playback_panel_) bag_playback_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_log_viewer(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (log_viewer_panel_) log_viewer_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_diagnostics(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (diag_panel_) diag_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_node_graph(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (node_graph_panel_) node_graph_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_tf_tree(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (tf_tree_panel_) tf_tree_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_param_editor(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (param_editor_) param_editor_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_service_caller(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (service_caller_panel_) service_caller_panel_->draw(p_open);
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_status_bar()
{
#ifdef SPECTRA_USE_IMGUI
    const ImGuiViewport* vp  = ImGui::GetMainViewport();
    const float bar_h        = ImGui::GetFrameHeight();

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - bar_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, bar_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.86f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
#ifdef IMGUI_HAS_DOCK
        ImGuiWindowFlags_NoDocking |
#endif
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##ros_status_bar", nullptr, flags))
    {
        const uint64_t total = total_messages_.load(std::memory_order_relaxed);
        const int plots = active_plot_count();
        const size_t mem = subplot_mgr_ ? subplot_mgr_->total_memory_bytes() : 0;

        ImGui::Text("Node: %s  |  Messages: %" PRIu64 "  |  Active plots: %d  |  Buffer: %.1f KB",
                    cfg_.node_name.c_str(),
                    total,
                    plots,
                    static_cast<double>(mem) / 1024.0);
    }
    ImGui::End();
#endif
}

void RosAppShell::draw_menu_bar()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Navigation Rail", nullptr, &show_nav_rail_);
        if (show_nav_rail_)
            ImGui::MenuItem("Expand Rail", nullptr, &nav_rail_expanded_);

        ImGui::SeparatorText("Core Panels");
        ImGui::MenuItem("Topic Monitor", nullptr, &show_topic_list_);
        ImGui::MenuItem("Topic Echo", nullptr, &show_topic_echo_);
        ImGui::MenuItem("Topic Statistics", nullptr, &show_topic_stats_);
        ImGui::MenuItem("Plot Area", nullptr, &show_plot_area_);

        ImGui::SeparatorText("Tools");
        ImGui::MenuItem("Bag Info", nullptr, &show_bag_info_);
        ImGui::MenuItem("Bag Playback", nullptr, &show_bag_playback_);
        ImGui::MenuItem("Log Viewer", nullptr, &show_log_viewer_);
        ImGui::MenuItem("Diagnostics", nullptr, &show_diagnostics_);

        ImGui::SeparatorText("Advanced");
        ImGui::MenuItem("Node Graph", nullptr, &show_node_graph_);
        ImGui::MenuItem("TF Tree", nullptr, &show_tf_tree_);
        ImGui::MenuItem("Parameter Editor", nullptr, &show_param_editor_);
        ImGui::MenuItem("Service Caller", nullptr, &show_service_caller_);

        ImGui::Separator();
        if (ImGui::MenuItem("Reset Dock Layout"))
            dock_layout_initialized_ = false;

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Layout"))
    {
        if (ImGui::MenuItem("Reset Docked Layout"))
            dock_layout_initialized_ = false;
        ImGui::SeparatorText("Presets");
        draw_layout_preset_menu();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Plots"))
    {
        // Add the current workspace selection to the plot.
        const bool has_topic = !workspace_state_.selected_topic.empty();
        const bool has_field = !workspace_state_.selected_field.empty();
        const char* add_label = has_field ? "Add Selected Field to Plot"
                                          : "Add Selected Topic to Plot";
        if (ImGui::MenuItem(add_label, nullptr, false, has_topic))
            workspace_state_.request_plot();
        ImGui::Separator();

        if (subplot_mgr_)
        {
            if (ImGui::MenuItem("Add Subplot Row"))
                subplot_mgr_->add_row();
            if (ImGui::MenuItem("Remove Last Row", nullptr, false, subplot_mgr_->rows() > 1))
                subplot_mgr_->remove_last_row();
            ImGui::Separator();
        }

        if (ImGui::MenuItem("Clear All Plots"))
            clear_plots();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Basic Display", "R"))
            reset_plot_display();
        if (ImGui::MenuItem("Auto-Fit Y", "A", false, subplot_mgr_ != nullptr))
            restore_plot_autofit(workspace_state_.active_subplot_idx);
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle Pause/Live", "Space", false, subplot_mgr_ != nullptr))
        {
            int slot = workspace_state_.active_subplot_idx;
            if (subplot_mgr_ && slot >= 1 && slot <= subplot_mgr_->capacity())
            {
                if (subplot_mgr_->is_scroll_paused(slot))
                    subplot_mgr_->resume_scroll(slot);
                else
                    subplot_mgr_->pause_scroll(slot);
            }
            else if (subplot_mgr_)
            {
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
        if (ImGui::MenuItem("Resume All Scroll", "Home"))
        {
            if (subplot_mgr_) subplot_mgr_->resume_all_scroll();
        }
        if (ImGui::MenuItem("Pause All Scroll"))
        {
            if (subplot_mgr_) subplot_mgr_->pause_all_scroll();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle Grid", "G", false, subplot_mgr_ != nullptr))
        {
            int slot = workspace_state_.active_subplot_idx;
            if (subplot_mgr_ && slot >= 1 && slot <= subplot_mgr_->capacity())
            {
                auto* se = subplot_mgr_->slot_entry_pub(slot);
                if (se && se->axes)
                    se->axes->grid(!se->axes->grid_enabled());
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Session"))
    {
        if (ImGui::MenuItem("Save Session", "Ctrl+Shift+W"))
            show_session_save_dialog_ = true;
        if (ImGui::MenuItem("Save Session As..."))
        {
            session_save_path_buf_ = RosSessionManager::default_session_path(cfg_.node_name);
            show_session_save_dialog_ = true;
        }
        if (ImGui::MenuItem("Load Session..."))
            show_session_load_dialog_ = true;

        ImGui::Separator();

        if (ImGui::BeginMenu("Recent Sessions"))
        {
            // Quick-switch: load any of the last 5 sessions with one click.
            draw_recent_sessions_menu();

            auto recent = session_mgr_->load_recent();
            if (!recent.empty())
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Clear Recent"))
                    session_mgr_->clear_recent();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Screenshot",
                            "Ctrl+Shift+S",
                            false,
                            screenshot_export_ != nullptr))
        {
            const std::string path =
                RosScreenshotExport::make_screenshot_path("/tmp", "spectra_ros");
            if (screenshot_export_)
                screenshot_export_->take_screenshot(path);
        }
        if (ImGui::MenuItem("Record Video...",
                            nullptr,
                            &show_record_dialog_,
                            screenshot_export_ != nullptr))
        {
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
#endif
}

std::string RosAppShell::window_title() const
{
    return "Spectra ROS2 \xe2\x80\x94 " + cfg_.node_name;
}

bool RosAppShell::add_topic_plot(const std::string& topic_field)
{
    if (!subplot_mgr_)
        return false;

    std::string topic;
    std::string field_path;

    const auto colon = topic_field.find(':');
    if (colon != std::string::npos)
    {
        topic = topic_field.substr(0, colon);
        field_path = topic_field.substr(colon + 1);
    }
    else
    {
        topic = topic_field;
    }

    if (topic.empty())
        return false;

    const std::string type_name = detect_topic_type(topic);

    if (field_path.empty())
        field_path = default_numeric_field(topic, type_name);

    if (field_path.empty())
    {
        session_status_msg_ = "No plottable numeric field found for " + topic;
        session_status_timer_ = 3.0f;
        return false;
    }

    int slot = -1;
    const int cap = subplot_mgr_->capacity();
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
    if (subplot_mgr_) subplot_mgr_->clear();
    if (plot_mgr_) plot_mgr_->clear();
    next_replace_slot_ = 1;
}

int RosAppShell::active_plot_count() const
{
    int n = 0;
    if (subplot_mgr_) n += subplot_mgr_->active_count();
    if (plot_mgr_)    n += static_cast<int>(plot_mgr_->plot_count());
    return n;
}

void RosAppShell::on_topic_selected(const std::string& topic,
                                    const std::string& type_hint)
{
    // Resolve the ROS type: prefer the caller-supplied hint (e.g. from a bag
    // file), falling back to live TopicDiscovery.
    std::string type = type_hint;
    if (type.empty() && discovery_)
    {
        TopicInfo info = discovery_->topic(topic);
        if (!info.types.empty()) type = info.types.front();
    }

    workspace_state_.select_topic(topic, type);

    if (topic_echo_)  topic_echo_->set_topic(topic, type);
    if (topic_stats_) topic_stats_->set_topic(topic);
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

void RosAppShell::wire_panel_callbacks()
{
    if (topic_list_)
    {
        topic_list_->set_select_callback(
            [this](const std::string& topic)
            {
                on_topic_selected(topic);
            });

        topic_list_->set_plot_callback(
            [this](const std::string& topic)
            {
                on_topic_plot(topic);
            });
    }

    drag_drop_ = std::make_unique<FieldDragDrop>();
    drag_drop_->set_plot_request_callback(
        [this](const FieldDragPayload& payload, PlotTarget target)
        {
            handle_plot_request(payload, target);
        });

    if (topic_list_) topic_list_->set_drag_drop(drag_drop_.get());
    if (topic_echo_) topic_echo_->set_drag_drop(drag_drop_.get());

    // Forward echo panel message arrivals to topic stats and topic list so
    // that statistics update for the selected topic even when it isn't plotted.
    if (topic_echo_)
    {
        topic_echo_->set_message_callback(
            [this](const std::string& topic, size_t bytes)
            {
                // Only forward to topic_list / topic_stats if there is no
                // active monitor subscription for this topic — the monitor
                // sub already calls notify_message, so forwarding here too
                // would double-count and inflate the displayed Hz.
                bool has_monitor = false;
                {
                    std::lock_guard<std::mutex> lk(monitor_subs_mutex_);
                    has_monitor = monitor_subs_.count(topic) > 0;
                }
                if (!has_monitor)
                {
                    if (topic_stats_)
                        topic_stats_->notify_message(topic, bytes, -1);
                    if (topic_list_)
                        topic_list_->notify_message(topic, bytes);
                }
            });
    }

    if (node_graph_panel_)
    {
        node_graph_panel_->set_select_callback(
            [this](const GraphNode& n)
            {
                if (n.kind == GraphNodeKind::Topic)
                {
                    on_topic_selected(n.id);
                }
                else if (param_editor_)
                {
                    param_editor_->set_target_node(n.id);
                    show_param_editor_ = true;
                }
            });

        node_graph_panel_->set_activate_callback(
            [this](const GraphNode& n)
            {
                if (n.kind == GraphNodeKind::Topic)
                    add_topic_plot(n.id);
            });
    }
}

void RosAppShell::handle_plot_request(const FieldDragPayload& payload,
                                      PlotTarget target)
{
    if (!payload.valid()) return;

    std::string topic_field = payload.topic_name;
    if (!payload.field_path.empty())
        topic_field += ':' + payload.field_path;

    // If workspace has an active subplot selection, target that slot.
    if (target == PlotTarget::CurrentAxes && workspace_state_.active_subplot_idx > 0
        && subplot_mgr_)
    {
        const int slot = workspace_state_.active_subplot_idx;
        std::string field = payload.field_path;
        if (field.empty())
            field = default_numeric_field(payload.topic_name, payload.type_name);
        if (!field.empty())
        {
            subplot_mgr_->add_plot(slot, payload.topic_name, field, payload.type_name);
            show_plot_area_ = true;
            return;
        }
    }

    add_topic_plot(topic_field);
}

void RosAppShell::setup_layout_visibility()
{
    switch (cfg_.layout)
    {
        case LayoutMode::Default:
            show_topic_list_  = true;
            show_topic_echo_  = true;
            show_topic_stats_ = true;
            show_plot_area_   = true;
            show_log_viewer_  = false;
            break;

        case LayoutMode::PlotOnly:
            show_topic_list_  = false;
            show_topic_echo_  = false;
            show_topic_stats_ = false;
            show_plot_area_   = true;
            show_log_viewer_  = false;
            break;

        case LayoutMode::Monitor:
            show_topic_list_  = true;
            show_topic_echo_  = true;
            show_topic_stats_ = true;
            show_plot_area_   = false;
            show_log_viewer_  = true;
            break;
    }
}

// ---------------------------------------------------------------------------
// Layout presets
// ---------------------------------------------------------------------------

/*static*/ const char* RosAppShell::layout_preset_name(LayoutPreset p)
{
    switch (p)
    {
        case LayoutPreset::Default:   return "Default";
        case LayoutPreset::Debug:     return "Debug";
        case LayoutPreset::Monitor:   return "Monitor";
        case LayoutPreset::BagReview: return "Bag Review";
    }
    return "Default";
}

void RosAppShell::apply_layout_preset(LayoutPreset preset)
{
    current_preset_ = preset;

    // Hide everything first; each preset enables what it needs.
    show_topic_list_     = false;
    show_topic_echo_     = false;
    show_topic_stats_    = false;
    show_plot_area_      = false;
    show_bag_info_       = false;
    show_bag_playback_   = false;
    show_log_viewer_     = false;
    show_diagnostics_    = false;
    show_node_graph_     = false;
    show_tf_tree_        = false;
    show_param_editor_   = false;
    show_service_caller_ = false;

    switch (preset)
    {
        case LayoutPreset::Default:
            show_topic_list_  = true;
            show_topic_echo_  = true;
            show_topic_stats_ = true;
            show_plot_area_   = true;
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
    }

    // Force a dock-layout rebuild on the next frame.
    dock_layout_initialized_ = false;
}

// ---------------------------------------------------------------------------
// draw_layout_preset_menu (inline submenu items)
// ---------------------------------------------------------------------------

void RosAppShell::draw_layout_preset_menu()
{
#ifdef SPECTRA_USE_IMGUI
    static const LayoutPreset kPresets[] = {
        LayoutPreset::Default,
        LayoutPreset::Debug,
        LayoutPreset::Monitor,
        LayoutPreset::BagReview,
    };
    for (const auto p : kPresets)
    {
        const bool sel = (current_preset_ == p);
        if (ImGui::MenuItem(layout_preset_name(p), nullptr, sel))
            apply_layout_preset(p);
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_recent_sessions_menu (inline submenu items)
// ---------------------------------------------------------------------------

void RosAppShell::draw_recent_sessions_menu()
{
#ifdef SPECTRA_USE_IMGUI
    if (!session_mgr_)
    {
        ImGui::TextDisabled("(no session manager)");
        return;
    }
    const auto recents = session_mgr_->load_recent();
    if (recents.empty())
    {
        ImGui::TextDisabled("(no recent sessions)");
        return;
    }
    for (size_t i = 0; i < recents.size() && i < 5; ++i)
    {
        const std::string& path = recents[i].path;
        // Show only the filename part as label; node name as shortcut hint.
        const auto slash = path.rfind('/');
        const std::string label = (slash != std::string::npos)
            ? path.substr(slash + 1) : path;
        if (ImGui::MenuItem(label.c_str(), recents[i].node.c_str()))
        {
            const auto result = load_session(path);
            if (result.ok)
            {
                session_status_msg_   = "Loaded: " + label;
                session_status_timer_ = 3.0f;
            }
            else
            {
                session_status_msg_   = "Load failed: " + result.error;
                session_status_timer_ = 4.0f;
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", path.c_str());
    }
#endif
}

RosSession RosAppShell::capture_session() const
{
    RosSession s;
    s.node_name     = cfg_.node_name;
    s.node_ns       = cfg_.node_ns;
    s.layout        = layout_mode_name(cfg_.layout);
    s.subplot_rows  = cfg_.subplot_rows;
    s.subplot_cols  = cfg_.subplot_cols;
    s.time_window_s = subplot_mgr_ ? subplot_mgr_->time_window()
                                   : cfg_.time_window_s;

    s.panels.topic_list      = show_topic_list_;
    s.panels.topic_echo      = show_topic_echo_;
    s.panels.topic_stats     = show_topic_stats_;
    s.panels.plot_area       = show_plot_area_;
    s.panels.bag_info        = show_bag_info_;
    s.panels.bag_playback    = show_bag_playback_;
    s.panels.log_viewer      = show_log_viewer_;
    s.panels.diagnostics     = show_diagnostics_;
    s.panels.node_graph      = show_node_graph_;
    s.panels.tf_tree         = show_tf_tree_;
    s.panels.param_editor    = show_param_editor_;
    s.panels.service_caller  = show_service_caller_;
    s.panels.nav_rail        = show_nav_rail_;

    s.nav_rail_expanded = nav_rail_expanded_;
    s.nav_rail_width    = nav_rail_width_;

    // Persist the most recently cached ImGui docking layout.
    // The cache is updated in draw() whenever ImGui sets WantSaveIniSettings.
    s.imgui_ini_data = cached_imgui_ini_;

    if (subplot_mgr_)
    {
        for (const auto& h : subplot_mgr_->handles())
        {
            if (!h.valid()) continue;
            SubscriptionEntry e;
            e.topic         = h.topic;
            e.field_path    = h.field_path;
            e.subplot_slot  = h.slot;
            e.time_window_s = subplot_mgr_->time_window();
            e.scroll_paused = subplot_mgr_->is_scroll_paused(h.slot);
            s.subscriptions.push_back(std::move(e));
        }
    }

    if (plot_mgr_)
    {
        for (const auto& h : plot_mgr_->handles())
        {
            if (!h.valid()) continue;
            SubscriptionEntry e;
            e.topic         = h.topic;
            e.field_path    = h.field_path;
            e.subplot_slot  = 0;
            e.time_window_s = plot_mgr_->time_window();
            e.scroll_paused = plot_mgr_->is_scroll_paused(h.id);
            s.subscriptions.push_back(std::move(e));
        }
    }

    return s;
}

void RosAppShell::apply_session(const RosSession& session)
{
    if (session.time_window_s > 0.0)
    {
        if (subplot_mgr_) subplot_mgr_->set_time_window(session.time_window_s);
        if (plot_mgr_)    plot_mgr_->set_time_window(session.time_window_s);
    }

    show_topic_list_     = session.panels.topic_list;
    show_topic_echo_     = session.panels.topic_echo;
    show_topic_stats_    = session.panels.topic_stats;
    show_plot_area_      = session.panels.plot_area;
    show_bag_info_       = session.panels.bag_info;
    show_bag_playback_   = session.panels.bag_playback;
    show_log_viewer_     = session.panels.log_viewer;
    show_diagnostics_    = session.panels.diagnostics;
    show_node_graph_     = session.panels.node_graph;
    show_tf_tree_        = session.panels.tf_tree;
    show_param_editor_   = session.panels.param_editor;
    show_service_caller_ = session.panels.service_caller;
    show_nav_rail_       = session.panels.nav_rail;

    nav_rail_expanded_ = session.nav_rail_expanded;
    set_nav_rail_width(static_cast<float>(session.nav_rail_width));

    // Queue the ImGui layout for restoration before the next DockSpace() call.
    // If the session was saved before layout persistence was added, this is
    // empty and apply_default_dock_layout() will run instead.
    if (!session.imgui_ini_data.empty())
    {
        pending_imgui_ini_   = session.imgui_ini_data;
        dock_layout_initialized_ = true;   // skip default layout; ini will apply
    }
    else
    {
        dock_layout_initialized_ = false;  // rebuild default layout
    }

    if (subplot_mgr_) subplot_mgr_->clear();
    if (plot_mgr_)    plot_mgr_->clear();

    for (const auto& e : session.subscriptions)
    {
        if (e.topic.empty()) continue;
        if (e.subplot_slot > 0 && subplot_mgr_)
        {
            subplot_mgr_->add_plot(e.subplot_slot, e.topic, e.field_path,
                                   e.type_name);
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
        session_status_msg_ = "Session loaded";
        session_status_timer_ = 3.0f;
    }
    return lr;
}

void RosAppShell::draw_session_save_dialog()
{
#ifdef SPECTRA_USE_IMGUI
    if (!show_session_save_dialog_) return;

    ImGui::SetNextWindowSize(ImVec2(520, 130), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::Begin("Save Session##g3", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::Text("Path:");
        ImGui::SameLine();

        static char path_buf[512];
        if (ImGui::IsWindowAppearing())
            std::snprintf(path_buf, sizeof(path_buf), "%s", session_save_path_buf_.c_str());

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##session_path", path_buf, sizeof(path_buf));
        session_save_path_buf_ = path_buf;

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(80, 0)))
        {
            SaveResult sr = save_session(session_save_path_buf_);
            session_status_msg_   = sr.ok ? "Session saved" : ("Save failed: " + sr.error);
            session_status_timer_ = 3.0f;
            show_session_save_dialog_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            show_session_save_dialog_ = false;
    }
    ImGui::End();

    if (!open) show_session_save_dialog_ = false;
#endif
}

void RosAppShell::draw_session_load_dialog()
{
#ifdef SPECTRA_USE_IMGUI
    if (!show_session_load_dialog_) return;

    ImGui::SetNextWindowSize(ImVec2(520, 130), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool open = true;
    if (ImGui::Begin("Load Session##g3", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::Text("Path:");
        ImGui::SameLine();

        static char load_buf[512];
        if (ImGui::IsWindowAppearing())
            std::snprintf(load_buf, sizeof(load_buf), "%s", session_save_path_buf_.c_str());

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

    if (!open) show_session_load_dialog_ = false;
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

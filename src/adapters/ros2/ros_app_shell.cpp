#include "ros_app_shell.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstring>

#include "ui/layout/layout_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#ifdef IMGUI_HAS_DOCK
#include <imgui_internal.h>
#endif
#endif

namespace spectra::adapters::ros2
{

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
    if (cfg.time_window_s < ScrollController::MIN_WINDOW_S)
        cfg.time_window_s = ScrollController::MIN_WINDOW_S;
    if (cfg.time_window_s > ScrollController::MAX_WINDOW_S)
        cfg.time_window_s = ScrollController::MAX_WINDOW_S;

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

    double dt = 1.0 / 60.0;
    if (last_poll_time_s_ > 0.0)
        dt = std::clamp(now_s - last_poll_time_s_, 1.0 / 300.0, 0.25);
    last_poll_time_s_ = now_s;

    if (subplot_mgr_)
        subplot_mgr_->set_now(now_s);

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
        ImGui::DockSpace(dockspace_id_, ImVec2(0.0f, 0.0f), dock_flags);

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
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("Plot Area", p_open, flags))
    {
        // Update the Spectra LayoutManager canvas rect to match this panel's
        // content region so the Vulkan renderer draws the figure here.
        if (layout_manager_)
        {
            const ImVec2 wpos  = ImGui::GetWindowPos();
            const ImVec2 wsz   = ImGui::GetWindowSize();
            // Account for title bar height in docked windows.
            const float title_h = ImGui::GetCurrentWindow()->TitleBarHeight
                                + ImGui::GetCurrentWindow()->MenuBarHeight;
            layout_manager_->set_canvas_override(
                spectra::Rect{wpos.x, wpos.y + title_h, wsz.x, wsz.y - title_h});
        }

        const int active = subplot_mgr_ ? subplot_mgr_->active_count() : 0;
        const int cap    = subplot_mgr_ ? subplot_mgr_->capacity() : 0;
        ImGui::Text("Active plots: %d / %d", active, cap);

        if (subplot_mgr_)
        {
            float tw = static_cast<float>(subplot_mgr_->time_window());
            if (ImGui::SliderFloat("Time Window", &tw, 1.0f, 3600.0f, "%.1f s"))
            {
                subplot_mgr_->set_time_window(static_cast<double>(tw));
                if (plot_mgr_) plot_mgr_->set_time_window(static_cast<double>(tw));
            }

            if (ImGui::Button("Resume All Scroll")) subplot_mgr_->resume_all_scroll();
            ImGui::SameLine();
            if (ImGui::Button("Pause All Scroll")) subplot_mgr_->pause_all_scroll();
        }

        ImGui::Separator();

        if (!workspace_state_.selected_topic.empty())
        {
            ImGui::TextDisabled("Selected: %s", workspace_state_.selected_topic.c_str());
            if (!workspace_state_.selected_field.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", workspace_state_.selected_field.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Add to Plot"))
                    workspace_state_.request_plot();
            }
            else
            {
                // No field selected — offer a quick-add from the default field.
                const std::string def_field =
                    default_numeric_field(workspace_state_.selected_topic,
                                         workspace_state_.selected_type);
                if (!def_field.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Plot default field"))
                    {
                        workspace_state_.select_field(def_field);
                        workspace_state_.request_plot();
                    }
                }
            }
        }
        else
        {
            ImGui::TextDisabled("Select a topic in the Topic Monitor to plot its fields.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Drag a topic/field from the Topic Monitor and drop it here.");

        if (drag_drop_)
            drag_drop_->draw_drop_zone();
    }
    ImGui::End();
#else
    (void)p_open;
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
        // Enabled when a topic is selected; uses the specific field if one is
        // selected, otherwise falls back to the first numeric field.
        const bool has_topic = !workspace_state_.selected_topic.empty();
        const bool has_field = !workspace_state_.selected_field.empty();
        const char* add_label = has_field ? "Add Selected Field to Plot"
                                          : "Add Selected Topic to Plot";
        if (ImGui::MenuItem(add_label, nullptr, false, has_topic))
            workspace_state_.request_plot();
        ImGui::Separator();

        if (ImGui::MenuItem("Clear All Plots"))
            clear_plots();
        ImGui::Separator();
        if (ImGui::MenuItem("Resume All Scroll"))
        {
            if (subplot_mgr_) subplot_mgr_->resume_all_scroll();
        }
        if (ImGui::MenuItem("Pause All Scroll"))
        {
            if (subplot_mgr_) subplot_mgr_->pause_all_scroll();
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

    if (slot < 0)
    {
        slot = std::clamp(next_replace_slot_, 1, cap);
        next_replace_slot_ = (slot % cap) + 1;
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
                                      PlotTarget /*target*/)
{
    if (!payload.valid()) return;

    std::string topic_field = payload.topic_name;
    if (!payload.field_path.empty())
        topic_field += ':' + payload.field_path;
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

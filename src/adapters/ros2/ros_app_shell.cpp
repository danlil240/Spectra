#include "ros_app_shell.hpp"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// LayoutMode helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// parse_args
// ---------------------------------------------------------------------------

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
            // Collect all subsequent non-flag arguments as topic specs.
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

        // Unknown flags are silently forwarded to rclcpp::init via argc/argv.
    }

    if (cfg.subplot_rows < 1) cfg.subplot_rows = 1;
    if (cfg.subplot_cols < 1) cfg.subplot_cols = 1;
    if (cfg.time_window_s < ScrollController::MIN_WINDOW_S)
        cfg.time_window_s = ScrollController::MIN_WINDOW_S;
    if (cfg.time_window_s > ScrollController::MAX_WINDOW_S)
        cfg.time_window_s = ScrollController::MAX_WINDOW_S;

    return cfg;
}

// ---------------------------------------------------------------------------
// RosAppShell — construction / destruction
// ---------------------------------------------------------------------------

RosAppShell::RosAppShell(const RosAppConfig& cfg)
    : cfg_(cfg)
{
    screenshot_export_ = std::make_unique<RosScreenshotExport>();
    session_mgr_       = std::make_unique<RosSessionManager>();
    // Pre-populate the save path from the node_name so auto-save works
    // even if the user never explicitly saves.
    session_save_path_buf_ =
        RosSessionManager::default_session_path(cfg_.node_name);
    session_mgr_->set_last_path(session_save_path_buf_);
}

RosAppShell::~RosAppShell()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool RosAppShell::init(int argc, char** argv)
{
    // Create bridge and introspector.
    bridge_ = std::make_unique<Ros2Bridge>();
    intr_   = std::make_unique<MessageIntrospector>();

    // Initialise ROS2 node.
    if (!bridge_->init(cfg_.node_name, cfg_.node_ns, argc, argv))
        return false;

    // TopicDiscovery takes the node in its constructor.
    discovery_ = std::make_unique<TopicDiscovery>(bridge_->node());

    // Create plot engines.
    plot_mgr_    = std::make_unique<RosPlotManager>(*bridge_, *intr_);
    subplot_mgr_ = std::make_unique<SubplotManager>(
        *bridge_, *intr_, cfg_.subplot_rows, cfg_.subplot_cols);
    subplot_mgr_->set_time_window(cfg_.time_window_s);

    // Create UI panels.
    // TopicListPanel has a default constructor.
    topic_list_  = std::make_unique<TopicListPanel>();
    topic_stats_ = std::make_unique<TopicStatsOverlay>();
    // TopicEchoPanel takes (node, introspector).
    topic_echo_  = std::make_unique<TopicEchoPanel>(bridge_->node(), *intr_);
    // BagInfoPanel — pure metadata display, no ROS2 dependency.
    bag_info_ = std::make_unique<BagInfoPanel>();
    bag_info_->set_title("Bag Info");
    bag_info_->set_topic_plot_callback(
        [this](const std::string& topic, const std::string& /*type*/)
        {
            add_topic_plot(topic);
        });

    // RosLogViewer + LogViewerPanel — subscribe /rosout (F5).
    log_viewer_ = std::make_unique<RosLogViewer>(bridge_->node());
    log_viewer_->subscribe();
    log_viewer_panel_ = std::make_unique<LogViewerPanel>(*log_viewer_);
    log_viewer_panel_->set_title("ROS2 Log");

    // DiagnosticsPanel — subscribe /diagnostics (F6).
    diag_panel_ = std::make_unique<DiagnosticsPanel>();
    diag_panel_->set_node(bridge_->node().get());
    diag_panel_->set_alert_callback(
        [](const std::string& name, DiagLevel level)
        {
            // Future: surface alerts in a notification bar.
            (void)name; (void)level;
        });
    diag_panel_->start();

    // Wire discovery to the list panel.
    topic_list_->set_topic_discovery(discovery_.get());

    // Wire the FieldDragDrop controller and inter-panel callbacks.
    wire_panel_callbacks();

    // Apply layout visibility from config.
    setup_layout_visibility();

    // Start background executor.
    bridge_->start_spin();

    // Start discovery (arms the periodic timer).
    discovery_->start();

    // Subscribe initial topics from CLI.
    subscribe_initial_topics();

    // Open bag file from CLI if specified.
    if (!cfg_.bag_file.empty())
    {
        bag_info_->open_bag(cfg_.bag_file);
        show_bag_info_ = true;
    }

    return true;
}

void RosAppShell::shutdown()
{
    if (!bridge_) return;

    // G3: auto-save session on clean shutdown.
    if (session_mgr_ && !session_mgr_->last_path().empty())
    {
        session_mgr_->auto_save(capture_session());
    }

    // Destroy panels and engines before stopping the bridge so they can
    // cleanly cancel their subscriptions via the executor.
    subplot_mgr_.reset();
    plot_mgr_.reset();
    topic_echo_.reset();
    topic_stats_.reset();
    topic_list_.reset();
    drag_drop_.reset();

    bag_info_.reset();
    log_viewer_panel_.reset();
    log_viewer_.reset();

    if (diag_panel_)
    {
        diag_panel_->stop();
        diag_panel_.reset();
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

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void RosAppShell::poll()
{
    if (!bridge_) return;

    // If ROS2 context was shut down (e.g. SIGINT caught by rclcpp),
    // request our own shutdown and bail out to avoid using invalid context.
    if (!rclcpp::ok())
    {
        request_shutdown();
        return;
    }

    // Advance wall-clock "now" for subplot scroll controllers.
    const double now_s = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    subplot_mgr_->set_now(now_s);

    // Drain ring buffers and append new points to series.
    plot_mgr_->poll();
    subplot_mgr_->poll();

    // Advance toast timer and recording session tick (E3).
    if (screenshot_export_)
        screenshot_export_->tick(1.0f / 60.0f);

    // G3: tick session status toast.
    if (session_status_timer_ > 0.0f)
        session_status_timer_ -= 1.0f / 60.0f;

    // Drain diagnostics ring buffer (F6).
    if (diag_panel_)
        diag_panel_->poll();
}

// ---------------------------------------------------------------------------
// ImGui draw methods
// ---------------------------------------------------------------------------

void RosAppShell::draw()
{
#ifdef SPECTRA_USE_IMGUI
    if (shutdown_requested()) return;

    draw_menu_bar();

    // ── Compute structured panel layout ─────────────────────────────────────
    //
    //   +--------------------+-----------------------------+------------------+
    //   | TopicListPanel     | Plot area (SubplotManager)  | TopicStatsOverlay|
    //   | (left, 20%)        | (center, ~55%)              | (right, 25%)     |
    //   +--------------------+-----------------------------+------------------+
    //   | TopicEchoPanel              (bottom, 35% height)                    |
    //   +--------------------------------------------------------------------+
    //
    // On the first frame we force positions (ImGuiCond_Always) so the user
    // never sees the random default placement.  After that we switch to
    // ImGuiCond_Once so that user-resized/moved panels persist.

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menu_h   = ImGui::GetFrameHeightWithSpacing();
    const float status_h = ImGui::GetFrameHeight();
    const float vp_x     = vp->WorkPos.x;
    const float vp_y     = vp->WorkPos.y + menu_h;
    const float vp_w     = vp->WorkSize.x;
    const float vp_h     = vp->WorkSize.y - menu_h - status_h;

    const float left_w   = vp_w * 0.20f;
    const float right_w  = vp_w * 0.25f;
    const float center_w = vp_w - left_w - right_w;
    const float top_h    = vp_h * 0.65f;
    const float bottom_h = vp_h - top_h;

    const ImGuiCond layout_cond = layout_initialized_ ? ImGuiCond_Once
                                                      : ImGuiCond_Always;
    layout_initialized_ = true;

    // ── Position each panel before its draw() call ──────────────────────────
    if (show_topic_list_)
    {
        ImGui::SetNextWindowPos(ImVec2(vp_x, vp_y), layout_cond);
        ImGui::SetNextWindowSize(ImVec2(left_w, top_h), layout_cond);
        draw_topic_list();
    }
    if (show_plot_area_)
    {
        ImGui::SetNextWindowPos(ImVec2(vp_x + left_w, vp_y), layout_cond);
        ImGui::SetNextWindowSize(ImVec2(center_w, top_h), layout_cond);
        draw_plot_area();
    }
    if (show_topic_stats_)
    {
        ImGui::SetNextWindowPos(ImVec2(vp_x + left_w + center_w, vp_y), layout_cond);
        ImGui::SetNextWindowSize(ImVec2(right_w, top_h), layout_cond);
        draw_topic_stats();
    }
    if (show_topic_echo_)
    {
        ImGui::SetNextWindowPos(ImVec2(vp_x, vp_y + top_h), layout_cond);
        ImGui::SetNextWindowSize(ImVec2(vp_w, bottom_h), layout_cond);
        draw_topic_echo();
    }
    if (show_log_viewer_)  draw_log_viewer();
    if (show_diagnostics_) draw_diagnostics();

    draw_status_bar();

    // E3: recording dialog.
    if (show_record_dialog_ && screenshot_export_)
        screenshot_export_->draw_record_dialog(&show_record_dialog_);

    // E3: Ctrl+Shift+S screenshot shortcut.
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
    {
        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_S, /*repeat=*/false) && screenshot_export_)
            {
                const std::string path =
                    RosScreenshotExport::make_screenshot_path("/tmp", "spectra_ros");
                screenshot_export_->take_screenshot(path);
            }
        }
    }

    // G3: session save/load dialogs.
    draw_session_save_dialog();
    draw_session_load_dialog();

    // G3: session status toast (save/load confirmation).
    if (session_status_timer_ > 0.0f && !session_status_msg_.empty())
    {
        ImGuiViewport* vp  = ImGui::GetMainViewport();
        const float    pad = 12.0f;
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - pad,
                   vp->WorkPos.y + vp->WorkSize.y - 48.0f),
            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.80f);
        const ImGuiWindowFlags sf =
            ImGuiWindowFlags_NoDecoration   | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove         | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ros_session_toast", nullptr, sf))
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s",
                               session_status_msg_.c_str());
        ImGui::End();
    }

    // E3: screenshot toast notification.
    if (screenshot_export_ && screenshot_export_->screenshot_toast_active())
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float    pad = 12.0f;
        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - pad,
                   vp->WorkPos.y + vp->WorkSize.y - pad),
            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.75f);
        const ImGuiWindowFlags toast_flags =
            ImGuiWindowFlags_NoDecoration    |
            ImGuiWindowFlags_NoInputs         |
            ImGuiWindowFlags_NoMove           |
            ImGuiWindowFlags_NoSavedSettings  |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav            |
            ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ros_ss_toast", nullptr, toast_flags))
        {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Screenshot saved");
            const auto& p = screenshot_export_->last_screenshot_path();
            if (!p.empty())
            {
                // Show only the filename portion for brevity.
                const auto sep = p.rfind('/');
                const char* name = (sep != std::string::npos) ? p.c_str() + sep + 1
                                                               : p.c_str();
                ImGui::TextDisabled("%s", name);
            }
        }
        ImGui::End();
    }
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
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Plot Area", p_open, flags))
    {
        if (subplot_mgr_)
        {
            const int active = subplot_mgr_->active_count();
            ImGui::TextDisabled("%d active plot%s", active, active == 1 ? "" : "s");
        }
        else
        {
            ImGui::TextDisabled("No plot manager");
        }
    }
    ImGui::End();
#else
    (void)p_open;
#endif
}

void RosAppShell::draw_status_bar()
{
#ifdef SPECTRA_USE_IMGUI
    const ImGuiViewport* vp  = ImGui::GetMainViewport();
    const float bar_h        = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - bar_h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, bar_h));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags sf =
        ImGuiWindowFlags_NoDecoration    | ImGuiWindowFlags_NoInputs       |
        ImGuiWindowFlags_NoMove          | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav    |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##ros_status_bar", nullptr, sf))
    {
        const uint64_t total = total_messages_.load(std::memory_order_relaxed);
        const int      plots = active_plot_count();
        ImGui::Text("Node: %s  |  Messages: %" PRIu64 "  |  Active plots: %d",
                    cfg_.node_name.c_str(), total, plots);
    }
    ImGui::End();
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

void RosAppShell::draw_menu_bar()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Topic List",  nullptr, &show_topic_list_);
            ImGui::MenuItem("Topic Echo",  nullptr, &show_topic_echo_);
            ImGui::MenuItem("Statistics",  nullptr, &show_topic_stats_);
            ImGui::MenuItem("Plot Area",   nullptr, &show_plot_area_);
            ImGui::MenuItem("Bag Info",    nullptr, &show_bag_info_);
            ImGui::MenuItem("Log Viewer",  nullptr, &show_log_viewer_);
            ImGui::MenuItem("Diagnostics", nullptr, &show_diagnostics_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Plots"))
        {
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

        // G3: Session menu.
        if (ImGui::BeginMenu("Session"))
        {
            if (ImGui::MenuItem("Save Session", "Ctrl+Shift+W"))
                show_session_save_dialog_ = true;
            if (ImGui::MenuItem("Save Session As\xe2\x80\xa6"))
            {
                // Reset buffer so user types new path.
                session_save_path_buf_ =
                    RosSessionManager::default_session_path(cfg_.node_name);
                show_session_save_dialog_ = true;
            }
            if (ImGui::MenuItem("Load Session\xe2\x80\xa6"))
                show_session_load_dialog_ = true;
            ImGui::Separator();
            if (ImGui::BeginMenu("Recent Sessions"))
            {
                auto recent = session_mgr_->load_recent();
                if (recent.empty())
                    ImGui::TextDisabled("(none)");
                for (const auto& e : recent)
                {
                    const auto sep = e.path.rfind('/');
                    const char* name = (sep != std::string::npos)
                        ? e.path.c_str() + sep + 1 : e.path.c_str();
                    if (ImGui::MenuItem(name, e.node.c_str()))
                        load_session(e.path);
                }
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

        // E3: Tools menu — screenshot + video recording.
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
            if (ImGui::MenuItem("Record Video\xe2\x80\xa6",
                                nullptr,
                                &show_record_dialog_,
                                screenshot_export_ != nullptr))
            {
                /* toggled via bool ref */
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
#endif
}

// ---------------------------------------------------------------------------
// Window title
// ---------------------------------------------------------------------------

std::string RosAppShell::window_title() const
{
    // U+2014 EM DASH in UTF-8: 0xE2 0x80 0x94
    return "Spectra ROS2 \xe2\x80\x94 " + cfg_.node_name;
}

// ---------------------------------------------------------------------------
// Plot management helpers
// ---------------------------------------------------------------------------

bool RosAppShell::add_topic_plot(const std::string& topic_field)
{
    if (!subplot_mgr_) return false;

    // Parse "topic_name" or "topic_name:field_path".
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

    if (topic.empty()) return false;

    // Fill the first empty slot in the subplot grid.
    const int cap = subplot_mgr_->capacity();
    for (int slot = 1; slot <= cap; ++slot)
    {
        if (!subplot_mgr_->has_plot(slot))
        {
            const SubplotHandle h = subplot_mgr_->add_plot(slot, topic, field_path);
            return h.valid();
        }
    }

    // All subplot slots full — add via RosPlotManager (spawns a new Figure).
    const PlotHandle h = plot_mgr_->add_plot(topic, field_path);
    return h.valid();
}

void RosAppShell::clear_plots()
{
    if (subplot_mgr_) subplot_mgr_->clear();
    if (plot_mgr_)    plot_mgr_->clear();
}

int RosAppShell::active_plot_count() const
{
    int n = 0;
    if (subplot_mgr_) n += subplot_mgr_->active_count();
    if (plot_mgr_)    n += static_cast<int>(plot_mgr_->plot_count());
    return n;
}

// ---------------------------------------------------------------------------
// Inter-panel callbacks
// ---------------------------------------------------------------------------

void RosAppShell::on_topic_selected(const std::string& topic)
{
    selected_topic_ = topic;

    // Resolve type from discovery (for echo panel).
    std::string type;
    if (discovery_)
    {
        TopicInfo info = discovery_->topic(topic);
        if (!info.types.empty()) type = info.types.front();
    }
    selected_type_ = type;

    if (topic_echo_)  topic_echo_->set_topic(topic, type);
    if (topic_stats_) topic_stats_->set_topic(topic);
}

void RosAppShell::on_topic_plot(const std::string& topic)
{
    add_topic_plot(topic);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void RosAppShell::subscribe_initial_topics()
{
    for (const auto& t : cfg_.initial_topics)
        add_topic_plot(t);
}

void RosAppShell::wire_panel_callbacks()
{
    if (!topic_list_) return;

    // Select callback (single-click) — route to echo + stats.
    // SelectCallback = function<void(const string& topic_name)>
    topic_list_->set_select_callback(
        [this](const std::string& topic)
        {
            on_topic_selected(topic);
        });

    // Plot callback (double-click) — subscribe and plot.
    // PlotCallback = function<void(const string& topic_name)>
    topic_list_->set_plot_callback(
        [this](const std::string& topic)
        {
            on_topic_plot(topic);
        });

    // Wire FieldDragDrop controller so topic rows become drag sources.
    drag_drop_ = std::make_unique<FieldDragDrop>();
    drag_drop_->set_plot_request_callback(
        [this](const FieldDragPayload& payload, PlotTarget target)
        {
            handle_plot_request(payload, target);
        });
    topic_list_->set_drag_drop(drag_drop_.get());
    if (topic_echo_) topic_echo_->set_drag_drop(drag_drop_.get());
}

void RosAppShell::handle_plot_request(const FieldDragPayload& payload,
                                      PlotTarget target)
{
    if (!payload.valid()) return;

    // All three targets map to "add plot" for now.
    // NewWindow will be fully supported in G2 when the Spectra App is wired.
    (void)target;

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
            break;

        case LayoutMode::PlotOnly:
            show_topic_list_  = false;
            show_topic_echo_  = false;
            show_topic_stats_ = false;
            show_plot_area_   = true;
            break;

        case LayoutMode::Monitor:
            show_topic_list_  = true;
            show_topic_echo_  = true;
            show_topic_stats_ = true;
            show_plot_area_   = false;
            break;
    }
}

// ---------------------------------------------------------------------------
// G3 — Session capture / apply / save / load
// ---------------------------------------------------------------------------

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

    // Panel visibility.
    s.panels.topic_list  = show_topic_list_;
    s.panels.topic_echo  = show_topic_echo_;
    s.panels.topic_stats = show_topic_stats_;
    s.panels.plot_area   = show_plot_area_;
    s.panels.bag_info    = show_bag_info_;

    // Subplot subscriptions.
    if (subplot_mgr_)
    {
        for (const auto& h : subplot_mgr_->handles())
        {
            if (!h.valid()) continue;
            SubscriptionEntry e;
            e.topic        = h.topic;
            e.field_path   = h.field_path;
            e.subplot_slot = h.slot;
            e.time_window_s = subplot_mgr_->time_window();
            e.scroll_paused = subplot_mgr_->is_scroll_paused(h.slot);
            s.subscriptions.push_back(std::move(e));
        }
    }

    // Standalone RosPlotManager subscriptions.
    if (plot_mgr_)
    {
        for (const auto& h : plot_mgr_->handles())
        {
            if (!h.valid()) continue;
            SubscriptionEntry e;
            e.topic        = h.topic;
            e.field_path   = h.field_path;
            e.subplot_slot = 0;   // 0 = standalone
            e.time_window_s = plot_mgr_->time_window();
            e.scroll_paused = plot_mgr_->is_scroll_paused(h.id);
            s.subscriptions.push_back(std::move(e));
        }
    }

    return s;
}

void RosAppShell::apply_session(const RosSession& session)
{
    // Time window.
    if (session.time_window_s > 0.0)
    {
        if (subplot_mgr_) subplot_mgr_->set_time_window(session.time_window_s);
        if (plot_mgr_)    plot_mgr_->set_time_window(session.time_window_s);
    }

    // Panel visibility.
    show_topic_list_  = session.panels.topic_list;
    show_topic_echo_  = session.panels.topic_echo;
    show_topic_stats_ = session.panels.topic_stats;
    show_plot_area_   = session.panels.plot_area;
    show_bag_info_    = session.panels.bag_info;

    // Re-subscribe: clear existing then re-add from session.
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
    if (!session_mgr_) {
        SaveResult r; r.error = "session manager not initialised"; return r;
    }
    return session_mgr_->save(capture_session(), path);
}

LoadResult RosAppShell::load_session(const std::string& path)
{
    if (!session_mgr_) {
        LoadResult r; r.error = "session manager not initialised"; return r;
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

// ---------------------------------------------------------------------------
// G3 — ImGui session dialogs
// ---------------------------------------------------------------------------

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
        // Use a static char buffer to drive the input.
        static char path_buf[512];
        if (ImGui::IsWindowAppearing())
            std::snprintf(path_buf, sizeof(path_buf), "%s",
                          session_save_path_buf_.c_str());

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
            std::snprintf(load_buf, sizeof(load_buf), "%s",
                          session_save_path_buf_.c_str());

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

}   // namespace spectra::adapters::ros2

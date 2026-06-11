#include "ros_app_shell.hpp"

#include <algorithm>
#include <unordered_set>

#include "scene/scene_manager.hpp"
#include "ui/tf_tree_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

namespace
{

std::string normalize_frame_label(const std::string& frame)
{
    if (!frame.empty() && frame.front() == '/')
        return frame.substr(1);
    return frame;
}

std::vector<std::string> collect_known_fixed_frames(const TfTreePanel*  tf_tree_panel,
                                                    const SceneManager& scene_manager,
                                                    const std::string&  current_fixed_frame)
{
    std::unordered_set<std::string> seen;
    std::vector<std::string>        frames;

    auto add_frame = [&](const std::string& frame)
    {
        const std::string normalized = normalize_frame_label(frame);
        if (normalized.empty())
            return;
        if (seen.insert(normalized).second)
            frames.push_back(normalized);
    };

    if (tf_tree_panel)
    {
        for (const auto& frame : tf_tree_panel->buffer().all_frames())
            add_frame(frame);
    }

    for (const auto& entity : scene_manager.entities())
        add_frame(entity.frame_id);

    add_frame(current_fixed_frame);

    std::sort(frames.begin(), frames.end());
    return frames;
}

}   // namespace

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
        ImGui::MenuItem("Displays", nullptr, &show_displays_panel_);
        ImGui::MenuItem("Scene Viewport", nullptr, &show_scene_viewport_);
        ImGui::MenuItem("Inspector", nullptr, &show_inspector_panel_);
        ImGui::MenuItem("Node Graph", nullptr, &show_node_graph_);
        ImGui::MenuItem("TF Tree", nullptr, &show_tf_tree_);
        ImGui::MenuItem("Parameter Editor", nullptr, &show_param_editor_);
        ImGui::MenuItem("Service Caller", nullptr, &show_service_caller_);

        if (show_scene_viewport_ || show_displays_panel_ || show_tf_tree_)
        {
            ImGui::SeparatorText("Scene");
            ImGui::TextUnformatted("Fixed Frame");
            ImGui::SetNextItemWidth(180.0f);
            const std::vector<std::string> frames =
                collect_known_fixed_frames(tf_tree_panel_.get(),
                                           scene_manager_,
                                           workspace_state_.fixed_frame);
            std::string current_label = workspace_state_.fixed_frame.empty()
                                            ? "(auto)"
                                            : workspace_state_.fixed_frame;
            if (ImGui::BeginCombo("##ros_fixed_frame_menu", current_label.c_str()))
            {
                const bool is_auto = workspace_state_.fixed_frame.empty();
                if (ImGui::Selectable("(auto)", is_auto))
                    workspace_state_.fixed_frame.clear();
                for (const auto& frame : frames)
                {
                    const bool selected = workspace_state_.fixed_frame == frame;
                    if (ImGui::Selectable(frame.c_str(), selected))
                        workspace_state_.fixed_frame = frame;
                }
                ImGui::EndCombo();
            }
        }

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
        const bool  has_topic = !workspace_state_.selected_topic.empty();
        const bool  has_field = !workspace_state_.selected_field.empty();
        const char* add_label =
            has_field ? "Add Selected Field to Plot" : "Add Selected Topic to Plot";
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
            if (subplot_mgr_)
                subplot_mgr_->resume_all_scroll();
        }
        if (ImGui::MenuItem("Pause All Scroll"))
        {
            if (subplot_mgr_)
                subplot_mgr_->pause_all_scroll();
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
            session_save_path_buf_    = RosSessionManager::default_session_path(cfg_.node_name);
            show_session_save_dialog_ = true;
        }
        if (ImGui::MenuItem("Load Session..."))
            show_session_load_dialog_ = true;
        if (ImGui::MenuItem("Import Session (merge)..."))
            show_session_merge_dialog_ = true;

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
        if (ImGui::MenuItem("Screenshot", "Ctrl+Shift+S", false, screenshot_export_ != nullptr))
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

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About Spectra-ROS"))
            ImGui::OpenPopup("##ros_about_popup");
        if (ImGui::MenuItem("Keyboard Shortcuts"))
            ImGui::OpenPopup("##ros_shortcuts_popup");
        ImGui::EndMenu();
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("##ros_about_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Spectra ROS — GPU-accelerated rqt + RViz studio");
        ImGui::TextDisabled("Node: %s", cfg_.node_name.c_str());
        if (ImGui::Button("Close##about"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("##ros_shortcuts_popup",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Ctrl+Shift+S  — Screenshot");
        ImGui::TextUnformatted("Ctrl+Shift+W  — Save session");
        ImGui::TextUnformatted("Space         — Toggle pause/live (active plot)");
        ImGui::TextUnformatted("Home / menu   — Resume all scroll");
        ImGui::TextUnformatted("G             — Toggle grid (active plot)");
        ImGui::TextUnformatted("R             — Reset basic plot display");
        ImGui::TextUnformatted("A             — Auto-fit Y (active plot)");
        if (ImGui::Button("Close##shortcuts"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::EndMainMenuBar();
#endif
}

void RosAppShell::draw_layout_preset_menu()
{
#ifdef SPECTRA_USE_IMGUI
    static const LayoutPreset kPresets[] = {
        LayoutPreset::Default,
        LayoutPreset::Debug,
        LayoutPreset::Monitor,
        LayoutPreset::BagReview,
        LayoutPreset::RViz,
        LayoutPreset::RVizPlot,
    };
    for (const auto p : kPresets)
    {
        const bool sel = (current_preset_ == p);
        if (ImGui::MenuItem(layout_preset_name(p), nullptr, sel))
            apply_layout_preset(p);
    }
#endif
}

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
        const auto        slash = path.rfind('/');
        const std::string label = (slash != std::string::npos) ? path.substr(slash + 1) : path;
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

}   // namespace spectra::adapters::ros2

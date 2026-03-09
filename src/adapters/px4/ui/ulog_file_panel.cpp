#include "ulog_file_panel.hpp"

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#ifdef IMGUI_HAS_DOCK
#include <imgui_internal.h>
#endif
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ULogFilePanel::ULogFilePanel(ULogReader& reader, Px4PlotManager& plot_mgr)
    : reader_(reader)
    , plot_mgr_(plot_mgr)
{
    detach_ctrl_.set_title("ULog File");
    detach_ctrl_.set_detached_size(500.0f, 400.0f);
    detach_ctrl_.set_draw_callback([this]() { draw_content(); });
}

ULogFilePanel::~ULogFilePanel() = default;

// ---------------------------------------------------------------------------
// draw — the panel owns all ImGui rendering; controller is pure state machine
// ---------------------------------------------------------------------------

void ULogFilePanel::draw(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    // Let controller detect OS window close.
    detach_ctrl_.update();

    auto state = detach_ctrl_.state();

    // Detached — content rendered by session_runtime in the OS window.
    if (state == spectra::PanelDetachController::State::Detached)
        return;

    // DetachPending — skip one frame so the dock slot is released cleanly.
    if (state == spectra::PanelDetachController::State::DetachPending)
        return;

    // After re-attach: force back into the target dockspace.
#ifdef IMGUI_HAS_DOCK
    if (state == spectra::PanelDetachController::State::AttachPending
        && detach_ctrl_.dock_id() != 0)
    {
        ImGui::SetNextWindowDockID(
            static_cast<ImGuiID>(detach_ctrl_.dock_id()), ImGuiCond_Always);
    }
#endif

    if (!ImGui::Begin(detach_ctrl_.title().c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    // ── Drag-to-detach: detect ImGui undocking and replace with OS window ──
    // Let ImGui handle the drag natively. When it undocks the window
    // (transition from docked → floating), intercept it immediately
    // and create a real OS window via PanelDetachController instead.
#ifdef IMGUI_HAS_DOCK
    bool currently_docked = ImGui::IsWindowDocked();
    if (was_docked_ && !currently_docked && detach_ctrl_.is_docked())
    {
        double sx, sy;
        detach_ctrl_.get_screen_cursor(sx, sy);
        detach_ctrl_.set_screen_position(static_cast<int>(sx), static_cast<int>(sy));
        detach_ctrl_.detach();
        was_docked_ = false;
        ImGui::End();
        return;
    }
    was_docked_ = currently_docked;
#endif

    draw_context_menu();
    draw_content();

    ImGui::End();
#else
    (void)p_open;
#endif
}

// ---------------------------------------------------------------------------
// draw_context_menu
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_context_menu()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::BeginPopupContextWindow("##ULogPanelDetach"))
    {
        if (detach_ctrl_.is_docked())
        {
            if (ImGui::MenuItem("Detach to Window"))
                detach_ctrl_.detach();
        }
        else if (detach_ctrl_.is_detached())
        {
            if (ImGui::MenuItem("Attach to Dockspace"))
                detach_ctrl_.attach();
        }
        ImGui::EndPopup();
    }
#endif
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_content()
{
#ifdef SPECTRA_USE_IMGUI
    draw_file_header();

    if (reader_.is_open())
    {
        if (ImGui::BeginTabBar("ULogTabs"))
        {
            if (ImGui::BeginTabItem("Topics"))
            {
                draw_topic_tree();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Metadata"))
            {
                draw_metadata();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Info"))
            {
                draw_info_table();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Parameters"))
            {
                draw_params_table();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log"))
            {
                draw_log_messages();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    else
    {
        ImGui::TextWrapped("No ULog file loaded. Click 'Open' to load a .ulg file.");
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_file_header
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_file_header()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::Button("Open..."))
    {
        // The shell owns the actual file dialog and load flow.
        if (file_cb_)
            file_cb_("");
    }

    if (reader_.is_open())
    {
        ImGui::SameLine();
        ImGui::Text("File: %s", reader_.metadata().path.c_str());
    }

    ImGui::Separator();
#endif
}

// ---------------------------------------------------------------------------
// draw_metadata
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_metadata()
{
#ifdef SPECTRA_USE_IMGUI
    if (!reader_.is_open())
        return;

    auto& meta = reader_.metadata();

    ImGui::Text("ULog Version: %d", meta.version);
    ImGui::Text("Duration: %.1f sec", meta.duration_sec());
    ImGui::Text("Messages: %zu", meta.message_count);
    ImGui::Text("Topics: %zu", reader_.topic_count());
    ImGui::Text("Dropouts: %zu (total %u ms)", meta.dropout_count, meta.total_dropout_ms);
    ImGui::Text("Start: %.3f s", meta.start_time_sec());
    ImGui::Text("End: %.3f s", meta.end_time_sec());

    // Vehicle info from info messages.
    auto sys_name = reader_.info_string("sys_name");
    auto ver_hw = reader_.info_string("ver_hw");
    auto ver_sw = reader_.info_string("ver_sw");

    if (!sys_name.empty())
        ImGui::Text("System: %s", sys_name.c_str());
    if (!ver_hw.empty())
        ImGui::Text("Hardware: %s", ver_hw.c_str());
    if (!ver_sw.empty())
        ImGui::Text("Software: %s", ver_sw.c_str());
#endif
}

// ---------------------------------------------------------------------------
// draw_topic_tree
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_topic_tree()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::InputTextWithHint("##TopicFilter", "Filter topics...",
                             topic_filter_buf_, sizeof(topic_filter_buf_));

    std::string filter(topic_filter_buf_);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    ImGui::BeginChild("TopicTree", ImVec2(0, 0), true);

    auto topics = plot_mgr_.available_topics();

    for (auto& topic : topics)
    {
        // Apply filter.
        if (!filter.empty())
        {
            std::string lower_topic = topic;
            std::transform(lower_topic.begin(), lower_topic.end(),
                           lower_topic.begin(), ::tolower);
            if (lower_topic.find(filter) == std::string::npos)
                continue;
        }

        // Get row count for this topic.
        auto* ts = reader_.data_for(topic);
        size_t count = ts ? ts->rows.size() : 0;

        char label[256];
        std::snprintf(label, sizeof(label), "%s (%zu)", topic.c_str(), count);

        if (ImGui::TreeNode(label))
        {
            auto fields = plot_mgr_.topic_fields(topic);
            for (auto& field : fields)
            {
                // Check if it's an array element: "name[idx]"
                std::string field_name = field;
                int array_idx = -1;
                auto bracket = field.find('[');
                if (bracket != std::string::npos)
                {
                    field_name = field.substr(0, bracket);
                    auto close = field.find(']', bracket);
                    if (close != std::string::npos)
                    {
                        array_idx = std::atoi(
                            field.substr(bracket + 1, close - bracket - 1).c_str());
                    }
                }

                if (ImGui::Selectable(field.c_str(), false,
                                       ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        if (field_cb_)
                            field_cb_(topic, field_name, array_idx);
                        else
                            plot_mgr_.add_field(topic, field_name, array_idx);
                    }
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndChild();
#endif
}

// ---------------------------------------------------------------------------
// draw_info_table
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_info_table()
{
#ifdef SPECTRA_USE_IMGUI
    auto& infos = reader_.info_messages();

    if (ImGui::BeginTable("InfoTable", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        for (auto& info : infos)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.type_str.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(info.as_string().c_str());
        }

        ImGui::EndTable();
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_params_table
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_params_table()
{
#ifdef SPECTRA_USE_IMGUI
    auto& params = reader_.parameters();

    if (ImGui::BeginTable("ParamsTable", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable))
    {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        for (auto& param : params)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(param.key.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", param.is_float ? "float" : "int32");
            ImGui::TableNextColumn();
            if (param.is_float)
                ImGui::Text("%.6f", param.value_float);
            else
                ImGui::Text("%d", param.value_int);
        }

        ImGui::EndTable();
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_log_messages
// ---------------------------------------------------------------------------

void ULogFilePanel::draw_log_messages()
{
#ifdef SPECTRA_USE_IMGUI
    auto& logs = reader_.log_messages();

    if (ImGui::BeginTable("LogTable", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Time (s)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Message");
        ImGui::TableHeadersRow();

        for (auto& log : logs)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", static_cast<double>(log.timestamp_us) * 1e-6);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(log.level_name());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(log.message.c_str());
        }

        ImGui::EndTable();
    }
#endif
}

}   // namespace spectra::adapters::px4

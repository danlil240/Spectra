#include "ulog_file_panel.hpp"

#include "px4_panel_helpers.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include "ui/theme/theme_imgui.hpp"
    #ifdef IMGUI_HAS_DOCK
        #include <imgui_internal.h>
        #include <cmath>
    #endif
#endif

#include <algorithm>
#include <format>
#include <cstring>

namespace spectra::adapters::px4
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ULogFilePanel::ULogFilePanel(ULogReader& reader, Px4PlotManager& plot_mgr)
    : reader_(reader), plot_mgr_(plot_mgr)
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
        ImGui::SetNextWindowDockID(static_cast<ImGuiID>(detach_ctrl_.dock_id()), ImGuiCond_Always);
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
        double sx = NAN;
        double sy = NAN;
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
    auto ver_hw   = reader_.info_string("ver_hw");
    auto ver_sw   = reader_.info_string("ver_sw");

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
    ImGui::InputTextWithHint("##TopicFilter",
                             "Filter topics...",
                             topic_filter_buf_,
                             sizeof(topic_filter_buf_));

    std::string filter(topic_filter_buf_);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    ImGui::BeginChild("TopicTree", ImVec2(0, 0), 1);

    auto topics = plot_mgr_.available_topics();

    bool any_shown = false;
    for (auto& topic : topics)
    {
        // Apply filter.
        if (!filter.empty())
        {
            std::string lower_topic = topic;
            std::transform(lower_topic.begin(), lower_topic.end(), lower_topic.begin(), ::tolower);
            if (lower_topic.find(filter) == std::string::npos)
                continue;
        }
        any_shown = true;

        // Get row count for this topic.
        auto*  ts    = reader_.data_for(topic);
        size_t count = ts ? ts->rows.size() : 0;

        const std::string label = std::format("{} ({})", topic, count);

        if (ImGui::TreeNode(label.c_str()))
        {
            auto fields = plot_mgr_.topic_fields(topic);
            for (auto& field : fields)
            {
                // Check if it's an array element: "name[idx]"
                std::string field_name = field;
                int         array_idx  = -1;
                auto        bracket    = field.find('[');
                if (bracket != std::string::npos)
                {
                    field_name = field.substr(0, bracket);
                    auto close = field.find(']', bracket);
                    if (close != std::string::npos)
                    {
                        array_idx =
                            std::atoi(field.substr(bracket + 1, close - bracket - 1).c_str());
                    }
                }

                if (ImGui::Selectable(field.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
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

    if (!any_shown)
    {
        const ImVec4 muted = spectra::ui::to_imvec4(spectra::ui::theme().text_secondary);
        if (topics.empty())
            ImGui::TextColored(muted, "This log contains no topics.");
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, muted);
            ImGui::TextWrapped("No topics match \"%s\".", topic_filter_buf_);
            ImGui::PopStyleColor();
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

    if (ImGui::BeginTable(
            "InfoTable",
            3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
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

    if (ImGui::BeginTable("ParamsTable",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                              | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable))
    {
        ImGui::TableSetupColumn("Name",
                                ImGuiTableColumnFlags_DefaultSort,
                                0.0f,
                                static_cast<ImGuiID>(ParamSortColumn::Name));
        ImGui::TableSetupColumn("Type", 0, 0.0f, static_cast<ImGuiID>(ParamSortColumn::Type));
        ImGui::TableSetupColumn("Value", 0, 0.0f, static_cast<ImGuiID>(ParamSortColumn::Value));
        ImGui::TableHeadersRow();

        // Re-sort only when the user changes the sort specs or the parameter
        // set changes (new file loaded); otherwise reuse the cached order.
        ImGuiTableSortSpecs* specs        = ImGui::TableGetSortSpecs();
        const bool           data_changed = param_order_src_ != params.data()
                                  || param_order_.size() != params.size()
                                  || param_order_path_ != reader_.metadata().path;
        if (specs && (specs->SpecsDirty || data_changed))
        {
            std::vector<ParamSortKey> keys;
            keys.reserve(static_cast<size_t>(specs->SpecsCount));
            for (int i = 0; i < specs->SpecsCount; ++i)
            {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[i];
                keys.push_back({static_cast<ParamSortColumn>(s.ColumnUserID),
                                s.SortDirection != ImGuiSortDirection_Descending});
            }
            param_order_      = sorted_parameter_indices(params, keys);
            param_order_src_  = params.data();
            param_order_path_ = reader_.metadata().path;
            specs->SpecsDirty = false;
        }
        else if (!specs && data_changed)
        {
            param_order_      = sorted_parameter_indices(params, {});
            param_order_src_  = params.data();
            param_order_path_ = reader_.metadata().path;
        }

        for (size_t idx : param_order_)
        {
            const auto& param = params[idx];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(param.key.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(parameter_type_name(param));
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

    if (ImGui::BeginTable(
            "LogTable",
            3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
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

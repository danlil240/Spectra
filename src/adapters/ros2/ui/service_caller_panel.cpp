#include "service_caller_panel.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ServiceCallerPanel::ServiceCallerPanel(ServiceCaller* caller) : caller_(caller) {}

// ---------------------------------------------------------------------------
// select_service
// ---------------------------------------------------------------------------

void ServiceCallerPanel::select_service(const std::string& name)
{
    if (name == selected_service_)
        return;
    selected_service_ = name;
    form_error_.clear();
    reload_request_form();
}

// ---------------------------------------------------------------------------
// reload_request_form
// ---------------------------------------------------------------------------

void ServiceCallerPanel::reload_request_form()
{
    request_fields_.clear();
    if (!caller_ || selected_service_.empty())
        return;

    // Ensure schema is loaded.
    caller_->load_schema(selected_service_);

    auto entry = caller_->find_service(selected_service_);
    if (!entry || !entry->schema_ok || !entry->request_schema)
        return;

    request_fields_ = ServiceCaller::fields_from_schema(*entry->request_schema);
}

// ---------------------------------------------------------------------------
// populate_from_record
// ---------------------------------------------------------------------------

void ServiceCallerPanel::populate_from_record(const CallRecord& rec)
{
    // Re-populate request fields with the values from a history entry.
    if (!request_fields_.empty())
        ServiceCaller::json_to_fields(rec.request_json, request_fields_);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char* ServiceCallerPanel::state_badge(CallState s)
{
    switch (s)
    {
        case CallState::Pending:
            return "[...]";
        case CallState::Done:
            return "[OK]";
        case CallState::TimedOut:
            return "[TIMEOUT]";
        case CallState::Error:
            return "[ERROR]";
    }
    return "[?]";
}

void ServiceCallerPanel::push_state_color(CallState s)
{
#ifdef SPECTRA_USE_IMGUI
    switch (s)
    {
        case CallState::Pending:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.2f, 1.0f));
            break;
        case CallState::Done:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.85f, 0.4f, 1.0f));
            break;
        case CallState::TimedOut:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.5f, 0.1f, 1.0f));
            break;
        case CallState::Error:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            break;
    }
#else
    (void)s;
#endif
}

void ServiceCallerPanel::pop_state_color()
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::PopStyleColor();
#endif
}

std::string ServiceCallerPanel::format_latency(double ms)
{
    if (ms <= 0.0)
        return "—";
    std::ostringstream oss;
    if (ms >= 1000.0)
        oss << static_cast<int>(ms / 1000.0) << "." << static_cast<int>(ms / 100.0) % 10 << " s";
    else
        oss << static_cast<int>(ms) << " ms";
    return oss.str();
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void ServiceCallerPanel::draw(bool* p_open)
{
#ifndef SPECTRA_USE_IMGUI
    (void)p_open;
    return;
#else
    if (refresh_requested_ && caller_)
    {
        caller_->refresh_services();
        services_snap_     = caller_->services();
        refresh_requested_ = false;
    }

    // Refresh history snapshot every frame.
    if (caller_)
        history_snap_ = caller_->history();

    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (!ImGui::Begin(title_.c_str(), p_open, flags))
    {
        ImGui::End();
        return;
    }

    // Toolbar row: refresh button + filter.
    if (ImGui::Button("Refresh"))
        refresh_requested_ = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("##svc_filter", filter_buf_, sizeof(filter_buf_)))
        filter_str_ = filter_buf_;
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu services)", services_snap_.size());
    ImGui::Separator();

    // Three-pane layout using columns.
    const float total_w = ImGui::GetContentRegionAvail().x;
    const float list_w  = total_w * list_ratio_;
    const float form_w  = total_w * form_ratio_;
    // history gets the rest minus two thin splitter gaps (4px each).
    const float hist_w  = total_w - list_w - form_w - 8.0f;
    const float avail_h = ImGui::GetContentRegionAvail().y;

    // --- Left pane: service list ---
    ImGui::BeginChild("##svc_list_pane", ImVec2(list_w, avail_h), true);
    draw_service_list(list_w);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 4.0f);

    // --- Middle pane: request form ---
    ImGui::BeginChild("##svc_form_pane", ImVec2(form_w, avail_h), true);
    draw_request_form(form_w);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 4.0f);

    // --- Right pane: history ---
    ImGui::BeginChild("##svc_hist_pane", ImVec2(hist_w, avail_h), true);
    draw_history_pane();
    ImGui::EndChild();

    ImGui::End();
#endif
}

// ---------------------------------------------------------------------------
// draw_service_list
// ---------------------------------------------------------------------------

void ServiceCallerPanel::draw_service_list(float /*pane_width*/)
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::TextDisabled("Services");
    ImGui::Separator();

    for (auto& entry : services_snap_)
    {
        // Filter.
        if (!filter_str_.empty())
        {
            if (entry.name.find(filter_str_) == std::string::npos
                && entry.type.find(filter_str_) == std::string::npos)
                continue;
        }

        const bool is_selected = (entry.name == selected_service_);

        // Selectable spanning full width.
        std::string label = entry.name + "##svc_" + entry.name;
        if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns))
        {
            select_service(entry.name);
        }

        // Tooltip: full type string.
        if (ImGui::IsItemHovered() && !entry.type.empty())
            ImGui::SetTooltip("%s", entry.type.c_str());
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_request_form
// ---------------------------------------------------------------------------

void ServiceCallerPanel::draw_request_form(float /*pane_width*/)
{
#ifdef SPECTRA_USE_IMGUI
    if (selected_service_.empty())
    {
        ImGui::TextDisabled("Select a service to call");
        return;
    }

    // Header: service name + type.
    ImGui::TextUnformatted(selected_service_.c_str());
    auto entry = caller_ ? caller_->find_service(selected_service_) : std::nullopt;
    if (entry && !entry->type.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", entry->type.c_str());
    }
    ImGui::Separator();

    // Schema status.
    if (!entry)
    {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.1f, 1.f), "Service not in discovery list");
        ImGui::Separator();
    }
    else if (!entry->schema_loaded)
    {
        ImGui::TextDisabled("Loading schema…");
        // Trigger async load.
        if (caller_)
            caller_->load_schema(selected_service_);
        ImGui::Separator();
    }
    else if (!entry->schema_ok)
    {
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.f), "Schema unavailable");
        ImGui::Separator();
    }
    else
    {
        // Request fields.
        if (request_fields_.empty())
            reload_request_form();

        ImGui::TextDisabled("Request fields:");
        ImGui::Spacing();

        for (int i = 0; i < static_cast<int>(request_fields_.size()); ++i)
            draw_field_row(request_fields_[i], i);

        ImGui::Spacing();
        ImGui::Separator();
    }

    // Timeout slider.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Timeout");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##timeout", &timeout_s_, 0.1f, 60.0f, "%.1f s");
    ImGui::SameLine();

    // Call button.
    bool can_call = caller_ != nullptr && !selected_service_.empty();
    if (!can_call)
        ImGui::BeginDisabled();

    if (ImGui::Button("Call Service", ImVec2(-1.0f, 0.0f)))
    {
        form_error_.clear();
        std::string req_json = ServiceCaller::fields_to_json(request_fields_);
        last_call_handle_ =
            caller_->call(selected_service_, req_json, static_cast<double>(timeout_s_));
        if (last_call_handle_ == INVALID_CALL_HANDLE)
            form_error_ = "Failed to dispatch call.";
    }

    if (!can_call)
        ImGui::EndDisabled();

    if (!form_error_.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.f), "%s", form_error_.c_str());
    }

    // Show latest call status inline.
    if (last_call_handle_ != INVALID_CALL_HANDLE && caller_)
    {
        const CallRecord* rec = caller_->record(last_call_handle_);
        if (rec)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Last call: ");
            ImGui::SameLine();
            auto s = rec->state.load(std::memory_order_acquire);
            push_state_color(s);
            ImGui::TextUnformatted(state_badge(s));
            pop_state_color();
            if (s == CallState::Done)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("  %s", format_latency(rec->latency_ms).c_str());
            }
            else if (s == CallState::Error || s == CallState::TimedOut)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.1f, 1.f),
                                   "  %s",
                                   rec->error_message.c_str());
            }
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_field_row
// ---------------------------------------------------------------------------

void ServiceCallerPanel::draw_field_row(ServiceFieldValue& fv, int idx)
{
#ifdef SPECTRA_USE_IMGUI
    // Indent for nested fields.
    if (fv.depth > 0)
        ImGui::Indent(static_cast<float>(fv.depth) * 16.0f);

    // Struct head: just a label.
    if (fv.is_struct_head())
    {
        ImGui::TextDisabled("%s:", fv.display_name.c_str());
        if (fv.depth > 0)
            ImGui::Unindent(static_cast<float>(fv.depth) * 16.0f);
        return;
    }

    // Label column (fixed width).
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fv.display_name.c_str());
    ImGui::SameLine(140.0f);

    std::string widget_id = "##field_" + std::to_string(idx);
    ImGui::SetNextItemWidth(-1.0f);

    if (fv.is_bool())
    {
        bool bval = (fv.value_str == "true");
        if (ImGui::Checkbox(widget_id.c_str(), &bval))
            fv.value_str = bval ? "true" : "false";
    }
    else if (fv.type == FieldType::String || fv.type == FieldType::WString)
    {
        char buf[256]{};
        std::strncpy(buf, fv.value_str.c_str(), sizeof(buf) - 1);
        if (ImGui::InputText(widget_id.c_str(), buf, sizeof(buf)))
            fv.value_str = buf;
    }
    else
    {
        // Numeric: float drag (display) backed by string.
        float fval = 0.0f;
        try
        {
            fval = std::stof(fv.value_str);
        }
        catch (...)
        {
        }
        if (ImGui::DragFloat(widget_id.c_str(), &fval, 0.01f))
        {
            std::ostringstream oss;
            oss << fval;
            fv.value_str = oss.str();
        }
    }

    if (fv.depth > 0)
        ImGui::Unindent(static_cast<float>(fv.depth) * 16.0f);
#else
    (void)fv;
    (void)idx;
#endif
}

// ---------------------------------------------------------------------------
// draw_history_pane
// ---------------------------------------------------------------------------

void ServiceCallerPanel::draw_history_pane()
{
#ifdef SPECTRA_USE_IMGUI
    // Header + controls.
    ImGui::TextDisabled("History (%zu)", history_snap_.size());
    ImGui::SameLine();

    if (ImGui::SmallButton("Clear") && caller_)
    {
        caller_->clear_history();
        history_snap_.clear();
        selected_history_ = INVALID_CALL_HANDLE;
        response_json_preview_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy JSON") && caller_)
    {
        std::string json = caller_->history_to_json();
        ImGui::SetClipboardText(json.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Import") && caller_)
    {
        const char* clip = ImGui::GetClipboardText();
        if (clip)
            caller_->history_from_json(clip);
    }
    ImGui::Separator();

    // Upper half: call log table.
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float table_h = selected_history_ != INVALID_CALL_HANDLE ? avail_h * 0.55f : avail_h;

    if (ImGui::BeginTable("##hist_table",
                          4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
                              | ImGuiTableFlags_SizingFixedFit,
                          ImVec2(-1.0f, table_h)))
    {
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Service", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Latency", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("Replay", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Iterate newest-first.
        for (auto it = history_snap_.rbegin(); it != history_snap_.rend(); ++it)
        {
            const CallRecord& rec      = **it;
            bool              selected = (rec.id == selected_history_);
            draw_history_entry(rec, selected);
        }

        ImGui::EndTable();
    }

    // Lower half: response viewer for selected entry.
    if (selected_history_ != INVALID_CALL_HANDLE && !response_json_preview_.empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("Response:");
        ImGui::BeginChild("##json_view",
                          ImVec2(-1.0f, 0.0f),
                          false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(response_json_preview_.c_str());
        ImGui::EndChild();
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_history_entry
// ---------------------------------------------------------------------------

void ServiceCallerPanel::draw_history_entry(const CallRecord& rec, bool selected)
{
#ifdef SPECTRA_USE_IMGUI
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    auto s = rec.state.load(std::memory_order_acquire);
    push_state_color(s);

    bool clicked =
        ImGui::Selectable(state_badge(s),
                          selected,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0.0f, 0.0f));

    pop_state_color();

    if (clicked)
    {
        if (selected_history_ == rec.id)
        {
            // Deselect.
            selected_history_ = INVALID_CALL_HANDLE;
            response_json_preview_.clear();
        }
        else
        {
            selected_history_ = rec.id;
            response_json_preview_ =
                rec.response_json.empty() ? "(no response)" : rec.response_json;
        }
    }

    // Tooltip: full request JSON.
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Request: %s", rec.request_json.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(rec.service_name.c_str());

    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(format_latency(rec.latency_ms).c_str());

    ImGui::TableSetColumnIndex(3);
    std::string replay_id = "Re##" + std::to_string(rec.id);
    if (ImGui::SmallButton(replay_id.c_str()))
    {
        // Re-select service + populate form.
        select_service(rec.service_name);
        populate_from_record(rec);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-populate form from this call");
#else
    (void)rec;
    (void)selected;
#endif
}

}   // namespace spectra::adapters::ros2

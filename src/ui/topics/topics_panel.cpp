// topics_panel.cpp — Spectra Topics discovery panel implementation.

#include "topics_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::ui::topics
{

namespace
{

uint64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

#ifdef SPECTRA_USE_IMGUI
const char* kind_label(ipc::TopicKind k)
{
    return (k == ipc::TopicKind::Scalar3D) ? "3D" : "2D";
}
#endif

}   // namespace

void TopicsPanel::set_topics(std::vector<ipc::TopicInfoEntry> topics)
{
    std::lock_guard<std::mutex> g(mutex_);
    topics_ = std::move(topics);
}

TopicDragPayload TopicsPanel::make_payload(const ipc::TopicInfoEntry& t)
{
    TopicDragPayload p{};
    const auto       n = std::min(t.name.size(), sizeof(p.name) - 1);
    std::memcpy(p.name, t.name.data(), n);
    p.name[n] = '\0';
    p.kind    = static_cast<uint32_t>(t.kind);
    return p;
}

bool TopicsPanel::submit_subscribe(const std::string& topic_name,
                                   uint64_t           figure_id,
                                   uint32_t           axes_index)
{
    if (!subscribe_cb_ || topic_name.empty() || figure_id == 0)
        return false;
    SubscribeRequest req;
    req.topic_name   = topic_name;
    req.figure_id    = figure_id;
    req.axes_index   = axes_index;
    req.series_index = 0xFFFFFFFFu;
    subscribe_cb_(req);
    return true;
}

bool TopicsPanel::draw()
{
#ifndef SPECTRA_USE_IMGUI
    return false;
#else
    if (!visible_)
        return false;

    // Periodic refresh — 1 Hz.  EVT_TOPIC_LIST_CHANGED will also drive an
    // immediate refresh on the transport side; this is just a fallback.
    const uint64_t now = now_ms();
    if (list_cb_ && (now - last_list_req_ms_ > 1000))
    {
        list_cb_();
        last_list_req_ms_ = now;
    }

    bool committed_subscribe = false;

    ImGui::SetNextWindowSize(ImVec2(360.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Topics", &visible_))
    {
        ImGui::End();
        return false;
    }

    if (ImGui::Button("Refresh"))
    {
        if (list_cb_)
            list_cb_();
        last_list_req_ms_ = now;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##topic_filter", "Filter…", filter_buf_, sizeof(filter_buf_));

    std::vector<ipc::TopicInfoEntry> snapshot;
    {
        std::lock_guard<std::mutex> g(mutex_);
        snapshot = topics_;
    }

    const std::string filter = filter_buf_;
    if (snapshot.empty())
    {
        ImGui::TextDisabled("(no topics published)");
    }
    else if (ImGui::BeginTable(
                 "##topics_tbl",
                 5,
                 ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 14.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Hz", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (const auto& t : snapshot)
        {
            if (!filter.empty() && t.name.find(filter) == std::string::npos)
                continue;

            ImGui::PushID(t.name.c_str());
            ImGui::TableNextRow();

            // Status dot
            ImGui::TableSetColumnIndex(0);
            const ImU32 dot =
                t.publisher_online ? IM_COL32(80, 200, 120, 255) : IM_COL32(140, 140, 140, 255);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(p.x + 7.0f, p.y + ImGui::GetTextLineHeight() * 0.5f + 2.0f),
                4.0f,
                dot);

            // Name (drag source)
            ImGui::TableSetColumnIndex(1);
            ImGui::Selectable(t.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
            {
                TopicDragPayload pl = make_payload(t);
                ImGui::SetDragDropPayload(TOPIC_DRAG_TYPE, &pl, sizeof(pl));
                ImGui::TextUnformatted(t.name.c_str());
                ImGui::EndDragDropSource();
            }

            // Kind
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(kind_label(t.kind));

            // Hz
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f", t.estimated_hz);

            // Subscribe button (targets active figure, axes 0)
            ImGui::TableSetColumnIndex(4);
            const bool can_sub = t.publisher_online && target_figure_id_ != 0 && subscribe_cb_;
            if (!can_sub)
                ImGui::BeginDisabled();
            if (ImGui::SmallButton("Plot"))
            {
                if (submit_subscribe(t.name, target_figure_id_, 0))
                    committed_subscribe = true;
            }
            if (!can_sub)
                ImGui::EndDisabled();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
    return committed_subscribe;
#endif
}

}   // namespace spectra::ui::topics

// field_drag_drop.cpp — FieldDragDrop implementation.

#include "field_drag_drop.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

#ifdef SPECTRA_USE_IMGUI

// Copy a FieldDragPayload into a fixed-size buffer for ImGui's drag payload
// mechanism.  ImGui stores a memcpy of the data we pass in SetDragDropPayload,
// so we use a flat struct with fixed-length char arrays.

constexpr size_t PATH_MAX_LEN  = 256;
constexpr size_t TYPE_MAX_LEN  = 128;
constexpr size_t LABEL_MAX_LEN = 320;

struct RawPayload
{
    char topic_name[PATH_MAX_LEN]{};
    char field_path[PATH_MAX_LEN]{};
    char type_name[TYPE_MAX_LEN]{};
    char label[LABEL_MAX_LEN]{};
};

RawPayload to_raw(const FieldDragPayload& p)
{
    RawPayload raw{};
    auto       safe_copy = [](char* dst, size_t n, const std::string& src)
    {
        size_t len = src.size() < n - 1 ? src.size() : n - 1;
        src.copy(dst, len);
        dst[len] = '\0';
    };
    safe_copy(raw.topic_name, PATH_MAX_LEN, p.topic_name);
    safe_copy(raw.field_path, PATH_MAX_LEN, p.field_path);
    safe_copy(raw.type_name, TYPE_MAX_LEN, p.type_name);
    safe_copy(raw.label, LABEL_MAX_LEN, p.label);
    return raw;
}

FieldDragPayload from_raw(const RawPayload& raw)
{
    FieldDragPayload p;
    p.topic_name = raw.topic_name;
    p.field_path = raw.field_path;
    p.type_name  = raw.type_name;
    p.label      = raw.label;
    return p;
}

#endif   // SPECTRA_USE_IMGUI

}   // anonymous namespace

// ---------------------------------------------------------------------------
// FieldDragDrop::begin_drag_source
// ---------------------------------------------------------------------------

bool FieldDragDrop::begin_drag_source(const FieldDragPayload& payload)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return false;
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        return false;

    RawPayload raw = to_raw(payload);
    ImGui::SetDragDropPayload(DRAG_TYPE, &raw, sizeof(raw));

    // Tooltip shown while dragging.
    const std::string& lbl =
        payload.label.empty() ? FieldDragPayload::make_label(payload.topic_name, payload.field_path)
                              : payload.label;

    ImGui::TextUnformatted("Plot:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", lbl.c_str());

    ImGui::EndDragDropSource();
    return true;
#else
    (void)payload;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::accept_drop_current_axes
// ---------------------------------------------------------------------------

bool FieldDragDrop::accept_drop_current_axes()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return false;
    if (!ImGui::BeginDragDropTarget())
        return false;

    bool accepted = false;
    if (const ImGuiPayload* imgui_payload = ImGui::AcceptDragDropPayload(DRAG_TYPE))
    {
        const auto*      raw = static_cast<const RawPayload*>(imgui_payload->Data);
        FieldDragPayload p   = from_raw(*raw);
        fire_request(p, PlotTarget::CurrentAxes);
        accepted = true;
    }
    ImGui::EndDragDropTarget();
    return accepted;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::accept_drop_new_window
// ---------------------------------------------------------------------------

bool FieldDragDrop::accept_drop_new_window()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return false;
    if (!ImGui::BeginDragDropTarget())
        return false;

    bool accepted = false;
    if (const ImGuiPayload* imgui_payload = ImGui::AcceptDragDropPayload(DRAG_TYPE))
    {
        const auto*      raw = static_cast<const RawPayload*>(imgui_payload->Data);
        FieldDragPayload p   = from_raw(*raw);
        fire_request(p, PlotTarget::NewWindow);
        accepted = true;
    }
    ImGui::EndDragDropTarget();
    return accepted;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::show_context_menu
// ---------------------------------------------------------------------------

void FieldDragDrop::show_context_menu(const FieldDragPayload& payload, const char* popup_id)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    if (!ImGui::BeginPopupContextItem(popup_id))
        return;

    const std::string& lbl =
        payload.label.empty() ? FieldDragPayload::make_label(payload.topic_name, payload.field_path)
                              : payload.label;

    // Header — non-interactive label showing what will be plotted.
    ImGui::TextDisabled("Plot: %s", lbl.c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Plot in new window"))
    {
        pending_         = true;
        pending_payload_ = payload;
        pending_target_  = PlotTarget::NewWindow;
    }

    if (ImGui::MenuItem("Plot in current axes"))
    {
        pending_         = true;
        pending_payload_ = payload;
        pending_target_  = PlotTarget::CurrentAxes;
    }

    if (ImGui::MenuItem("Plot in new subplot"))
    {
        pending_         = true;
        pending_payload_ = payload;
        pending_target_  = PlotTarget::NewSubplot;
    }

    ImGui::EndPopup();
#else
    (void)payload;
    (void)popup_id;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::draw_drop_zone
// ---------------------------------------------------------------------------

bool FieldDragDrop::draw_drop_zone()
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return false;
    // Invisible button covering the entire content region acts as the drop target.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 0.0f)
        avail.x = 1.0f;
    if (avail.y <= 0.0f)
        avail.y = 1.0f;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##drop_zone", avail);

    // Highlighted overlay when a compatible payload is in flight.
    if (is_dragging())
    {
        const bool  hovered  = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const ImU32 fill_col = hovered ? IM_COL32(60, 180, 255, 55) : IM_COL32(60, 180, 255, 22);
        const ImU32 border_col =
            hovered ? IM_COL32(60, 180, 255, 220) : IM_COL32(60, 180, 255, 100);

        ImDrawList*  dl   = ImGui::GetWindowDrawList();
        const ImVec2 pmax = {pos.x + avail.x, pos.y + avail.y};
        dl->AddRectFilled(pos, pmax, fill_col, 4.0f);
        dl->AddRect(pos, pmax, border_col, 4.0f, 0, 2.0f);

        if (hovered)
        {
            // Center label.
            const char*  lbl      = "Drop to plot here";
            const ImVec2 text_sz  = ImGui::CalcTextSize(lbl);
            const ImVec2 text_pos = {pos.x + (avail.x - text_sz.x) * 0.5f,
                                     pos.y + (avail.y - text_sz.y) * 0.5f};
            dl->AddText(text_pos, IM_COL32(60, 180, 255, 240), lbl);
        }
    }

    return accept_drop_current_axes();
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::is_dragging
// ---------------------------------------------------------------------------

bool FieldDragDrop::is_dragging() const
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return false;
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    return payload != nullptr && payload->IsDataType(DRAG_TYPE) && payload->Data != nullptr;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::try_get_dragging_payload
// ---------------------------------------------------------------------------

bool FieldDragDrop::try_get_dragging_payload(FieldDragPayload& out) const
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return false;
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(DRAG_TYPE) || !payload->Data)
        return false;
    const auto* raw = static_cast<const RawPayload*>(payload->Data);
    out             = from_raw(*raw);
    return true;
#else
    (void)out;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// FieldDragDrop::consume_pending_request
// ---------------------------------------------------------------------------

bool FieldDragDrop::consume_pending_request(FieldDragPayload& payload_out, PlotTarget& target_out)
{
    if (!pending_)
        return false;

    payload_out = pending_payload_;
    target_out  = pending_target_;
    pending_    = false;

    // Also fire via callback if one is registered.
    fire_request(payload_out, target_out);

    return true;
}

// ---------------------------------------------------------------------------
// FieldDragDrop::fire_request (private)
// ---------------------------------------------------------------------------

void FieldDragDrop::fire_request(const FieldDragPayload& payload, PlotTarget target)
{
    if (request_cb_)
        request_cb_(payload, target);
}

}   // namespace spectra::adapters::ros2

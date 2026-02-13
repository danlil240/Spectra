#ifdef PLOTIX_USE_IMGUI

#include "widgets.hpp"
#include "icons.hpp"

#include <imgui.h>
#include <cstring>
#include <cstdio>

namespace plotix::ui::widgets {

// ─── Section Header ─────────────────────────────────────────────────────────

bool section_header(const char* label, bool* open, ImFont* font) {
    const auto& c = theme();

    ImGui::PushID(label);

    // Full-width clickable area
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Hover highlight
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, c.accent_subtle.a));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, c.accent_muted.a));

    bool clicked = ImGui::Selectable("##hdr", false, ImGuiSelectableFlags_None,
                                     ImVec2(avail, ImGui::GetTextLineHeightWithSpacing() + 4.0f));
    if (clicked && open) {
        *open = !*open;
    }

    ImGui::PopStyleColor(3);

    // Draw chevron + label on top of the selectable
    ImGui::SetCursorScreenPos(cursor);

    // Chevron icon
    const char* chevron = (open && *open) ? icon_str(Icon::ChevronDown) : icon_str(Icon::ChevronRight);
    ImFont* icon_f = icon_font(tokens::ICON_SM);
    if (icon_f) ImGui::PushFont(icon_f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(chevron);
    ImGui::PopStyleColor();
    if (icon_f) ImGui::PopFont();

    ImGui::SameLine(0.0f, tokens::SPACE_2);

    // Label text
    if (font) ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (font) ImGui::PopFont();

    // Move cursor past the selectable height
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + ImGui::GetTextLineHeightWithSpacing() + 4.0f));

    ImGui::PopID();

    return open ? *open : true;
}

// ─── Separator ──────────────────────────────────────────────────────────────

void separator() {
    const auto& c = theme();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, c.border_subtle.a));
    ImGui::Separator();
    ImGui::PopStyleColor();
}

// ─── Info Row ───────────────────────────────────────────────────────────────

void info_row(const char* label, const char* value) {
    const auto& c = theme();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

void info_row_mono(const char* label, const char* value) {
    const auto& c = theme();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

// ─── Color Field ────────────────────────────────────────────────────────────

bool color_field(const char* label, plotix::Color& color) {
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);

    float col[4] = {color.r, color.g, color.b, color.a};
    bool changed = ImGui::ColorEdit4("##color", col,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHSV);
    if (changed) {
        color = plotix::Color{col[0], col[1], col[2], col[3]};
    }

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Slider Field ───────────────────────────────────────────────────────────

bool slider_field(const char* label, float& value, float min, float max,
                  const char* fmt) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));
    ImGui::PushItemWidth(-1);

    bool changed = ImGui::SliderFloat("##slider", &value, min, max, fmt);

    ImGui::PopItemWidth();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

// ─── Drag Field ─────────────────────────────────────────────────────────────

bool drag_field(const char* label, float& value, float speed,
                float min, float max, const char* fmt) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    bool changed = ImGui::DragFloat("##drag", &value, speed, min, max, fmt);

    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Drag Field 2 ───────────────────────────────────────────────────────────

bool drag_field2(const char* label, float& v0, float& v1, float speed,
                 const char* fmt) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    float v[2] = {v0, v1};
    bool changed = ImGui::DragFloat2("##drag2", v, speed, 0.0f, 0.0f, fmt);
    if (changed) {
        v0 = v[0];
        v1 = v[1];
    }

    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Checkbox Field ─────────────────────────────────────────────────────────

bool checkbox_field(const char* label, bool& value) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));

    bool changed = ImGui::Checkbox(label, &value);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Toggle Field ───────────────────────────────────────────────────────────

bool toggle_field(const char* label, bool& value) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 36.0f);

    // Draw a toggle switch using ImGui draw list
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float height = 18.0f;
    float width = 34.0f;
    float radius = height * 0.5f;

    // Invisible button for interaction
    bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
    if (clicked) value = !value;

    // Background
    ImU32 bg_col = value
        ? ImGui::ColorConvertFloat4ToU32(ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a))
        : ImGui::ColorConvertFloat4ToU32(ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg_col, radius);

    // Knob
    float knob_x = value ? (pos.x + width - radius) : (pos.x + radius);
    draw->AddCircleFilled(ImVec2(knob_x, pos.y + radius),
                          radius - 2.0f,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)));

    ImGui::PopID();
    return clicked;
}

// ─── Combo Field ────────────────────────────────────────────────────────────

bool combo_field(const char* label, int& current, const char* const* items, int count) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, tokens::RADIUS_MD);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    bool changed = ImGui::Combo("##combo", &current, items, count);

    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return changed;
}

// ─── Text Field ─────────────────────────────────────────────────────────────

bool text_field(const char* label, std::string& value) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    char buf[256];
    std::strncpy(buf, value.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    bool changed = ImGui::InputText("##text", buf, sizeof(buf));
    if (changed) {
        value = buf;
    }

    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Button Field ───────────────────────────────────────────────────────────

bool button_field(const char* label) {
    const auto& c = theme();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, c.accent_subtle.a));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, c.accent_muted.a));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));

    bool clicked = ImGui::Button(label, ImVec2(-1, 0));

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
}

// ─── Icon Button Small ──────────────────────────────────────────────────────

bool icon_button_small(const char* icon, const char* tooltip, bool active) {
    const auto& c = theme();

    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, c.accent_muted.a));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, c.accent_subtle.a));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

    ImFont* f = icon_font(tokens::ICON_SM);
    if (f) ImGui::PushFont(f);

    bool clicked = ImGui::Button(icon, ImVec2(24, 24));

    if (f) ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (tooltip && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return clicked;
}

// ─── Group ──────────────────────────────────────────────────────────────────

void begin_group(const char* id) {
    ImGui::PushID(id);
    ImGui::Indent(tokens::SPACE_3);
}

void end_group() {
    ImGui::Unindent(tokens::SPACE_3);
    ImGui::PopID();
}

// ─── Color Swatch ───────────────────────────────────────────────────────────

void color_swatch(const plotix::Color& color, float size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col, 3.0f);
    ImGui::Dummy(ImVec2(size, size));
}

// ─── Spacing Helpers ────────────────────────────────────────────────────────

void small_spacing() {
    ImGui::Spacing();
}

void section_spacing() {
    ImGui::Spacing();
    ImGui::Spacing();
}

} // namespace plotix::ui::widgets

#endif // PLOTIX_USE_IMGUI

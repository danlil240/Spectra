#ifdef PLOTIX_USE_IMGUI

#include "widgets.hpp"
#include "icons.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>

namespace plotix::ui::widgets {

// ─── Section Animation State ─────────────────────────────────────────────────

static std::unordered_map<std::string, SectionAnimState>& section_anim_map() {
    static std::unordered_map<std::string, SectionAnimState> map;
    return map;
}

SectionAnimState& get_section_anim(const char* id) {
    return section_anim_map()[id];
}

void update_section_animations(float dt) {
    constexpr float ANIM_SPEED = 8.0f; // ~125ms to full open/close
    for (auto& [key, state] : section_anim_map()) {
        float target = state.target_open ? 1.0f : 0.0f;
        if (std::abs(state.anim_t - target) > 0.001f) {
            // Exponential ease toward target
            state.anim_t += (target - state.anim_t) * std::min(1.0f, ANIM_SPEED * dt);
        } else {
            state.anim_t = target;
        }
    }
}

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
        // Update animation target
        auto& anim = get_section_anim(label);
        anim.target_open = *open;
    }

    ImGui::PopStyleColor(3);

    // Sync animation state if open changed externally
    if (open) {
        auto& anim = get_section_anim(label);
        if (*open != anim.was_open) {
            anim.target_open = *open;
            anim.was_open = *open;
        }
    }

    // Draw chevron + label on top of the selectable
    ImGui::SetCursorScreenPos(cursor);

    // Animated chevron rotation: interpolate between right and down
    float chevron_t = 1.0f;
    if (open) {
        auto& anim = get_section_anim(label);
        chevron_t = anim.anim_t;
    }
    const char* chevron = (chevron_t > 0.5f) ? icon_str(Icon::ChevronDown) : icon_str(Icon::ChevronRight);
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

    // Return true if section should be drawn (either open or animating closed)
    if (!open) return true;
    auto& anim = get_section_anim(label);
    return *open || anim.anim_t > 0.01f;
}

bool begin_animated_section(const char* id) {
    auto& anim = get_section_anim(id);
    float t = anim.anim_t;

    if (t <= 0.01f) {
        // Fully collapsed — skip content entirely
        return false;
    }

    // Apply alpha fade
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * t);

    // Use a clipping child window to animate height
    // We use a fixed max-height approach: content is drawn at full size,
    // but the child window clips it based on animation progress.
    if (t < 0.99f) {
        // Estimate content height — use a generous max, the child will clip
        float max_h = 600.0f * t;
        ImGui::BeginChild(id, ImVec2(0, max_h), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    } else {
        // Fully open — auto-resize to content height
        ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    }

    return true;
}

void end_animated_section() {
    ImGui::EndChild();
    ImGui::PopStyleVar(); // Alpha
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, tokens::RADIUS_PILL);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, tokens::RADIUS_LG);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
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

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, 0.7f));
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
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

    ImFont* f = icon_font(tokens::ICON_SM);
    if (f) ImGui::PushFont(f);

    bool clicked = ImGui::Button(icon, ImVec2(24, 24));

    if (f) ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, tokens::RADIUS_MD);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.3f));
        ImGui::SetTooltip("%s", tooltip);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
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
    draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), col, tokens::RADIUS_SM);
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

// ─── Sparkline ──────────────────────────────────────────────────────────────

void sparkline(const char* id, std::span<const float> values, float width,
               float height, const plotix::Color& color) {
    if (values.empty()) return;

    const auto& c = theme();
    ImGui::PushID(id);

    float w = (width < 0.0f) ? ImGui::GetContentRegionAvail().x : width;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Find data range
    float vmin = values[0], vmax = values[0];
    for (float v : values) {
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    float range = (vmax - vmin);
    if (range < 1e-10f) range = 1.0f;

    // Determine line color
    plotix::Color line_col = (color.r > 0.0f || color.g > 0.0f || color.b > 0.0f)
        ? color : plotix::Color{c.accent.r, c.accent.g, c.accent.b, c.accent.a};
    ImU32 col32 = ImGui::ColorConvertFloat4ToU32(
        ImVec4(line_col.r, line_col.g, line_col.b, line_col.a));
    ImU32 fill_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(line_col.r, line_col.g, line_col.b, 0.15f));

    // Draw filled area
    size_t n = values.size();
    float step = w / static_cast<float>(n > 1 ? n - 1 : 1);
    float baseline_y = pos.y + height;

    // Build polyline for fill
    std::vector<ImVec2> fill_pts;
    fill_pts.reserve(n + 2);
    fill_pts.push_back(ImVec2(pos.x, baseline_y));
    for (size_t i = 0; i < n; ++i) {
        float x = pos.x + static_cast<float>(i) * step;
        float y = pos.y + height - ((values[i] - vmin) / range) * height;
        fill_pts.push_back(ImVec2(x, y));
    }
    fill_pts.push_back(ImVec2(pos.x + w, baseline_y));
    draw->AddConvexPolyFilled(fill_pts.data(), static_cast<int>(fill_pts.size()), fill_col);

    // Draw line
    for (size_t i = 0; i + 1 < n; ++i) {
        float x0 = pos.x + static_cast<float>(i) * step;
        float y0 = pos.y + height - ((values[i] - vmin) / range) * height;
        float x1 = pos.x + static_cast<float>(i + 1) * step;
        float y1 = pos.y + height - ((values[i + 1] - vmin) / range) * height;
        draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col32, 1.5f);
    }

    ImGui::Dummy(ImVec2(w, height));
    ImGui::PopID();
}

// ─── Progress Bar ───────────────────────────────────────────────────────────

void progress_bar(const char* label, float fraction, const char* overlay) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));

    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopID();
}

// ─── Badge ──────────────────────────────────────────────────────────────────

void badge(const char* text, const plotix::Color& bg, const plotix::Color& fg) {
    const auto& c = theme();

    plotix::Color bg_col = (bg.r > 0.0f || bg.g > 0.0f || bg.b > 0.0f)
        ? bg : plotix::Color{c.accent_muted.r, c.accent_muted.g, c.accent_muted.b, c.accent_muted.a};
    plotix::Color fg_col = (fg.r > 0.0f || fg.g > 0.0f || fg.b > 0.0f)
        ? fg : plotix::Color{c.accent.r, c.accent.g, c.accent.b, c.accent.a};

    ImVec2 text_size = ImGui::CalcTextSize(text);
    float pad_x = tokens::SPACE_2;
    float pad_y = 2.0f;
    float total_w = text_size.x + pad_x * 2.0f;
    float total_h = text_size.y + pad_y * 2.0f;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImU32 bg32 = ImGui::ColorConvertFloat4ToU32(ImVec4(bg_col.r, bg_col.g, bg_col.b, bg_col.a));
    draw->AddRectFilled(pos, ImVec2(pos.x + total_w, pos.y + total_h), bg32, tokens::RADIUS_PILL);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad_x, pos.y + pad_y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(fg_col.r, fg_col.g, fg_col.b, fg_col.a));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + total_h + 2.0f));
    ImGui::Dummy(ImVec2(total_w, 0)); // Advance cursor
}

// ─── Separator Label ────────────────────────────────────────────────────────

void separator_label(const char* label, ImFont* font) {
    const auto& c = theme();
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();

    if (font) ImGui::PushFont(font);
    ImVec2 text_size = ImGui::CalcTextSize(label);
    if (font) ImGui::PopFont();

    float line_y = pos.y + text_size.y * 0.5f;
    float gap = tokens::SPACE_2;
    float text_x = (avail - text_size.x) * 0.5f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 line_col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, c.border_subtle.a));

    // Left line
    if (text_x > gap) {
        draw->AddLine(ImVec2(pos.x, line_y), ImVec2(pos.x + text_x - gap, line_y), line_col);
    }
    // Right line
    float right_start = pos.x + text_x + text_size.x + gap;
    if (right_start < pos.x + avail) {
        draw->AddLine(ImVec2(right_start, line_y), ImVec2(pos.x + avail, line_y), line_col);
    }

    // Centered text
    ImGui::SetCursorScreenPos(ImVec2(pos.x + text_x, pos.y));
    if (font) ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, c.text_tertiary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (font) ImGui::PopFont();
}

// ─── Integer Drag Field ─────────────────────────────────────────────────────

bool int_drag_field(const char* label, int& value, int speed, int min, int max,
                    const char* fmt) {
    const auto& c = theme();
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, tokens::RADIUS_SM);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, c.bg_tertiary.a));
    ImGui::PushItemWidth(-1);

    float fspeed = static_cast<float>(speed);
    bool changed = ImGui::DragInt("##idrag", &value, fspeed, min, max, fmt);

    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

// ─── Stat Row ───────────────────────────────────────────────────────────────

void stat_row(const char* label, const char* value, const char* unit) {
    const auto& c = theme();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    if (unit) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s %s", value, unit);
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::TextUnformatted(value);
    }
    ImGui::PopStyleColor();
}

void stat_row_colored(const char* label, const char* value,
                      const plotix::Color& dot_color, const char* unit) {
    // const auto& c = theme();  // Currently unused

    // Color dot
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float dot_r = 4.0f;
    float text_h = ImGui::GetTextLineHeight();
    ImU32 col32 = ImGui::ColorConvertFloat4ToU32(
        ImVec4(dot_color.r, dot_color.g, dot_color.b, dot_color.a));
    draw->AddCircleFilled(ImVec2(pos.x + dot_r, pos.y + text_h * 0.5f), dot_r, col32);
    ImGui::Dummy(ImVec2(dot_r * 2.0f + 4.0f, 0));
    ImGui::SameLine();

    stat_row(label, value, unit);
}

} // namespace plotix::ui::widgets

#endif // PLOTIX_USE_IMGUI

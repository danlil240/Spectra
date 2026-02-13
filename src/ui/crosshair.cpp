#ifdef PLOTIX_USE_IMGUI

#include "crosshair.hpp"
#include "input.hpp"
#include "theme.hpp"

#include <imgui.h>
#include <cmath>
#include <cstdio>

namespace plotix {

// Helper: draw a dashed line on an ImDrawList
static void draw_dashed_line(ImDrawList* dl, ImVec2 p0, ImVec2 p1,
                             ImU32 color, float dash, float gap, float thickness) {
    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f) return;

    float nx = dx / length;
    float ny = dy / length;
    float drawn = 0.0f;
    bool drawing = true;

    while (drawn < length) {
        float seg = drawing ? dash : gap;
        float end = std::min(drawn + seg, length);
        if (drawing) {
            dl->AddLine(
                ImVec2(p0.x + nx * drawn, p0.y + ny * drawn),
                ImVec2(p0.x + nx * end, p0.y + ny * end),
                color, thickness);
        }
        drawn = end;
        drawing = !drawing;
    }
}

void Crosshair::draw(const CursorReadout& cursor, const Rect& viewport,
                     float xlim_min, float xlim_max, float ylim_min, float ylim_max) {
    // Animate opacity
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f) opacity_ = target;
    if (opacity_ < 0.01f) return;

    const auto& colors = ui::ThemeManager::instance().colors();
    ImU32 line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(
        colors.crosshair.r, colors.crosshair.g, colors.crosshair.b, colors.crosshair.a * opacity_));

    float sx = static_cast<float>(cursor.screen_x);
    float sy = static_cast<float>(cursor.screen_y);

    // Clamp to viewport
    float vx0 = viewport.x;
    float vy0 = viewport.y;
    float vx1 = viewport.x + viewport.w;
    float vy1 = viewport.y + viewport.h;

    if (sx < vx0 || sx > vx1 || sy < vy0 || sy > vy1) return;

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // Vertical line (full height of viewport)
    draw_dashed_line(fg, ImVec2(sx, vy0), ImVec2(sx, vy1),
                     line_color, dash_length_, gap_length_, 1.0f);

    // Horizontal line (full width of viewport)
    draw_dashed_line(fg, ImVec2(vx0, sy), ImVec2(vx1, sy),
                     line_color, dash_length_, gap_length_, 1.0f);

    // Axis-intersection labels
    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(
        colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.9f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(ImVec4(
        colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

    char x_label[32], y_label[32];
    std::snprintf(x_label, sizeof(x_label), "%.4g", cursor.data_x);
    std::snprintf(y_label, sizeof(y_label), "%.4g", cursor.data_y);

    ImFont* font = ImGui::GetFont();
    constexpr float label_pad = 3.0f;

    // X label at bottom of viewport
    {
        ImVec2 sz = font->CalcTextSizeA(font->FontSize * 0.85f, 200.0f, 0.0f, x_label);
        float lx = sx - sz.x * 0.5f;
        float ly = vy1 + 2.0f;
        // Clamp horizontally
        if (lx < vx0) lx = vx0;
        if (lx + sz.x + label_pad * 2.0f > vx1) lx = vx1 - sz.x - label_pad * 2.0f;

        fg->AddRectFilled(
            ImVec2(lx - label_pad, ly),
            ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
            label_bg, 3.0f);
        fg->AddText(font, font->FontSize * 0.85f,
                    ImVec2(lx, ly + label_pad), label_text, x_label);
    }

    // Y label at left of viewport
    {
        ImVec2 sz = font->CalcTextSizeA(font->FontSize * 0.85f, 200.0f, 0.0f, y_label);
        float lx = vx0 - sz.x - label_pad * 2.0f - 2.0f;
        float ly = sy - sz.y * 0.5f;
        // Clamp vertically
        if (ly < vy0) ly = vy0;
        if (ly + sz.y + label_pad * 2.0f > vy1) ly = vy1 - sz.y - label_pad * 2.0f;

        fg->AddRectFilled(
            ImVec2(lx, ly - label_pad),
            ImVec2(lx + sz.x + label_pad * 2.0f, ly + sz.y + label_pad),
            label_bg, 3.0f);
        fg->AddText(font, font->FontSize * 0.85f,
                    ImVec2(lx + label_pad, ly), label_text, y_label);
    }
}

} // namespace plotix

#endif // PLOTIX_USE_IMGUI

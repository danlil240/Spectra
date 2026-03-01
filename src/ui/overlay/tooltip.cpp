#ifdef SPECTRA_USE_IMGUI

    #include "tooltip.hpp"

    #include <algorithm>
    #include <cmath>
    #include <cstdio>
    #include <imgui.h>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

void Tooltip::set_fonts(ImFont* body, ImFont* heading)
{
    font_body_    = body;
    font_heading_ = heading;
}

void Tooltip::draw(const NearestPointResult& nearest, float window_width, float window_height)
{
    if (!enabled_)
        return;

    // Animate opacity
    target_opacity_ = (nearest.found && nearest.distance_px <= snap_radius_px_) ? 1.0f : 0.0f;
    float dt        = ImGui::GetIO().DeltaTime;
    float speed     = 12.0f;
    opacity_ += (target_opacity_ - opacity_) * std::min(1.0f, speed * dt);
    if (std::abs(opacity_ - target_opacity_) < 0.01f)
        opacity_ = target_opacity_;

    if (opacity_ < 0.01f)
        return;

    const auto& colors = ui::ThemeManager::instance().colors();

    // Format coordinate strings
    char x_buf[64], y_buf[64];
    std::snprintf(x_buf, sizeof(x_buf), "%.6g", nearest.data_x);
    std::snprintf(y_buf, sizeof(y_buf), "%.6g", nearest.data_y);

    const char* series_name  = "Unknown";
    Color       series_color = colors::gray;
    if (nearest.series)
    {
        if (!nearest.series->label().empty())
            series_name = nearest.series->label().c_str();
        series_color = nearest.series->color();
    }

    // Tooltip layout constants — compact, glass-like
    constexpr float padding     = 8.0f;
    constexpr float swatch_size = 9.0f;
    constexpr float row_height  = 16.0f;
    constexpr float min_width   = 130.0f;

    // Measure text to size the tooltip
    ImFont* body_font = font_body_ ? font_body_ : ImGui::GetFont();
    ImVec2  name_size = body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, series_name);

    char coord_line[192];
    std::snprintf(coord_line, sizeof(coord_line), "X: %s  Y: %s", x_buf, y_buf);
    ImVec2 coord_size = body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, coord_line);

    char idx_line[64];
    std::snprintf(idx_line, sizeof(idx_line), "Index: %zu", nearest.point_index);
    ImVec2 idx_size = body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, idx_line);

    float content_w =
        std::max({name_size.x + swatch_size + 6.0f, coord_size.x, idx_size.x, min_width});
    float tooltip_w = content_w + padding * 2.0f;
    float tooltip_h = padding * 2.0f + row_height * 3.0f;

    // Position: offset from the snap point, clamped to window
    float offset_x = 16.0f;
    float offset_y = -tooltip_h - 8.0f;
    float tx       = nearest.screen_x + offset_x;
    float ty       = nearest.screen_y + offset_y;

    // Clamp to window bounds
    if (tx + tooltip_w > window_width - 4.0f)
        tx = nearest.screen_x - tooltip_w - offset_x;
    if (ty < 4.0f)
        ty = nearest.screen_y + 16.0f;
    if (tx < 4.0f)
        tx = 4.0f;
    if (ty + tooltip_h > window_height - 4.0f)
        ty = window_height - tooltip_h - 4.0f;

    // Draw tooltip window with soft drop shadow for depth
    ImGui::SetNextWindowPos(ImVec2(tx, ty));
    ImGui::SetNextWindowSize(ImVec2(tooltip_w, tooltip_h));

    // Soft shadow (drawn on foreground draw list before the window)
    {
        ImDrawList* fg      = ImGui::GetForegroundDrawList();
        float       sh_off  = 2.0f;
        float       sh_r    = ui::tokens::RADIUS_MD + 2.0f;
        ImU32       sh_col  = IM_COL32(0, 0, 0, static_cast<int>(30.0f * opacity_));
        fg->AddRectFilled(ImVec2(tx + sh_off, ty + sh_off),
                          ImVec2(tx + tooltip_w + sh_off, ty + tooltip_h + sh_off),
                          sh_col, sh_r);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.tooltip_bg.r, colors.tooltip_bg.g, colors.tooltip_bg.b, colors.tooltip_bg.a));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(colors.tooltip_border.r,
                                 colors.tooltip_border.g,
                                 colors.tooltip_border.b,
                                 colors.tooltip_border.a));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##data_tooltip", nullptr, flags))
    {
        if (font_body_)
            ImGui::PushFont(font_body_);

        // Row 1: color swatch + series name
        ImVec2      cursor = ImGui::GetCursorScreenPos();
        ImDrawList* dl     = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
            ImVec2(cursor.x, cursor.y + 3.0f),
            ImVec2(cursor.x + swatch_size, cursor.y + 3.0f + swatch_size),
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(series_color.r, series_color.g, series_color.b, series_color.a)),
            2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + swatch_size + 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_primary.r,
                                     colors.text_primary.g,
                                     colors.text_primary.b,
                                     colors.text_primary.a));
        ImGui::TextUnformatted(series_name);
        ImGui::PopStyleColor();

        // Row 2: coordinates
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::TextUnformatted(coord_line);
        ImGui::PopStyleColor();

        // Row 3: point index
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_tertiary.r,
                                     colors.text_tertiary.g,
                                     colors.text_tertiary.b,
                                     colors.text_tertiary.a));
        ImGui::TextUnformatted(idx_line);
        ImGui::PopStyleColor();

        if (font_body_)
            ImGui::PopFont();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    // Draw snap indicator dot at the data point
    if (nearest.found && nearest.distance_px <= snap_radius_px_)
    {
        ImDrawList* fg        = ImGui::GetForegroundDrawList();
        ImU32       dot_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(series_color.r, series_color.g, series_color.b, opacity_));
        ImU32 ring_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity_));
        fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 4.5f, dot_color);
        fg->AddCircle(ImVec2(nearest.screen_x, nearest.screen_y), 4.5f, ring_color, 0, 1.0f);
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

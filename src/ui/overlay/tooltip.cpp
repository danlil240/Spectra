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

    // Hysteresis: keep tooltip visible for 100ms after cursor leaves snap radius
    float dt       = ImGui::GetIO().DeltaTime;
    bool  in_range = nearest.found && nearest.distance_px <= snap_radius_px_;

    if (in_range)
    {
        hysteresis_     = 0.0f;
        target_opacity_ = 1.0f;
    }
    else
    {
        hysteresis_ += dt;
        if (hysteresis_ > 0.1f)   // 100ms hysteresis delay
            target_opacity_ = 0.0f;
    }

    // Asymmetric fade: 50ms in (speed=20), 100ms out (speed=10)
    float speed = (target_opacity_ > opacity_) ? 20.0f : 10.0f;
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

    // Format dy/dx string
    char dydx_line[96] = {};
    bool show_dydx     = nearest.dy_dx_valid;
    if (show_dydx)
        std::snprintf(dydx_line, sizeof(dydx_line), "dy/dx: %.6g", nearest.dy_dx);

    const char* series_name  = "Unknown";
    Color       series_color = colors::gray;
    if (nearest.series)
    {
        if (!nearest.series->label().empty())
            series_name = nearest.series->label().c_str();
        series_color = nearest.series->color();
    }

    // Tooltip layout constants — compact, glass-like (Vision.png style)
    constexpr float padding     = 8.0f;
    constexpr float swatch_size = 9.0f;
    constexpr float row_height  = 16.0f;
    constexpr float min_width   = 130.0f;

    // Measure text to size the tooltip
    ImFont* body_font = font_body_ ? font_body_ : ImGui::GetFont();
    ImVec2  name_size = body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, series_name);

    // Vision.png layout: X and Y on separate lines
    char x_line[96], y_line[96];
    std::snprintf(x_line, sizeof(x_line), "X: %s", x_buf);
    std::snprintf(y_line, sizeof(y_line), "Y: %s", y_buf);
    ImVec2 x_line_size = body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, x_line);
    ImVec2 y_line_size = body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, y_line);
    ImVec2 dydx_size   = show_dydx
                             ? body_font->CalcTextSizeA(body_font->FontSize, 1000.0f, 0.0f, dydx_line)
                             : ImVec2(0.0f, 0.0f);

    int   row_count = 3 + (show_dydx ? 1 : 0);   // name + X + Y + optional dy/dx
    float content_w = std::max(
        {name_size.x + swatch_size + 6.0f, x_line_size.x, y_line_size.x, dydx_size.x, min_width});
    float tooltip_w = content_w + padding * 2.0f;
    float tooltip_h = padding * 2.0f + row_height * static_cast<float>(row_count);

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
        ImDrawList* fg     = ImGui::GetForegroundDrawList();
        float       sh_off = 2.0f;
        float       sh_r   = ui::tokens::RADIUS_MD + 2.0f;
        ImU32       sh_col = IM_COL32(0, 0, 0, static_cast<int>(30.0f * opacity_));
        fg->AddRectFilled(ImVec2(tx + sh_off, ty + sh_off),
                          ImVec2(tx + tooltip_w + sh_off, ty + tooltip_h + sh_off),
                          sh_col,
                          sh_r);

        // Night theme: subtle accent glow halo around tooltip (Vision.png glass effect)
        if (colors.glow_intensity > 0.01f)
        {
            ImU32 glow_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.08f * opacity_ * colors.glow_intensity));
            fg->AddRect(ImVec2(tx - 2.0f, ty - 2.0f),
                        ImVec2(tx + tooltip_w + 2.0f, ty + tooltip_h + 2.0f),
                        glow_col,
                        sh_r + 2.0f,
                        0,
                        3.0f);
        }
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

        // Row 2: X coordinate (Vision.png style — separate line)
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::TextUnformatted(x_line);
        ImGui::PopStyleColor();

        // Row 3: Y coordinate
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(colors.text_secondary.r,
                                     colors.text_secondary.g,
                                     colors.text_secondary.b,
                                     colors.text_secondary.a));
        ImGui::TextUnformatted(y_line);
        ImGui::PopStyleColor();

        // Row 4: dy/dx derivative (when available)
        if (show_dydx)
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(colors.text_tertiary.r,
                                         colors.text_tertiary.g,
                                         colors.text_tertiary.b,
                                         colors.text_tertiary.a));
            ImGui::TextUnformatted(dydx_line);
            ImGui::PopStyleColor();
        }

        if (font_body_)
            ImGui::PopFont();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    // Draw triangular arrow pointer toward data point
    if (nearest.found && nearest.distance_px <= snap_radius_px_)
    {
        ImDrawList*     fg         = ImGui::GetForegroundDrawList();
        ImU32           arrow_col  = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                                colors.tooltip_bg.g,
                                                                colors.tooltip_bg.b,
                                                                colors.tooltip_bg.a * opacity_));
        constexpr float arrow_size = 6.0f;

        // Determine which edge the arrow should appear on
        bool tooltip_above = (ty + tooltip_h < nearest.screen_y);
        bool tooltip_below = (ty > nearest.screen_y);

        float arrow_x = std::clamp(nearest.screen_x,
                                   tx + arrow_size + 4.0f,
                                   tx + tooltip_w - arrow_size - 4.0f);

        if (tooltip_above)
        {
            // Arrow points down from tooltip bottom
            fg->AddTriangleFilled(ImVec2(arrow_x - arrow_size, ty + tooltip_h),
                                  ImVec2(arrow_x + arrow_size, ty + tooltip_h),
                                  ImVec2(arrow_x, ty + tooltip_h + arrow_size),
                                  arrow_col);
        }
        else if (tooltip_below)
        {
            // Arrow points up from tooltip top
            fg->AddTriangleFilled(ImVec2(arrow_x - arrow_size, ty),
                                  ImVec2(arrow_x + arrow_size, ty),
                                  ImVec2(arrow_x, ty - arrow_size),
                                  arrow_col);
        }
    }

    // Draw snap indicator dot at the data point and connection line
    if (nearest.found && nearest.distance_px <= snap_radius_px_)
    {
        ImDrawList* fg        = ImGui::GetForegroundDrawList();
        ImU32       dot_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(series_color.r, series_color.g, series_color.b, opacity_));
        ImU32 ring_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity_));

        // Connection line from tooltip to data point (dashed, subtle)
        {
            ImVec2 tooltip_center(tx + tooltip_w * 0.5f, ty + tooltip_h);
            ImVec2 data_point(nearest.screen_x, nearest.screen_y);

            // Only draw if there's meaningful distance
            float dx   = data_point.x - tooltip_center.x;
            float dy   = data_point.y - tooltip_center.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 20.0f)
            {
                ImU32 line_color =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                          colors.crosshair.g,
                                                          colors.crosshair.b,
                                                          colors.crosshair.a * 0.4f * opacity_));
                // Draw dashed connection line
                constexpr float dash_len = 4.0f;
                constexpr float gap_len  = 3.0f;
                float           nx       = dx / dist;
                float           ny       = dy / dist;
                float           pos      = 0.0f;
                while (pos < dist)
                {
                    float seg_start = pos;
                    float seg_end   = std::min(pos + dash_len, dist);
                    fg->AddLine(
                        ImVec2(tooltip_center.x + nx * seg_start,
                               tooltip_center.y + ny * seg_start),
                        ImVec2(tooltip_center.x + nx * seg_end, tooltip_center.y + ny * seg_end),
                        line_color,
                        1.0f);
                    pos = seg_end + gap_len;
                }
            }
        }

        // Night theme: series-colored glow halo around snap dot (Vision.png style)
        if (colors.glow_intensity > 0.01f)
        {
            // Outer soft bloom — large, very faint
            ImU32 glow_outer =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.12f * opacity_ * colors.glow_intensity));
            fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 12.0f, glow_outer, 24);
            // Inner bright bloom
            ImU32 glow_inner =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.30f * opacity_ * colors.glow_intensity));
            fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 7.0f, glow_inner, 16);
        }

        fg->AddCircleFilled(ImVec2(nearest.screen_x, nearest.screen_y), 4.5f, dot_color);
        fg->AddCircle(ImVec2(nearest.screen_x, nearest.screen_y), 4.5f, ring_color, 0, 1.0f);
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

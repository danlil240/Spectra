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

void Tooltip::draw(const NearestPointResult& nearest,
                   float                     window_width,
                   float                     window_height,
                   ImDrawList*               dl)
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

    // Tooltip layout constants
    constexpr float padding   = 10.0f;
    constexpr float swatch_r  = 5.0f;    // circle radius for color dot
    constexpr float col_gap   = 10.0f;   // gap between label and value columns
    constexpr float min_width = 140.0f;

    ImFont* body_font = font_body_ ? font_body_ : ImGui::GetFont();
    float   font_sz   = body_font->LegacySize;
    float   row_h     = font_sz + 4.0f;

    // Format label/value pairs
    const char* lbl_x    = "X";
    const char* lbl_y    = "Y";
    const char* lbl_dydx = "dy/dx";

    // Measure column widths
    float lbl_w =
        std::max({body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_x).x,
                  body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_y).x,
                  show_dydx ? body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, lbl_dydx).x : 0.0f});
    float val_w = std::max(
        {body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, x_buf).x,
         body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, y_buf).x,
         show_dydx ? body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, dydx_line + 6).x : 0.0f});
    float name_w =
        body_font->CalcTextSizeA(font_sz, 1000.f, 0.f, series_name).x + swatch_r * 2.0f + 6.0f;
    float data_row_w = lbl_w + col_gap + val_w;
    float content_w  = std::max({name_w, data_row_w, min_width});
    int   data_rows  = 2 + (show_dydx ? 1 : 0);
    float divider_h  = 6.0f;
    float tooltip_w  = content_w + padding * 2.0f;
    float tooltip_h  = padding * 2.0f + row_h   // name row
                      + divider_h               // separator gap
                      + row_h * static_cast<float>(data_rows);

    // Position: above-right of the snap point, clamped to window
    float offset_x = 14.0f;
    float offset_y = -tooltip_h - 8.0f;
    float tx       = nearest.screen_x + offset_x;
    float ty       = nearest.screen_y + offset_y;

    if (tx + tooltip_w > window_width - 4.0f)
        tx = nearest.screen_x - tooltip_w - offset_x;
    if (ty < 4.0f)
        ty = nearest.screen_y + 16.0f;
    if (tx < 4.0f)
        tx = 4.0f;
    if (ty + tooltip_h > window_height - 4.0f)
        ty = window_height - tooltip_h - 4.0f;

    // Draw entirely on the foreground draw list — this renders above all ImGui windows,
    // including popups and open menus, since the foreground draw list is composited last.
    {
        ImDrawList* fg  = dl ? dl : ImGui::GetForegroundDrawList();
        float       rnd = ui::tokens::RADIUS_MD;

        // Shadow
        ImU32 sh_col = IM_COL32(0, 0, 0, static_cast<int>(40.0f * opacity_));
        fg->AddRectFilled(ImVec2(tx + 3.0f, ty + 3.0f),
                          ImVec2(tx + tooltip_w + 3.0f, ty + tooltip_h + 3.0f),
                          sh_col,
                          rnd + 2.0f);

        // Night-theme accent glow
        if (colors.glow_intensity > 0.01f)
        {
            ImU32 glow_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(series_color.r,
                                                      series_color.g,
                                                      series_color.b,
                                                      0.10f * opacity_ * colors.glow_intensity));
            fg->AddRect(ImVec2(tx - 1.0f, ty - 1.0f),
                        ImVec2(tx + tooltip_w + 1.0f, ty + tooltip_h + 1.0f),
                        glow_col,
                        rnd + 2.0f,
                        0,
                        2.0f);
        }

        // Background
        ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_bg.r,
                                                             colors.tooltip_bg.g,
                                                             colors.tooltip_bg.b,
                                                             colors.tooltip_bg.a * opacity_));
        fg->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tooltip_w, ty + tooltip_h), bg_col, rnd);

        // Border
        ImU32 border_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.tooltip_border.r,
                                                  colors.tooltip_border.g,
                                                  colors.tooltip_border.b,
                                                  colors.tooltip_border.a * opacity_));
        fg->AddRect(ImVec2(tx, ty),
                    ImVec2(tx + tooltip_w, ty + tooltip_h),
                    border_col,
                    rnd,
                    0,
                    0.75f);

        float ox = tx + padding;
        float oy = ty + padding;

        // ── Row 1: circle swatch + series name ──
        float cy_name = oy + (row_h - swatch_r * 2.0f) * 0.5f;
        fg->AddCircleFilled(ImVec2(ox + swatch_r, cy_name + swatch_r),
                            swatch_r,
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(series_color.r, series_color.g, series_color.b, opacity_)));
        fg->AddCircle(ImVec2(ox + swatch_r, cy_name + swatch_r),
                      swatch_r,
                      ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.18f * opacity_)));

        fg->AddText(body_font,
                    font_sz,
                    ImVec2(ox + swatch_r * 2.0f + 6.0f, oy + (row_h - font_sz) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                          colors.text_primary.g,
                                                          colors.text_primary.b,
                                                          colors.text_primary.a * opacity_)),
                    series_name);

        // ── Divider ──
        {
            float div_y   = oy + row_h + divider_h * 0.5f - 0.5f;
            ImU32 div_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_subtle.r,
                                                                  colors.border_subtle.g,
                                                                  colors.border_subtle.b,
                                                                  0.35f * opacity_));
            fg->AddLine(ImVec2(ox, div_y), ImVec2(ox + content_w, div_y), div_col, 0.75f);
        }

        // ── Data rows: label (dim) + value (bright) ──
        auto draw_data_row = [&](int row_idx, const char* label, const char* value)
        {
            float ry = oy + row_h + divider_h + row_h * static_cast<float>(row_idx);
            fg->AddText(body_font,
                        font_sz,
                        ImVec2(ox, ry + (row_h - font_sz) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_secondary.r,
                                                              colors.text_secondary.g,
                                                              colors.text_secondary.b,
                                                              0.70f * opacity_)),
                        label);
            fg->AddText(body_font,
                        font_sz,
                        ImVec2(ox + lbl_w + col_gap, ry + (row_h - font_sz) * 0.5f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                              colors.text_primary.g,
                                                              colors.text_primary.b,
                                                              colors.text_primary.a * opacity_)),
                        value);
        };

        draw_data_row(0, lbl_x, x_buf);
        draw_data_row(1, lbl_y, y_buf);
        if (show_dydx)
            draw_data_row(2, lbl_dydx, dydx_line + 6);
    }

    // Draw triangular arrow pointer toward data point
    if (nearest.found && nearest.distance_px <= snap_radius_px_)
    {
        ImDrawList*     fg         = dl ? dl : ImGui::GetForegroundDrawList();
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
        ImDrawList* fg        = dl ? dl : ImGui::GetForegroundDrawList();
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

#ifdef SPECTRA_USE_IMGUI

    #include "crosshair.hpp"

    #include <algorithm>
    #include <cfloat>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>

    #include "crosshair_mapping.hpp"
    #include "overlay_text_fit.hpp"
    #include "readout_format.hpp"
    #include "ui/input/input.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

// Fitted crosshair readout: the text to draw and its measured size.
struct CrosshairReadoutLabel
{
    std::string text;
    ImVec2      size;
};

// Measure a readout at full width (no measuring cap) and, if it is wider than
// max_w, ellipsize it (UTF-8 safe) so its box stays inside the viewport.
static CrosshairReadoutLabel fit_label(ImFont*            font,
                                       float              font_size,
                                       const std::string& text,
                                       float              max_w)
{
    const ui::overlay_fit::MeasureFn measure = [&](std::string_view s)
    { return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x; };
    CrosshairReadoutLabel out;
    out.text = ui::overlay_fit::ellipsize(text, std::max(0.0f, max_w), measure);
    out.size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, out.text.c_str());
    if (out.size.y <= 0.0f)
        out.size.y = font_size;
    return out;
}

// Helper: draw a dashed line on an ImDrawList
static void draw_dashed_line(ImDrawList* dl,
                             ImVec2      p0,
                             ImVec2      p1,
                             ImU32       color,
                             float       dash,
                             float       gap,
                             float       thickness)
{
    float dx     = p1.x - p0.x;
    float dy     = p1.y - p0.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    float nx      = dx / length;
    float ny      = dy / length;
    float drawn   = 0.0f;
    bool  drawing = true;

    while (drawn < length)
    {
        float seg = drawing ? dash : gap;
        float end = std::min(drawn + seg, length);
        if (drawing)
        {
            dl->AddLine(ImVec2(p0.x + nx * drawn, p0.y + ny * drawn),
                        ImVec2(p0.x + nx * end, p0.y + ny * end),
                        color,
                        thickness);
        }
        drawn   = end;
        drawing = !drawing;
    }
}

void Crosshair::draw(const CursorReadout& cursor,
                     const Rect&          viewport,
                     float                xlim_min,
                     float                xlim_max,
                     float                ylim_min,
                     float                ylim_max,
                     ImDrawList*          dl,
                     const Axes*          axes)
{
    // Animate opacity
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt     = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

    const auto& colors     = theme_mgr_->colors();
    ImU32       line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                             colors.crosshair.g,
                                                             colors.crosshair.b,
                                                             colors.crosshair.a * opacity_));

    auto sx = static_cast<float>(cursor.screen_x);
    auto sy = static_cast<float>(cursor.screen_y);

    // Clamp to viewport
    float vx0 = viewport.x;
    float vy0 = viewport.y;
    float vx1 = viewport.x + viewport.w;
    float vy1 = viewport.y + viewport.h;

    if (sx < vx0 || sx > vx1 || sy < vy0 || sy > vy1)
        return;

    ImDrawList* fg = dl ? dl : ImGui::GetForegroundDrawList();

    if (colors.glow_intensity > 0.01f)
    {
        ImU32 glow_color =
            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent_glow.r,
                                                  colors.accent_glow.g,
                                                  colors.accent_glow.b,
                                                  colors.accent_glow.a * opacity_ * 0.26f));
        fg->AddLine(ImVec2(sx, vy0), ImVec2(sx, vy1), glow_color, 4.0f);
        fg->AddLine(ImVec2(vx0, sy), ImVec2(vx1, sy), glow_color, 4.0f);
    }

    // Vertical line (full height of viewport)
    draw_dashed_line(fg,
                     ImVec2(sx, vy0),
                     ImVec2(sx, vy1),
                     line_color,
                     dash_length_,
                     gap_length_,
                     1.0f);

    // Horizontal line (full width of viewport)
    draw_dashed_line(fg,
                     ImVec2(vx0, sy),
                     ImVec2(vx1, sy),
                     line_color,
                     dash_length_,
                     gap_length_,
                     1.0f);

    {
        constexpr float DOT_RADIUS = 4.0f;
        ImU32           dot_fill   = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, opacity_));
        fg->AddCircleFilled(ImVec2(sx, sy), DOT_RADIUS, dot_fill, 12);
        ImU32 dot_stroke = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_canvas.r, colors.bg_canvas.g, colors.bg_canvas.b, opacity_));
        fg->AddCircle(ImVec2(sx, sy), DOT_RADIUS, dot_stroke, 12, 1.0f);
        fg->AddCircle(
            ImVec2(sx, sy),
            DOT_RADIUS + 3.0f,
            ImGui::ColorConvertFloat4ToU32(
                ImVec4(colors.accent.r, colors.accent.g, colors.accent.b, opacity_ * 0.28f)),
            18,
            1.0f);

        if (colors.glow_intensity > 0.01f)
        {
            constexpr float GLOW_RADIUS = 10.0f;
            ImU32           glow_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent_glow.r,
                                                      colors.accent_glow.g,
                                                      colors.accent_glow.b,
                                                      colors.accent_glow.a * opacity_ * 0.56f));
            fg->AddCircleFilled(ImVec2(sx, sy), GLOW_RADIUS, glow_col, 16);
        }
    }

    // Axis-intersection labels
    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.84f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

    // Compact labels: just the digits one pixel resolves, never wider than the
    // label box allows (epoch-sized values switch to scientific notation).
    const double x_res =
        axes ? crosshair_mapping::pixel_resolution_x(*axes, cursor.screen_x)
             : std::abs(static_cast<double>(xlim_max) - xlim_min) / std::max(1.0f, viewport.w);
    const double y_res =
        axes ? crosshair_mapping::pixel_resolution_y(*axes, cursor.screen_y)
             : std::abs(static_cast<double>(ylim_max) - ylim_min) / std::max(1.0f, viewport.h);
    const std::string x_label = ui::format_compact_value(cursor.data_x, x_res);
    const std::string y_label = ui::format_compact_value(cursor.data_y, y_res);

    ImFont*         font      = ImGui::GetFont();
    constexpr float label_pad = 3.0f;

    // X label at bottom of viewport
    {
        const CrosshairReadoutLabel fl =
            fit_label(font, font->LegacySize * 0.85f, x_label, (vx1 - vx0) - label_pad * 2.0f);
        const ImVec2 sz = fl.size;
        // Keep the whole label box inside the viewport horizontally.
        float lx = ui::overlay_fit::clamp_span(sx - sz.x * 0.5f - label_pad,
                                               sz.x + label_pad * 2.0f,
                                               vx0,
                                               vx1)
                   + label_pad;
        float ly = vy1 + 2.0f;

        fg->AddRectFilled(ImVec2(lx - label_pad, ly),
                          ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
                          label_bg,
                          3.0f);
        fg->AddText(font,
                    font->LegacySize * 0.85f,
                    ImVec2(lx, ly + label_pad),
                    label_text,
                    fl.text.c_str());
    }

    // Y label inside left edge of viewport
    {
        const float                 lx = vx0 + 4.0f;
        const CrosshairReadoutLabel fl =
            fit_label(font, font->LegacySize * 0.85f, y_label, vx1 - lx - label_pad);
        const ImVec2 sz = fl.size;
        // Keep the whole label box inside the viewport vertically.
        float ly = ui::overlay_fit::clamp_span(sy - sz.y * 0.5f - label_pad,
                                               sz.y + label_pad * 2.0f,
                                               vy0,
                                               vy1)
                   + label_pad;

        fg->AddRectFilled(ImVec2(lx - label_pad, ly - label_pad),
                          ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad),
                          label_bg,
                          3.0f);
        fg->AddText(font, font->LegacySize * 0.85f, ImVec2(lx, ly), label_text, fl.text.c_str());
    }
}

void Crosshair::draw_all_axes(const CursorReadout& cursor,
                              Figure&              figure,
                              AxisLinkManager* /*link_mgr*/,
                              ImDrawList* dl)
{
    // Animate opacity (shared across all axes)
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt     = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

    const auto& colors     = theme_mgr_->colors();
    ImU32       line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                             colors.crosshair.g,
                                                             colors.crosshair.b,
                                                             colors.crosshair.a * opacity_));

    auto cx = static_cast<float>(cursor.screen_x);
    auto cy = static_cast<float>(cursor.screen_y);

    ImDrawList* fg = dl ? dl : ImGui::GetForegroundDrawList();

    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.9f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

    ImFont*         font      = ImGui::GetFont();
    constexpr float label_pad = 3.0f;

    // Find which axes the cursor is over
    Axes* hovered_axes = nullptr;
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (cx >= vp.x && cx <= vp.x + vp.w && cy >= vp.y && cy <= vp.y + vp.h)
        {
            hovered_axes = axes_ptr.get();
            break;
        }
    }

    if (!hovered_axes)
        return;

    // Unproject the cursor on the hovered axes through its scale transform
    // (log axes are linear in log(v)); double keeps epoch-scale precision.
    const double data_x = crosshair_mapping::screen_to_data_x(*hovered_axes, cx);
    const double data_y = crosshair_mapping::screen_to_data_y(*hovered_axes, cy);

    // Draw on ALL axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();

        float vx0 = vp.x;
        float vy0 = vp.y;
        float vx1 = vp.x + vp.w;
        float vy1 = vp.y + vp.h;

        // Vertical line at the same data-X on every axes
        auto sx = static_cast<float>(crosshair_mapping::data_to_screen_x(*axes_ptr, data_x));

        if (sx >= vx0 && sx <= vx1)
        {
            draw_dashed_line(fg,
                             ImVec2(sx, vy0),
                             ImVec2(sx, vy1),
                             line_color,
                             dash_length_,
                             gap_length_,
                             1.0f);

            // X label at bottom
            const std::string x_label =
                ui::format_compact_value(data_x,
                                         crosshair_mapping::pixel_resolution_x(*axes_ptr, sx));
            const CrosshairReadoutLabel fl =
                fit_label(font, font->LegacySize * 0.85f, x_label, (vx1 - vx0) - label_pad * 2.0f);
            const ImVec2 sz = fl.size;
            float        lx = ui::overlay_fit::clamp_span(sx - sz.x * 0.5f - label_pad,
                                                   sz.x + label_pad * 2.0f,
                                                   vx0,
                                                   vx1)
                       + label_pad;
            float ly = vy1 + 2.0f;

            fg->AddRectFilled(ImVec2(lx - label_pad, ly),
                              ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
                              label_bg,
                              3.0f);
            fg->AddText(font,
                        font->LegacySize * 0.85f,
                        ImVec2(lx, ly + label_pad),
                        label_text,
                        fl.text.c_str());
        }

        // Horizontal line on the hovered axes (at cursor Y)
        if (axes_ptr.get() == hovered_axes)
        {
            if (cy >= vy0 && cy <= vy1)
            {
                draw_dashed_line(fg,
                                 ImVec2(vx0, cy),
                                 ImVec2(vx1, cy),
                                 line_color,
                                 dash_length_,
                                 gap_length_,
                                 1.0f);

                // Y label inside left edge of viewport
                const std::string y_label =
                    ui::format_compact_value(data_y,
                                             crosshair_mapping::pixel_resolution_y(*axes_ptr, cy));
                const float                 lx2 = vx0 + 4.0f;
                const CrosshairReadoutLabel fl =
                    fit_label(font, font->LegacySize * 0.85f, y_label, vx1 - lx2 - label_pad);
                const ImVec2 sz  = fl.size;
                float        ly2 = ui::overlay_fit::clamp_span(cy - sz.y * 0.5f - label_pad,
                                                        sz.y + label_pad * 2.0f,
                                                        vy0,
                                                        vy1)
                            + label_pad;

                fg->AddRectFilled(ImVec2(lx2 - label_pad, ly2 - label_pad),
                                  ImVec2(lx2 + sz.x + label_pad, ly2 + sz.y + label_pad),
                                  label_bg,
                                  3.0f);
                fg->AddText(font,
                            font->LegacySize * 0.85f,
                            ImVec2(lx2, ly2),
                            label_text,
                            fl.text.c_str());
            }
        }
        // Horizontal line on non-hovered axes at the same data-Y
        else
        {
            // Map the hovered axes' data-Y into this subplot's screen coordinates
            auto sy = static_cast<float>(crosshair_mapping::data_to_screen_y(*axes_ptr, data_y));

            if (sy >= vy0 && sy <= vy1)
            {
                // Dimmer line for non-hovered axes
                ImU32 dim_color =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(colors.crosshair.r,
                                                          colors.crosshair.g,
                                                          colors.crosshair.b,
                                                          colors.crosshair.a * opacity_ * 0.6f));

                draw_dashed_line(fg,
                                 ImVec2(vx0, sy),
                                 ImVec2(vx1, sy),
                                 dim_color,
                                 dash_length_,
                                 gap_length_,
                                 1.0f);

                // Y label showing the same data-Y value
                const std::string y_label =
                    ui::format_compact_value(data_y,
                                             crosshair_mapping::pixel_resolution_y(*axes_ptr, sy));
                const float                 lx2 = vx0 + 4.0f;
                const CrosshairReadoutLabel fl =
                    fit_label(font, font->LegacySize * 0.85f, y_label, vx1 - lx2 - label_pad);
                const ImVec2 sz  = fl.size;
                float        ly2 = ui::overlay_fit::clamp_span(sy - sz.y * 0.5f - label_pad,
                                                        sz.y + label_pad * 2.0f,
                                                        vy0,
                                                        vy1)
                            + label_pad;

                ImU32 dim_label_text = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                                             colors.text_primary.g,
                                                                             colors.text_primary.b,
                                                                             opacity_ * 0.6f));

                fg->AddRectFilled(ImVec2(lx2 - label_pad, ly2 - label_pad),
                                  ImVec2(lx2 + sz.x + label_pad, ly2 + sz.y + label_pad),
                                  label_bg,
                                  3.0f);
                fg->AddText(font,
                            font->LegacySize * 0.85f,
                            ImVec2(lx2, ly2),
                            dim_label_text,
                            fl.text.c_str());
            }
        }
    }
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

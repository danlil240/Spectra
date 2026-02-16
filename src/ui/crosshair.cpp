#ifdef PLOTIX_USE_IMGUI

    #include "crosshair.hpp"

    #include <cmath>
    #include <cstdio>
    #include <imgui.h>
    #include <plotix/axes.hpp>
    #include <plotix/figure.hpp>
    #include <plotix/series.hpp>

    #include "axis_link.hpp"
    #include "input.hpp"
    #include "theme.hpp"

namespace plotix
{

// Helper: draw a dashed line on an ImDrawList
static void draw_dashed_line(
    ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 color, float dash, float gap, float thickness)
{
    float dx = p1.x - p0.x;
    float dy = p1.y - p0.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    float nx = dx / length;
    float ny = dy / length;
    float drawn = 0.0f;
    bool drawing = true;

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
        drawn = end;
        drawing = !drawing;
    }
}

void Crosshair::draw(const CursorReadout& cursor,
                     const Rect& viewport,
                     float /*xlim_min*/,
                     float /*xlim_max*/,
                     float /*ylim_min*/,
                     float /*ylim_max*/)
{
    // Animate opacity
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

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

    if (sx < vx0 || sx > vx1 || sy < vy0 || sy > vy1)
        return;

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // Vertical line (full height of viewport)
    draw_dashed_line(
        fg, ImVec2(sx, vy0), ImVec2(sx, vy1), line_color, dash_length_, gap_length_, 1.0f);

    // Horizontal line (full width of viewport)
    draw_dashed_line(
        fg, ImVec2(vx0, sy), ImVec2(vx1, sy), line_color, dash_length_, gap_length_, 1.0f);

    // Axis-intersection labels
    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.9f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

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
        if (lx < vx0)
            lx = vx0;
        if (lx + sz.x + label_pad * 2.0f > vx1)
            lx = vx1 - sz.x - label_pad * 2.0f;

        fg->AddRectFilled(ImVec2(lx - label_pad, ly),
                          ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
                          label_bg,
                          3.0f);
        fg->AddText(font, font->FontSize * 0.85f, ImVec2(lx, ly + label_pad), label_text, x_label);
    }

    // Y label at left of viewport
    {
        ImVec2 sz = font->CalcTextSizeA(font->FontSize * 0.85f, 200.0f, 0.0f, y_label);
        float lx = vx0 - sz.x - label_pad * 2.0f - 2.0f;
        float ly = sy - sz.y * 0.5f;
        // Clamp vertically
        if (ly < vy0)
            ly = vy0;
        if (ly + sz.y + label_pad * 2.0f > vy1)
            ly = vy1 - sz.y - label_pad * 2.0f;

        fg->AddRectFilled(ImVec2(lx, ly - label_pad),
                          ImVec2(lx + sz.x + label_pad * 2.0f, ly + sz.y + label_pad),
                          label_bg,
                          3.0f);
        fg->AddText(font, font->FontSize * 0.85f, ImVec2(lx + label_pad, ly), label_text, y_label);
    }
}

void Crosshair::draw_all_axes(const CursorReadout& cursor,
                              Figure& figure,
                              AxisLinkManager* link_mgr)
{
    // Animate opacity (shared across all axes)
    float target = (enabled_ && cursor.valid) ? 1.0f : 0.0f;
    float dt = ImGui::GetIO().DeltaTime;
    opacity_ += (target - opacity_) * std::min(1.0f, 14.0f * dt);
    if (std::abs(opacity_ - target) < 0.01f)
        opacity_ = target;
    if (opacity_ < 0.01f)
        return;

    const auto& colors = ui::ThemeManager::instance().colors();
    ImU32 line_color = ImGui::ColorConvertFloat4ToU32(ImVec4(
        colors.crosshair.r, colors.crosshair.g, colors.crosshair.b, colors.crosshair.a * opacity_));

    float cx = static_cast<float>(cursor.screen_x);
    float cy = static_cast<float>(cursor.screen_y);

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    ImU32 label_bg = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.9f * opacity_));
    ImU32 label_text = ImGui::ColorConvertFloat4ToU32(
        ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity_));

    ImFont* font = ImGui::GetFont();
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

    // Get the data-X coordinate from the hovered axes
    auto xlim_h = hovered_axes->x_limits();
    const auto& vp_h = hovered_axes->viewport();
    float norm_x_h = (cx - vp_h.x) / vp_h.w;
    float data_x = xlim_h.min + norm_x_h * (xlim_h.max - xlim_h.min);

    // Draw on ALL axes
    for (auto& axes_ptr : figure.axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        auto xlim = axes_ptr->x_limits();
        auto ylim = axes_ptr->y_limits();
        float x_range = xlim.max - xlim.min;
        float y_range = ylim.max - ylim.min;
        if (x_range == 0.0f)
            x_range = 1.0f;
        if (y_range == 0.0f)
            y_range = 1.0f;

        float vx0 = vp.x;
        float vy0 = vp.y;
        float vx1 = vp.x + vp.w;
        float vy1 = vp.y + vp.h;

        // Vertical line at the same data-X on every axes
        float norm_x = (data_x - xlim.min) / x_range;
        float sx = vp.x + norm_x * vp.w;

        if (sx >= vx0 && sx <= vx1)
        {
            draw_dashed_line(
                fg, ImVec2(sx, vy0), ImVec2(sx, vy1), line_color, dash_length_, gap_length_, 1.0f);

            // X label at bottom
            char x_label[32];
            std::snprintf(x_label, sizeof(x_label), "%.4g", data_x);
            ImVec2 sz = font->CalcTextSizeA(font->FontSize * 0.85f, 200.0f, 0.0f, x_label);
            float lx = sx - sz.x * 0.5f;
            float ly = vy1 + 2.0f;
            if (lx < vx0)
                lx = vx0;
            if (lx + sz.x + label_pad * 2.0f > vx1)
                lx = vx1 - sz.x - label_pad * 2.0f;

            fg->AddRectFilled(ImVec2(lx - label_pad, ly),
                              ImVec2(lx + sz.x + label_pad, ly + sz.y + label_pad * 2.0f),
                              label_bg,
                              3.0f);
            fg->AddText(
                font, font->FontSize * 0.85f, ImVec2(lx, ly + label_pad), label_text, x_label);
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

                // Y label at left
                float norm_y = 1.0f - (cy - vy0) / vp.h;
                float data_y = ylim.min + norm_y * y_range;
                char y_label[32];
                std::snprintf(y_label, sizeof(y_label), "%.4g", data_y);
                ImVec2 sz = font->CalcTextSizeA(font->FontSize * 0.85f, 200.0f, 0.0f, y_label);
                float lx2 = vx0 - sz.x - label_pad * 2.0f - 2.0f;
                float ly2 = cy - sz.y * 0.5f;
                if (ly2 < vy0)
                    ly2 = vy0;
                if (ly2 + sz.y + label_pad * 2.0f > vy1)
                    ly2 = vy1 - sz.y - label_pad * 2.0f;

                fg->AddRectFilled(ImVec2(lx2, ly2 - label_pad),
                                  ImVec2(lx2 + sz.x + label_pad * 2.0f, ly2 + sz.y + label_pad),
                                  label_bg,
                                  3.0f);
                fg->AddText(font,
                            font->FontSize * 0.85f,
                            ImVec2(lx2 + label_pad, ly2),
                            label_text,
                            y_label);
            }
        }
        // Shared cursor: draw horizontal line on non-hovered linked axes
        // by interpolating Y from the nearest series at data_x.
        else if (link_mgr && link_mgr->is_linked(axes_ptr.get()))
        {
            auto sc = link_mgr->shared_cursor_for(axes_ptr.get());
            if (sc.valid)
            {
                // Find the Y value at data_x by interpolating the first visible series
                float interp_y = 0.0f;
                bool found_y = false;
                for (const auto& series_ptr : axes_ptr->series())
                {
                    if (!series_ptr || !series_ptr->visible())
                        continue;

                    const float* x_data = nullptr;
                    const float* y_data = nullptr;
                    size_t count = 0;

                    if (auto* ls = dynamic_cast<LineSeries*>(series_ptr.get()))
                    {
                        x_data = ls->x_data().data();
                        y_data = ls->y_data().data();
                        count = ls->point_count();
                    }
                    else if (auto* sc_s = dynamic_cast<ScatterSeries*>(series_ptr.get()))
                    {
                        x_data = sc_s->x_data().data();
                        y_data = sc_s->y_data().data();
                        count = sc_s->point_count();
                    }

                    if (!x_data || !y_data || count == 0)
                        continue;

                    // Binary search for the interval containing data_x
                    // (assumes x_data is sorted)
                    if (data_x < x_data[0] || data_x > x_data[count - 1])
                        continue;

                    size_t lo = 0, hi = count - 1;
                    while (lo + 1 < hi)
                    {
                        size_t mid = (lo + hi) / 2;
                        if (x_data[mid] <= data_x)
                            lo = mid;
                        else
                            hi = mid;
                    }

                    // Linear interpolation between lo and hi
                    float dx = x_data[hi] - x_data[lo];
                    if (dx > 0.0f)
                    {
                        float t = (data_x - x_data[lo]) / dx;
                        interp_y = y_data[lo] + t * (y_data[hi] - y_data[lo]);
                    }
                    else
                    {
                        interp_y = y_data[lo];
                    }
                    found_y = true;
                    break;  // Use first visible series
                }

                if (found_y)
                {
                    // Convert interp_y to screen coordinates
                    float norm_iy = (interp_y - ylim.min) / y_range;
                    float sy = vy0 + (1.0f - norm_iy) * vp.h;

                    if (sy >= vy0 && sy <= vy1)
                    {
                        // Dimmer line for non-hovered axes
                        ImU32 dim_color = ImGui::ColorConvertFloat4ToU32(
                            ImVec4(colors.crosshair.r,
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

                        // Y label at left
                        char y_label[32];
                        std::snprintf(y_label, sizeof(y_label), "%.4g", interp_y);
                        ImVec2 sz =
                            font->CalcTextSizeA(font->FontSize * 0.85f, 200.0f, 0.0f, y_label);
                        float lx2 = vx0 - sz.x - label_pad * 2.0f - 2.0f;
                        float ly2 = sy - sz.y * 0.5f;
                        if (ly2 < vy0)
                            ly2 = vy0;
                        if (ly2 + sz.y + label_pad * 2.0f > vy1)
                            ly2 = vy1 - sz.y - label_pad * 2.0f;

                        ImU32 dim_label_text =
                            ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_primary.r,
                                                                  colors.text_primary.g,
                                                                  colors.text_primary.b,
                                                                  opacity_ * 0.6f));

                        fg->AddRectFilled(
                            ImVec2(lx2, ly2 - label_pad),
                            ImVec2(lx2 + sz.x + label_pad * 2.0f, ly2 + sz.y + label_pad),
                            label_bg,
                            3.0f);
                        fg->AddText(font,
                                    font->FontSize * 0.85f,
                                    ImVec2(lx2 + label_pad, ly2),
                                    dim_label_text,
                                    y_label);
                    }
                }
            }
        }
    }
}

}  // namespace plotix

#endif  // PLOTIX_USE_IMGUI

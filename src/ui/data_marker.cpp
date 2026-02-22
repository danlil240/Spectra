#ifdef SPECTRA_USE_IMGUI

    #include "data_marker.hpp"

    #include <cmath>
    #include <cstdio>
    #include <imgui.h>

    #include "theme.hpp"

namespace spectra
{

void DataMarkerManager::add(float data_x, float data_y, const Series* series, size_t index)
{
    DataMarker m;
    m.data_x      = data_x;
    m.data_y      = data_y;
    m.series      = series;
    m.point_index = index;
    m.color       = series ? series->color() : colors::white;
    markers_.push_back(m);
}

void DataMarkerManager::remove(size_t marker_index)
{
    if (marker_index < markers_.size())
    {
        markers_.erase(markers_.begin() + static_cast<std::ptrdiff_t>(marker_index));
    }
}

void DataMarkerManager::clear()
{
    markers_.clear();
}

void DataMarkerManager::data_to_screen(float       data_x,
                                       float       data_y,
                                       const Rect& viewport,
                                       float       xlim_min,
                                       float       xlim_max,
                                       float       ylim_min,
                                       float       ylim_max,
                                       float&      screen_x,
                                       float&      screen_y)
{
    float x_range = xlim_max - xlim_min;
    float y_range = ylim_max - ylim_min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    float norm_x = (data_x - xlim_min) / x_range;
    float norm_y = (data_y - ylim_min) / y_range;

    screen_x = viewport.x + norm_x * viewport.w;
    // Y is inverted (screen Y goes down, data Y goes up)
    screen_y = viewport.y + (1.0f - norm_y) * viewport.h;
}

void DataMarkerManager::draw(const Rect& viewport,
                             float       xlim_min,
                             float       xlim_max,
                             float       ylim_min,
                             float       ylim_max,
                             float       opacity)
{
    if (markers_.empty())
        return;

    const auto& colors = ui::ThemeManager::instance().colors();
    ImDrawList* fg     = ImGui::GetForegroundDrawList();

    for (size_t i = 0; i < markers_.size(); ++i)
    {
        const auto& m = markers_[i];
        float       sx, sy;
        data_to_screen(m.data_x,
                       m.data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);

        // Skip if outside viewport
        if (sx < viewport.x || sx > viewport.x + viewport.w || sy < viewport.y
            || sy > viewport.y + viewport.h)
            continue;

        // Outer ring (white/bg)
        ImU32 ring_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.bg_primary.r, colors.bg_primary.g, colors.bg_primary.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), 7.0f, ring_col);

        // Inner filled circle (series color)
        ImU32 fill_col =
            ImGui::ColorConvertFloat4ToU32(ImVec4(m.color.r, m.color.g, m.color.b, opacity));
        fg->AddCircleFilled(ImVec2(sx, sy), 5.0f, fill_col);

        // Border
        ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.border_default.r,
                                                                 colors.border_default.g,
                                                                 colors.border_default.b,
                                                                 opacity * 0.5f));
        fg->AddCircle(ImVec2(sx, sy), 7.0f, border_col, 0, 1.0f);

        // Small label showing coordinates
        char label[48];
        std::snprintf(label, sizeof(label), "(%.3g, %.3g)", m.data_x, m.data_y);
        ImFont* font = ImGui::GetFont();
        ImVec2  sz   = font->CalcTextSizeA(font->FontSize * 0.8f, 200.0f, 0.0f, label);

        float lx = sx + 10.0f;
        float ly = sy - sz.y * 0.5f;

        // Background pill
        ImU32 bg_col   = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.bg_elevated.r,
                                                             colors.bg_elevated.g,
                                                             colors.bg_elevated.b,
                                                             0.92f * opacity));
        ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, opacity));

        fg->AddRectFilled(ImVec2(lx - 4.0f, ly - 2.0f),
                          ImVec2(lx + sz.x + 4.0f, ly + sz.y + 2.0f),
                          bg_col,
                          4.0f);
        fg->AddText(font, font->FontSize * 0.8f, ImVec2(lx, ly), text_col, label);
    }
}

int DataMarkerManager::hit_test(float       screen_x,
                                float       screen_y,
                                const Rect& viewport,
                                float       xlim_min,
                                float       xlim_max,
                                float       ylim_min,
                                float       ylim_max,
                                float       radius_px) const
{
    for (size_t i = 0; i < markers_.size(); ++i)
    {
        float sx, sy;
        data_to_screen(markers_[i].data_x,
                       markers_[i].data_y,
                       viewport,
                       xlim_min,
                       xlim_max,
                       ylim_min,
                       ylim_max,
                       sx,
                       sy);
        float dx = screen_x - sx;
        float dy = screen_y - sy;
        if (dx * dx + dy * dy <= radius_px * radius_px)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

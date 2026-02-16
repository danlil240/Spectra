#ifdef SPECTRA_USE_IMGUI

    #include "legend_interaction.hpp"

    #include <algorithm>
    #include <cmath>
    #include <cstdio>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>

    #include "design_tokens.hpp"
    #include "theme.hpp"

namespace spectra
{

void LegendInteraction::set_fonts(ImFont* body, ImFont* icon)
{
    font_body_ = body;
    font_icon_ = icon;
}

// ─── State management ───────────────────────────────────────────────────────

LegendSeriesState& LegendInteraction::get_state(const Series* s)
{
    auto it = series_states_.find(s);
    if (it != series_states_.end())
        return it->second;
    LegendSeriesState state;
    state.user_visible = s ? s->visible() : true;
    state.opacity = state.user_visible ? 1.0f : 0.0f;
    state.target_opacity = state.opacity;
    return series_states_.emplace(s, state).first->second;
}

LegendInteraction::LegendOffset& LegendInteraction::get_offset(size_t axes_index)
{
    return legend_offsets_[axes_index];
}

// ─── Update ─────────────────────────────────────────────────────────────────

void LegendInteraction::update(float dt, Figure& figure)
{
    // Animate opacity for all tracked series
    for (auto& [series_ptr, state] : series_states_)
    {
        float speed = (toggle_duration_ > 0.0f) ? (1.0f / toggle_duration_) : 100.0f;
        float diff = state.target_opacity - state.opacity;
        if (std::abs(diff) > 0.001f)
        {
            state.opacity += diff * std::min(1.0f, speed * dt);
            if (std::abs(state.opacity - state.target_opacity) < 0.005f)
            {
                state.opacity = state.target_opacity;
            }
        }
    }

    // Clean up stale entries (series that no longer exist)
    // This is a lightweight GC — only runs when map is large
    if (series_states_.size() > 100)
    {
        for (auto it = series_states_.begin(); it != series_states_.end();)
        {
            bool found = false;
            for (auto& axes_ptr : figure.axes())
            {
                if (!axes_ptr)
                    continue;
                for (auto& s : axes_ptr->series())
                {
                    if (s.get() == it->first)
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            if (!found)
            {
                it = series_states_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

// ─── Queries ────────────────────────────────────────────────────────────────

float LegendInteraction::series_opacity(const Series* series) const
{
    auto it = series_states_.find(series);
    if (it != series_states_.end())
        return it->second.opacity;
    return series ? (series->visible() ? 1.0f : 0.0f) : 1.0f;
}

bool LegendInteraction::is_series_visible(const Series* series) const
{
    auto it = series_states_.find(series);
    if (it != series_states_.end())
        return it->second.user_visible;
    return series ? series->visible() : true;
}

// ─── Drawing ────────────────────────────────────────────────────────────────

bool LegendInteraction::draw(Axes& axes, const Rect& viewport, size_t axes_index)
{
    const auto& series_list = axes.series();
    if (series_list.empty())
        return false;

    // Count labeled series
    int labeled_count = 0;
    for (auto& s : series_list)
    {
        if (s && !s->label().empty())
            ++labeled_count;
    }
    if (labeled_count == 0)
        return false;

    const auto& colors = ui::ThemeManager::instance().colors();
    ImFont* font = font_body_ ? font_body_ : ImGui::GetFont();
    float font_size = font->FontSize;

    // Layout constants
    constexpr float pad_x = 10.0f;
    constexpr float pad_y = 8.0f;
    constexpr float swatch_size = 10.0f;
    constexpr float swatch_gap = 6.0f;
    constexpr float row_height = 20.0f;
    constexpr float eye_width = 16.0f;

    // Measure legend size
    float max_label_w = 0.0f;
    for (auto& s : series_list)
    {
        if (!s || s->label().empty())
            continue;
        ImVec2 sz = font->CalcTextSizeA(font_size, 300.0f, 0.0f, s->label().c_str());
        max_label_w = std::max(max_label_w, sz.x);
    }

    float legend_w = pad_x * 2.0f + swatch_size + swatch_gap + max_label_w;
    if (toggleable_)
        legend_w += eye_width + 4.0f;
    float legend_h = pad_y * 2.0f + static_cast<float>(labeled_count) * row_height;

    // Default position: top-right of viewport
    float default_x = viewport.x + viewport.w - legend_w - 12.0f;
    float default_y = viewport.y + 12.0f;

    auto& offset = get_offset(axes_index);
    float lx = default_x + offset.dx;
    float ly = default_y + offset.dy;

    // Clamp to viewport
    lx = std::max(viewport.x + 4.0f, std::min(lx, viewport.x + viewport.w - legend_w - 4.0f));
    ly = std::max(viewport.y + 4.0f, std::min(ly, viewport.y + viewport.h - legend_h - 4.0f));

    // Draw legend window
    char win_id[32];
    std::snprintf(win_id, sizeof(win_id), "##legend_%zu", axes_index);

    ImGui::SetNextWindowPos(ImVec2(lx, ly));
    ImGui::SetNextWindowSize(ImVec2(legend_w, legend_h));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(colors.bg_elevated.r, colors.bg_elevated.g, colors.bg_elevated.b, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(colors.border_subtle.r,
                                 colors.border_subtle.g,
                                 colors.border_subtle.b,
                                 colors.border_subtle.a));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
                             | ImGuiWindowFlags_NoScrollWithMouse;

    // Allow moving if draggable
    if (!draggable_)
    {
        flags |= ImGuiWindowFlags_NoMove;
    }

    bool consumed = false;

    if (ImGui::Begin(win_id, nullptr, flags))
    {
        // Handle legend dragging
        if (draggable_)
        {
            // ImVec2 win_pos = ImGui::GetWindowPos();  // Currently unused
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGui::IsAnyItemHovered())
            {
                dragging_ = true;
                drag_axes_index_ = axes_index;
                drag_start_mx_ = ImGui::GetIO().MousePos.x;
                drag_start_my_ = ImGui::GetIO().MousePos.y;
                drag_start_ox_ = offset.dx;
                drag_start_oy_ = offset.dy;
            }

            if (dragging_ && drag_axes_index_ == axes_index)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    float dmx = ImGui::GetIO().MousePos.x - drag_start_mx_;
                    float dmy = ImGui::GetIO().MousePos.y - drag_start_my_;
                    offset.dx = drag_start_ox_ + dmx;
                    offset.dy = drag_start_oy_ + dmy;
                    consumed = true;
                }
                else
                {
                    dragging_ = false;
                }
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cursor = ImGui::GetCursorScreenPos();

        int row = 0;
        for (auto& s : series_list)
        {
            if (!s || s->label().empty())
                continue;

            auto& state = get_state(s.get());
            float row_y = cursor.y + static_cast<float>(row) * row_height;
            float row_x = cursor.x;

            // Determine visual opacity
            float vis_alpha = state.opacity;
            Color sc = s->color();

            // Color swatch
            ImU32 swatch_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(sc.r, sc.g, sc.b, sc.a * vis_alpha));
            float swatch_y = row_y + (row_height - swatch_size) * 0.5f;
            dl->AddRectFilled(ImVec2(row_x, swatch_y),
                              ImVec2(row_x + swatch_size, swatch_y + swatch_size),
                              swatch_col,
                              2.0f);

            // Series label
            ImU32 text_col = ImGui::ColorConvertFloat4ToU32(ImVec4(
                colors.text_primary.r, colors.text_primary.g, colors.text_primary.b, vis_alpha));
            float label_x = row_x + swatch_size + swatch_gap;
            float label_y = row_y + (row_height - font_size) * 0.5f;
            dl->AddText(font, font_size, ImVec2(label_x, label_y), text_col, s->label().c_str());

            // Click-to-toggle: invisible button over the row
            if (toggleable_)
            {
                ImGui::SetCursorScreenPos(ImVec2(row_x, row_y));
                char btn_id[48];
                std::snprintf(btn_id, sizeof(btn_id), "##legend_toggle_%zu_%d", axes_index, row);

                float btn_w = swatch_size + swatch_gap + max_label_w;
                if (ImGui::InvisibleButton(btn_id, ImVec2(btn_w, row_height)))
                {
                    state.user_visible = !state.user_visible;
                    state.target_opacity = state.user_visible ? 1.0f : 0.15f;

                    // Apply visibility to the actual series
                    s->visible(state.user_visible);
                    consumed = true;
                }

                // Hover highlight
                if (ImGui::IsItemHovered())
                {
                    ImU32 hover_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.accent_subtle.r,
                                                                            colors.accent_subtle.g,
                                                                            colors.accent_subtle.b,
                                                                            0.3f));
                    dl->AddRectFilled(ImVec2(row_x - 4.0f, row_y),
                                      ImVec2(row_x + btn_w + 4.0f, row_y + row_height),
                                      hover_col,
                                      3.0f);

                    // Cursor hint
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }

                // Eye icon (visibility indicator) on the right
                float eye_x = row_x + btn_w + 4.0f;
                float eye_y = row_y + (row_height - font_size * 0.7f) * 0.5f;
                const char* eye_label = state.user_visible ? "o" : "-";
                ImU32 eye_col = ImGui::ColorConvertFloat4ToU32(ImVec4(colors.text_tertiary.r,
                                                                      colors.text_tertiary.g,
                                                                      colors.text_tertiary.b,
                                                                      colors.text_tertiary.a));
                dl->AddText(font, font_size * 0.7f, ImVec2(eye_x, eye_y), eye_col, eye_label);
            }

            ++row;
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return consumed;
}

}  // namespace spectra

#endif  // SPECTRA_USE_IMGUI

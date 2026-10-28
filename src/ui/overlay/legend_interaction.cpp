#ifdef SPECTRA_USE_IMGUI

    #include "legend_interaction.hpp"

    #include <algorithm>
    #include <cfloat>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <string>

    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/glass_draw.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "legend_layout.hpp"
    #include "render/text_fit.hpp"

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
    state.user_visible   = s ? s->visible() : true;
    state.opacity        = state.user_visible ? 1.0f : 0.0f;
    state.target_opacity = state.opacity;
    return series_states_.emplace(s, state).first->second;
}

LegendInteraction::LegendOffset& LegendInteraction::get_offset(uintptr_t figure_id,
                                                               size_t    axes_index)
{
    return legend_offsets_[make_offset_key(figure_id, axes_index)];
}

// ─── Update ─────────────────────────────────────────────────────────────────

void LegendInteraction::update(float dt, Figure& figure)
{
    // Animate opacity for all tracked series
    for (auto& [series_ptr, state] : series_states_)
    {
        float speed = (toggle_duration_ > 0.0f) ? (1.0f / toggle_duration_) : 100.0f;
        float diff  = state.target_opacity - state.opacity;
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
            bool found   = false;
            auto in_axes = [&](const AxesBase* axes)
            {
                if (!axes)
                    return false;
                for (auto& s : axes->series())
                    if (s.get() == it->first)
                        return true;
                return false;
            };
            for (auto& axes_ptr : figure.axes())
                if ((found = in_axes(axes_ptr.get())))
                    break;
            if (!found)   // 3D axes have legend entries too
                for (auto& axes_ptr : figure.all_axes())
                    if ((found = in_axes(axes_ptr.get())))
                        break;
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

bool LegendInteraction::draw(AxesBase&           axes,
                             const Rect&         viewport,
                             size_t              axes_index,
                             const LegendConfig& config,
                             uintptr_t           figure_id,
                             const FigureStyle*  figure_style)
{
    const auto& series_list = axes.series();
    if (series_list.empty())
        return false;

    // Count labeled series (excluding reference lines hidden from legend)
    int labeled_count = 0;
    for (auto& s : series_list)
    {
        if (s && !s->label().empty() && s->show_in_legend())
            ++labeled_count;
    }
    if (labeled_count == 0)
        return false;

    // Drawn over the plot surface, so follow its ink (see plot_surface_colors()).
    const ui::ThemeColors theme_colors =
        figure_style ? theme_mgr_->plot_surface_colors(*figure_style) : theme_mgr_->colors();
    ImFont* font = font_body_ ? font_body_ : ImGui::GetFont();

    // Use font size from LegendConfig; fall back to font's native size
    float font_size = (config.font_size > 0.0f) ? config.font_size : font->LegacySize;

    // Use padding from LegendConfig
    float           pad         = config.padding;
    float           pad_x       = pad + 2.0f;   // slight extra horizontal padding
    float           pad_y       = pad;
    constexpr float swatch_size = 10.0f;
    constexpr float swatch_gap  = 6.0f;
    float           row_height  = font_size + 6.0f;
    constexpr float eye_width   = 16.0f;

    // Measure legend size.  Labels are measured at full width but limited
    // to max_label_budget; longer ones are ellipsized when drawn and show
    // their full text on hover (audit S04).
    constexpr float max_label_budget = 300.0f;
    auto            text_w           = [&](const std::string& t)
    { return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, t.c_str()).x; };
    float max_label_w = 0.0f;
    for (auto& s : series_list)
    {
        if (!s || s->label().empty() || !s->show_in_legend())
            continue;
        max_label_w = std::max(max_label_w, std::min(text_w(s->label()), max_label_budget));
    }

    // One column = swatch + label (+ visibility eye).  Entries flow into
    // extra columns instead of being cut off; scrolling is only a last resort
    // when even the widest grid that fits the plot can't hold them all.
    constexpr float col_gap    = legend_layout::COL_GAP;
    const float     reserved_w = swatch_size + swatch_gap + (toggleable_ ? eye_width + 4.0f : 0.0f);
    float           col_w      = reserved_w + max_label_w;
    // OutsideRight lays out in the strip the figure layout reserved right of the axes.
    const bool outside = config.position == LegendPosition::OutsideRight;
    const Rect region  = outside ? legend_layout::legend_area(axes, config.position) : viewport;
    const auto grid =
        legend_layout::compute_grid(labeled_count,
                                    col_w,
                                    row_height,
                                    pad_x,
                                    pad_y,
                                    col_gap,
                                    legend_layout::max_box_w(region, config.position),
                                    legend_layout::max_box_h(region, config.position));
    if (!legend_layout::fits(region, grid.width, grid.height, config.position))
        return false;   // not even one row fits this (tiny) axes
    // compute_grid may narrow the column to keep the legend inside the axes.
    col_w                  = legend_layout::column_width(grid, pad_x, col_gap);
    const float label_w    = std::max(0.0f, col_w - reserved_w + 0.5f);   // 0.5: float slack
    const bool  scrollable = grid.scroll;
    float       legend_w   = grid.width;
    float       legend_h   = grid.height;
    if (scrollable)
        legend_w += 8.0f;   // room for scrollbar

    // Resolve LegendPosition::Best to the corner with the least data overlap.
    if (config.position == LegendPosition::None)
        return false;   // hidden — should have been gated by the caller
    const OffsetKey      key  = make_offset_key(figure_id, axes_index);
    auto                 it   = best_corner_.find(key);
    const LegendPosition prev = (it != best_corner_.end()) ? it->second : LegendPosition::None;
    const LegendPosition position =
        legend_layout::resolve_position(axes, viewport, legend_w, legend_h, config.position, prev);
    if (config.position == LegendPosition::Best)
        best_corner_[key] = position;

    auto&      offset = get_offset(figure_id, axes_index);
    const Rect box =
        legend_layout::place(region, position, legend_w, legend_h, offset.dx, offset.dy);
    const float lx = box.x;
    const float ly = box.y;
    legend_w       = box.w;   // place() keeps the box inside the viewport
    legend_h       = box.h;

    // Draw legend window — use figure_id in the ImGui ID to prevent cross-figure collisions
    const std::string win_id =
        std::format("##legend_{:x}_{}", static_cast<unsigned long>(figure_id), axes_index);

    ImGui::SetNextWindowPos(ImVec2(lx, ly));
    ImGui::SetNextWindowSize(ImVec2(legend_w, legend_h));

    // Use LegendConfig colors; fall back to theme colors when unset (see
    // LegendConfig::uses_bg_color: black RGB without has_bg_color = unset).
    ImVec4 bg_col;
    if (!config.uses_bg_color())
        bg_col = ImVec4(theme_colors.tooltip_bg.r,
                        theme_colors.tooltip_bg.g,
                        theme_colors.tooltip_bg.b,
                        theme_colors.tooltip_bg.a);
    else
        bg_col = ImVec4(config.bg_color.r, config.bg_color.g, config.bg_color.b, config.bg_color.a);

    ImVec4 border_col;
    if (!config.uses_border_color())
        border_col = ImVec4(theme_colors.border_subtle.r,
                            theme_colors.border_subtle.g,
                            theme_colors.border_subtle.b,
                            theme_colors.border_subtle.a);
    else
        border_col = ImVec4(config.border_color.r,
                            config.border_color.g,
                            config.border_color.b,
                            config.border_color.a);

    // Translucent glass card: only honor an explicit user bg_color; otherwise
    // draw a frosted pane manually (see below) and keep the ImGui WindowBg clear.
    const bool user_bg = config.uses_bg_color();

    // Label ink: theme text_secondary, unless a custom background makes it unreadable.
    Color label_ink{theme_colors.text_secondary.r,
                    theme_colors.text_secondary.g,
                    theme_colors.text_secondary.b,
                    1.0f};
    if (user_bg)
        label_ink = legend_layout::label_ink(label_ink,
                                             config.bg_color,
                                             Color{theme_colors.bg_canvas.r,
                                                   theme_colors.bg_canvas.g,
                                                   theme_colors.bg_canvas.b,
                                                   1.0f});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_LG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x + 4.0f, pad_y + 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, user_bg ? 0.5f : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, user_bg ? bg_col : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, user_bg ? border_col : ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoBringToFrontOnFocus
                             | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!scrollable)
    {
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    }

    // Allow moving if draggable
    if (!draggable_)
    {
        flags |= ImGuiWindowFlags_NoMove;
    }

    bool consumed = false;

    if (ImGui::Begin(win_id.c_str(), nullptr, flags))
    {
        // Frosted glass card behind the legend content (skip if user set a bg).
        if (!user_bg)
        {
            ImDrawList* wdl = ImGui::GetWindowDrawList();
            ImVec2      wp0 = ImGui::GetWindowPos();
            ImVec2      wsz = ImGui::GetWindowSize();
            ImVec2      wp1 = ImVec2(wp0.x + wsz.x, wp0.y + wsz.y);
            wdl->PushClipRectFullScreen();
            ui::glass_draw::draw_glass_card(wdl,
                                            wp0,
                                            wp1,
                                            ui::tokens::RADIUS_LG,
                                            theme_colors,
                                            0.52f,
                                            theme_colors.glow_intensity);
            wdl->PopClipRect();
        }

        // Handle legend dragging
        if (draggable_)
        {
            // ImVec2 win_pos = ImGui::GetWindowPos();  // Currently unused
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !ImGui::IsAnyItemHovered())
            {
                dragging_        = true;
                drag_axes_index_ = axes_index;
                drag_start_mx_   = ImGui::GetIO().MousePos.x;
                drag_start_my_   = ImGui::GetIO().MousePos.y;
                drag_start_ox_   = offset.dx;
                drag_start_oy_   = offset.dy;
            }

            if (dragging_ && drag_axes_index_ == axes_index)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    float dmx = ImGui::GetIO().MousePos.x - drag_start_mx_;
                    float dmy = ImGui::GetIO().MousePos.y - drag_start_my_;
                    offset.dx = drag_start_ox_ + dmx;
                    offset.dy = drag_start_oy_ + dmy;
                    consumed  = true;
                }
                else
                {
                    dragging_ = false;
                }
            }
        }

        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2      cursor = ImGui::GetCursorScreenPos();

        int row = 0;
        for (auto& s : series_list)
        {
            if (!s || s->label().empty() || !s->show_in_legend())
                continue;

            auto& state = get_state(s.get());

            // Sync legend state with model if visibility was changed externally
            // (e.g. via inspector eye icon). Snap opacity immediately — no fade
            // animation for external changes, since the rendering idle gate would
            // cut the animation short before it completes (only 3 grace frames).
            bool model_vis = s->visible();
            if (state.user_visible != model_vis)
            {
                state.user_visible   = model_vis;
                state.target_opacity = model_vis ? 1.0f : 0.15f;
                state.opacity        = state.target_opacity;
            }

            // Column-major grid placement (see legend_layout::compute_grid)
            const int grid_col = grid.rows > 0 ? row / grid.rows : 0;
            const int grid_row = grid.rows > 0 ? row % grid.rows : row;
            float     row_y    = cursor.y + static_cast<float>(grid_row) * row_height;
            float     row_x    = cursor.x + static_cast<float>(grid_col) * (col_w + col_gap);

            // Determine visual opacity
            float vis_alpha = state.opacity;
            Color sc =
                ui::adapt_series_color_to_theme(legend_layout::swatch_color(*s), theme_colors);

            // Color swatch
            ImU32 swatch_col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(sc.r, sc.g, sc.b, sc.a * vis_alpha));
            float swatch_y = row_y + (row_height - swatch_size) * 0.5f;
            dl->AddRectFilled(ImVec2(row_x, swatch_y),
                              ImVec2(row_x + swatch_size, swatch_y + swatch_size),
                              swatch_col,
                              2.0f);

            // Series label — spec §4.3: use text_secondary for legend labels
            // (on a custom legend background: a contrasting ink, see label_ink).
            ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(label_ink.r, label_ink.g, label_ink.b, vis_alpha));
            float              label_x = row_x + swatch_size + swatch_gap;
            float              label_y = row_y + (row_height - font_size) * 0.5f;
            const std::string& full    = s->label();
            const FittedText   label   = fit_text_to_width(
                full,
                label_w,
                FontSize::Label,
                FontSize::Label,
                [&](const std::string& t, FontSize) { return text_w(t); },
                "...");
            dl->AddText(font, font_size, ImVec2(label_x, label_y), text_col, label.text.c_str());
            if (label.truncated
                && ImGui::IsMouseHoveringRect(ImVec2(row_x, row_y),
                                              ImVec2(row_x + col_w, row_y + row_height))
                && ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s", full.c_str());

            // Click-to-toggle: invisible button over the whole row, including
            // the visibility icon slot at its right end.
            if (toggleable_)
            {
                ImGui::SetCursorScreenPos(ImVec2(row_x, row_y));
                const std::string btn_id = std::format("##legend_toggle_{:x}_{}_{}",
                                                       static_cast<unsigned long>(figure_id),
                                                       axes_index,
                                                       row);

                const float btn_w = col_w;
                if (ImGui::InvisibleButton(btn_id.c_str(), ImVec2(btn_w, row_height)))
                {
                    state.user_visible   = !state.user_visible;
                    state.target_opacity = state.user_visible ? 1.0f : 0.15f;

                    // Apply visibility to the actual series
                    s->visible(state.user_visible);
                    consumed = true;
                }

                // Hover highlight
                const bool hovered = ImGui::IsItemHovered();
                if (hovered)
                {
                    ImU32 hover_col =
                        ImGui::ColorConvertFloat4ToU32(ImVec4(theme_colors.accent_subtle.r,
                                                              theme_colors.accent_subtle.g,
                                                              theme_colors.accent_subtle.b,
                                                              0.3f));
                    dl->AddRectFilled(ImVec2(row_x - 4.0f, row_y),
                                      ImVec2(row_x + btn_w + 4.0f, row_y + row_height),
                                      hover_col,
                                      3.0f);

                    // Cursor hint
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }

                // Visibility icon: an eye appears only while the row is
                // hovered, so idle legends stay clean.  A hidden series keeps
                // an eye-slash so it is clear why its row is dimmed.
                ImFont* icon_f = ui::icon_font(ui::tokens::ICON_XS);
                if (icon_f && (hovered || !state.user_visible))
                {
                    const char* icon_s =
                        ui::icon_str(state.user_visible ? ui::Icon::Eye : ui::Icon::EyeOff);
                    const float icon_sz = ui::tokens::ICON_XS;
                    const float eye_x   = row_x + btn_w - eye_width;
                    const float eye_y   = row_y + (row_height - icon_sz) * 0.5f;
                    const auto& ec =
                        hovered ? theme_colors.text_secondary : theme_colors.text_tertiary;
                    ImU32 eye_col = ImGui::ColorConvertFloat4ToU32(ImVec4(ec.r, ec.g, ec.b, ec.a));
                    dl->AddText(icon_f, icon_sz, ImVec2(eye_x, eye_y), eye_col, icon_s);
                }
            }

            ++row;
        }

        // Reserve the full grid as window content so a scrollable legend
        // can actually scroll to its last entries.
        ImGui::SetCursorScreenPos(cursor);
        ImGui::Dummy(ImVec2(std::max(1.0f, legend_w - 2.0f * pad_x),
                            static_cast<float>(grid.rows) * row_height));
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return consumed;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

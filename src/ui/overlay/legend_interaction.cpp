#ifdef SPECTRA_USE_IMGUI

    #include "legend_interaction.hpp"

    #include <algorithm>
    #include <cfloat>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <span>
    #include <vector>
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

namespace
{
constexpr float SWATCH_SIZE      = 10.0f;
constexpr float SWATCH_GAP       = 6.0f;
constexpr float EYE_WIDTH        = 16.0f;
constexpr float MAX_LABEL_BUDGET = 300.0f;   // legend box label width before ellipsizing
constexpr float MAX_POPUP_LABEL  = 420.0f;   // full legend popup label width
constexpr int   MAX_POPUP_ROWS   = 18;       // visible rows before the popup list scrolls
constexpr float HIDDEN_OPACITY   = 0.15f;    // dimming of a hidden series' row

// spectra::Color or ui::Color (theme tokens).
template <typename C>
ImU32 to_u32(const C& c, float alpha_scale = 1.0f)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alpha_scale));
}
}   // namespace

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

size_t LegendInteraction::window_slot(uintptr_t figure_id, size_t axes_index, bool& fresh)
{
    const OffsetKey key = make_offset_key(figure_id, axes_index);
    auto            it  = window_slots_.find(key);
    fresh               = false;
    if (it != window_slots_.end() && it->second.figure_id == figure_id)
        return it->second.slot;
    size_t slot;
    if (!free_window_slots_.empty())
    {
        slot = free_window_slots_.back();
        free_window_slots_.pop_back();
        fresh = true;   // reused: the ImGui window still holds another legend's state
    }
    else
    {
        slot = next_window_slot_++;
    }
    if (it != window_slots_.end())   // key collision with another figure: replace
        free_window_slots_.push_back(it->second.slot);
    window_slots_[key] = WindowSlot{figure_id, slot};
    return slot;
}

std::string LegendInteraction::window_name(uintptr_t figure_id, size_t axes_index) const
{
    auto it = window_slots_.find(make_offset_key(figure_id, axes_index));
    if (it == window_slots_.end() || it->second.figure_id != figure_id)
        return {};
    return std::format("##legend_{}", it->second.slot);
}

void LegendInteraction::forget_figure(uintptr_t figure_id)
{
    for (auto it = window_slots_.begin(); it != window_slots_.end();)
    {
        if (it->second.figure_id == figure_id)
        {
            const OffsetKey key = it->first;
            free_window_slots_.push_back(it->second.slot);
            legend_offsets_.erase(key);
            best_corner_.erase(key);
            overflow_cells_.erase(key);
            it = window_slots_.erase(it);
        }
        else
        {
            ++it;
        }
    }
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
    // Labeled series not hidden from the legend (same list as the PNG / SVG legends).
    const std::vector<Series*> entries       = legend_layout::legend_entries(axes);
    const int                  labeled_count = static_cast<int>(entries.size());
    if (labeled_count == 0)
        return false;
    if (config.position == LegendPosition::None)
        return false;   // hidden — should have been gated by the caller
    // A Best legend the figure layout moved outside (legend_layout::auto_outside).
    const LegendPosition requested = legend_layout::effective_position(axes, config);

    // Drawn over the plot surface, so follow its ink (see plot_surface_colors()).
    const ui::ThemeColors theme_colors =
        figure_style ? theme_mgr_->plot_surface_colors(*figure_style) : theme_mgr_->colors();
    ImFont* font = font_body_ ? font_body_ : ImGui::GetFont();

    // Use font size from LegendConfig; fall back to font's native size
    float font_size = (config.font_size > 0.0f) ? config.font_size : font->LegacySize;

    // Use padding from LegendConfig
    float pad        = config.padding;
    float pad_x      = pad + 2.0f;   // slight extra horizontal padding
    float pad_y      = pad;
    float row_height = font_size + 6.0f;

    // Measure legend size.  Labels are measured at full width but limited
    // to MAX_LABEL_BUDGET; longer ones are ellipsized when drawn and show
    // their full text on hover (audit S04).
    auto text_w = [&](const std::string& t)
    { return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, t.c_str()).x; };
    float max_label_w = 0.0f;
    for (const Series* s : entries)
        max_label_w = std::max(max_label_w, std::min(text_w(s->label()), MAX_LABEL_BUDGET));

    // One column = swatch + label (+ visibility eye).  Entries flow into
    // extra columns instead of being cut off.  When even the widest grid that
    // fits the plot cannot hold them all, the last cell becomes a "+N more"
    // cell (the same legend_layout::compute_static_grid policy as the PNG and
    // SVG legends): hovering it lists the hidden entries, clicking it opens
    // the full, scrollable legend (draw_overflow_cell).
    constexpr float col_gap    = legend_layout::COL_GAP;
    const float     reserved_w = row_reserved_width();
    float           col_w      = reserved_w + max_label_w;
    // OutsideRight lays out in the strip the figure layout reserved right of the axes.
    const bool outside = requested == LegendPosition::OutsideRight;
    const Rect region  = outside ? legend_layout::legend_area(axes, requested) : viewport;
    const auto grid =
        legend_layout::compute_static_grid(labeled_count,
                                           col_w,
                                           row_height,
                                           font_size + 2.0f,
                                           text_w(legend_layout::overflow_text(labeled_count)),
                                           pad_x,
                                           pad_y,
                                           col_gap,
                                           legend_layout::max_box_w(region, requested),
                                           legend_layout::max_box_h(region, requested));
    const OffsetKey key = make_offset_key(figure_id, axes_index);
    if (grid.rows <= 0 || !legend_layout::fits(region, grid.width, grid.height, requested))
    {
        overflow_cells_.erase(key);
        return false;   // not even one row fits this (tiny) axes
    }
    // compute_grid may narrow the column (and use tighter rows) to keep the
    // legend inside the axes.
    row_height             = grid.cell_h;
    col_w                  = legend_layout::column_width(grid, pad_x, col_gap);
    const float label_w    = std::max(0.0f, col_w - reserved_w + 0.5f);   // 0.5: float slack
    const bool  scrollable = grid.scroll;
    float       legend_w   = grid.width;
    float       legend_h   = grid.height;
    if (scrollable)
        legend_w += 8.0f;   // room for scrollbar

    // Resolve LegendPosition::Best to the corner with the least data overlap.
    auto                 it   = best_corner_.find(key);
    const LegendPosition prev = (it != best_corner_.end()) ? it->second : LegendPosition::None;
    const LegendPosition position =
        legend_layout::resolve_position(axes, viewport, legend_w, legend_h, requested, prev);
    if (requested == LegendPosition::Best)
        best_corner_[key] = position;

    auto&      offset = get_offset(figure_id, axes_index);
    const Rect box =
        legend_layout::place(region, position, legend_w, legend_h, offset.dx, offset.dy);
    const float lx = box.x;
    const float ly = box.y;
    legend_w       = box.w;   // place() keeps the box inside the viewport
    legend_h       = box.h;

    // Draw legend window — one pooled window name per (figure, axes) so legends
    // of figures shown side by side never share ImGui state (scroll).
    bool              fresh_slot = false;
    const size_t      slot       = window_slot(figure_id, axes_index, fresh_slot);
    const std::string win_id     = std::format("##legend_{}", slot);

    ImGui::SetNextWindowPos(ImVec2(lx, ly));
    ImGui::SetNextWindowSize(ImVec2(legend_w, legend_h));
    if (fresh_slot)
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));

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

        ImVec2         cursor = ImGui::GetCursorScreenPos();
        const RowStyle row_style{font, font_size, row_height, label_w, label_ink, &theme_colors};

        for (int row = 0; row < grid.shown; ++row)
        {
            // Column-major grid placement (see legend_layout::compute_grid)
            const int         grid_col = grid.rows > 0 ? row / grid.rows : 0;
            const int         grid_row = grid.rows > 0 ? row % grid.rows : row;
            const float       row_y    = cursor.y + static_cast<float>(grid_row) * row_height;
            const float       row_x = cursor.x + static_cast<float>(grid_col) * (col_w + col_gap);
            const std::string id    = std::format("##legend_toggle_{:x}_{}_{}",
                                               static_cast<unsigned long>(figure_id),
                                               axes_index,
                                               row);
            consumed |= draw_entry_row(*entries[static_cast<size_t>(row)],
                                       ImVec2(row_x, row_y),
                                       col_w,
                                       row_style,
                                       id.c_str());
        }

        // "+N more": the last cell summarises the entries that did not fit.
        if (grid.overflow > 0)
        {
            const int   grid_col = grid.shown / grid.rows;
            const int   grid_row = grid.shown % grid.rows;
            const float row_y    = cursor.y + static_cast<float>(grid_row) * row_height;
            const float row_x    = cursor.x + static_cast<float>(grid_col) * (col_w + col_gap);
            consumed |= draw_overflow_cell(std::span<Series* const>(entries),
                                           grid.shown,
                                           ImVec2(row_x, row_y),
                                           col_w,
                                           row_style,
                                           box,
                                           key);
        }
        else
        {
            overflow_cells_.erase(key);
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

// ─── Rows, "+N more" cell and the full legend popup ─────────────────────────

float LegendInteraction::row_reserved_width() const
{
    return SWATCH_SIZE + SWATCH_GAP + (toggleable_ ? EYE_WIDTH + 4.0f : 0.0f);
}

void LegendInteraction::toggle_series(Series& s)
{
    auto& state          = get_state(&s);
    state.user_visible   = !state.user_visible;
    state.target_opacity = state.user_visible ? 1.0f : HIDDEN_OPACITY;
    s.visible(state.user_visible);   // apply visibility to the actual series
}

bool LegendInteraction::draw_entry_row(Series&         s,
                                       const ImVec2&   pos,
                                       float           width,
                                       const RowStyle& style,
                                       const char*     id)
{
    const ui::ThemeColors& tc      = *style.colors;
    ImDrawList*            dl      = ImGui::GetWindowDrawList();
    auto&                  state   = get_state(&s);
    const float            row_x   = pos.x;
    const float            row_y   = pos.y;
    const float            row_h   = style.row_height;
    bool                   clicked = false;

    // Sync legend state with model if visibility was changed externally
    // (e.g. via inspector eye icon). Snap opacity immediately — no fade
    // animation for external changes, since the rendering idle gate would
    // cut the animation short before it completes (only 3 grace frames).
    const bool model_vis = s.visible();
    if (state.user_visible != model_vis)
    {
        state.user_visible   = model_vis;
        state.target_opacity = model_vis ? 1.0f : HIDDEN_OPACITY;
        state.opacity        = state.target_opacity;
    }
    const float vis_alpha = state.opacity;

    // Color swatch
    const Color sc       = ui::adapt_series_color_to_theme(legend_layout::swatch_color(s), tc);
    const float swatch_y = row_y + (row_h - SWATCH_SIZE) * 0.5f;
    dl->AddRectFilled(ImVec2(row_x, swatch_y),
                      ImVec2(row_x + SWATCH_SIZE, swatch_y + SWATCH_SIZE),
                      to_u32(sc, vis_alpha),
                      2.0f);

    // Series label — spec §4.3: use text_secondary for legend labels
    // (on a custom legend background: a contrasting ink, see label_ink).
    const ImU32 text_col    = to_u32(Color{style.ink.r, style.ink.g, style.ink.b, 1.0f}, vis_alpha);
    const float label_x     = row_x + SWATCH_SIZE + SWATCH_GAP;
    const float label_y     = row_y + (row_h - style.font_size) * 0.5f;
    const std::string& full = s.label();
    auto               text_w = [&](const std::string& t, FontSize)
    { return style.font->CalcTextSizeA(style.font_size, FLT_MAX, 0.0f, t.c_str()).x; };
    const FittedText label =
        fit_text_to_width(full, style.label_w, FontSize::Label, FontSize::Label, text_w, "...");
    dl->AddText(style.font,
                style.font_size,
                ImVec2(label_x, label_y),
                text_col,
                label.text.c_str());
    if (label.truncated
        && ImGui::IsMouseHoveringRect(ImVec2(row_x, row_y), ImVec2(row_x + width, row_y + row_h))
        && ImGui::IsWindowHovered())
        ImGui::SetTooltip("%s", full.c_str());

    // Click-to-toggle: invisible button over the whole row, including the
    // visibility icon slot at its right end.
    if (toggleable_)
    {
        ImGui::SetCursorScreenPos(ImVec2(row_x, row_y));
        if (ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, width), row_h)))
        {
            toggle_series(s);
            clicked = true;
        }

        // Hover highlight
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            dl->AddRectFilled(
                ImVec2(row_x - 4.0f, row_y),
                ImVec2(row_x + width + 4.0f, row_y + row_h),
                to_u32(Color{tc.accent_subtle.r, tc.accent_subtle.g, tc.accent_subtle.b, 0.3f}),
                3.0f);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        // Visibility icon: an eye appears only while the row is hovered, so
        // idle legends stay clean.  A hidden series keeps an eye-slash so it
        // is clear why its row is dimmed.
        ImFont* icon_f = ui::icon_font(ui::tokens::ICON_XS);
        if (icon_f && (hovered || !state.user_visible))
        {
            const char* icon_s =
                ui::icon_str(state.user_visible ? ui::Icon::Eye : ui::Icon::EyeOff);
            const float icon_sz = ui::tokens::ICON_XS;
            const float eye_x   = row_x + width - EYE_WIDTH;
            const float eye_y   = row_y + (row_h - icon_sz) * 0.5f;
            dl->AddText(icon_f,
                        icon_sz,
                        ImVec2(eye_x, eye_y),
                        to_u32(hovered ? tc.text_secondary : tc.text_tertiary),
                        icon_s);
        }
    }
    else
    {
        // Still claim the row's space (the popup list lays rows out in sequence).
        ImGui::SetCursorScreenPos(ImVec2(row_x, row_y));
        ImGui::Dummy(ImVec2(std::max(1.0f, width), row_h));
    }
    return clicked;
}

bool LegendInteraction::draw_overflow_cell(std::span<Series* const> entries,
                                           int                      shown,
                                           const ImVec2&            pos,
                                           float                    width,
                                           const RowStyle&          style,
                                           const Rect&              legend_box,
                                           uint64_t                 key)
{
    const ui::ThemeColors& tc       = *style.colors;
    const int              n        = static_cast<int>(entries.size());
    const int              hidden   = n - shown;
    const float            row_h    = style.row_height;
    bool                   consumed = false;

    OverflowCell& oc = overflow_cells_[key];
    oc               = OverflowCell{};
    oc.cell          = Rect{pos.x, pos.y, width, row_h};
    oc.shown         = shown;
    oc.hidden        = hidden;

    const std::string text     = legend_layout::overflow_text(hidden);
    const std::string popup_id = std::format("##legend_full_{:x}", key);
    const std::string cell_id  = std::format("##legend_more_{:x}", key);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    const bool clicked =
        ImGui::InvisibleButton(cell_id.c_str(), ImVec2(std::max(1.0f, width), row_h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
    {
        dl->AddRectFilled(
            ImVec2(pos.x - 4.0f, pos.y),
            ImVec2(pos.x + width + 4.0f, pos.y + row_h),
            to_u32(Color{tc.accent_subtle.r, tc.accent_subtle.g, tc.accent_subtle.b, 0.3f}),
            3.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    // Link-like ink while hovered, so the cell reads as clickable.
    const ImU32 ink =
        hovered ? to_u32(tc.accent) : to_u32(Color{style.ink.r, style.ink.g, style.ink.b, 1.0f});
    dl->AddText(style.font,
                style.font_size,
                ImVec2(pos.x, pos.y + (row_h - style.font_size) * 0.5f),
                ink,
                text.c_str());

    if (clicked)
    {
        ImGui::OpenPopup(popup_id.c_str());
        consumed = true;
    }

    // Hover: the hidden entries with their colour keys (bounded list).
    if (hovered && !ImGui::IsPopupOpen(popup_id.c_str()))
    {
        oc.hovered         = true;
        const auto   lines = legend_layout::overflow_tooltip_lines(entries, shown);
        const size_t keyed =
            static_cast<int>(lines.size()) == hidden ? lines.size() : lines.size() - 1;
        ImGui::BeginTooltip();
        for (size_t k = 0; k < lines.size(); ++k)
        {
            if (k >= keyed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::ColorConvertU32ToFloat4(to_u32(tc.text_tertiary)));
                ImGui::TextUnformatted(lines[k].c_str());
                ImGui::PopStyleColor();
                continue;
            }
            const Series& s  = *entries[static_cast<size_t>(shown) + k];
            const Color   sc = ui::adapt_series_color_to_theme(legend_layout::swatch_color(s), tc);
            const float   a  = s.visible() ? 1.0f : HIDDEN_OPACITY;
            const ImVec2  p  = ImGui::GetCursorScreenPos();
            const float   lh = ImGui::GetTextLineHeight();
            const float   sy = p.y + (lh - SWATCH_SIZE) * 0.5f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, sy),
                                                      ImVec2(p.x + SWATCH_SIZE, sy + SWATCH_SIZE),
                                                      to_u32(sc, a),
                                                      2.0f);
            ImGui::Dummy(ImVec2(SWATCH_SIZE, lh));
            ImGui::SameLine(0.0f, SWATCH_GAP);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::ColorConvertU32ToFloat4(to_u32(tc.text_primary, a)));
            ImGui::TextUnformatted(lines[k].c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::ColorConvertU32ToFloat4(to_u32(tc.text_tertiary)));
        ImGui::Text("Click to show all %d entries", n);
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
    }

    // Full legend: every entry, scrollable, toggleable like the legend rows.
    if (!ImGui::IsPopupOpen(popup_id.c_str()))
        return consumed;

    auto text_w = [&](const std::string& t)
    { return style.font->CalcTextSizeA(style.font_size, FLT_MAX, 0.0f, t.c_str()).x; };
    float full_label_w = 0.0f;
    for (const Series* s : entries)
        full_label_w = std::max(full_label_w, std::min(text_w(s->label()), MAX_POPUP_LABEL));
    const float  row_w   = row_reserved_width() + full_label_w;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float  max_list =
        std::max(row_h * 4.0f,
                 std::min(display.y * 0.6f, row_h * static_cast<float>(MAX_POPUP_ROWS)));
    const float list_h  = std::min(static_cast<float>(n) * row_h, max_list);
    const float pad     = ui::tokens::SPACE_2;
    const float popup_w = row_w + 2.0f * pad + 8.0f + ImGui::GetStyle().ScrollbarSize;

    // Beside the legend box: right of it if there is room, else left of it.
    float px = legend_box.x + legend_box.w + ui::tokens::SPACE_1;
    if (px + popup_w > display.x)
        px = std::max(0.0f, legend_box.x - ui::tokens::SPACE_1 - popup_w);
    ImGui::SetNextWindowPos(ImVec2(px, std::max(0.0f, legend_box.y)), ImGuiCond_Appearing);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    if (ImGui::BeginPopup(popup_id.c_str(), ImGuiWindowFlags_NoSavedSettings))
    {
        oc.popup_open = true;
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::ColorConvertU32ToFloat4(to_u32(tc.text_secondary)));
        ImGui::Text("Legend \xC2\xB7 %d entries", n);
        ImGui::PopStyleColor();
        ImGui::Separator();

        RowStyle list_style = style;
        list_style.label_w  = full_label_w + 0.5f;   // 0.5: float slack
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild("##legend_full_list",
                              ImVec2(row_w + 8.0f + ImGui::GetStyle().ScrollbarSize, list_h),
                              ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground))
        {
            const ImVec2 lp = ImGui::GetWindowPos();
            const ImVec2 ls = ImGui::GetWindowSize();
            oc.popup_list   = Rect{lp.x, lp.y, ls.x, ls.y};
            ImGuiListClipper clipper;
            clipper.Begin(n, row_h);
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const ImVec2      p  = ImGui::GetCursorScreenPos();
                    const std::string id = std::format("##legend_full_row_{}", i);
                    consumed |= draw_entry_row(*entries[static_cast<size_t>(i)],
                                               ImVec2(p.x + 4.0f, p.y),
                                               row_w,
                                               list_style,
                                               id.c_str());
                    ++oc.popup_rows;
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            consumed = true;
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    return consumed;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

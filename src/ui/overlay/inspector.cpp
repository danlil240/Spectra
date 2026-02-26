#ifdef SPECTRA_USE_IMGUI

    #include "inspector.hpp"

    #include <algorithm>
    #include <cmath>
    #include <cstdio>
    #include <imgui.h>
    #include <limits>
    #include <numeric>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>
    #include <vector>

    #include "ui/commands/series_clipboard.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/imgui/widgets.hpp"

namespace spectra::ui
{

// ─── Lifecycle ──────────────────────────────────────────────────────────────

void Inspector::set_context(const SelectionContext& ctx)
{
    ctx_ = ctx;
}

void Inspector::set_fonts(ImFont* body, ImFont* heading, ImFont* title)
{
    font_body_    = body;
    font_heading_ = heading;
    font_title_   = title;
}

// ─── Main Draw ──────────────────────────────────────────────────────────────

void Inspector::draw(Figure& figure)
{
    // Context header
    // const auto& c = theme();  // Currently unused

    switch (ctx_.type)
    {
        case SelectionType::None:
        case SelectionType::Figure:
            // Default: show figure properties only
            draw_figure_properties(figure);
            break;

        case SelectionType::SeriesBrowser:
            // Show only series browser (no figure properties)
            draw_series_browser(figure);
            break;

        case SelectionType::Axes:
            if (ctx_.axes)
            {
                draw_axes_properties(*ctx_.axes, ctx_.axes_index);
            }
            break;

        case SelectionType::Series:
            // Always show the series browser so user can Shift+click to multi-select
            draw_series_browser(figure);
            if (ctx_.series)
            {
                draw_series_properties(*ctx_.series, ctx_.series_index);
            }
            break;
    }
}

// ─── Figure Properties ──────────────────────────────────────────────────────

void Inspector::draw_figure_properties(Figure& fig)
{
    const auto& c = theme();

    // Context title
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    ImGui::TextUnformatted("Figure");
    ImGui::PopStyleColor();
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Subtitle with info
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    char subtitle[64];
    int  total_series = 0;
    for (const auto& ax : fig.axes())
    {
        if (ax)
            total_series += static_cast<int>(ax->series().size());
    }
    std::snprintf(subtitle,
                  sizeof(subtitle),
                  "%d axes, %d series",
                  static_cast<int>(fig.axes().size()),
                  total_series);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    widgets::section_spacing();
    widgets::separator();
    widgets::section_spacing();

    // ── Background section
    auto& sty = fig.style();
    if (widgets::section_header("BACKGROUND", &sec_appearance_, font_heading_))
    {
        if (widgets::begin_animated_section("BACKGROUND"))
        {
            widgets::begin_group("bg");
            widgets::color_field("Background Color", sty.background);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Margins section
    if (widgets::section_header("MARGINS", &sec_margins_, font_heading_))
    {
        if (widgets::begin_animated_section("MARGINS"))
        {
            widgets::begin_group("margins");
            widgets::drag_field("Top", sty.margin_top, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Bottom", sty.margin_bottom, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Left", sty.margin_left, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("Right", sty.margin_right, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::section_spacing();
            widgets::drag_field("H Gap", sty.subplot_hgap, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::drag_field("V Gap", sty.subplot_vgap, 0.5f, 0.0f, 200.0f, "%.0f px");
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Legend section
    auto& leg = fig.legend();
    if (widgets::section_header("LEGEND", &sec_legend_, font_heading_))
    {
        if (widgets::begin_animated_section("LEGEND"))
        {
            widgets::begin_group("legend");
            bool show_legend = leg.visible;
            if (widgets::checkbox_field("Show Legend", show_legend))
            {
                leg.visible = show_legend;
            }

            const char* positions[] = {"Top Right",
                                       "Top Left",
                                       "Bottom Right",
                                       "Bottom Left",
                                       "Hidden"};
            int         pos         = static_cast<int>(leg.position);
            if (widgets::combo_field("Position", pos, positions, 5))
            {
                leg.position = static_cast<LegendPosition>(pos);
            }

            float font_size = leg.font_size;
            if (widgets::drag_field("Font Size", font_size, 0.5f, 6.0f, 32.0f, "%.0f px"))
            {
                leg.font_size = font_size;
            }

            float padding = leg.padding;
            if (widgets::drag_field("Padding", padding, 0.5f, 0.0f, 40.0f, "%.0f px"))
            {
                leg.padding = padding;
            }

            spectra::Color bg_color = leg.bg_color;
            if (widgets::color_field("Background", bg_color))
            {
                leg.bg_color = bg_color;
            }

            spectra::Color border_color = leg.border_color;
            if (widgets::color_field("Border", border_color))
            {
                leg.border_color = border_color;
            }
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Quick Actions
    if (widgets::section_header("QUICK ACTIONS", &sec_quick_, font_heading_))
    {
        if (widgets::begin_animated_section("QUICK ACTIONS"))
        {
            widgets::begin_group("quick");
            if (widgets::button_field("Reset to Defaults"))
            {
                sty = FigureStyle{};
                leg = LegendConfig{};
            }
            widgets::end_group();
            widgets::end_animated_section();
        }
    }
}

// ─── Series Browser ─────────────────────────────────────────────────────────

void Inspector::draw_series_browser(Figure& fig)
{
    const auto& c = theme();

    if (font_heading_)
        ImGui::PushFont(font_heading_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted("SERIES");
    ImGui::PopStyleColor();
    if (font_heading_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Paste button (shown when clipboard has data)
    if (clipboard_ && clipboard_->has_data())
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));

        ImFont* icf = icon_font(tokens::ICON_SM);
        if (icf) ImGui::PushFont(icf);
        size_t clip_n = clipboard_->count();
        std::string paste_lbl = std::string(icon_str(Icon::Duplicate))
            + (clip_n > 1 ? "  Paste " + std::to_string(clip_n) + " Series" : "  Paste");
        if (ImGui::Button(paste_lbl.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 24)))
        {
            // Paste into selected axes if available, else first axes
            AxesBase* target = nullptr;
            if (ctx_.type == SelectionType::Series || ctx_.type == SelectionType::Axes)
                target = ctx_.axes_base ? ctx_.axes_base : static_cast<AxesBase*>(ctx_.axes);
            if (!target)
            {
                if (!fig.all_axes().empty())
                {
                    for (auto& ab : fig.all_axes_mut())
                        if (ab) { target = ab.get(); break; }
                }
                if (!target)
                {
                    for (auto& ax : fig.axes_mut())
                        if (ax) { target = ax.get(); break; }
                }
            }
            if (target)
                clipboard_->paste_all(*target);
        }
        if (icf) ImGui::PopFont();
        ImGui::PopStyleColor(3);
        widgets::small_spacing();
    }

    // Helper lambda to draw series rows for any axes (2D or 3D)
    auto draw_axes_series = [&](AxesBase* ax_base, int ax_idx)
    {
        int s_idx = 0;
        for (auto& s : ax_base->series_mut())
        {
            if (!s)
            {
                s_idx++;
                continue;
            }
            ImGui::PushID(ax_idx * 1000 + s_idx);

            const char* name = s->label().empty() ? "Unnamed" : s->label().c_str();

            // Color swatch
            {
                const auto&    sc = s->color();
                spectra::Color sw{sc.r, sc.g, sc.b, sc.a};
                widgets::color_swatch(sw, 12.0f);
            }
            ImGui::SameLine(0.0f, tokens::SPACE_2);

            // Visibility toggle
            bool    vis    = s->visible();
            ImFont* icon_f = icon_font(tokens::ICON_SM);
            if (icon_f)
                ImGui::PushFont(icon_f);
            const char* eye_icon = vis ? icon_str(Icon::Eye) : icon_str(Icon::EyeOff);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                vis ? ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a)
                    : ImVec4(c.text_tertiary.r,
                             c.text_tertiary.g,
                             c.text_tertiary.b,
                             c.text_tertiary.a));
            if (ImGui::Button(eye_icon, ImVec2(20, 20)))
            {
                s->visible(!vis);
            }
            ImGui::PopStyleColor(2);
            if (icon_f)
                ImGui::PopFont();

            ImGui::SameLine(0.0f, tokens::SPACE_2);

            // Clickable series name → select it (supports multi-select with Shift)
            bool is_selected = (ctx_.type == SelectionType::Series && ctx_.is_selected(s.get()));
            if (is_selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(c.accent.r, c.accent.g, c.accent.b, c.accent.a));
            }
            // Use AllowOverlap so action buttons on the same row work
            if (ImGui::Selectable(name, is_selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - 72.0f, 0)))
            {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyShift)
                {
                    // Shift+click: toggle in multi-selection
                    ctx_.toggle_series(&fig, dynamic_cast<Axes*>(ax_base), ax_base, ax_idx, s.get(), s_idx);
                }
                else
                {
                    // Regular click: single select
                    ctx_.select_series(&fig, dynamic_cast<Axes*>(ax_base), ax_idx, s.get(), s_idx);
                    ctx_.axes_base = ax_base;   // always set for 3D support
                    if (!ctx_.selected_series.empty())
                        ctx_.selected_series[0].axes_base = ax_base;
                }
            }
            // Also detect Shift+click via IsItemClicked when Selectable doesn't fire
            // (ImGui::Selectable may not fire when clicking an already-selected item)
            else if (ImGui::IsItemClicked(0) && ImGui::GetIO().KeyShift)
            {
                ctx_.toggle_series(&fig, dynamic_cast<Axes*>(ax_base), ax_base, ax_idx, s.get(), s_idx);
            }
            if (is_selected)
            {
                ImGui::PopStyleColor();
            }

            // Action buttons: Copy / Cut / Delete (shown on same row)
            if (clipboard_)
            {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 68.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(c.accent_subtle.r, c.accent_subtle.g, c.accent_subtle.b, 0.5f));

                // Copy button
                ImFont* icf = icon_font(tokens::ICON_SM);
                if (icf) ImGui::PushFont(icf);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));

                char copy_id[32];
                std::snprintf(copy_id, sizeof(copy_id), "%s##cp%d_%d", icon_str(Icon::Copy), ax_idx, s_idx);
                if (ImGui::Button(copy_id, ImVec2(20, 20)))
                {
                    clipboard_->copy(*s);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Copy");

                ImGui::SameLine(0, 2.0f);

                // Cut button
                char cut_id[32];
                std::snprintf(cut_id, sizeof(cut_id), "%s##ct%d_%d", icon_str(Icon::Edit), ax_idx, s_idx);
                if (ImGui::Button(cut_id, ImVec2(20, 20)))
                {
                    clipboard_->cut(*s);
                    if (defer_removal_)
                        defer_removal_(ax_base, s.get());
                    else
                        ax_base->remove_series(static_cast<size_t>(s_idx));
                    ctx_.clear();
                    ImGui::PopStyleColor();   // Text
                    if (icf) ImGui::PopFont();
                    ImGui::PopStyleColor(2);  // Button, ButtonHovered
                    ImGui::PopID();
                    break;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cut");

                ImGui::SameLine(0, 2.0f);

                // Delete button
                ImGui::PopStyleColor();   // pop text_secondary
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.35f, 0.35f, 1.0f));
                char del_id[32];
                std::snprintf(del_id, sizeof(del_id), "%s##dl%d_%d", icon_str(Icon::Trash), ax_idx, s_idx);
                if (ImGui::Button(del_id, ImVec2(20, 20)))
                {
                    Series* deleted = s.get();
                    if (defer_removal_)
                        defer_removal_(ax_base, deleted);
                    else
                        ax_base->remove_series(static_cast<size_t>(s_idx));
                    if (ctx_.series == deleted)
                        ctx_.clear();
                    ImGui::PopStyleColor();   // red text
                    if (icf) ImGui::PopFont();
                    ImGui::PopStyleColor(2);  // Button, ButtonHovered
                    ImGui::PopID();
                    break;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Delete");

                ImGui::PopStyleColor();   // red text
                if (icf) ImGui::PopFont();
                ImGui::PopStyleColor(2);  // Button, ButtonHovered
            }

            ImGui::PopID();
            s_idx++;
        }
    };

    // Iterate all axes (2D + 3D) via the unified all_axes list.
    // If the figure has no all_axes entries, fall back to the 2D-only list.
    int ax_idx = 0;
    if (!fig.all_axes().empty())
    {
        for (auto& ax_base : fig.all_axes_mut())
        {
            if (ax_base)
                draw_axes_series(ax_base.get(), ax_idx);
            ax_idx++;
        }
    }
    else
    {
        for (auto& ax : fig.axes_mut())
        {
            if (ax)
                draw_axes_series(ax.get(), ax_idx);
            ax_idx++;
        }
    }
}

// ─── Axes Properties ────────────────────────────────────────────────────────

void Inspector::draw_axes_properties(Axes& ax, int index)
{
    const auto& c = theme();

    // Context title
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));
    char title[32];
    std::snprintf(title, sizeof(title), "Axes %d", index + 1);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Subtitle
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    char sub[64];
    std::snprintf(sub, sizeof(sub), "%zu series", ax.series().size());
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();

    widgets::section_spacing();
    widgets::separator();
    widgets::section_spacing();

    // ── X Axis
    if (widgets::section_header("X AXIS", &sec_axis_x_, font_heading_))
    {
        if (widgets::begin_animated_section("X AXIS"))
        {
            widgets::begin_group("xaxis");
            auto xlim = ax.x_limits();
            if (widgets::drag_field2("Range", xlim.min, xlim.max, 0.01f, "%.3f"))
            {
                ax.xlim(xlim.min, xlim.max);
            }
            std::string xlabel = ax.get_xlabel();
            if (widgets::text_field("Label", xlabel))
            {
                ax.xlabel(xlabel);
            }
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Y Axis
    if (widgets::section_header("Y AXIS", &sec_axis_y_, font_heading_))
    {
        if (widgets::begin_animated_section("Y AXIS"))
        {
            widgets::begin_group("yaxis");
            auto ylim = ax.y_limits();
            if (widgets::drag_field2("Range", ylim.min, ylim.max, 0.01f, "%.3f"))
            {
                ax.ylim(ylim.min, ylim.max);
            }
            std::string ylabel = ax.get_ylabel();
            if (widgets::text_field("Label", ylabel))
            {
                ax.ylabel(ylabel);
            }
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Grid & Border
    if (widgets::section_header("GRID & BORDER", &sec_grid_, font_heading_))
    {
        if (widgets::begin_animated_section("GRID & BORDER"))
        {
            widgets::begin_group("grid");
            bool grid = ax.grid_enabled();
            if (widgets::checkbox_field("Show Grid", grid))
            {
                ax.set_grid_enabled(grid);
            }
            bool border = ax.border_enabled();
            if (widgets::checkbox_field("Show Border", border))
            {
                ax.set_border_enabled(border);
            }

            auto& as = ax.axis_style();
            widgets::color_field("Grid Color", as.grid_color);
            widgets::drag_field("Grid Width", as.grid_width, 0.1f, 0.5f, 5.0f, "%.1f px");
            widgets::drag_field("Tick Length", as.tick_length, 0.5f, 0.0f, 20.0f, "%.0f px");
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Autoscale
    if (widgets::section_header("AUTOSCALE", &sec_style_, font_heading_))
    {
        if (widgets::begin_animated_section("AUTOSCALE"))
        {
            widgets::begin_group("autoscale");
            const char* modes[] = {"Fit", "Tight", "Padded", "Manual"};
            int         mode    = static_cast<int>(ax.get_autoscale_mode());
            if (widgets::combo_field("Mode", mode, modes, 4))
            {
                ax.autoscale_mode(static_cast<AutoscaleMode>(mode));
            }
            if (widgets::button_field("Auto-fit Now"))
            {
                ax.auto_fit();
            }
            widgets::end_group();
            widgets::end_animated_section();
        }
    }

    // ── Axes Statistics (aggregate across all series)
    if (widgets::section_header("STATISTICS", &sec_axes_stats_, font_heading_))
    {
        if (widgets::begin_animated_section("STATISTICS"))
        {
            widgets::begin_group("axes_stats");
            draw_axes_statistics(ax);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }
}

// ─── Series Properties ──────────────────────────────────────────────────────

void Inspector::draw_series_properties(Series& s, int /*index*/)
{
    const auto& c = theme();

    // Context header with color swatch
    if (font_title_)
        ImGui::PushFont(font_title_);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_primary.r, c.text_primary.g, c.text_primary.b, c.text_primary.a));

    // Determine type name
    const char* type_name = "Series";
    if (dynamic_cast<LineSeries*>(&s))
        type_name = "Line Series";
    else if (dynamic_cast<ScatterSeries*>(&s))
        type_name = "Scatter Series";

    const char* name = s.label().empty() ? "Unnamed" : s.label().c_str();
    char        title[128];
    std::snprintf(title, sizeof(title), "%s: %s", type_name, name);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    if (font_title_)
        ImGui::PopFont();

    widgets::small_spacing();

    // Color swatch + type badge
    {
        const auto&    sc = s.color();
        spectra::Color swatch_col{sc.r, sc.g, sc.b, sc.a};
        widgets::color_swatch(swatch_col, 16.0f);
    }
    ImGui::SameLine(0.0f, tokens::SPACE_2);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(c.text_secondary.r, c.text_secondary.g, c.text_secondary.b, c.text_secondary.a));
    ImGui::TextUnformatted(type_name);
    ImGui::PopStyleColor();

    widgets::section_spacing();
    widgets::separator();
    widgets::section_spacing();

    // ── Appearance
    if (widgets::section_header("APPEARANCE", &sec_appearance_, font_heading_))
    {
        if (widgets::begin_animated_section("APPEARANCE"))
        {
            widgets::begin_group("appearance");

            spectra::Color col = s.color();
            if (widgets::color_field("Color", col))
            {
                s.set_color(col);
            }

            bool vis = s.visible();
            if (widgets::toggle_field("Visible", vis))
            {
                s.visible(vis);
            }

            // Line style dropdown (all series types)
            {
                static const char* line_style_names[] =
                    {"None", "Solid", "Dashed", "Dotted", "Dash-Dot", "Dash-Dot-Dot"};
                int ls_idx = static_cast<int>(s.line_style());
                if (widgets::combo_field("Line Style", ls_idx, line_style_names, 6))
                {
                    s.line_style(static_cast<spectra::LineStyle>(ls_idx));
                }
            }

            // Marker style dropdown (all series types)
            {
                static const char* marker_style_names[] = {"None",
                                                           "Point",
                                                           "Circle",
                                                           "Plus",
                                                           "Cross",
                                                           "Star",
                                                           "Square",
                                                           "Diamond",
                                                           "Triangle Up",
                                                           "Triangle Down",
                                                           "Triangle Left",
                                                           "Triangle Right",
                                                           "Pentagon",
                                                           "Hexagon",
                                                           "Filled Circle",
                                                           "Filled Square",
                                                           "Filled Diamond",
                                                           "Filled Triangle Up"};
                int                ms_idx               = static_cast<int>(s.marker_style());
                if (widgets::combo_field("Marker", ms_idx, marker_style_names, 18))
                {
                    s.marker_style(static_cast<spectra::MarkerStyle>(ms_idx));
                }
            }

            // Marker size (shown when marker is not None)
            if (s.marker_style() != spectra::MarkerStyle::None)
            {
                float msz = s.marker_size();
                if (widgets::slider_field("Marker Size", msz, 1.0f, 30.0f, "%.1f px"))
                {
                    s.marker_size(msz);
                }
            }

            // Opacity
            {
                float op = s.opacity();
                if (widgets::slider_field("Opacity", op, 0.0f, 1.0f, "%.2f"))
                {
                    s.opacity(op);
                }
            }

            // Type-specific controls
            if (auto* line = dynamic_cast<LineSeries*>(&s))
            {
                float w = line->width();
                if (widgets::slider_field("Line Width", w, 0.5f, 12.0f, "%.1f px"))
                {
                    line->width(w);
                }
            }
            if (auto* scatter = dynamic_cast<ScatterSeries*>(&s))
            {
                float sz = scatter->size();
                if (widgets::slider_field("Point Size", sz, 0.5f, 30.0f, "%.1f px"))
                {
                    scatter->size(sz);
                }
            }

            // Label editing
            std::string lbl = s.label();
            if (widgets::text_field("Label", lbl))
            {
                s.label(lbl);
            }

            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Data Preview (sparkline)
    if (widgets::section_header("PREVIEW", &sec_preview_, font_heading_))
    {
        if (widgets::begin_animated_section("PREVIEW"))
        {
            widgets::begin_group("preview");
            draw_series_sparkline(s);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Data Statistics
    if (widgets::section_header("DATA", &sec_stats_, font_heading_))
    {
        if (widgets::begin_animated_section("DATA"))
        {
            widgets::begin_group("data");
            draw_series_statistics(s);
            widgets::end_group();
            widgets::small_spacing();
            widgets::end_animated_section();
        }
    }

    // ── Back button
    widgets::section_spacing();
    if (widgets::button_field("Back to Figure"))
    {
        if (ctx_.figure)
        {
            ctx_.select_figure(ctx_.figure);
        }
        else
        {
            ctx_.clear();
        }
    }
}

// ─── Helper: compute percentile from sorted data ────────────────────────────

static double compute_percentile(const std::vector<float>& sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    if (sorted.size() == 1)
        return static_cast<double>(sorted[0]);
    double idx = p * static_cast<double>(sorted.size() - 1);
    size_t lo  = static_cast<size_t>(idx);
    size_t hi  = lo + 1;
    if (hi >= sorted.size())
        return static_cast<double>(sorted.back());
    double frac = idx - static_cast<double>(lo);
    return static_cast<double>(sorted[lo]) * (1.0 - frac) + static_cast<double>(sorted[hi]) * frac;
}

// ─── Helper: get data spans from any series type ────────────────────────────

static void get_series_data(const Series&           s,
                            std::span<const float>& x_data,
                            std::span<const float>& y_data,
                            size_t&                 count)
{
    x_data = {};
    y_data = {};
    count  = 0;
    if (const auto* line = dynamic_cast<const LineSeries*>(&s))
    {
        x_data = line->x_data();
        y_data = line->y_data();
        count  = line->point_count();
    }
    else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
    {
        x_data = scatter->x_data();
        y_data = scatter->y_data();
        count  = scatter->point_count();
    }
}

// ─── Series Statistics ──────────────────────────────────────────────────────

void Inspector::draw_series_statistics(const Series& s)
{
    char buf[96];

    std::span<const float> x_data;
    std::span<const float> y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    // Point count with badge
    std::snprintf(buf, sizeof(buf), "%zu", count);
    widgets::stat_row("Points", buf);

    if (count == 0)
        return;

    widgets::small_spacing();
    widgets::separator_label("X Axis", font_heading_);
    widgets::small_spacing();

    // X statistics
    if (!x_data.empty())
    {
        auto [xmin_it, xmax_it] = std::minmax_element(x_data.begin(), x_data.end());
        float xmin              = *xmin_it;
        float xmax              = *xmax_it;

        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(xmin));
        widgets::stat_row("Min", buf);
        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(xmax));
        widgets::stat_row("Max", buf);

        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(xmax - xmin));
        widgets::stat_row("Range", buf);

        // X mean
        double x_sum  = std::accumulate(x_data.begin(), x_data.end(), 0.0);
        double x_mean = x_sum / static_cast<double>(count);
        std::snprintf(buf, sizeof(buf), "%.6g", x_mean);
        widgets::stat_row("Mean", buf);
    }

    widgets::small_spacing();
    widgets::separator_label("Y Axis", font_heading_);
    widgets::small_spacing();

    // Y statistics
    if (!y_data.empty())
    {
        auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
        float ymin              = *ymin_it;
        float ymax              = *ymax_it;

        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(ymin));
        widgets::stat_row("Min", buf);
        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(ymax));
        widgets::stat_row("Max", buf);

        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(ymax - ymin));
        widgets::stat_row("Range", buf);

        // Mean
        double sum  = std::accumulate(y_data.begin(), y_data.end(), 0.0);
        double mean = sum / static_cast<double>(count);
        std::snprintf(buf, sizeof(buf), "%.6g", mean);
        widgets::stat_row("Mean", buf);

        // Median (requires sorted copy)
        std::vector<float> sorted(y_data.begin(), y_data.end());
        std::sort(sorted.begin(), sorted.end());
        double median = compute_percentile(sorted, 0.5);
        std::snprintf(buf, sizeof(buf), "%.6g", median);
        widgets::stat_row("Median", buf);

        // Std deviation
        double sq_sum = 0.0;
        for (float v : y_data)
        {
            double diff = static_cast<double>(v) - mean;
            sq_sum += diff * diff;
        }
        double stddev = std::sqrt(sq_sum / static_cast<double>(count));
        std::snprintf(buf, sizeof(buf), "%.6g", stddev);
        widgets::stat_row("Std Dev", buf);

        // Percentiles (only for datasets with enough points)
        if (count >= 4)
        {
            widgets::small_spacing();
            widgets::separator_label("Percentiles", font_heading_);
            widgets::small_spacing();

            double p25 = compute_percentile(sorted, 0.25);
            double p75 = compute_percentile(sorted, 0.75);
            double p05 = compute_percentile(sorted, 0.05);
            double p95 = compute_percentile(sorted, 0.95);

            std::snprintf(buf, sizeof(buf), "%.6g", p05);
            widgets::stat_row("P5", buf);
            std::snprintf(buf, sizeof(buf), "%.6g", p25);
            widgets::stat_row("P25 (Q1)", buf);
            std::snprintf(buf, sizeof(buf), "%.6g", median);
            widgets::stat_row("P50 (Med)", buf);
            std::snprintf(buf, sizeof(buf), "%.6g", p75);
            widgets::stat_row("P75 (Q3)", buf);
            std::snprintf(buf, sizeof(buf), "%.6g", p95);
            widgets::stat_row("P95", buf);

            // IQR
            std::snprintf(buf, sizeof(buf), "%.6g", p75 - p25);
            widgets::stat_row("IQR", buf);
        }
    }
}

// ─── Series Sparkline ───────────────────────────────────────────────────────

void Inspector::draw_series_sparkline(const Series& s)
{
    std::span<const float> x_data;
    std::span<const float> y_data;
    size_t                 count = 0;
    get_series_data(s, x_data, y_data, count);

    if (y_data.empty())
    {
        widgets::info_row("Preview", "No data");
        return;
    }

    // Downsample to max ~200 points for the sparkline
    constexpr size_t   MAX_SPARKLINE = 200;
    std::vector<float> downsampled;
    if (count <= MAX_SPARKLINE)
    {
        downsampled.assign(y_data.begin(), y_data.end());
    }
    else
    {
        downsampled.reserve(MAX_SPARKLINE);
        for (size_t i = 0; i < MAX_SPARKLINE; ++i)
        {
            size_t src = i * count / MAX_SPARKLINE;
            downsampled.push_back(y_data[src]);
        }
    }

    const auto&    sc = s.color();
    spectra::Color line_col{sc.r, sc.g, sc.b, sc.a};
    widgets::sparkline("##series_spark", downsampled, -1.0f, 40.0f, line_col);
}

// ─── Axes Statistics ────────────────────────────────────────────────────────

void Inspector::draw_axes_statistics(const Axes& ax)
{
    char buf[96];

    size_t total_points   = 0;
    size_t visible_series = 0;
    size_t total_series   = ax.series().size();

    double global_ymin = std::numeric_limits<double>::max();
    double global_ymax = std::numeric_limits<double>::lowest();
    double global_xmin = std::numeric_limits<double>::max();
    double global_xmax = std::numeric_limits<double>::lowest();

    for (const auto& s : ax.series())
    {
        if (!s)
            continue;
        if (s->visible())
            visible_series++;

        std::span<const float> x_data;
        std::span<const float> y_data;
        size_t                 count = 0;
        get_series_data(*s, x_data, y_data, count);
        total_points += count;

        if (!x_data.empty())
        {
            auto [xmin_it, xmax_it] = std::minmax_element(x_data.begin(), x_data.end());
            global_xmin             = std::min(global_xmin, static_cast<double>(*xmin_it));
            global_xmax             = std::max(global_xmax, static_cast<double>(*xmax_it));
        }
        if (!y_data.empty())
        {
            auto [ymin_it, ymax_it] = std::minmax_element(y_data.begin(), y_data.end());
            global_ymin             = std::min(global_ymin, static_cast<double>(*ymin_it));
            global_ymax             = std::max(global_ymax, static_cast<double>(*ymax_it));
        }
    }

    std::snprintf(buf, sizeof(buf), "%zu / %zu", visible_series, total_series);
    widgets::stat_row("Visible", buf);

    std::snprintf(buf, sizeof(buf), "%zu", total_points);
    widgets::stat_row("Total Points", buf);

    if (total_points > 0)
    {
        widgets::small_spacing();

        if (global_xmin <= global_xmax)
        {
            std::snprintf(buf, sizeof(buf), "[%.4g, %.4g]", global_xmin, global_xmax);
            widgets::stat_row("X Extent", buf);
        }
        if (global_ymin <= global_ymax)
        {
            std::snprintf(buf, sizeof(buf), "[%.4g, %.4g]", global_ymin, global_ymax);
            widgets::stat_row("Y Extent", buf);
        }
    }
}

}   // namespace spectra::ui

#endif   // SPECTRA_USE_IMGUI

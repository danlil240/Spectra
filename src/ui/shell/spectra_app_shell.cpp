#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/spectra_app_shell.hpp"

    #include <algorithm>
    #include <spectra/figure.hpp>

    #include "imgui.h"
    #include "io/export_registry.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/welcome_actions.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/data_sidebar/data_sidebar.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/input/input.hpp"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/overlay/custom_transform_dialog.hpp"
    #include "ui/overlay/data_interaction.hpp"
    #include "ui/overlay/knob_manager.hpp"
    #include "ui/overlay/marker_actions.hpp"
    #include "ui/overlay/readout_format.hpp"
    #include "ui/settings/settings_panel.hpp"
    #include "ui/settings/settings_store.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/logs/log_browser_panel.hpp"
    #include "ui/topics/topics_panel.hpp"
    #include "ui/workspace/layout_session.hpp"

    #include <cfloat>
    #include <filesystem>
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra::ui::shell
{
namespace
{
using spectra::ui::Icon;

MenuAction make_toggle_action(const char*           label,
                              std::function<bool()> checked,
                              std::function<void()> on_click,
                              Icon                  icon)
{
    MenuAction action;
    action.label    = label;
    action.icon     = static_cast<uint16_t>(icon);
    action.checked  = std::move(checked);
    action.on_click = std::move(on_click);
    return action;
}
}   // namespace

// ─── SpectraCanvasHost ───────────────────────────────────────────────────────

SpectraCanvasHost::SpectraCanvasHost(spectra::ImGuiIntegration* imgui, spectra::LayoutManager* lm)
    : CanvasHost(lm), imgui_(imgui)
{
}

void SpectraCanvasHost::draw()
{
    if (!imgui_)
        return;
    auto* shell = imgui_->app_shell();
    if (!shell)
        return;
    if (auto* fig = shell->current_figure())
        imgui_->draw_canvas_content(*fig);
}

// ─── SpectraNavRail ──────────────────────────────────────────────────────────

SpectraNavRail::SpectraNavRail(spectra::ImGuiIntegration* imgui, PanelRegistry* registry)
    : NavRail(registry), imgui_(imgui)
{
}

void SpectraNavRail::build_items(std::vector<NavItem>& out) const
{
    if (!imgui_)
        return;

    // "Label (Key)" when the item's command has a shortcut.
    auto with_key = [this](const std::string& label, const std::string& command_id)
    {
        const std::string key =
            menu_shortcut_text(imgui_->shortcut_manager_, imgui_->command_registry_, command_id);
        return key.empty() ? label : label + " (" + key + ")";
    };

    auto add_tool = [&](Icon icon, const char* label, ToolMode mode)
    {
        NavItem item;
        item.icon      = icon;
        item.label     = label;
        item.id        = label;
        item.tooltip   = imgui_->tool_tooltip(label, mode);
        item.tool_mode = true;
        item.is_active = [imgui = imgui_, mode]() { return imgui->interaction_mode_ == mode; };
        item.on_click  = [imgui = imgui_, mode]() { imgui->set_interaction_mode(mode); };
        out.push_back(std::move(item));
    };

    add_tool(Icon::MousePointer, "Select", ToolMode::Select);
    add_tool(Icon::Hand, "Pan", ToolMode::Pan);
    add_tool(Icon::ZoomIn, "Zoom", ToolMode::BoxZoom);

    NavItem sep1;
    sep1.is_section_header = true;
    sep1.label             = "##sep1";
    out.push_back(std::move(sep1));

    add_tool(Icon::Ruler, "Measure", ToolMode::Measure);
    add_tool(Icon::Comment, "Annotate", ToolMode::Annotate);
    add_tool(Icon::VectorSquare, "ROI", ToolMode::ROI);

    NavItem sep2;
    sep2.is_section_header = true;
    sep2.label             = "##sep2";
    out.push_back(std::move(sep2));

    {
        // Data tips: opens a popover listing them (go to / remove / clear all,
        // undoable) instead of deleting them all on a click.
        const size_t n = imgui_->data_interaction_ && imgui_->current_figure_
                             ? figure_marker_indices(imgui_->data_interaction_->markers(),
                                                     *imgui_->current_figure_)
                                   .size()
                             : 0;
        NavItem      markers;
        markers.icon      = Icon::MapPin;
        markers.label     = "Markers";
        markers.id        = "markers";
        markers.tooltip   = "Data tips (" + std::to_string(n) + ")";
        markers.is_active = [this]() { return markers_popover_open_; };
        markers.on_click  = [this]() { markers_popover_open_ = !markers_popover_open_; };
        out.push_back(std::move(markers));
    }

    {
        NavItem xform;
        xform.icon      = Icon::MagicWand;
        xform.label     = "Transform";
        xform.id        = "transform";
        xform.tooltip   = "Transform data (derivative, FFT, custom...)";
        xform.is_active = [imgui = imgui_]() { return imgui->custom_transform_dialog_.is_open(); };
        xform.on_click  = [imgui = imgui_]() { imgui->open_transform_dialog(); };
        out.push_back(std::move(xform));
    }

    NavItem sep3;
    sep3.is_section_header = true;
    sep3.label             = "##sep3";
    out.push_back(std::move(sep3));

    NavRail::build_items(out);

    if (auto* shell = imgui_->app_shell())
    {
        for (NavItem& item : out)
        {
            if (item.is_section_header || item.id.empty() || item.id.rfind("core.", 0) != 0)
                continue;

            // SettingsPanel gates its own draw() on an internal visible_ flag that is
            // independent of the PanelRegistry entry used for menu/nav bookkeeping, so
            // toggling the registry panel alone never opens the window. Drive the real
            // panel directly instead.
            if (item.id == "core.settings")
            {
                item.tooltip    = with_key(item.label, "panel.open_settings");
                item.pin_bottom = true;
                item.is_active  = [imgui = imgui_]()
                { return imgui->settings_panel() && imgui->settings_panel()->is_visible(); };
                item.on_click = [imgui = imgui_]()
                {
                    if (auto* sp = imgui->settings_panel())
                        sp->set_visible(!sp->is_visible());
                };
                continue;
            }

            const std::string panel_id = item.id;
            item.tooltip   = with_key(item.label, SpectraAppShell::panel_command_id(panel_id));
            item.is_active = [shell, panel_id]() { return shell->panel_visible(panel_id); };
            item.on_click  = [shell, panel_id]() { shell->toggle_panel(panel_id); };
        }
    }

    NavItem help;
    help.icon       = Icon::Help;
    help.label      = "Help";
    help.id         = "help";
    help.tooltip    = with_key("Help", "help.show");
    help.pin_bottom = true;
    help.is_active  = []() { return false; };
    help.on_click   = [imgui = imgui_]()
    {
        if (imgui->command_registry())
            imgui->command_registry()->execute("help.show");
    };
    out.push_back(std::move(help));
}

void SpectraNavRail::draw()
{
    if (!imgui_ || !layout_manager_ || !imgui_->show_nav_rail_)
        return;

    const spectra::Rect bounds = layout_manager_->nav_rail_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    // Wheel-scrollable (no scrollbar): on windows too short for every item
    // even at the smallest cells, the bottom ones (Help) stay reachable.
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    const float rail_w = bounds.w;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, bounds.h), ImGuiCond_Always);

    if (ImGui::Begin("##navrail", nullptr, flags))
    {
        {
            ImDrawList* dl            = ImGui::GetWindowDrawList();
            const float right_edge    = bounds.x + rail_w;
            const float shadow_spread = ui::tokens::ELEVATION_1_SPREAD;
            for (int i = 0; i < 4; ++i)
            {
                const float t     = static_cast<float>(i) / 4.0f;
                const float alpha = 0.09f * (1.0f - t);
                const float off   = shadow_spread * t;
                dl->AddRectFilled(ImVec2(right_edge, bounds.y),
                                  ImVec2(right_edge + off + 1.0f, bounds.y + bounds.h),
                                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 255)));
            }
            const auto& c = ui::theme();
            dl->AddLine(ImVec2(right_edge - 1.0f, bounds.y),
                        ImVec2(right_edge - 1.0f, bounds.y + bounds.h),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(c.border_subtle.r, c.border_subtle.g, c.border_subtle.b, 0.52f)),
                        1.0f);
        }

        ImFont*     label_font = imgui_->font_heading_;
        const float btn_w      = rail_w;

        std::vector<NavItem> items;
        build_items(items);

        int button_count = 0;
        int sep_count    = 0;
        for (const NavItem& item : items)
        {
            if (item.is_section_header)
                ++sep_count;
            else
                ++button_count;
        }

        const float scale =
            LayoutManager::nav_rail_scale_for_height(bounds.h, button_count, sep_count);

        auto draw_separator = [&]()
        {
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * scale));
            const float sep_inset = ui::tokens::SPACE_4 * scale;
            ImVec2      p0        = ImVec2(ImGui::GetWindowPos().x + sep_inset,
                               std::floor(ImGui::GetCursorScreenPos().y));
            ImVec2      p1        = ImVec2(ImGui::GetWindowPos().x + rail_w - sep_inset, p0.y);
            ImGui::GetWindowDrawList()->AddLine(
                p0,
                p1,
                IM_COL32(static_cast<int>(ui::theme().border_subtle.r * 255),
                         static_cast<int>(ui::theme().border_subtle.g * 255),
                         static_cast<int>(ui::theme().border_subtle.b * 255),
                         32),
                1.0f);
            ImGui::Dummy(ImVec2(0, ui::tokens::SPACE_2 * scale));
        };

        // Grouping: the tool-mode buttons (one is always active) sit on a
        // shared tray drawn behind them (channel 0); Settings and Help are
        // pinned to the bottom of the rail, away from the panel toggles.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        int pinned_count = 0;
        for (const NavItem& item : items)
            if (item.pin_bottom)
                ++pinned_count;
        bool  pinned_placed = false;
        float btn_h         = 0.0f;
        float tray_top      = -1.0f;
        float tray_bottom   = -1.0f;
        auto  flush_tray    = [&]()
        {
            if (tray_top < 0.0f)
                return;
            const auto& c     = ui::theme();
            const float inset = ui::tokens::SPACE_1 * scale;
            dl->ChannelsSetCurrent(0);
            const ImVec2 t0(bounds.x + inset, tray_top - inset);
            const ImVec2 t1(bounds.x + rail_w - inset - 1.0f, tray_bottom + inset);
            dl->AddRectFilled(t0,
                              t1,
                              ImGui::ColorConvertFloat4ToU32(
                                  ImVec4(c.bg_tertiary.r, c.bg_tertiary.g, c.bg_tertiary.b, 0.75f)),
                              ui::tokens::RADIUS_MD);
            dl->AddRect(
                t0,
                t1,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(c.border_default.r, c.border_default.g, c.border_default.b, 0.45f)),
                ui::tokens::RADIUS_MD);
            dl->ChannelsSetCurrent(1);
            tray_top = tray_bottom = -1.0f;
        };

        for (const NavItem& item : items)
        {
            if (item.is_section_header)
            {
                flush_tray();
                draw_separator();
                continue;
            }
            if (item.pin_bottom && !pinned_placed)
            {
                pinned_placed      = true;
                const float bottom = bounds.y + bounds.h - ui::tokens::SPACE_3;
                const float pin_y  = bottom - static_cast<float>(pinned_count) * btn_h;
                const float cur_y  = ImGui::GetCursorScreenPos().y;
                if (btn_h > 0.0f && pin_y > cur_y)
                {
                    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, pin_y));
                    // Hairline above the pinned group.
                    const float sep_inset = ui::tokens::SPACE_4 * scale;
                    dl->AddLine(ImVec2(bounds.x + sep_inset, std::floor(pin_y) - 1.0f),
                                ImVec2(bounds.x + rail_w - sep_inset, std::floor(pin_y) - 1.0f),
                                IM_COL32(static_cast<int>(ui::theme().border_subtle.r * 255),
                                         static_cast<int>(ui::theme().border_subtle.g * 255),
                                         static_cast<int>(ui::theme().border_subtle.b * 255),
                                         32),
                                1.0f);
                }
            }
            if (!item.tool_mode)
                flush_tray();
            const bool active = item.is_active && item.is_active();
            if (imgui_->icon_label_button_rail(ui::icon_str(item.icon),
                                               item.label.c_str(),
                                               active,
                                               imgui_->font_icon_,
                                               label_font,
                                               btn_w,
                                               scale,
                                               item.tooltip.c_str()))
            {
                if (item.on_click)
                    item.on_click();
                if (item.id == "markers" && markers_popover_open_)
                    markers_popover_fresh_ = true;
            }
            btn_h = std::max(btn_h, ImGui::GetItemRectSize().y);
            if (item.tool_mode)
            {
                if (tray_top < 0.0f)
                    tray_top = ImGui::GetItemRectMin().y;
                tray_bottom = ImGui::GetItemRectMax().y;
            }
            if (item.id == "markers")
            {
                markers_btn_x_  = ImGui::GetItemRectMin().x;
                markers_btn_y_  = ImGui::GetItemRectMin().y;
                markers_btn_x1_ = ImGui::GetItemRectMax().x;
                markers_btn_y1_ = ImGui::GetItemRectMax().y;
            }
        }
        flush_tray();
        dl->ChannelsMerge();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);

    draw_markers_popover();
}

void SpectraNavRail::draw_markers_popover()
{
    const bool fresh       = markers_popover_fresh_;
    markers_popover_fresh_ = false;
    if (!markers_popover_open_)
        return;
    auto*   di  = imgui_->data_interaction_;
    Figure* fig = imgui_->current_figure_;
    if (!di || !fig)
    {
        markers_popover_open_ = false;
        return;
    }

    const auto&                    c       = ui::theme();
    spectra::ImGuiIntegration*     imgui   = imgui_;
    spectra::UndoManager*          undo    = imgui_->undo_manager_;
    const spectra::CurrentFigureFn cur_fig = [imgui]() { return imgui->current_figure_; };
    spectra::FigureLookup          lookup;
    lookup.id_of   = imgui_->get_figure_id_;
    lookup.resolve = imgui_->get_figure_ptr_;

    ImGui::SetNextWindowPos(ImVec2(markers_btn_x1_ + ui::tokens::SPACE_2, markers_btn_y_),
                            ImGuiCond_Always);
    // Fixed width (scales with the UI font) so value columns are not clipped.
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 22.0f, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ui::tokens::SPACE_3, ui::tokens::SPACE_3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui::tokens::RADIUS_MD);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(c.bg_elevated.r, c.bg_elevated.g, c.bg_elevated.b, 1.0f));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing;
    bool hovered = false;
    if (ImGui::Begin("##MarkersPopover", nullptr, flags))
    {
        hovered                           = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows
                                         | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const std::vector<size_t> indices = figure_marker_indices(di->markers(), *fig);

        if (imgui_->font_heading_)
            ImGui::PushFont(imgui_->font_heading_);
        ImGui::Text("DATA TIPS (%zu)", indices.size());
        if (imgui_->font_heading_)
            ImGui::PopFont();
        ImGui::Spacing();

        int remove_idx = -1;
        if (indices.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(c.text_secondary.r,
                                         c.text_secondary.g,
                                         c.text_secondary.b,
                                         c.text_secondary.a));
            ImGui::TextUnformatted("No data tips yet.");
            ImGui::TextUnformatted("Click a point in Pan mode to pin one.");
            ImGui::PopStyleColor();
        }
        else
        {
            // Rows are as tall as the trash button (icon font) so they do not
            // overlap; the list scrolls past 8 rows.
            float icon_h = ImGui::GetFrameHeight();
            if (imgui_->font_icon_)
            {
                ImGui::PushFont(imgui_->font_icon_);
                icon_h = ImGui::GetFrameHeight();
                ImGui::PopFont();
            }
            const float row_h = std::max(ImGui::GetFrameHeight(), icon_h);
            const float max_h = 8.0f * (row_h + ImGui::GetStyle().ItemSpacing.y);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, max_h));
            ImGui::BeginChild("##MarkerList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
            for (size_t row = 0; row < indices.size(); ++row)
            {
                const DataMarker& m = di->markers()[indices[row]];
                ImGui::PushID(static_cast<int>(indices[row]));

                // Series colour dot
                const ImVec2         p  = ImGui::GetCursorScreenPos();
                const float          r  = row_h * 0.18f;
                const spectra::Color sc = m.series ? m.series->color() : m.color;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(p.x + r + 2.0f, p.y + row_h * 0.5f),
                    r,
                    ImGui::ColorConvertFloat4ToU32(ImVec4(sc.r, sc.g, sc.b, 1.0f)));
                ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, row_h));
                ImGui::SameLine();

                std::string text = marker_series_name(m) + "   " + format_compact_value(m.data_x)
                                   + ", " + format_compact_value(m.data_y);
                if (m.is_3d)
                    text += ", " + format_compact_value(m.data_z);
                const float trash_w = row_h;
                if (ImGui::Selectable(text.c_str(),
                                      false,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(ImGui::GetContentRegionAvail().x - trash_w
                                                 - ImGui::GetStyle().ItemSpacing.x,
                                             row_h)))
                {
                    center_view_on_marker(m, *fig, undo, lookup);
                }
                if (ImGui::IsItemHovered() && !m.is_3d)
                    ImGui::SetTooltip("Go to this data tip");
                ImGui::SameLine();
                if (imgui_->font_icon_)
                    ImGui::PushFont(imgui_->font_icon_);
                const bool rm = ImGui::SmallButton(ui::icon_str(Icon::Trash));
                if (imgui_->font_icon_)
                    ImGui::PopFont();
                if (rm)
                    remove_idx = static_cast<int>(indices[row]);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Remove data tip");
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::BeginDisabled(indices.empty());
        if (ImGui::Button("Clear all"))
            clear_figure_markers_undoable(*di, *fig, undo, cur_fig);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, c.text_tertiary.a));
        ImGui::TextUnformatted("Ctrl+Z to undo");
        ImGui::PopStyleColor();

        if (remove_idx >= 0)
            remove_marker_undoable(*di, static_cast<size_t>(remove_idx), undo, cur_fig);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Close on Esc or a click outside the popover and its rail button.
    const ImVec2 mouse     = ImGui::GetMousePos();
    const bool   on_button = mouse.x >= markers_btn_x_ && mouse.x <= markers_btn_x1_
                           && mouse.y >= markers_btn_y_ && mouse.y <= markers_btn_y1_;
    if (!fresh
        && ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered && !on_button)
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        markers_popover_open_ = false;
}

// ─── SpectraAppShell ─────────────────────────────────────────────────────────

SpectraAppShell::SpectraAppShell(spectra::ImGuiIntegration* imgui) : imgui_(imgui)
{
    config_.dockspace  = false;
    config_.menu_bar   = false;
    config_.nav_rail   = false;
    config_.status_bar = false;
    config_.app_name   = "Spectra";
}

void SpectraAppShell::bind_imgui(spectra::ImGuiIntegration* imgui)
{
    imgui_ = imgui;
}

void SpectraAppShell::set_layout_manager(spectra::LayoutManager* lm)
{
    AppShell::set_layout_manager(lm);
    if (spectra_nav_)
        spectra_nav_->set_layout_manager(lm);
}

void SpectraAppShell::set_current_figure(spectra::Figure* figure)
{
    current_figure_ = figure;
}

void SpectraAppShell::ensure_initialized()
{
    if (!imgui_)
        return;

    if (!initialized_)
    {
        set_layout_manager(&imgui_->get_layout_manager());
        spectra_nav_ = std::make_unique<SpectraNavRail>(imgui_, &panels_);
        spectra_nav_->set_layout_manager(layout_manager_);
        nav_rail_.set_registry(&panels_);
        initialize();
    }

    if (!spectra_canvas_)
        spectra_canvas_ = dynamic_cast<SpectraCanvasHost*>(&canvas_host());
}

void SpectraAppShell::sync_before_frame()
{
    ensure_initialized();
    sync_panel_state_from_imgui();
    sync_file_menu();
}

SpectraNavRail& SpectraAppShell::spectra_nav_rail()
{
    ensure_initialized();
    return *spectra_nav_;
}

const SpectraNavRail& SpectraAppShell::spectra_nav_rail() const
{
    return *spectra_nav_;
}

SpectraCanvasHost& SpectraAppShell::spectra_canvas_host()
{
    ensure_initialized();
    return *spectra_canvas_;
}

bool SpectraAppShell::panel_visible(std::string_view id) const
{
    const Panel* panel = panels_.find(id);
    return panel && panel->visible();
}

bool SpectraAppShell::set_panel_visible(std::string_view id, bool visible)
{
    if (!panels_.set_visible(id, visible))
        return false;
    sync_panel_state_to_imgui();
    return true;
}

bool SpectraAppShell::toggle_panel(std::string_view id)
{
    if (!panels_.toggle(id))
        return false;
    sync_panel_state_to_imgui();
    return true;
}

std::map<std::string, bool> SpectraAppShell::capture_panel_visibility() const
{
    return panels_.capture_visibility();
}

void SpectraAppShell::apply_panel_visibility(const std::map<std::string, bool>& vis)
{
    panels_.apply_visibility(vis);
    sync_panel_state_to_imgui();
}

void SpectraAppShell::draw_nav_rail()
{
    ensure_initialized();
    if (spectra_nav_)
        spectra_nav_->draw();
}

void SpectraAppShell::draw_status_bar()
{
    ensure_initialized();
    sync_status_bar_segments();
    status_bar_.draw();
}

void SpectraAppShell::draw_menus_inline()
{
    if (!imgui_)
        return;

    menu_bar_.draw_inline(
        [this](const char* label, const std::vector<MenuAction>& items)
        {
            const auto menu_items = to_imgui_menu_items(items, menu_bar_.shortcut_resolver());
            imgui_->render_menubar_menu(label, menu_items);
        });
}

void SpectraAppShell::draw_registered_panels()
{
    if (!imgui_ || !current_figure_)
        return;
    panels_.draw_all();
}

void SpectraAppShell::sync_status_bar_segments()
{
    status_bar_.clear();
    if (imgui_)
        imgui_->populate_status_bar(status_bar_);
}

void SpectraAppShell::sync_panel_state_from_imgui()
{
    if (!imgui_)
        return;

    auto& lm = imgui_->get_layout_manager();
    if (auto* p = panels_.find("core.data"))
        p->set_visible(lm.is_left_panel_visible());
    if (auto* p = panels_.find("core.inspector"))
        p->set_visible(lm.is_inspector_visible());
    if (auto* p = panels_.find("core.timeline"))
        p->set_visible(imgui_->show_timeline_);
    if (auto* p = panels_.find("core.curve_editor"))
        p->set_visible(imgui_->show_curve_editor_);
    if (auto* p = panels_.find("core.plugins"))
        p->set_visible(imgui_->show_plugins_panel_);
    if (auto* p = panels_.find("core.topics"))
        p->set_visible(imgui_->topics_panel_ && imgui_->topics_panel_->is_visible());
    if (auto* p = panels_.find("core.logs"))
        p->set_visible(imgui_->log_browser_panel_ && imgui_->log_browser_panel_->is_visible());
}

void SpectraAppShell::sync_panel_state_to_imgui()
{
    if (!imgui_)
        return;

    auto& lm = imgui_->get_layout_manager();
    if (const Panel* p = panels_.find("core.data"))
    {
        if (p->visible() && !lm.is_left_panel_visible() && imgui_->data_sidebar_)
            imgui_->data_sidebar_->focus_search();
        lm.set_left_panel_visible(p->visible());
    }
    if (const Panel* p = panels_.find("core.inspector"))
    {
        lm.set_inspector_visible(p->visible());
        imgui_->panel_open_ = p->visible();
    }
    if (const Panel* p = panels_.find("core.timeline"))
        imgui_->show_timeline_ = p->visible();
    if (const Panel* p = panels_.find("core.curve_editor"))
        imgui_->show_curve_editor_ = p->visible();
    if (const Panel* p = panels_.find("core.plugins"))
        imgui_->show_plugins_panel_ = p->visible();
    if (const Panel* p = panels_.find("core.topics"))
    {
        if (imgui_->topics_panel_)
            imgui_->topics_panel_->set_visible(p->visible());
    }
    if (const Panel* p = panels_.find("core.logs"))
    {
        if (imgui_->log_browser_panel_)
            imgui_->log_browser_panel_->set_visible(p->visible());
    }
}

std::unique_ptr<CanvasHost> SpectraAppShell::create_canvas_host()
{
    return std::make_unique<SpectraCanvasHost>(imgui_, layout_manager_);
}

void SpectraAppShell::on_register_panels()
{
    if (!imgui_)
        return;

    auto add = [this](const char* id,
                      const char* title,
                      const char* category,
                      Icon        icon,
                      DockSlot    slot,
                      bool        default_visible,
                      auto        draw_fn)
    {
        PanelInfo info;
        info.id              = id;
        info.title           = title;
        info.category        = category;
        info.icon            = icon;
        info.slot            = slot;
        info.default_visible = default_visible;
        panels().add(std::make_unique<CallbackPanel>(std::move(info), std::move(draw_fn)));
    };

    // Data sidebar (src/ui/data_sidebar/): docked left of the canvas; its
    // visibility lives in LayoutManager (see the sync functions).
    add("core.data",
        "Data",
        "Panels",
        Icon::Database,
        DockSlot::Left,
        false,
        [this](bool* /*p_open*/)
        {
            if (!imgui_->data_sidebar_)
                return;
            // ImGuiIntegration replaces its LayoutManager on init.
            imgui_->data_sidebar_->set_layout_manager(&imgui_->get_layout_manager());
            imgui_->data_sidebar_->draw(imgui_->font_heading_);
        });

    add("core.inspector",
        "Inspector",
        "Panels",
        Icon::Axes,
        DockSlot::Right,
        true,
        [this](bool* p_open)
        {
            if (!imgui_ || !current_figure_)
                return;
            if (p_open && !*p_open)
            {
                imgui_->get_layout_manager().set_inspector_visible(false);
                imgui_->panel_open_ = false;
                return;
            }
            imgui_->draw_inspector(*current_figure_);
        });

    add("core.timeline",
        "Timeline",
        "Panels",
        Icon::Timeline,
        DockSlot::Bottom,
        false,
        [this](bool* p_open)
        {
            if (p_open && !*p_open)
                imgui_->show_timeline_ = false;
            if (imgui_->show_timeline_ && imgui_->timeline_editor_)
                imgui_->draw_timeline_panel();
        });

    add("core.curve_editor",
        "Curve Editor",
        "Panels",
        Icon::ChartLine,
        DockSlot::Floating,
        false,
        [this](bool* p_open)
        {
            if (p_open && !*p_open)
                imgui_->show_curve_editor_ = false;
            if (imgui_->show_curve_editor_ && imgui_->curve_editor_)
                imgui_->draw_curve_editor_panel();
        });

    add("core.plugins",
        "Plugins",
        "Panels",
        Icon::Wrench,
        DockSlot::Floating,
        false,
        [this](bool* p_open)
        {
            if (p_open && !*p_open)
                imgui_->show_plugins_panel_ = false;
            imgui_->draw_plugins_panel();
        });

    add("core.topics",
        "Topics",
        "Panels",
        Icon::Broadcast,
        DockSlot::Floating,
        false,
        [this](bool* /*p_open*/)
        {
            if (imgui_->topics_panel_)
                imgui_->topics_panel_->draw();
        });

    add("core.logs",
        "Log Browser",
        "Panels",
        Icon::FolderOpen,
        DockSlot::Floating,
        false,
        [this](bool* /*p_open*/)
        {
            if (imgui_->log_browser_panel_)
                imgui_->log_browser_panel_->draw();
        });

    add("core.settings",
        "Settings",
        "Panels",
        Icon::Settings,
        DockSlot::Floating,
        false,
        [this](bool* /*p_open*/)
        {
            if (imgui_->settings_panel_)
                imgui_->settings_panel_->draw();
        });
}

std::string SpectraAppShell::panel_command_id(const std::string& panel_id)
{
    constexpr std::string_view prefix = "core.";
    if (panel_id == "core.settings")
        return "panel.open_settings";
    if (panel_id.rfind(prefix, 0) == 0)
        return "panel.toggle_" + panel_id.substr(prefix.size());
    return {};
}

PanelMenuHooks SpectraAppShell::panel_menu_hooks()
{
    PanelMenuHooks hooks;
    hooks.command_id = [](const std::string& id) { return panel_command_id(id); };
    hooks.is_visible = [this](const std::string& id)
    {
        // SettingsPanel keeps its own visibility flag (see SpectraNavRail::build_items).
        if (id == "core.settings")
            return imgui_ && imgui_->settings_panel() && imgui_->settings_panel()->is_visible();
        return panel_visible(id);
    };
    hooks.toggle = [this](const std::string& id)
    {
        if (!imgui_)
            return;
        if (id == "core.settings")
        {
            if (auto* sp = imgui_->settings_panel())
                sp->set_visible(!sp->is_visible());
            return;
        }
        // Prefer the registered command so the menu, nav rail and keyboard shortcut
        // share one code path (including undo).
        const std::string cmd = panel_command_id(id);
        if (imgui_->command_registry_ && imgui_->command_registry_->find(cmd))
            imgui_->command_registry_->execute(cmd);
        else
            toggle_panel(id);
    };
    return hooks;
}

void SpectraAppShell::on_populate_menus(MenuBar& bar)
{
    if (!imgui_)
        return;

    CoreMenuHooks hooks;
    hooks.panel_visible     = [this](std::string_view id) { return panel_visible(id); };
    hooks.set_panel_visible = [this](std::string_view id, bool v) { set_panel_visible(id, v); };
    populate_core_menus(bar, imgui_, hooks);

    menus_populated_ = true;
}

void SpectraAppShell::populate_core_menus(MenuBar&                   bar,
                                          spectra::ImGuiIntegration* imgui,
                                          const CoreMenuHooks&       hooks)
{
    if (!imgui)
        return;

    bar.set_shortcut_resolver(
        [imgui](const std::string& command_id) {
            return menu_shortcut_text(imgui->shortcut_manager_,
                                      imgui->command_registry_,
                                      command_id);
        });

    // Menu item that runs a registry command and shows that command's shortcut.
    auto command = [imgui](const char* label, const char* command_id)
    {
        MenuAction action;
        action.label      = label;
        action.command_id = command_id;
        action.on_click   = [imgui, id = std::string(command_id)]()
        {
            if (imgui->command_registry_)
                imgui->command_registry_->execute(id);
        };
        return action;
    };

    // Same, greyed out while no figure is open (welcome screen): there is
    // nothing to save, export or close.
    auto figure_command = [imgui, &command](const char* label, const char* command_id)
    {
        MenuAction action = command(label, command_id);
        action.enabled    = [imgui]() { return imgui->current_figure_ != nullptr; };
        return action;
    };

    auto panel_visible = [hooks](std::string_view id)
    { return hooks.panel_visible && hooks.panel_visible(id); };

    // File: one save concept (the window's .spectra file); everything that
    // writes another format lives under Export.
    auto& file = bar.menu("File");
    file.add(command("New Figure", "figure.new"));
    file.add(command("Open...", "file.open"));
    file.add(command("Open Log...", "file.open_log"));
    {
        MenuAction recent;
        recent.label   = RECENT_LAYOUTS_MENU;   // filled by sync_recent_layouts_menu()
        recent.icon    = static_cast<uint16_t>(Icon::Clock);
        recent.cascade = true;
        recent.submenu.push_back({.label = "No Recent Files", .enabled = []() { return false; }});
        file.add(std::move(recent));
    }
    file.add_separator();
    file.add(figure_command("Save", "file.save"));
    file.add(figure_command("Save As...", "file.save_as"));
    {
        MenuAction exp;
        exp.label   = EXPORT_MENU;
        exp.icon    = static_cast<uint16_t>(Icon::Export);
        exp.cascade = true;
        exp.submenu.push_back(figure_command("PNG Image...", "file.export_png"));
        exp.submenu.push_back(figure_command("SVG Image...", "file.export_svg"));
        exp.submenu.push_back(figure_command("PDF Document...", "file.export_pdf"));
        exp.submenu.push_back(figure_command("CSV Data...", "file.export_csv"));
        exp.submenu.push_back({.separator = true});
        exp.submenu.push_back(command("Layout (bindings only)...", "file.save_layout"));
        file.add(std::move(exp));
    }
    file.add(figure_command("Copy as Image", "file.copy_to_clipboard"));
    file.add_separator();
    file.add(command("Data Bindings", "file.data_bindings"));
    file.add_separator();
    file.add(figure_command("Close Figure", "figure.close"));
    file.add(command("Exit", "app.quit"));

    auto& edit = bar.menu("Edit");
    edit.add(command("Undo", "edit.undo"));
    edit.add(command("Redo", "edit.redo"));

    auto& view = bar.menu("View");
    view.add(command("Zoom to Fit", "view.autofit"));
    view.add(command("Reset View", "view.reset"));
    view.add(command("Previous View", "view.back"));
    view.add(command("Next View", "view.forward"));
    view.add(command("Toggle Grid", "view.toggle_grid"));
    view.add(command("Toggle Legend", "view.toggle_legend"));
    // Crosshair readouts: the compare-at-X tooltip and how it reads the series.
    view.add(make_toggle_action(
        "Crosshair",
        [imgui]()
        { return imgui->data_interaction_ && imgui->data_interaction_->crosshair_active(); },
        [imgui]()
        {
            if (imgui->data_interaction_)
                imgui->data_interaction_->toggle_crosshair();
        },
        Icon::Crosshair));
    view.add(make_toggle_action(
        "3D Spike Lines",
        [imgui]()
        { return imgui->data_interaction_ && imgui->data_interaction_->spikes3d_active(); },
        [imgui]()
        {
            if (imgui->command_registry_)
                imgui->command_registry_->execute("view.toggle_spikes_3d");
        },
        Icon::Crosshair));
    view.add(make_toggle_action(
        "Compare at X",
        [imgui]()
        {
            return imgui->data_interaction_ && imgui->data_interaction_->crosshair_active()
                   && imgui->data_interaction_->compare_enabled();
        },
        [imgui]()
        {
            if (imgui->command_registry_)
                imgui->command_registry_->execute("view.toggle_compare_at_x");
        },
        Icon::ChartLine));
    view.add(make_toggle_action(
        "Compare: Nearest Sample",
        [imgui]()
        {
            return imgui->data_interaction_
                   && imgui->data_interaction_->compare_mode()
                          == spectra::probe::ProbeMode::Nearest;
        },
        [imgui]()
        {
            if (imgui->command_registry_)
                imgui->command_registry_->execute("view.compare_toggle_nearest");
        },
        Icon::LineDotted));
    view.add(make_toggle_action(
        "Compare: Sort by Value",
        [imgui]()
        {
            return imgui->data_interaction_
                   && imgui->data_interaction_->compare_sort()
                          == spectra::probe::CompareSort::ValueDescending;
        },
        [imgui]()
        {
            if (imgui->command_registry_)
                imgui->command_registry_->execute("view.compare_toggle_sort");
        },
        Icon::List));
    // Undoable, like "Clear all" in the nav rail's data-tip popover.
    view.add({.label = "Remove All Data Tips",
              .on_click =
                  [imgui]()
              {
                  if (imgui->data_interaction_ && imgui->current_figure_)
                      clear_figure_markers_undoable(*imgui->data_interaction_,
                                                    *imgui->current_figure_,
                                                    imgui->undo_manager_,
                                                    [imgui]() { return imgui->current_figure_; });
              },
              .enabled =
                  [imgui]()
              {
                  return imgui->data_interaction_ && imgui->current_figure_
                         && !figure_marker_indices(imgui->data_interaction_->markers(),
                                                   *imgui->current_figure_)
                                 .empty();
              }});
    {
        // Colour vision deficiency preview: view only, never exported.
        MenuAction cvd;
        cvd.label = "Color Vision Preview";
        cvd.icon  = static_cast<uint16_t>(Icon::Eye);
        for (ui::CVDType type : {ui::CVDType::None,
                                 ui::CVDType::Protanopia,
                                 ui::CVDType::Deuteranopia,
                                 ui::CVDType::Tritanopia,
                                 ui::CVDType::Achromatopsia})
        {
            MenuAction item = command(
                type == ui::CVDType::None ? "Off (Normal Vision)" : ui::cvd_type_label(type),
                "");
            item.command_id = std::string("view.cvd.") + ui::cvd_type_id(type);
            item.on_click   = [imgui, id = item.command_id]()
            {
                if (imgui->command_registry_)
                    imgui->command_registry_->execute(id);
            };
            item.checked = [imgui, type]()
            { return imgui->theme_mgr_ && imgui->theme_mgr_->cvd_preview() == type; };
            cvd.submenu.push_back(std::move(item));
        }
        view.add(std::move(cvd));
    }
    view.add_separator();
    // One checkable item per panel. The registry panels (Inspector, Timeline,
    // Curve Editor, Plugins, Topics, Settings) are appended after these by
    // MenuBar::bind_panel_registry (or add_core_panel_menu on the fallback path)
    // with their nav-rail icons; the items below are views that are not panels.
    view.add(make_toggle_action(
        "Navigation Rail",
        [imgui]() { return imgui->show_nav_rail_; },
        [imgui]() { imgui->set_nav_rail_visible(!imgui->show_nav_rail_); },
        Icon::Menu));
    view.add(make_toggle_action(
        "Data Editor",
        [imgui, panel_visible]()
        {
            return panel_visible("core.inspector")
                   && imgui->active_section_ == spectra::ImGuiIntegration::Section::DataEditor;
        },
        [imgui, panel_visible, hooks]()
        {
            const bool showing =
                panel_visible("core.inspector")
                && imgui->active_section_ == spectra::ImGuiIntegration::Section::DataEditor;
            if (!showing)
                imgui->active_section_ = spectra::ImGuiIntegration::Section::DataEditor;
            if (hooks.set_panel_visible)
                hooks.set_panel_visible("core.inspector", !showing);
        },
        Icon::Database));
    {
        MenuAction params = make_toggle_action(
            "Parameters",
            [imgui]()
            {
                return imgui->knob_manager_ && !imgui->knob_manager_->empty()
                       && imgui->knob_manager_->is_visible();
            },
            [imgui]()
            {
                if (imgui->knob_manager_)
                    imgui->knob_manager_->set_visible(!imgui->knob_manager_->is_visible());
            },
            Icon::Function);
        params.enabled = [imgui]()
        { return imgui->knob_manager_ && !imgui->knob_manager_->empty(); };
        view.add(std::move(params));
    }

    auto& tools = bar.menu("Tools");
    tools.add(command("Screenshot (PNG)", "file.export_png"));
    tools.add_separator();
    {
        // Appearance (theme, palette, colour vision preview, glass) lives in
        // Settings; this opens it on that tab.
        MenuAction appearance = command("Appearance...", "panel.open_appearance");
        appearance.icon       = static_cast<uint16_t>(Icon::Palette);
        tools.add(std::move(appearance));
    }
    tools.add(command("Command Palette", "app.command_palette"));

    auto& plot = bar.menu("Plot");
    plot.add(command("Y = 0 Line", "plot.hline_zero"));
    plot.add(command("X = 0 Line", "plot.vline_zero"));
    plot.add_separator();
    plot.add(command("Horizontal Line...", "plot.hline"));
    plot.add(command("Vertical Line...", "plot.vline"));
    plot.add(command("Plot Function...", "plot.function"));
}

const std::vector<SpectraAppShell::CorePanelEntry>& SpectraAppShell::core_panels()
{
    static const std::vector<CorePanelEntry> panels = {
        {"core.data", "Data", Icon::Database},
        {"core.inspector", "Inspector", Icon::Axes},
        {"core.timeline", "Timeline", Icon::Timeline},
        {"core.curve_editor", "Curve Editor", Icon::ChartLine},
        {"core.plugins", "Plugins", Icon::Wrench},
        {"core.topics", "Topics", Icon::Broadcast},
        {"core.logs", "Log Browser", Icon::FolderOpen},
        {"core.settings", "Settings", Icon::Settings},
    };
    return panels;
}

void SpectraAppShell::add_core_panel_menu(MenuBar&                   bar,
                                          spectra::ImGuiIntegration* imgui,
                                          const PanelMenuHooks&      hooks)
{
    // Same shape as MenuBar::bind_panel_registry builds from the shell's
    // PanelRegistry: a "Panels" submenu (flattened into its own section when
    // drawn) with one checkable item per core panel.
    Menu& view = bar.menu("View");
    view.remove_submenu("Panels");

    MenuAction panels_action;
    panels_action.label = "Panels";
    for (const CorePanelEntry& entry : core_panels())
    {
        const std::string id = entry.id;
        MenuAction        item;
        item.label      = entry.title;
        item.icon       = static_cast<uint16_t>(entry.icon);
        item.command_id = panel_command_id(id);
        if (hooks.is_visible)
            item.checked = [is_visible = hooks.is_visible, id]() { return is_visible(id); };
        if (hooks.toggle)
        {
            item.on_click = [toggle = hooks.toggle, id]() { toggle(id); };
        }
        else
        {
            item.on_click = [imgui, cmd = item.command_id]()
            {
                if (imgui && imgui->command_registry_)
                    imgui->command_registry_->execute(cmd);
            };
        }
        panels_action.submenu.push_back(std::move(item));
    }
    view.add(std::move(panels_action));
}

void SpectraAppShell::sync_file_menu()
{
    if (!imgui_ || !menus_populated_)
        return;
    sync_plugin_export_menu(menu_bar_, imgui_);
    sync_recent_layouts_menu(menu_bar_, imgui_);
}

void SpectraAppShell::sync_recent_layouts_menu(MenuBar& bar, spectra::ImGuiIntegration* imgui)
{
    if (!imgui || !bar.has_menu("File"))
        return;
    spectra::LayoutSession*               session = imgui->layout_session();
    spectra::ui::settings::SettingsStore* store   = imgui->settings_store();

    // The start page's list: recent data files, then .spectra workspaces and
    // layouts (opened, saved or restored).
    static const std::vector<std::string>         none;
    const std::vector<spectra::WelcomeRecentItem> recent =
        spectra::build_welcome_recent(store ? store->data().recent_files : none,
                                      session ? session->recent().entries() : none);

    std::vector<MenuAction> items;
    if (recent.empty())
    {
        items.push_back({.label = "No Recent Files", .enabled = []() { return false; }});
    }
    else
    {
        for (const auto& item : recent)
        {
            const std::string path = item.path;
            // Deferred: opening edits the recent lists this menu is built from.
            std::function<void()> open =
                item.layout && session
                    ? std::function<void()>([session, path]() { session->request_open(path); })
                    : std::function<void()>([imgui, path]() { imgui->request_open_path(path); });
            items.push_back(
                {.label    = item.name,
                 .shortcut = item.folder,
                 .icon     = static_cast<uint16_t>(item.layout ? Icon::Layout : Icon::FileText),
                 .on_click = std::move(open)});
        }
        items.push_back({.separator = true});
        items.push_back({.label    = "Clear Recent",
                         .on_click = [session, store]()
                         {
                             if (session)
                                 session->recent().clear();
                             if (store)
                                 store->clear_recent_files();
                         }});
    }

    for (auto& action : bar.menu("File").items_mut())
    {
        if (action.label == RECENT_LAYOUTS_MENU)
        {
            action.submenu = std::move(items);
            break;
        }
    }
}

void SpectraAppShell::sync_plugin_export_menu(MenuBar& bar, spectra::ImGuiIntegration* imgui)
{
    if (!imgui)
        return;

    auto& file = bar.menu("File");
    file.remove_submenu("##plugin_exports");

    // Plugin formats go at the end of File > Export (or, in a File menu
    // without one, into their own section).
    MenuAction* export_menu = nullptr;
    for (auto& action : file.items_mut())
    {
        if (action.label == EXPORT_MENU)
            export_menu = &action;
    }
    if (export_menu)
    {
        auto& sub = export_menu->submenu;
        sub.erase(std::remove_if(sub.begin(),
                                 sub.end(),
                                 [](const MenuAction& a) {
                                     return a.label == "##plugin_exports"
                                            || a.command_id.rfind("file.export_plugin.", 0) == 0;
                                 }),
                  sub.end());
    }
    if (!imgui->export_format_registry_)
        return;

    const auto formats = imgui->export_format_registry_->available_formats();
    if (formats.empty())
        return;

    std::vector<MenuAction> plugin_items;
    for (const auto& fmt : formats)
    {
        const std::string name  = fmt.name;
        const std::string label = export_menu ? fmt.name + " (." + fmt.extension + ")..."
                                              : "Export " + fmt.name + " (." + fmt.extension + ")";
        plugin_items.push_back({.label      = label,
                                .command_id = "file.export_plugin." + name,
                                .on_click   = [imgui, name]()
                                {
                                    if (imgui->command_registry_)
                                        imgui->command_registry_->execute("file.export_plugin."
                                                                          + name);
                                }});
    }
    if (export_menu)
    {
        export_menu->submenu.push_back({.label = "##plugin_exports", .separator = true});
        for (auto& item : plugin_items)
            export_menu->submenu.push_back(std::move(item));
        return;
    }
    MenuAction plugin_parent;
    plugin_parent.label   = "##plugin_exports";
    plugin_parent.submenu = std::move(plugin_items);
    file.add(std::move(plugin_parent));
}

void SpectraAppShell::on_populate_nav_rail(NavRail& /*rail*/) {}

void SpectraAppShell::on_build_status_bar(StatusBar& /*bar*/) {}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

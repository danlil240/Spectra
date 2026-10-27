#include "px4_app_shell.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #ifdef IMGUI_HAS_DOCK
        #include <imgui_internal.h>
    #endif
    #include "ui/imgui/widgets.hpp"
    #include "ui/shell/canvas_host.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/nav_rail.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/shell/status_bar.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/icons.hpp"
    #include "ui/theme/theme.hpp"

    #include <algorithm>
    #include <array>
    #include <filesystem>
#endif

namespace spectra::adapters::px4
{

#ifdef SPECTRA_USE_IMGUI
namespace
{
using spectra::ui::Icon;
using spectra::ui::shell::CallbackPanel;
using spectra::ui::shell::DockSlot;
using spectra::ui::shell::PanelInfo;

std::string file_display_name(const std::string& path)
{
    const std::string name = std::filesystem::path(path).filename().string();
    return name.empty() ? path : name;
}
}   // namespace

// Canvas host that shows an "Open ULog" empty state while nothing is loaded.
class Px4CanvasHost : public spectra::ui::shell::CanvasHost
{
   public:
    Px4CanvasHost(Px4AppShell& shell, spectra::LayoutManager* lm) : CanvasHost(lm), shell_(shell) {}

   protected:
    void draw_empty_state() override
    {
        if (shell_.empty_state_visible())
            shell_.draw_canvas_empty_state(canvas_rect());
    }

   private:
    Px4AppShell& shell_;
};

std::unique_ptr<spectra::ui::shell::CanvasHost> Px4AppShell::create_canvas_host()
{
    return std::make_unique<Px4CanvasHost>(*this, layout_manager());
}

void Px4AppShell::draw_canvas_empty_state(const spectra::Rect& region)
{
    if (region.w < 1.0f || region.h < 1.0f)
        return;

    namespace tokens = spectra::ui::tokens;
    // Approximate height of icon + title + two-line subtitle + buttons + hint,
    // used to centre the block vertically in the canvas.
    constexpr float kBlockHeight = 190.0f;

    ImGui::SetCursorScreenPos(ImVec2(region.x, region.y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(tokens::SPACE_4, tokens::SPACE_4));
    if (ImGui::BeginChild("##px4_empty_state",
                          ImVec2(region.w, region.h),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                              | ImGuiWindowFlags_NoBackground))
    {
        ImGui::SetCursorPosY(std::max(tokens::SPACE_4, (region.h - kBlockHeight) * 0.5f));

        const std::array<spectra::ui::widgets::EmptyStateAction, 2> actions{{
            {.label = "Open ULog\xe2\x80\xa6", .id = "open_ulog", .primary = true},
            {.label = "Connect to PX4", .id = "connect_live", .primary = false},
        }};
        const int clicked = spectra::ui::widgets::empty_state(
            Icon::FileText,
            "No flight log loaded",
            "Open a PX4 .ulg file to browse its topics and plot attitude, altitude, velocity "
            "and battery, or connect to a vehicle over MAVLink.",
            actions);
        if (clicked == 0)
            open_ulog_with_dialog();
        else if (clicked == 1)
            connect_live();

        const std::string hint =
            "Live link: MAVLink UDP " + cfg_.host + ":" + std::to_string(cfg_.port);
        const auto&  c       = spectra::ui::theme();
        const ImVec2 hint_sz = ImGui::CalcTextSize(hint.c_str());
        ImGui::Dummy(ImVec2(0.0f, tokens::SPACE_3));
        ImGui::SetCursorPosX(std::max(0.0f, (region.w - hint_sz.x) * 0.5f));
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(c.text_tertiary.r, c.text_tertiary.g, c.text_tertiary.b, 0.7f));
        ImGui::TextUnformatted(hint.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Px4AppShell::on_register_panels()
{
    auto add_panel = [this](const char* id,
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

    add_panel("px4.ulog_file",
              "ULog File",
              "Data",
              Icon::FileText,
              DockSlot::Left,
              true,
              [this](bool* p_open) { draw_ulog_file(p_open); });

    add_panel("px4.live_connection",
              "PX4 Live",
              "Connection",
              Icon::Link,
              DockSlot::Left,
              false,
              [this](bool* p_open) { draw_live_connection(p_open); });

    for (const auto& [id, visible] : pending_panel_visibility_)
        panels().set_visible(id, visible);
}

void Px4AppShell::on_populate_menus(spectra::ui::shell::MenuBar& bar)
{
    auto& file = bar.menu("File");
    file.add({.label = "Open ULog...", .shortcut = "Ctrl+O", .on_click = [this]() { open_ulog_with_dialog(); }});
    file.add_separator();
    file.add({.label = "Exit", .shortcut = "Ctrl+Q", .on_click = [this]() { request_shutdown(); }});

    auto& plots = bar.menu("Plots");
    plots.add(
        {.label    = "Auto Plot",
         .on_click = [this]() { auto_plot_ulog(); },
         .enabled  = [this]() { return reader_.is_open(); }});
    plots.add_separator();
    plots.add(
        {.label    = "Close All Plots",
         .on_click = [this]() { close_all_plots(); },
         .enabled  = [this]() { return auto_plot_active_ || plot_mgr_.field_count() > 0; }});

    auto& view = bar.menu("View");
    view.add(
        {.label    = "Navigation Rail",
         .on_click =
             [this]()
             {
                 set_nav_rail_visible(!nav_rail_visible());
                 sync_layout_chrome();
             },
         .checked = [this]() { return nav_rail_visible(); }});
    view.add(
        {.label    = "Expand Rail",
         .on_click =
             [this]()
             {
                 set_nav_rail_expanded(!nav_rail_expanded());
                 sync_layout_chrome();
             },
         .enabled = [this]() { return nav_rail_visible(); },
         .checked = [this]() { return nav_rail_expanded(); }});
    view.add_separator();
    view.add({.label = "Reset Dock Layout", .on_click = [this]() { request_dock_layout_reset(); }});
    view.add_separator();

    if (file_panel_)
    {
        view.add(
            {.label    = "Detach ULog Panel",
             .on_click =
                 [this]()
                 {
                     if (file_panel_->is_detached())
                         file_panel_->attach();
                     else
                         file_panel_->detach();
                 },
             .checked = [this]() { return file_panel_->is_detached(); }});
    }
    if (live_panel_)
    {
        view.add(
            {.label    = "Detach Live Panel",
             .on_click =
                 [this]()
                 {
                     if (live_panel_->is_detached())
                         live_panel_->attach();
                     else
                         live_panel_->detach();
                 },
             .checked = [this]() { return live_panel_->is_detached(); }});
    }

    auto& connection = bar.menu("Connection");
    connection.add({.label    = "Connect",
                    .on_click = [this]() { connect_live(); },
                    .enabled  = [this]() { return !bridge_.is_connected(); }});
    connection.add(
        {.label    = "Disconnect",
         .on_click =
             [this]()
             {
                 bridge_.shutdown();
                 plot_mgr_.set_bridge(nullptr);
             },
         .enabled = [this]() { return bridge_.is_connected(); }});
}

void Px4AppShell::on_populate_nav_rail(spectra::ui::shell::NavRail& rail)
{
    // Custom items already cover every registered panel; listing the registry
    // too would show each panel twice.
    rail.set_show_registry_panels(false);

    spectra::ui::shell::NavItem ulog_item;
    ulog_item.id        = "px4.nav.ulog_file";
    ulog_item.label     = "ULog File";
    ulog_item.icon      = Icon::FileText;
    ulog_item.is_active = [this]() { return panel_visible("px4.ulog_file"); };
    ulog_item.on_click  = [this]() { panels().toggle("px4.ulog_file"); };
    rail.add_custom_item(std::move(ulog_item));

    spectra::ui::shell::NavItem live_item;
    live_item.id        = "px4.nav.live_connection";
    live_item.label     = "PX4 Live";
    live_item.icon      = Icon::Link;
    live_item.is_active = [this]() { return panel_visible("px4.live_connection"); };
    live_item.on_click  = [this]() { panels().toggle("px4.live_connection"); };
    rail.add_custom_item(std::move(live_item));

    spectra::ui::shell::NavItem sep;
    sep.label             = "##px4_nav_sep";
    sep.is_section_header = true;
    rail.add_custom_item(std::move(sep));

    spectra::ui::shell::NavItem reset_item;
    reset_item.id       = "px4.nav.reset_layout";
    reset_item.label    = "Reset Layout";
    reset_item.icon     = Icon::Refresh;
    reset_item.on_click = [this]() { request_dock_layout_reset(); };
    rail.add_custom_item(std::move(reset_item));
}

void Px4AppShell::on_default_layout(unsigned int dockspace_id)
{
    #ifdef IMGUI_HAS_DOCK
    if (dockspace_id == 0)
        return;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetWindowSize());

    ImGuiID dock_left = 0;
    ImGuiID dock_main = 0;
    ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.30f, &dock_left, &dock_main);

    ImGui::DockBuilderDockWindow("ULog File", dock_left);
    ImGui::DockBuilderDockWindow("PX4 Live", dock_left);

    ImGui::DockBuilderFinish(dockspace_id);

    if (file_panel_)
        file_panel_->set_dock_id(dockspace_id);
    if (live_panel_)
        live_panel_->set_dock_id(dockspace_id);
    #else
    (void)dockspace_id;
    if (file_panel_)
        file_panel_->set_dock_id(dockspace_id);
    if (live_panel_)
        live_panel_->set_dock_id(dockspace_id);
    #endif
}

void Px4AppShell::on_build_status_bar(spectra::ui::shell::StatusBar& bar)
{
    bar.clear();

    bar.add_segment(
        {.align   = spectra::ui::shell::StatusAlign::Left,
         .draw_fn = [this]()
         {
             if (!last_open_error_.empty())
             {
                 ImGui::Text("ULog open failed: %s", last_open_error_.c_str());
                 ImGui::SameLine();
             }

             if (reader_.is_open())
             {
                 ImGui::Text("ULog: %s | Duration: %.1fs | Topics: %zu | Messages: %zu",
                             file_display_name(reader_.metadata().path).c_str(),
                             reader_.metadata().duration_sec(),
                             reader_.topic_count(),
                             reader_.metadata().message_count);
             }

             if (bridge_.is_receiving())
             {
                 if (reader_.is_open())
                     ImGui::SameLine();
                 ImGui::Text("Live: %s:%d (%.0f msg/s)",
                             bridge_.host().c_str(),
                             bridge_.port(),
                             bridge_.message_rate());
             }

             if (reader_.is_open() || bridge_.is_receiving())
                 ImGui::SameLine();
             ImGui::Text("Fields: %zu", plot_mgr_.field_count());
         }});
}
#endif

}   // namespace spectra::adapters::px4

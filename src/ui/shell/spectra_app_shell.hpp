#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <functional>
    #include <map>
    #include <memory>
    #include <string>
    #include <string_view>
    #include <vector>

    #include "ui/shell/app_shell.hpp"
    #include "ui/theme/icons.hpp"

namespace spectra
{
class Figure;
class ImGuiIntegration;
}   // namespace spectra

namespace spectra::ui::shell
{
class SpectraNavRail;
class SpectraCanvasHost;

/// Core Spectra app shell — routes ImGuiIntegration chrome through shared shell components.
class SpectraAppShell : public AppShell
{
   public:
    explicit SpectraAppShell(spectra::ImGuiIntegration* imgui = nullptr);

    void bind_imgui(spectra::ImGuiIntegration* imgui);
    void set_layout_manager(spectra::LayoutManager* lm);
    void set_current_figure(spectra::Figure* figure);

    void ensure_initialized();
    void sync_before_frame();

    spectra::Figure* current_figure() const { return current_figure_; }

    void draw_nav_rail();
    void draw_status_bar();
    void draw_menus_inline();
    void draw_registered_panels();

    SpectraNavRail&       spectra_nav_rail();
    const SpectraNavRail& spectra_nav_rail() const;
    SpectraCanvasHost&    spectra_canvas_host();

    bool panel_visible(std::string_view id) const;
    bool set_panel_visible(std::string_view id, bool visible);
    bool toggle_panel(std::string_view id);

    std::map<std::string, bool> capture_panel_visibility() const;
    void                        apply_panel_visibility(const std::map<std::string, bool>& vis);

    // Command that toggles a core panel ("core.timeline" -> "panel.toggle_timeline").
    static std::string panel_command_id(const std::string& panel_id);

    // ── Core menu model ────────────────────────────────────────────────
    // One menu definition shared by the app shell and ImGuiIntegration's
    // fallback path (no shell bound), so both show the same items.
    struct CoreMenuHooks
    {
        std::function<bool(std::string_view panel_id)>               panel_visible{};
        std::function<void(std::string_view panel_id, bool visible)> set_panel_visible{};
    };
    struct CorePanelEntry
    {
        const char* id;
        const char* title;
        Icon        icon;
    };

    // File / Edit / View / Tools / Plot menus (without the "Panels" section).
    static void populate_core_menus(MenuBar&                   bar,
                                    spectra::ImGuiIntegration* imgui,
                                    const CoreMenuHooks&       hooks);
    // The core panels in menu / nav-rail order.
    static const std::vector<CorePanelEntry>& core_panels();
    // View > Panels section for hosts without a PanelRegistry (fallback path).
    static void add_core_panel_menu(MenuBar&                   bar,
                                    spectra::ImGuiIntegration* imgui,
                                    const PanelMenuHooks&      hooks);
    // Plugin export formats under File (rebuilt each frame; formats can change).
    static void sync_plugin_export_menu(MenuBar& bar, spectra::ImGuiIntegration* imgui);

   protected:
    void on_register_panels() override;
    void on_populate_menus(MenuBar& bar) override;
    PanelMenuHooks              panel_menu_hooks() override;
    void on_populate_nav_rail(NavRail& rail) override;
    void on_build_status_bar(StatusBar& bar) override;
    std::unique_ptr<CanvasHost> create_canvas_host() override;

   private:
    void sync_panel_state_from_imgui();
    void sync_panel_state_to_imgui();
    void sync_status_bar_segments();
    void sync_file_menu();

    spectra::ImGuiIntegration*  imgui_          = nullptr;
    spectra::Figure*            current_figure_ = nullptr;
    std::unique_ptr<SpectraNavRail>   spectra_nav_;
    SpectraCanvasHost*                spectra_canvas_ = nullptr;
    bool menus_populated_ = false;
};

/// Nav rail with core tool-mode buttons + registry panel toggles (Vision icon+label style).
class SpectraNavRail : public NavRail
{
   public:
    SpectraNavRail(spectra::ImGuiIntegration* imgui, PanelRegistry* registry);

    void build_items(std::vector<NavItem>& out) const override;
    void draw() override;

   private:
    spectra::ImGuiIntegration* imgui_ = nullptr;
};

/// Canvas host wrapping core draw_canvas (glass frame, welcome screen, scrollbar).
class SpectraCanvasHost : public CanvasHost
{
   public:
    SpectraCanvasHost(spectra::ImGuiIntegration* imgui, spectra::LayoutManager* lm);

    void draw() override;

   private:
    spectra::ImGuiIntegration* imgui_ = nullptr;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

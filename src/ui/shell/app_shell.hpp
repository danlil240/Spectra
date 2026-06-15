#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <memory>
    #include <string>

    #include "ui/shell/canvas_host.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/nav_rail.hpp"
    #include "ui/shell/panel_registry.hpp"
    #include "ui/shell/status_bar.hpp"

namespace spectra
{
class LayoutManager;
}

namespace spectra::ui::shell
{
struct AppShellConfig
{
    bool        nav_rail   = true;
    bool        menu_bar   = true;
    bool        status_bar = true;
    bool        dockspace  = true;
    std::string app_name   = "Spectra";
};

class AppShell
{
   public:
    explicit AppShell(AppShellConfig cfg = {});
    virtual ~AppShell();

    AppShell(const AppShell&)            = delete;
    AppShell& operator=(const AppShell&) = delete;

    void                    set_layout_manager(spectra::LayoutManager* lm);
    spectra::LayoutManager* layout_manager() const;

    void initialize();
    void draw_frame();

    bool is_initialized() const { return initialized_; }

    PanelRegistry& panels();
    NavRail&       nav_rail();
    MenuBar&       menu_bar();
    StatusBar&     status_bar();
    CanvasHost&    canvas_host();

   protected:
    virtual void on_register_panels() {}
    virtual void on_populate_menus(MenuBar&) {}
    virtual void on_populate_nav_rail(NavRail&) {}
    virtual void on_build_status_bar(StatusBar&) {}
    virtual void on_default_layout(unsigned int dockspace_id) {}
    virtual void on_draw_canvas() {}

    void draw_dockspace();

    AppShellConfig              config_;
    PanelRegistry               panels_;
    NavRail                     nav_rail_;
    MenuBar                     menu_bar_;
    StatusBar                   status_bar_;
    std::unique_ptr<CanvasHost> canvas_host_;
    spectra::LayoutManager*     layout_manager_          = nullptr;
    bool                        initialized_             = false;
    bool                        dock_layout_initialized_ = false;
    unsigned int                dockspace_id_            = 0;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

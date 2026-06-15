#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <functional>
    #include <string>
    #include <string_view>
    #include <vector>

namespace spectra::ui::shell
{
struct MenuAction
{
    std::string             label;
    std::string             shortcut;
    std::function<void()>   on_click;
    std::function<bool()>   enabled;
    std::function<bool()>   checked;
    bool                    separator = false;
    std::vector<MenuAction> submenu;
};

class Menu
{
   public:
    explicit Menu(std::string name);

    const std::string&             name() const;
    Menu&                          add(MenuAction action);
    void                           add_separator();
    const std::vector<MenuAction>& items() const;
    std::vector<MenuAction>&       items_mut();

   private:
    std::string             name_;
    std::vector<MenuAction> items_;
};

class PanelRegistry;

class MenuBar
{
   public:
    Menu&                    menu(std::string_view name);
    bool                     has_menu(std::string_view name) const;
    std::vector<std::string> menu_names() const;

    void bind_panel_registry(PanelRegistry&   registry,
                             std::string_view top_level     = "View",
                             std::string_view submenu_label = "Panels");

    void draw();

   private:
    std::vector<Menu> menus_;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

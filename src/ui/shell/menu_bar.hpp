#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <cstdint>
    #include <deque>
    #include <functional>
    #include <string>
    #include <string_view>
    #include <vector>

    #include "ui/imgui/imgui_integration.hpp"

namespace spectra::ui::shell
{
struct MenuAction
{
    std::string label{};
    // Explicit shortcut text. When empty and command_id is set, the shortcut is
    // looked up from the live shortcut bindings at draw time (see ShortcutResolver).
    std::string shortcut{};
    // Command-registry id this item runs; used only for the shortcut lookup.
    std::string command_id{};
    // ui::Icon codepoint drawn in the icon column (0 = none).
    uint16_t                icon = 0;
    std::function<void()>   on_click      {};
    std::function<bool()>   enabled       {};
    std::function<bool()>   checked       {};
    bool                    separator     = false;
    std::vector<MenuAction> submenu       {};
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
    void                           remove_submenu(std::string_view label);

   private:
    std::string             name_;
    std::vector<MenuAction> items_;
};

class PanelRegistry;

// Maps a command id to the shortcut text shown in a menu (empty = no shortcut).
using ShortcutResolver = std::function<std::string(const std::string& command_id)>;

// Optional overrides for the panel items built by MenuBar::bind_panel_registry.
// Shells whose panels mirror state owned elsewhere route visibility and toggling
// through these so the checkmark and the click agree with the real panel.
struct PanelMenuHooks
{
    std::function<bool(const std::string& panel_id)>        is_visible{};
    std::function<void(const std::string& panel_id)>        toggle{};
    std::function<std::string(const std::string& panel_id)> command_id{};
};

class MenuBar
{
   public:
    Menu&                    menu(std::string_view name);
    bool                     has_menu(std::string_view name) const;
    std::vector<std::string> menu_names() const;

    // Builds a checkable submenu from a SNAPSHOT of the registry's current panels.
    // The PanelRegistry must outlive this MenuBar and stay valid while menu callbacks
    // run. Panels registered after this call do not appear until bind_panel_registry
    // is called again.
    void bind_panel_registry(PanelRegistry&   registry,
                             std::string_view top_level     = "View",
                             std::string_view submenu_label = "Panels",
                             PanelMenuHooks   hooks         = {});

    // Renders top-level menus in the current ImGui window (command bar style).
    // Each menu is passed to draw_fn for custom rendering (e.g. hover-switch popups).
    void draw_inline(
        const std::function<void(const char* label, const std::vector<MenuAction>& items)>& draw_fn)
        const;

    void set_trailing_draw(std::function<void()> fn);

    // Supplies shortcut text for items that name a command_id but no explicit shortcut.
    void                    set_shortcut_resolver(ShortcutResolver fn);
    const ShortcutResolver& shortcut_resolver() const { return shortcut_resolver_; }

    // Vision command-bar frame + inline menus (requires bound ImGuiIntegration).
    void draw_command_bar(spectra::ImGuiIntegration* imgui);

    void draw();

   private:
    std::deque<Menu>        menus_;
    std::function<void()>   trailing_draw_;
    ShortcutResolver        shortcut_resolver_;
};

// Shared converter for Vision command-bar menu rendering. Submenus are flattened
// into their own separated section; shortcut text comes from MenuAction::shortcut,
// or from `resolve_shortcut(command_id)` when that is empty.
std::vector<spectra::ImGuiIntegration::MenuItem> to_imgui_menu_items(
    const std::vector<MenuAction>& actions,
    const ShortcutResolver&        resolve_shortcut = {});
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

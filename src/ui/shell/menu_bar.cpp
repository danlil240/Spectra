#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/menu_bar.hpp"

    #include <algorithm>

    #include "imgui.h"
    #include "ui/shell/panel_registry.hpp"

namespace spectra::ui::shell
{
namespace
{
void render_menu_items(const std::vector<MenuAction>& items)
{
    for (const MenuAction& action : items)
    {
        if (action.separator)
        {
            ImGui::Separator();
            continue;
        }

        if (!action.submenu.empty())
        {
            if (ImGui::BeginMenu(action.label.c_str()))
            {
                render_menu_items(action.submenu);
                ImGui::EndMenu();
            }
            continue;
        }

        const bool  en           = !action.enabled || action.enabled();
        const bool  is_checkable = static_cast<bool>(action.checked);
        const bool  chk          = is_checkable && action.checked();
        const char* shortcut_ptr = action.shortcut.empty() ? nullptr : action.shortcut.c_str();

        if (ImGui::MenuItem(action.label.c_str(), shortcut_ptr, chk, en) && action.on_click)
            action.on_click();
    }
}
}   // namespace

Menu::Menu(std::string name) : name_(std::move(name)) {}

const std::string& Menu::name() const
{
    return name_;
}

Menu& Menu::add(MenuAction action)
{
    items_.push_back(std::move(action));
    return *this;
}

void Menu::add_separator()
{
    MenuAction sep;
    sep.separator = true;
    items_.push_back(std::move(sep));
}

const std::vector<MenuAction>& Menu::items() const
{
    return items_;
}

std::vector<MenuAction>& Menu::items_mut()
{
    return items_;
}

Menu& MenuBar::menu(std::string_view name)
{
    const std::string name_str(name);
    for (Menu& m : menus_)
    {
        if (m.name() == name_str)
            return m;
    }
    menus_.emplace_back(name_str);
    return menus_.back();
}

bool MenuBar::has_menu(std::string_view name) const
{
    const std::string name_str(name);
    for (const Menu& m : menus_)
    {
        if (m.name() == name_str)
            return true;
    }
    return false;
}

std::vector<std::string> MenuBar::menu_names() const
{
    std::vector<std::string> names;
    names.reserve(menus_.size());
    for (const Menu& m : menus_)
        names.push_back(m.name());
    return names;
}

void MenuBar::bind_panel_registry(PanelRegistry&   registry,
                                  std::string_view top_level,
                                  std::string_view submenu_label)
{
    Menu& top_menu = menu(top_level);

    auto&      items             = top_menu.items_mut();
    const auto submenu_label_str = std::string(submenu_label);
    items.erase(
        std::remove_if(items.begin(),
                       items.end(),
                       [&](const MenuAction& action)
                       { return action.label == submenu_label_str && !action.submenu.empty(); }),
        items.end());

    MenuAction panels_action;
    panels_action.label = submenu_label_str;

    PanelRegistry* reg_ptr = &registry;
    for (Panel* panel : registry.all())
    {
        const std::string id    = panel->id();
        const std::string title = panel->title();

        MenuAction item;
        item.label   = title;
        item.checked = [reg_ptr, id]()
        {
            Panel* p = reg_ptr->find(id);
            return p && p->visible();
        };
        item.on_click = [reg_ptr, id]() { reg_ptr->toggle(id); };
        panels_action.submenu.push_back(std::move(item));
    }

    top_menu.add(std::move(panels_action));
}

void MenuBar::draw()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    for (const Menu& m : menus_)
    {
        if (ImGui::BeginMenu(m.name().c_str()))
        {
            render_menu_items(m.items());
            ImGui::EndMenu();
        }
    }

    ImGui::EndMainMenuBar();
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

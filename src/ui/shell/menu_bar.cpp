#ifdef SPECTRA_USE_IMGUI
    #include "ui/shell/menu_bar.hpp"

    #include <algorithm>

    #include "imgui.h"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/ui_interaction_log.hpp"
    #include "ui/shell/panel_registry.hpp"

namespace spectra::ui::shell
{
namespace
{
}   // namespace

namespace
{
using ImGuiMenuItem = spectra::ImGuiIntegration::MenuItem;

void append_separator(std::vector<ImGuiMenuItem>& out)
{
    // No leading or doubled separators.
    if (!out.empty() && !out.back().label.empty())
        out.emplace_back("", nullptr);
}

void append_item(std::vector<ImGuiMenuItem>& out,
                 const MenuAction&           action,
                 const ShortcutResolver&     resolve_shortcut)
{
    if (action.separator)
    {
        append_separator(out);
        return;
    }
    const bool    enabled = !action.enabled || action.enabled();
    ImGuiMenuItem item(action.label, enabled ? action.on_click : nullptr);
    item.shortcut = action.shortcut;
    if (item.shortcut.empty() && !action.command_id.empty() && resolve_shortcut)
        item.shortcut = resolve_shortcut(action.command_id);
    item.checked = action.checked;
    item.icon    = action.icon;
    out.push_back(std::move(item));
}
}   // namespace

std::vector<spectra::ImGuiIntegration::MenuItem> to_imgui_menu_items(
    const std::vector<MenuAction>& actions,
    const ShortcutResolver&        resolve_shortcut)
{
    std::vector<ImGuiMenuItem> out;
    out.reserve(actions.size());
    for (const MenuAction& action : actions)
    {
        if (!action.submenu.empty())
        {
            // The command bar has no cascading menus: a submenu becomes its own
            // separated section of the parent menu.
            append_separator(out);
            for (const MenuAction& sub : action.submenu)
                append_item(out, sub, resolve_shortcut);
            append_separator(out);
            continue;
        }
        append_item(out, action, resolve_shortcut);
    }
    if (!out.empty() && out.back().label.empty())
        out.pop_back();
    return out;
}

namespace
{
void render_menu_items(const std::vector<MenuAction>& items, const ShortcutResolver& resolve)
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
                render_menu_items(action.submenu, resolve);
                ImGui::EndMenu();
            }
            continue;
        }

        const bool  en           = !action.enabled || action.enabled();
        const bool  is_checkable = static_cast<bool>(action.checked);
        const bool  chk          = is_checkable && action.checked();
        std::string shortcut     = action.shortcut;
        if (shortcut.empty() && !action.command_id.empty() && resolve)
            shortcut = resolve(action.command_id);
        const char* shortcut_ptr = shortcut.empty() ? nullptr : shortcut.c_str();

        if (ImGui::MenuItem(action.label.c_str(), shortcut_ptr, chk, en) && action.on_click)
        {
            log_ui_action("menu", action.label, "ok");
            action.on_click();
        }
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

void Menu::remove_submenu(std::string_view label)
{
    const std::string label_str(label);
    items_.erase(std::remove_if(items_.begin(),
                                items_.end(),
                                [&](const MenuAction& action)
                                { return action.label == label_str && !action.submenu.empty(); }),
                 items_.end());
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
                                  std::string_view submenu_label,
                                  PanelMenuHooks   hooks)
{
    Menu& top_menu = menu(top_level);
    top_menu.remove_submenu(submenu_label);

    MenuAction panels_action;
    panels_action.label = std::string(submenu_label);

    PanelRegistry* reg_ptr = &registry;
    for (Panel* panel : registry.all())
    {
        const std::string id    = panel->id();
        const std::string title = panel->title();

        MenuAction item;
        item.label = title;
        item.icon  = static_cast<uint16_t>(panel->icon());
        if (hooks.command_id)
            item.command_id = hooks.command_id(id);
        if (hooks.is_visible)
        {
            item.checked = [is_visible = hooks.is_visible, id]() { return is_visible(id); };
        }
        else
        {
            item.checked = [reg_ptr, id]()
            {
                Panel* p = reg_ptr->find(id);
                return p && p->visible();
            };
        }
        if (hooks.toggle)
            item.on_click = [toggle = hooks.toggle, id]() { toggle(id); };
        else
            item.on_click = [reg_ptr, id]() { reg_ptr->toggle(id); };
        panels_action.submenu.push_back(std::move(item));
    }

    top_menu.add(std::move(panels_action));
}

void MenuBar::draw_inline(
    const std::function<void(const char* label, const std::vector<MenuAction>& items)>& draw_fn)
    const
{
    if (!draw_fn)
        return;

    for (const Menu& m : menus_)
    {
        draw_fn(m.name().c_str(), m.items());
        ImGui::SameLine();
    }
}

void MenuBar::draw()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    for (const Menu& m : menus_)
    {
        if (ImGui::BeginMenu(m.name().c_str()))
        {
            render_menu_items(m.items(), shortcut_resolver_);
            ImGui::EndMenu();
        }
    }

    ImGui::EndMainMenuBar();
}

void MenuBar::set_trailing_draw(std::function<void()> fn)
{
    trailing_draw_ = std::move(fn);
}

void MenuBar::set_shortcut_resolver(ShortcutResolver fn)
{
    shortcut_resolver_ = std::move(fn);
}

void MenuBar::draw_command_bar(spectra::ImGuiIntegration* imgui)
{
    if (!imgui)
        return;

    if (!imgui->begin_command_bar())
    {
        imgui->end_command_bar();
        return;
    }

    for (const Menu& m : menus_)
    {
        const auto items = to_imgui_menu_items(m.items(), shortcut_resolver_);
        imgui->render_menubar_menu(m.name().c_str(), items);
        ImGui::SameLine();
    }

    if (trailing_draw_)
        trailing_draw_();

    imgui->end_command_bar();
}
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI

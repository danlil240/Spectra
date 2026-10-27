#ifdef SPECTRA_USE_IMGUI
    #include <gtest/gtest.h>
    #include <memory>
    #include "ui/commands/command_registry.hpp"
    #include "ui/commands/shortcut_manager.hpp"
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/shell/panel_registry.hpp"
    #include "ui/shell/spectra_app_shell.hpp"

using namespace spectra::ui;
using namespace spectra::ui::shell;

namespace
{
std::unique_ptr<Panel> make_panel(const std::string& id,
                                  const std::string& title,
                                  bool               default_visible = false)
{
    PanelInfo info;
    info.id              = id;
    info.title           = title;
    info.default_visible = default_visible;
    return std::make_unique<CallbackPanel>(info, [](bool*) {});
}

const MenuAction* find_panels_submenu(MenuBar& bar)
{
    const Menu& view = bar.menu("View");
    for (const MenuAction& action : view.items())
    {
        if (action.label == "Panels" && !action.submenu.empty())
            return &action;
    }
    return nullptr;
}
}   // namespace

TEST(ShellMenuBar, MenuGetOrCreateAndOrder)
{
    MenuBar bar;

    Menu& file_first  = bar.menu("File");
    Menu& file_second = bar.menu("File");
    EXPECT_EQ(&file_first, &file_second);

    bar.menu("Edit");
    bar.menu("View");

    const auto names = bar.menu_names();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "File");
    EXPECT_EQ(names[1], "Edit");
    EXPECT_EQ(names[2], "View");
}

TEST(ShellMenuBar, MenuReferencesStayValidAcrossGrowth)
{
    MenuBar bar;

    Menu& view = bar.menu("View");
    bar.menu("File");
    bar.menu("Edit");
    bar.menu("Tools");

    MenuAction action;
    action.label = "Something";
    view.add(std::move(action));

    EXPECT_EQ(&view, &bar.menu("View"));
    EXPECT_EQ(bar.menu("View").items().size(), 1u);
}

TEST(ShellMenuBar, HasMenu)
{
    MenuBar bar;

    EXPECT_FALSE(bar.has_menu("File"));
    bar.menu("File");
    EXPECT_TRUE(bar.has_menu("File"));
    EXPECT_FALSE(bar.has_menu("Missing"));
}

TEST(ShellMenuBar, MenuAddSeparatorAndItems)
{
    Menu menu("Test");

    MenuAction first;
    first.label = "First";
    menu.add(std::move(first));

    menu.add_separator();

    MenuAction second;
    second.label = "Second";
    menu.add(std::move(second));

    const auto& items = menu.items();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].label, "First");
    EXPECT_FALSE(items[0].separator);
    EXPECT_TRUE(items[1].separator);
    EXPECT_EQ(items[2].label, "Second");
    EXPECT_FALSE(items[2].separator);
}

TEST(ShellMenuBar, BindPanelRegistryBuildsPanelsSubmenu)
{
    PanelRegistry reg;
    reg.add(make_panel("panel.one", "Panel One", false));
    reg.add(make_panel("panel.two", "Panel Two", true));

    MenuBar bar;
    bar.bind_panel_registry(reg);

    ASSERT_TRUE(bar.has_menu("View"));

    const MenuAction* panels = find_panels_submenu(bar);
    ASSERT_NE(panels, nullptr);
    ASSERT_EQ(panels->submenu.size(), 2u);
    EXPECT_EQ(panels->submenu[0].label, "Panel One");
    EXPECT_EQ(panels->submenu[1].label, "Panel Two");

    const MenuAction& first_item = panels->submenu[0];
    ASSERT_TRUE(first_item.checked);
    EXPECT_FALSE(first_item.checked());
    ASSERT_TRUE(first_item.on_click);

    first_item.on_click();
    Panel* panel = reg.find("panel.one");
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->visible());
    EXPECT_TRUE(first_item.checked());
}

TEST(ShellMenuBar, RebindReplacesPanelsSubmenu)
{
    PanelRegistry reg;
    reg.add(make_panel("panel.one", "Panel One"));
    reg.add(make_panel("panel.two", "Panel Two"));

    MenuBar bar;
    bar.bind_panel_registry(reg);
    bar.bind_panel_registry(reg);

    const Menu& view         = bar.menu("View");
    size_t      panels_count = 0;
    for (const MenuAction& action : view.items())
    {
        if (action.label == "Panels" && !action.submenu.empty())
            ++panels_count;
    }
    EXPECT_EQ(panels_count, 1u);
}

TEST(ShellMenuBar, ToImguiMenuItemsFlattensSubmenusAndDisabled)
{
    bool enabled_clicked = false;
    bool sub_clicked     = false;

    MenuAction parent;
    parent.label = "Parent";
    parent.submenu.push_back({.label = "Sub One", .on_click = [&]() { sub_clicked = true; }});
    parent.submenu.push_back({.separator = true});
    parent.submenu.push_back({.label = "Sub Disabled", .enabled = []() { return false; }});

    MenuAction top;
    top.label    = "Top";
    top.on_click = [&]() { enabled_clicked = true; };

    MenuAction disabled;
    disabled.label   = "Disabled Top";
    disabled.enabled = []() { return false; };

    // The flattened submenu becomes its own section, fenced by separators.
    const auto items = to_imgui_menu_items({top, parent, disabled});
    ASSERT_EQ(items.size(), 7u);
    EXPECT_EQ(items[0].label, "Top");
    EXPECT_TRUE(items[0].callback);
    EXPECT_TRUE(items[1].label.empty());
    EXPECT_EQ(items[2].label, "Sub One");
    EXPECT_TRUE(items[2].callback);
    EXPECT_TRUE(items[3].label.empty());
    EXPECT_FALSE(items[3].callback);
    EXPECT_EQ(items[4].label, "Sub Disabled");
    EXPECT_FALSE(items[4].callback);
    EXPECT_TRUE(items[5].label.empty());
    EXPECT_EQ(items[6].label, "Disabled Top");
    EXPECT_FALSE(items[6].callback);

    items[0].callback();
    EXPECT_TRUE(enabled_clicked);
    items[2].callback();
    EXPECT_TRUE(sub_clicked);
}

TEST(ShellMenuBar, ToImguiMenuItemsDropsRedundantSeparators)
{
    MenuAction sep;
    sep.separator = true;
    MenuAction a;
    a.label = "A";
    MenuAction b;
    b.label = "B";

    const auto items = to_imgui_menu_items({sep, a, sep, sep, b, sep});
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].label, "A");
    EXPECT_TRUE(items[1].label.empty());
    EXPECT_EQ(items[2].label, "B");
}

TEST(ShellMenuBar, ToImguiMenuItemsCarriesShortcutCheckAndIcon)
{
    bool       on = true;
    MenuAction explicit_sc;
    explicit_sc.label      = "Explicit";
    explicit_sc.shortcut   = "Ctrl+E";
    explicit_sc.command_id = "cmd.explicit";

    MenuAction resolved;
    resolved.label      = "Resolved";
    resolved.command_id = "cmd.resolved";
    resolved.icon       = static_cast<uint16_t>(Icon::Grid);
    resolved.checked    = [&]() { return on; };

    MenuAction unbound;
    unbound.label      = "Unbound";
    unbound.command_id = "cmd.unbound";

    std::vector<std::string> asked;
    ShortcutResolver         resolver = [&](const std::string& id) -> std::string
    {
        asked.push_back(id);
        return id == "cmd.resolved" ? "Shift+R" : "";
    };

    const auto items = to_imgui_menu_items({explicit_sc, resolved, unbound}, resolver);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].shortcut, "Ctrl+E");   // explicit text wins, no lookup
    EXPECT_FALSE(items[0].checked);
    EXPECT_EQ(items[0].icon, 0);

    EXPECT_EQ(items[1].shortcut, "Shift+R");
    EXPECT_EQ(items[1].icon, static_cast<uint16_t>(Icon::Grid));
    ASSERT_TRUE(items[1].checked);
    EXPECT_TRUE(items[1].checked());
    on = false;
    EXPECT_FALSE(items[1].checked());

    EXPECT_TRUE(items[2].shortcut.empty());
    EXPECT_EQ(asked, (std::vector<std::string>{"cmd.resolved", "cmd.unbound"}));

    // Without a resolver only explicit shortcuts are shown.
    const auto plain = to_imgui_menu_items({explicit_sc, resolved});
    EXPECT_EQ(plain[0].shortcut, "Ctrl+E");
    EXPECT_TRUE(plain[1].shortcut.empty());
}

TEST(ShellMenuBar, BindPanelRegistryUsesHooksAndIcons)
{
    PanelRegistry reg;
    PanelInfo     info;
    info.id    = "core.timeline";
    info.title = "Timeline";
    info.icon  = Icon::Timeline;
    reg.add(std::make_unique<CallbackPanel>(info, [](bool*) {}));

    bool                     external_visible = false;
    std::vector<std::string> toggled;
    PanelMenuHooks           hooks;
    hooks.is_visible = [&](const std::string&) { return external_visible; };
    hooks.toggle     = [&](const std::string& id)
    {
        toggled.push_back(id);
        external_visible = !external_visible;
    };
    hooks.command_id = [](const std::string& id) { return "cmd." + id; };

    MenuBar bar;
    bar.bind_panel_registry(reg, "View", "Panels", hooks);

    const MenuAction* panels = find_panels_submenu(bar);
    ASSERT_NE(panels, nullptr);
    ASSERT_EQ(panels->submenu.size(), 1u);
    const MenuAction& item = panels->submenu[0];
    EXPECT_EQ(item.label, "Timeline");
    EXPECT_EQ(item.command_id, "cmd.core.timeline");
    EXPECT_EQ(item.icon, static_cast<uint16_t>(Icon::Timeline));
    ASSERT_TRUE(item.checked);
    EXPECT_FALSE(item.checked());

    item.on_click();
    EXPECT_EQ(toggled, (std::vector<std::string>{"core.timeline"}));
    EXPECT_TRUE(item.checked());
    // The hook owns visibility; the registry entry is left alone.
    EXPECT_FALSE(reg.find("core.timeline")->visible());
}

// P1-13: the View menu must list each panel once (the command bar flattens the
// Panels submenu into the View menu, so panel items must not be repeated above it).
TEST(ShellMenuBar, FlattenedViewMenuListsEachPanelOnce)
{
    PanelRegistry reg;
    reg.add(make_panel("core.inspector", "Inspector", true));
    reg.add(make_panel("core.timeline", "Timeline"));

    MenuBar bar;
    Menu&   view = bar.menu("View");
    view.add({.label = "Zoom to Fit", .command_id = "view.autofit"});
    view.add_separator();
    view.add({.label = "Navigation Rail", .checked = []() { return true; }});
    bar.bind_panel_registry(reg);

    const auto               items = to_imgui_menu_items(view.items());
    std::vector<std::string> labels;
    for (const auto& it : items)
        labels.push_back(it.label);
    EXPECT_EQ(labels,
              (std::vector<
                  std::string>{"Zoom to Fit", "", "Navigation Rail", "", "Inspector", "Timeline"}));
    for (const auto& it : items)
    {
        if (!it.label.empty() && it.label != "Zoom to Fit")
        {
            EXPECT_TRUE(it.checked) << it.label;
        }
    }
}

TEST(ShellMenuBar, SpectraPanelCommandIds)
{
    EXPECT_EQ(SpectraAppShell::panel_command_id("core.inspector"), "panel.toggle_inspector");
    EXPECT_EQ(SpectraAppShell::panel_command_id("core.timeline"), "panel.toggle_timeline");
    EXPECT_EQ(SpectraAppShell::panel_command_id("core.curve_editor"), "panel.toggle_curve_editor");
    EXPECT_EQ(SpectraAppShell::panel_command_id("core.topics"), "panel.toggle_topics");
    EXPECT_EQ(SpectraAppShell::panel_command_id("core.plugins"), "panel.toggle_plugins");
    EXPECT_EQ(SpectraAppShell::panel_command_id("core.settings"), "panel.open_settings");
    EXPECT_TRUE(SpectraAppShell::panel_command_id("ros.scene").empty());
}

TEST(ShellMenuBar, MenuShortcutTextPrefersLiveBinding)
{
    spectra::CommandRegistry registry;
    registry.register_command("panel.toggle_timeline", "Toggle Timeline", []() {}, "T", "Panel");
    registry.register_command("panel.toggle_inspector", "Toggle Inspector", []() {}, "", "Panel");

    // Registry-only fallback uses the registered text.
    EXPECT_EQ(spectra::menu_shortcut_text(nullptr, &registry, "panel.toggle_timeline"), "T");
    EXPECT_TRUE(spectra::menu_shortcut_text(nullptr, &registry, "panel.toggle_inspector").empty());
    EXPECT_TRUE(spectra::menu_shortcut_text(nullptr, &registry, "missing").empty());
    EXPECT_TRUE(spectra::menu_shortcut_text(nullptr, nullptr, "panel.toggle_timeline").empty());

    // A live binding wins, and reflects rebinding.
    spectra::ShortcutManager shortcuts;
    shortcuts.register_defaults();
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, &registry, "panel.toggle_curve_editor"),
              "Shift+T");
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, &registry, "panel.toggle_topics"),
              "Ctrl+Shift+T");
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, &registry, "app.command_palette"), "Ctrl+K");
    // Several bindings: the registered one is canonical; otherwise the pick is stable.
    registry.register_command("figure.new", "New Figure", []() {}, "Ctrl+T", "Figure");
    shortcuts.bind(spectra::Shortcut::from_string("Ctrl+T"), "figure.new");   // + default Ctrl+N
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, &registry, "figure.new"), "Ctrl+T");
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, nullptr, "figure.new"), "Ctrl+N");
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, nullptr, "figure.close"), "Q");

    shortcuts.unbind_command("panel.toggle_timeline");
    EXPECT_TRUE(
        spectra::menu_shortcut_text(&shortcuts, &registry, "panel.toggle_timeline").empty());
    shortcuts.bind(spectra::Shortcut::from_string("Ctrl+Alt+L"), "panel.toggle_timeline");
    EXPECT_EQ(spectra::menu_shortcut_text(&shortcuts, &registry, "panel.toggle_timeline"),
              "Ctrl+Alt+L");
}
#else
    #include <gtest/gtest.h>
TEST(ShellMenuBar, SkippedWithoutImGui)
{
    SUCCEED();
}
#endif

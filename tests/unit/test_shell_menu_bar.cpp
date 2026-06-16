#ifdef SPECTRA_USE_IMGUI
    #include <gtest/gtest.h>
    #include <memory>
    #include "ui/shell/menu_bar.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/shell/panel_registry.hpp"

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

    const auto items = to_imgui_menu_items({top, parent, disabled});
    ASSERT_EQ(items.size(), 5u);
    EXPECT_EQ(items[0].label, "Top");
    EXPECT_TRUE(items[0].callback);
    EXPECT_EQ(items[1].label, "Sub One");
    EXPECT_TRUE(items[1].callback);
    EXPECT_TRUE(items[2].label.empty());
    EXPECT_FALSE(items[2].callback);
    EXPECT_EQ(items[3].label, "Sub Disabled");
    EXPECT_FALSE(items[3].callback);
    EXPECT_EQ(items[4].label, "Disabled Top");
    EXPECT_FALSE(items[4].callback);

    items[0].callback();
    EXPECT_TRUE(enabled_clicked);
    items[1].callback();
    EXPECT_TRUE(sub_clicked);
}
#else
    #include <gtest/gtest.h>
TEST(ShellMenuBar, SkippedWithoutImGui)
{
    SUCCEED();
}
#endif

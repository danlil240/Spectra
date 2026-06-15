#ifdef SPECTRA_USE_IMGUI
    #include <gtest/gtest.h>
    #include <memory>

    #include "ui/shell/app_shell.hpp"
    #include "ui/shell/panel.hpp"

using namespace spectra::ui::shell;

namespace
{
class TestShell : public AppShell
{
   public:
    using AppShell::AppShell;

   protected:
    void on_register_panels() override
    {
        PanelInfo info_a;
        info_a.id       = "test.a";
        info_a.title    = "Panel A";
        info_a.category = "General";
        panels().add(std::make_unique<CallbackPanel>(info_a, [](bool*) {}));

        PanelInfo info_b;
        info_b.id       = "test.b";
        info_b.title    = "Panel B";
        info_b.category = "Tools";
        panels().add(std::make_unique<CallbackPanel>(info_b, [](bool*) {}));
    }
};

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

TEST(ShellAppShell, InitializeRegistersPanels)
{
    TestShell shell;
    shell.initialize();

    EXPECT_TRUE(shell.is_initialized());
    EXPECT_EQ(shell.panels().all().size(), 2u);
}

TEST(ShellAppShell, MenuBarPanelsSubmenu)
{
    TestShell shell;
    shell.initialize();

    const MenuAction* panels_submenu = find_panels_submenu(shell.menu_bar());
    ASSERT_NE(panels_submenu, nullptr);
    ASSERT_EQ(panels_submenu->submenu.size(), 2u);
    EXPECT_EQ(panels_submenu->submenu[0].label, "Panel A");
    EXPECT_EQ(panels_submenu->submenu[1].label, "Panel B");
}

TEST(ShellAppShell, NavRailReflectsPanels)
{
    TestShell shell;
    shell.initialize();

    std::vector<NavItem> items;
    shell.nav_rail().build_items(items);

    size_t panel_items = 0;
    bool   found_a     = false;
    bool   found_b     = false;
    for (const NavItem& item : items)
    {
        if (item.is_section_header)
            continue;
        ++panel_items;
        if (item.id == "test.a")
            found_a = true;
        if (item.id == "test.b")
            found_b = true;
    }

    EXPECT_EQ(panel_items, 2u);
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST(ShellAppShell, InitializeIdempotent)
{
    TestShell shell;
    shell.initialize();
    shell.initialize();

    EXPECT_TRUE(shell.is_initialized());
    EXPECT_EQ(shell.panels().all().size(), 2u);
}
#else
    #include <gtest/gtest.h>
TEST(ShellAppShell, SkippedWithoutImGui)
{
    SUCCEED();
}
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <gtest/gtest.h>
    #include <memory>
    #include "ui/shell/panel.hpp"
    #include "ui/shell/panel_registry.hpp"

using namespace spectra::ui;
using namespace spectra::ui::shell;

namespace
{
std::unique_ptr<Panel> make_panel(const std::string& id,
                                  const std::string& category,
                                  bool               default_visible = false)
{
    PanelInfo info;
    info.id              = id;
    info.title           = id;
    info.category        = category;
    info.default_visible = default_visible;
    return std::make_unique<CallbackPanel>(info, [](bool*) {});
}
}   // namespace

TEST(ShellPanelRegistry, AddFindAndDuplicateId)
{
    PanelRegistry reg;

    Panel* first = reg.add(make_panel("panel.a", "General"));
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(reg.find("panel.a"), first);
    EXPECT_EQ(reg.find("missing"), nullptr);

    Panel* duplicate = reg.add(make_panel("panel.a", "Other"));
    EXPECT_EQ(duplicate, nullptr);
    EXPECT_EQ(reg.all().size(), 1u);
}

TEST(ShellPanelRegistry, AllPreservesInsertionOrder)
{
    PanelRegistry reg;
    reg.add(make_panel("first", "A"));
    reg.add(make_panel("second", "B"));
    reg.add(make_panel("third", "A"));

    const auto panels = reg.all();
    ASSERT_EQ(panels.size(), 3u);
    EXPECT_EQ(panels[0]->id(), "first");
    EXPECT_EQ(panels[1]->id(), "second");
    EXPECT_EQ(panels[2]->id(), "third");
}

TEST(ShellPanelRegistry, CategoriesFirstSeenOrderAndDedup)
{
    PanelRegistry reg;
    reg.add(make_panel("p1", "Alpha"));
    reg.add(make_panel("p2", "Beta"));
    reg.add(make_panel("p3", "Alpha"));
    reg.add(make_panel("p4", "Gamma"));
    reg.add(make_panel("p5", "Beta"));

    const auto cats = reg.categories();
    ASSERT_EQ(cats.size(), 3u);
    EXPECT_EQ(cats[0], "Alpha");
    EXPECT_EQ(cats[1], "Beta");
    EXPECT_EQ(cats[2], "Gamma");
}

TEST(ShellPanelRegistry, InCategoryPreservesInsertionOrder)
{
    PanelRegistry reg;
    reg.add(make_panel("a1", "Tools"));
    reg.add(make_panel("b1", "View"));
    reg.add(make_panel("a2", "Tools"));
    reg.add(make_panel("a3", "Tools"));
    reg.add(make_panel("b2", "View"));

    const auto tools = reg.in_category("Tools");
    ASSERT_EQ(tools.size(), 3u);
    EXPECT_EQ(tools[0]->id(), "a1");
    EXPECT_EQ(tools[1]->id(), "a2");
    EXPECT_EQ(tools[2]->id(), "a3");

    const auto view = reg.in_category("View");
    ASSERT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0]->id(), "b1");
    EXPECT_EQ(view[1]->id(), "b2");

    EXPECT_TRUE(reg.in_category("Missing").empty());
}

TEST(ShellPanelRegistry, SetVisibleAndToggle)
{
    PanelRegistry reg;
    reg.add(make_panel("toggle.me", "General", false));

    EXPECT_FALSE(reg.set_visible("unknown", true));
    EXPECT_FALSE(reg.toggle("unknown"));

    ASSERT_TRUE(reg.set_visible("toggle.me", true));
    Panel* panel = reg.find("toggle.me");
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->visible());

    ASSERT_TRUE(reg.toggle("toggle.me"));
    EXPECT_FALSE(panel->visible());

    ASSERT_TRUE(reg.toggle("toggle.me"));
    EXPECT_TRUE(panel->visible());
}

TEST(ShellPanelRegistry, CaptureAndApplyVisibilityRoundTrip)
{
    PanelRegistry reg;
    reg.add(make_panel("one", "A", true));
    reg.add(make_panel("two", "A", false));
    reg.add(make_panel("three", "B", true));

    const auto captured = reg.capture_visibility();
    ASSERT_EQ(captured.size(), 3u);
    EXPECT_TRUE(captured.at("one"));
    EXPECT_FALSE(captured.at("two"));
    EXPECT_TRUE(captured.at("three"));

    for (Panel* panel : reg.all())
        panel->set_visible(!panel->visible());

    reg.apply_visibility(captured);

    EXPECT_TRUE(reg.find("one")->visible());
    EXPECT_FALSE(reg.find("two")->visible());
    EXPECT_TRUE(reg.find("three")->visible());
}

TEST(ShellPanelRegistry, ApplyVisibilityIgnoresUnknownAndLeavesUnmentioned)
{
    PanelRegistry reg;
    reg.add(make_panel("keep", "A", true));
    reg.add(make_panel("touch", "A", false));

    reg.find("keep")->set_visible(false);
    reg.find("touch")->set_visible(true);

    std::map<std::string, bool> partial;
    partial["touch"]          = false;
    partial["does.not.exist"] = true;

    reg.apply_visibility(partial);

    EXPECT_FALSE(reg.find("keep")->visible());
    EXPECT_FALSE(reg.find("touch")->visible());
}
#else
    #include <gtest/gtest.h>
TEST(ShellPanelRegistry, SkippedWithoutImGui)
{
    SUCCEED();
}
#endif

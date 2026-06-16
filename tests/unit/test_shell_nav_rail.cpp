#ifdef SPECTRA_USE_IMGUI
    #include <gtest/gtest.h>
    #include <memory>
    #include "ui/shell/nav_rail.hpp"
    #include "ui/shell/panel.hpp"
    #include "ui/shell/panel_registry.hpp"

using namespace spectra::ui;
using namespace spectra::ui::shell;

namespace
{
std::unique_ptr<Panel> make_panel(const std::string& id,
                                  const std::string& title,
                                  const std::string& category,
                                  bool               default_visible = false)
{
    PanelInfo info;
    info.id              = id;
    info.title           = title;
    info.category        = category;
    info.default_visible = default_visible;
    return std::make_unique<CallbackPanel>(info, [](bool*) {});
}

struct ItemKey
{
    bool        is_section_header;
    std::string label;
    std::string section;

    friend bool operator==(const ItemKey& a, const ItemKey& b)
    {
        return a.is_section_header == b.is_section_header && a.label == b.label
               && a.section == b.section;
    }
};

class TestNavRail : public NavRail
{
   public:
    using NavRail::NavRail;
    using NavRail::matches_filter;
};

std::vector<ItemKey> item_keys(const std::vector<NavItem>& items)
{
    std::vector<ItemKey> keys;
    keys.reserve(items.size());
    for (const NavItem& item : items)
        keys.push_back({item.is_section_header, item.label, item.section});
    return keys;
}
}   // namespace

TEST(ShellNavRail, NullRegistryYieldsEmpty)
{
    NavRail              rail;
    std::vector<NavItem> items;
    rail.build_items(items);
    EXPECT_TRUE(items.empty());

    NavRail rail_with_null(nullptr);
    items.clear();
    rail_with_null.build_items(items);
    EXPECT_TRUE(items.empty());
}

TEST(ShellNavRail, GroupingAndOrder)
{
    PanelRegistry reg;
    reg.add(make_panel("t1", "Topic One", "Topics"));
    reg.add(make_panel("p1", "Plot One", "Plots"));
    reg.add(make_panel("t2", "Topic Two", "Topics"));
    reg.add(make_panel("p2", "Plot Two", "Plots"));

    NavRail              rail(&reg);
    std::vector<NavItem> items;
    rail.build_items(items);

    const std::vector<ItemKey> expected = {
        {true, "Topics", "Topics"},
        {false, "Topic One", "Topics"},
        {false, "Topic Two", "Topics"},
        {true, "Plots", "Plots"},
        {false, "Plot One", "Plots"},
        {false, "Plot Two", "Plots"},
    };
    EXPECT_EQ(item_keys(items), expected);

    ASSERT_EQ(items.size(), 6u);
    EXPECT_TRUE(items[0].id.empty());   // section header
    EXPECT_EQ(items[1].id, "t1");
    EXPECT_EQ(items[2].id, "t2");
    EXPECT_TRUE(items[3].id.empty());   // section header
    EXPECT_EQ(items[4].id, "p1");
    EXPECT_EQ(items[5].id, "p2");
}

TEST(ShellNavRail, IsActiveAndOnClick)
{
    PanelRegistry reg;
    reg.add(make_panel("panel.toggle", "Toggle Me", "General", false));

    NavRail              rail(&reg);
    std::vector<NavItem> items;
    rail.build_items(items);
    ASSERT_EQ(items.size(), 2u);

    const NavItem* panel_item = nullptr;
    for (const NavItem& item : items)
    {
        if (!item.is_section_header)
        {
            panel_item = &item;
            break;
        }
    }
    ASSERT_NE(panel_item, nullptr);
    ASSERT_TRUE(panel_item->is_active);
    ASSERT_TRUE(panel_item->on_click);

    EXPECT_FALSE(panel_item->is_active());

    panel_item->on_click();
    EXPECT_TRUE(reg.find("panel.toggle")->visible());
    EXPECT_TRUE(panel_item->is_active());
}

TEST(ShellNavRail, SearchFilter)
{
    PanelRegistry reg;
    reg.add(make_panel("t1", "Alpha Topic", "Topics"));
    reg.add(make_panel("p1", "Beta Plot", "Plots"));
    reg.add(make_panel("t2", "xyz Special", "Topics"));

    NavRail rail(&reg);
    rail.set_search("xyz");

    std::vector<NavItem> items;
    rail.build_items(items);

    const std::vector<ItemKey> filtered = {
        {true, "Topics", "Topics"},
        {false, "xyz Special", "Topics"},
    };
    EXPECT_EQ(item_keys(items), filtered);

    rail.set_search("");
    items.clear();
    rail.build_items(items);
    EXPECT_EQ(items.size(), 5u);
}

TEST(ShellNavRail, MatchesFilter)
{
    EXPECT_TRUE(TestNavRail::matches_filter("Hello World", ""));
    EXPECT_TRUE(TestNavRail::matches_filter("Hello World", "world"));
    EXPECT_TRUE(TestNavRail::matches_filter("Hello World", "HELLO"));
    EXPECT_FALSE(TestNavRail::matches_filter("Hello World", "xyz"));
}

TEST(ShellNavRail, ExpandedAndSearchEnabledRoundTrip)
{
    NavRail rail;
    EXPECT_FALSE(rail.expanded());
    EXPECT_FALSE(rail.search_enabled());

    rail.set_expanded(true);
    rail.set_search_enabled(true);
    EXPECT_TRUE(rail.expanded());
    EXPECT_TRUE(rail.search_enabled());

    rail.set_expanded(false);
    rail.set_search_enabled(false);
    EXPECT_FALSE(rail.expanded());
    EXPECT_FALSE(rail.search_enabled());
}

TEST(ShellNavRail, CuratedModeOmitsRegistryPanels)
{
    PanelRegistry reg;
    reg.add(make_panel("t1", "Topic One", "Topics"));
    reg.add(make_panel("p1", "Plot One", "Plots"));
    reg.add(make_panel("t2", "Topic Two", "Topics"));

    NavItem custom;
    custom.id    = "custom.action";
    custom.label = "Custom";
    custom.icon  = Icon::Home;

    NavRail rail(&reg);
    rail.add_custom_item(custom);
    rail.set_show_registry_panels(false);

    std::vector<NavItem> items;
    rail.build_items(items);

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].id, "custom.action");
    EXPECT_FALSE(items[0].is_section_header);

    rail.set_show_registry_panels(true);
    items.clear();
    rail.build_items(items);
    EXPECT_EQ(items.size(), 6u);
}
#else
    #include <gtest/gtest.h>
TEST(ShellNavRail, SkippedWithoutImGui)
{
    SUCCEED();
}
#endif

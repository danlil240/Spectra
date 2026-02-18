#include <gtest/gtest.h>
#include <spectra/series.hpp>
#include <string>

#include "ui/tab_bar.hpp"

using namespace spectra;

// TabBar pure-logic tests (tab management, callbacks, state queries).
// Drawing/rendering is ImGui-dependent and tested via golden image tests.
//
// NOTE: TabBar constructor creates a default "Figure 1" tab (non-closeable).
// add_tab() auto-activates the new tab.
// remove_tab() skips tabs with can_close=false.

// ─── Initial State ───────────────────────────────────────────────────────────

TEST(TabBar, ConstructorCreatesDefaultTab)
{
    TabBar tb;
    EXPECT_EQ(tb.get_tab_count(), 1u);
    EXPECT_EQ(tb.get_tab_title(0), "Figure 1");
    EXPECT_TRUE(tb.has_active_tab());
}

// ─── Tab Management ──────────────────────────────────────────────────────────

TEST(TabBar, AddTabIncreasesCount)
{
    TabBar tb;
    EXPECT_EQ(tb.get_tab_count(), 1u);

    size_t idx = tb.add_tab("Figure 2");
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(tb.get_tab_count(), 2u);
}

TEST(TabBar, AddMultipleTabs)
{
    TabBar tb;
    size_t idx1 = tb.add_tab("Plot A");
    size_t idx2 = tb.add_tab("Plot B");

    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(idx2, 2u);
    EXPECT_EQ(tb.get_tab_count(), 3u);
}

TEST(TabBar, GetTabTitle)
{
    TabBar tb;
    tb.add_tab("My Plot");

    EXPECT_EQ(tb.get_tab_title(0), "Figure 1");
    EXPECT_EQ(tb.get_tab_title(1), "My Plot");
}

TEST(TabBar, SetTabTitle)
{
    TabBar tb;
    tb.set_tab_title(0, "Renamed");
    EXPECT_EQ(tb.get_tab_title(0), "Renamed");
}

TEST(TabBar, RemoveCloseableTab)
{
    TabBar tb;
    tb.add_tab("Closeable");
    EXPECT_EQ(tb.get_tab_count(), 2u);

    tb.remove_tab(1);
    EXPECT_EQ(tb.get_tab_count(), 1u);
}

TEST(TabBar, RemoveNonCloseableTabIsNoOp)
{
    TabBar tb;
    tb.clear_tabs();
    tb.add_tab("Locked", false);  // explicitly non-closeable
    tb.remove_tab(0);
    EXPECT_EQ(tb.get_tab_count(), 1u);
    EXPECT_EQ(tb.get_tab_title(0), "Locked");
}

TEST(TabBar, RemoveTabShiftsTitles)
{
    TabBar tb;
    tb.add_tab("Tab A");
    tb.add_tab("Tab B");
    tb.add_tab("Tab C");

    tb.remove_tab(1);  // Remove "Tab A"

    EXPECT_EQ(tb.get_tab_count(), 3u);
    EXPECT_EQ(tb.get_tab_title(0), "Figure 1");
    EXPECT_EQ(tb.get_tab_title(1), "Tab B");
    EXPECT_EQ(tb.get_tab_title(2), "Tab C");
}

// ─── Active Tab ──────────────────────────────────────────────────────────────

TEST(TabBar, AddTabAutoActivates)
{
    TabBar tb;
    EXPECT_EQ(tb.get_active_tab(), 0u);

    tb.add_tab("New Tab");
    EXPECT_EQ(tb.get_active_tab(), 1u);
}

TEST(TabBar, SetActiveTab)
{
    TabBar tb;
    tb.add_tab("Tab 1");
    tb.add_tab("Tab 2");

    tb.set_active_tab(0);
    EXPECT_EQ(tb.get_active_tab(), 0u);

    tb.set_active_tab(2);
    EXPECT_EQ(tb.get_active_tab(), 2u);
}

TEST(TabBar, RemoveActiveTabAdjustsIndex)
{
    TabBar tb;
    tb.add_tab("Tab A");
    tb.add_tab("Tab B");  // index 2, now active

    tb.remove_tab(2);
    EXPECT_LT(tb.get_active_tab(), tb.get_tab_count());
}

TEST(TabBar, RemoveBeforeActiveAdjustsIndex)
{
    TabBar tb;
    tb.add_tab("Tab A");
    tb.add_tab("Tab B");
    tb.add_tab("Tab C");  // index 3, now active

    tb.remove_tab(1);  // Remove "Tab A"
    EXPECT_EQ(tb.get_tab_title(tb.get_active_tab()), "Tab C");
}

TEST(TabBar, HasActiveTabAlwaysTrueWithDefaultTab)
{
    TabBar tb;
    EXPECT_TRUE(tb.has_active_tab());

    tb.add_tab("Temp");
    tb.remove_tab(1);
    EXPECT_TRUE(tb.has_active_tab());
}

// ─── Callbacks ───────────────────────────────────────────────────────────────

TEST(TabBar, TabChangeCallbackFires)
{
    TabBar tb;
    tb.add_tab("Tab 1");  // index 1, now active

    size_t callback_index = SIZE_MAX;
    tb.set_tab_change_callback([&callback_index](size_t idx) { callback_index = idx; });

    tb.set_active_tab(0);
    EXPECT_EQ(callback_index, 0u);
}

TEST(TabBar, TabChangeCallbackNotFiredForSameTab)
{
    TabBar tb;
    tb.add_tab("Tab 1");  // index 1, now active

    int call_count = 0;
    tb.set_tab_change_callback([&call_count](size_t) { ++call_count; });

    tb.set_active_tab(1);  // Already active
    EXPECT_EQ(call_count, 0);
}

TEST(TabBar, AddTabDoesNotFireCallback)
{
    TabBar tb;
    int add_count = 0;
    tb.set_tab_add_callback([&add_count]() { ++add_count; });

    // add_tab() is called programmatically by FigureManager,
    // so it must NOT fire on_tab_add_ (which would re-queue a create).
    // The callback is only fired by the + button UI interaction.
    tb.add_tab("New");
    EXPECT_EQ(add_count, 0);

    tb.add_tab("Another");
    EXPECT_EQ(add_count, 0);
    EXPECT_EQ(tb.get_tab_count(), 3u);  // 1 default + 2 added
}

TEST(TabBar, TabCloseCallbackFires)
{
    TabBar tb;
    tb.add_tab("Closeable");

    size_t closed_index = SIZE_MAX;
    tb.set_tab_close_callback([&closed_index](size_t idx) { closed_index = idx; });

    tb.remove_tab(1);
    EXPECT_EQ(closed_index, 1u);
}

TEST(TabBar, TabCloseCallbackNotFiredForNonCloseable)
{
    TabBar tb;
    tb.clear_tabs();
    tb.add_tab("Locked", false);  // explicitly non-closeable
    int close_count = 0;
    tb.set_tab_close_callback([&close_count](size_t) { ++close_count; });

    tb.remove_tab(0);
    EXPECT_EQ(close_count, 0);
}

// ─── Can-close flag ──────────────────────────────────────────────────────────

TEST(TabBar, DefaultAddedTabIsCloseable)
{
    TabBar tb;
    tb.add_tab("Closeable");
    size_t before = tb.get_tab_count();
    tb.remove_tab(1);
    EXPECT_EQ(tb.get_tab_count(), before - 1);
}

TEST(TabBar, NonCloseableTabCannotBeRemoved)
{
    TabBar tb;
    tb.add_tab("Permanent", false);
    size_t before = tb.get_tab_count();
    tb.remove_tab(1);
    EXPECT_EQ(tb.get_tab_count(), before);
}

// ─── Multiple operations ─────────────────────────────────────────────────────

TEST(TabBar, AddRemoveAddSequence)
{
    TabBar tb;
    tb.add_tab("A");
    tb.add_tab("B");
    tb.remove_tab(1);  // Remove "A"
    tb.add_tab("C");

    EXPECT_EQ(tb.get_tab_count(), 3u);
    EXPECT_EQ(tb.get_tab_title(0), "Figure 1");
    EXPECT_EQ(tb.get_tab_title(1), "B");
    EXPECT_EQ(tb.get_tab_title(2), "C");
}

TEST(TabBar, ManyTabs)
{
    TabBar tb;
    for (int i = 0; i < 50; ++i)
    {
        tb.add_tab("Tab " + std::to_string(i));
    }
    EXPECT_EQ(tb.get_tab_count(), 51u);

    tb.set_active_tab(50);
    EXPECT_EQ(tb.get_active_tab(), 50u);

    tb.remove_tab(25);
    EXPECT_EQ(tb.get_tab_count(), 50u);
}

TEST(TabBar, SetActiveTabOutOfRangeIsIgnored)
{
    TabBar tb;
    tb.add_tab("Tab 1");
    size_t before = tb.get_active_tab();
    tb.set_active_tab(100);
    EXPECT_EQ(tb.get_active_tab(), before);
}

TEST(TabBar, RemoveOutOfRangeIsNoOp)
{
    TabBar tb;
    size_t before = tb.get_tab_count();
    tb.remove_tab(999);
    EXPECT_EQ(tb.get_tab_count(), before);
}

// ─── TabInfo struct ──────────────────────────────────────────────────────────

TEST(TabInfo, ConstructorDefaults)
{
    TabBar::TabInfo info("Test");
    EXPECT_EQ(info.title, "Test");
    EXPECT_TRUE(info.can_close);
    EXPECT_FALSE(info.is_modified);
}

TEST(TabInfo, ConstructorCustom)
{
    TabBar::TabInfo info("Custom", false, true);
    EXPECT_EQ(info.title, "Custom");
    EXPECT_FALSE(info.can_close);
    EXPECT_TRUE(info.is_modified);
}

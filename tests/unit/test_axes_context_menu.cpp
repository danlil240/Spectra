#include <algorithm>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "ui/commands/command_registry.hpp"
#include "ui/imgui/axes_context_menu_model.hpp"

using namespace spectra;

namespace
{

void register_menu_commands(CommandRegistry& reg)
{
    auto add = [&](const char* id, const char* label, const char* shortcut)
    { reg.register_command(id, label, []() {}, shortcut, "Test"); };
    add("view.autofit", "Auto-Fit Active Axes", "A");
    add("view.reset", "Reset View", "R");
    add("view.toggle_grid", "Toggle Grid", "G");
    add("view.toggle_legend", "Toggle Legend", "L");
    add("view.toggle_crosshair", "Toggle Crosshair", "C");
    add("file.copy_to_clipboard", "Copy Figure to Clipboard", "Ctrl+Shift+C");
    add("file.export_png", "Export PNG", "Ctrl+S");
    add("file.export_svg", "Export SVG", "Ctrl+Shift+S");
    add("series.copy", "Copy Series", "Ctrl+C");
    add("series.cut", "Cut Series", "Ctrl+X");
    add("series.delete", "Delete Series", "Delete");
    add("series.paste", "Paste Series", "Ctrl+V");
}

const AxesMenuItem* find_command(const std::vector<AxesMenuItem>& items, const std::string& id)
{
    for (const auto& it : items)
        if (it.kind == AxesMenuItemKind::Command && it.command_id == id)
            return &it;
    return nullptr;
}

const AxesMenuItem* find_kind(const std::vector<AxesMenuItem>& items, AxesMenuItemKind kind)
{
    for (const auto& it : items)
        if (it.kind == kind)
            return &it;
    return nullptr;
}

size_t count_actions(const std::vector<AxesMenuItem>& items)
{
    return static_cast<size_t>(std::count_if(items.begin(),
                                             items.end(),
                                             [](const AxesMenuItem& it)
                                             { return it.kind != AxesMenuItemKind::Separator; }));
}

void expect_well_formed_separators(const std::vector<AxesMenuItem>& items)
{
    ASSERT_FALSE(items.empty());
    EXPECT_NE(items.front().kind, AxesMenuItemKind::Separator);
    EXPECT_NE(items.back().kind, AxesMenuItemKind::Separator);
    for (size_t i = 1; i < items.size(); ++i)
    {
        EXPECT_FALSE(items[i].kind == AxesMenuItemKind::Separator
                     && items[i - 1].kind == AxesMenuItemKind::Separator)
            << "double separator at " << i;
    }
}

}   // namespace

// Regression for P0-4: a single 2D axes with nothing selected used to show an
// empty menu because every item was gated on multiple axes or a selection.
TEST(AxesContextMenu, SingleAxesNoSelectionHasUsefulActions)
{
    CommandRegistry reg;
    register_menu_commands(reg);

    AxesContextMenuState st;
    st.has_series_clipboard = true;
    auto items              = build_axes_context_menu(st, &reg);

    expect_well_formed_separators(items);
    EXPECT_GE(count_actions(items), 7u);
    for (const char* id : {"view.autofit",
                           "view.reset",
                           "view.toggle_grid",
                           "view.toggle_legend",
                           "file.copy_to_clipboard",
                           "file.export_png",
                           "file.export_svg"})
    {
        EXPECT_NE(find_command(items, id), nullptr) << id;
    }
    EXPECT_NE(find_kind(items, AxesMenuItemKind::AxesProperties), nullptr);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::LinkX), nullptr);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::CopySeries), nullptr);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::PasteSeries), nullptr);
}

TEST(AxesContextMenu, CommandItemsCarryRegistryShortcuts)
{
    CommandRegistry reg;
    register_menu_commands(reg);
    auto items = build_axes_context_menu({}, &reg);

    const auto* grid = find_command(items, "view.toggle_grid");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->label, "Toggle Grid");
    EXPECT_EQ(grid->shortcut, "G");

    const auto* fit = find_command(items, "view.autofit");
    ASSERT_NE(fit, nullptr);
    EXPECT_EQ(fit->shortcut, "A");
    EXPECT_EQ(fit->label, "Autoscale Axes");
}

TEST(AxesContextMenu, MissingCommandsAreSkipped)
{
    CommandRegistry reg;
    reg.register_command("view.reset", "Reset View", []() {}, "R");
    auto items = build_axes_context_menu({}, &reg);

    EXPECT_NE(find_command(items, "view.reset"), nullptr);
    EXPECT_EQ(find_command(items, "view.toggle_grid"), nullptr);
    expect_well_formed_separators(items);

    auto no_registry = build_axes_context_menu({}, nullptr);
    EXPECT_EQ(find_kind(no_registry, AxesMenuItemKind::Command), nullptr);
    // Properties does not depend on the registry
    EXPECT_NE(find_kind(no_registry, AxesMenuItemKind::AxesProperties), nullptr);
    expect_well_formed_separators(no_registry);
}

TEST(AxesContextMenu, SeriesSelectionAddsSeriesActions)
{
    CommandRegistry reg;
    register_menu_commands(reg);

    AxesContextMenuState st;
    st.has_series_clipboard = true;
    st.selected_series      = 1;
    auto items              = build_axes_context_menu(st, &reg);

    const auto* copy = find_kind(items, AxesMenuItemKind::CopySeries);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->label, "Copy Series");
    EXPECT_EQ(copy->shortcut, "Ctrl+C");
    const auto* del = find_kind(items, AxesMenuItemKind::DeleteSeries);
    ASSERT_NE(del, nullptr);
    EXPECT_TRUE(del->danger);
    EXPECT_EQ(items.front().kind, AxesMenuItemKind::CopySeries);

    st.selected_series  = 3;
    st.clipboard_series = 2;
    items               = build_axes_context_menu(st, &reg);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::CutSeries)->label, "Cut 3 Series");
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::PasteSeries)->label, "Paste 2 Series");
    expect_well_formed_separators(items);
}

TEST(AxesContextMenu, MultipleAxesAddsLinkActions)
{
    CommandRegistry reg;
    register_menu_commands(reg);

    AxesContextMenuState st;
    st.has_multiple_axes = true;
    auto items           = build_axes_context_menu(st, &reg);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::LinkX), nullptr);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::LinkY), nullptr);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::LinkBoth), nullptr);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::UnlinkAll), nullptr);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::UnlinkThis), nullptr);

    st.is_linked = true;
    items        = build_axes_context_menu(st, &reg);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::UnlinkThis), nullptr);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::LinkGroupsInfo), nullptr);
    expect_well_formed_separators(items);
}

TEST(AxesContextMenu, Axes3DMenu)
{
    CommandRegistry reg;
    register_menu_commands(reg);

    AxesContextMenuState st;
    st.is_3d             = true;
    st.has_multiple_axes = true;
    auto items           = build_axes_context_menu(st, &reg);

    expect_well_formed_separators(items);
    EXPECT_NE(find_command(items, "view.autofit"), nullptr);
    EXPECT_NE(find_command(items, "view.reset"), nullptr);
    EXPECT_NE(find_command(items, "view.toggle_grid"), nullptr);
    EXPECT_NE(find_command(items, "file.export_png"), nullptr);
    EXPECT_NE(find_kind(items, AxesMenuItemKind::Link3D), nullptr);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::LinkX), nullptr);
    // 2D-only items
    EXPECT_EQ(find_command(items, "view.toggle_legend"), nullptr);
    EXPECT_EQ(find_command(items, "file.export_svg"), nullptr);
    EXPECT_EQ(find_kind(items, AxesMenuItemKind::AxesProperties), nullptr);
}

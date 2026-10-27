#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "ui/commands/command_registry.hpp"

using namespace spectra;

// ─── Registration ────────────────────────────────────────────────────────────

TEST(CommandRegistry, InitiallyEmpty)
{
    CommandRegistry reg;
    EXPECT_EQ(reg.count(), 0u);
}

TEST(CommandRegistry, RegisterIncrementsCount)
{
    CommandRegistry reg;
    reg.register_command("test.cmd", "Test Command", []() {});
    EXPECT_EQ(reg.count(), 1u);
}

TEST(CommandRegistry, RegisterMultiple)
{
    CommandRegistry reg;
    reg.register_command("cmd.a", "Command A", []() {});
    reg.register_command("cmd.b", "Command B", []() {});
    reg.register_command("cmd.c", "Command C", []() {});
    EXPECT_EQ(reg.count(), 3u);
}

TEST(CommandRegistry, RegisterOverwritesSameId)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Original", [&value]() { value = 1; });
    reg.register_command("test.cmd", "Replaced", [&value]() { value = 2; });
    EXPECT_EQ(reg.count(), 1u);
    reg.execute("test.cmd");
    EXPECT_EQ(value, 2);
}

TEST(CommandRegistry, UnregisterRemoves)
{
    CommandRegistry reg;
    reg.register_command("test.cmd", "Test", []() {});
    EXPECT_EQ(reg.count(), 1u);
    reg.unregister_command("test.cmd");
    EXPECT_EQ(reg.count(), 0u);
}

TEST(CommandRegistry, UnregisterNonExistentIsNoOp)
{
    CommandRegistry reg;
    reg.register_command("test.cmd", "Test", []() {});
    reg.unregister_command("nonexistent");
    EXPECT_EQ(reg.count(), 1u);
}

// ─── Execution ───────────────────────────────────────────────────────────────

TEST(CommandRegistry, ExecuteCallsCallback)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Test", [&value]() { value = 42; });
    bool ok = reg.execute("test.cmd");
    EXPECT_TRUE(ok);
    EXPECT_EQ(value, 42);
}

TEST(CommandRegistry, ExecuteNonExistentReturnsFalse)
{
    CommandRegistry reg;
    EXPECT_FALSE(reg.execute("nonexistent"));
}

TEST(CommandRegistry, ExecuteDisabledReturnsFalse)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Test", [&value]() { value = 42; });
    reg.set_enabled("test.cmd", false);
    EXPECT_FALSE(reg.execute("test.cmd"));
    EXPECT_EQ(value, 0);
}

TEST(CommandRegistry, ExecuteNullCallbackReturnsFalse)
{
    CommandRegistry reg;
    reg.register_command("test.cmd", "Test", nullptr);
    EXPECT_FALSE(reg.execute("test.cmd"));
}

TEST(CommandRegistry, SetEnabledToggle)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Test", [&value]() { value++; });

    reg.set_enabled("test.cmd", false);
    EXPECT_FALSE(reg.execute("test.cmd"));
    EXPECT_EQ(value, 0);

    reg.set_enabled("test.cmd", true);
    EXPECT_TRUE(reg.execute("test.cmd"));
    EXPECT_EQ(value, 1);
}

// ─── Find ────────────────────────────────────────────────────────────────────

TEST(CommandRegistry, FindExisting)
{
    CommandRegistry reg;
    reg.register_command("test.cmd", "Test Command", []() {}, "Ctrl+T", "Testing");

    const Command* cmd = reg.find("test.cmd");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->id, "test.cmd");
    EXPECT_EQ(cmd->label, "Test Command");
    EXPECT_EQ(cmd->shortcut, "Ctrl+T");
    EXPECT_EQ(cmd->category, "Testing");
}

TEST(CommandRegistry, FindNonExistentReturnsNull)
{
    CommandRegistry reg;
    EXPECT_EQ(reg.find("nonexistent"), nullptr);
}

// ─── Search ──────────────────────────────────────────────────────────────────

TEST(CommandRegistry, SearchEmptyQueryReturnsAll)
{
    CommandRegistry reg;
    reg.register_command("cmd.a", "Alpha", []() {});
    reg.register_command("cmd.b", "Beta", []() {});
    reg.register_command("cmd.c", "Gamma", []() {});

    auto results = reg.search("");
    EXPECT_EQ(results.size(), 3u);
}

TEST(CommandRegistry, SearchExactMatch)
{
    CommandRegistry reg;
    reg.register_command("view.reset", "Reset View", []() {});
    reg.register_command("view.zoom", "Zoom In", []() {});
    reg.register_command("edit.undo", "Undo", []() {});

    auto results = reg.search("Reset View");
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].command->id, "view.reset");
}

TEST(CommandRegistry, SearchPrefixMatch)
{
    CommandRegistry reg;
    reg.register_command("view.reset", "Reset View", []() {});
    reg.register_command("view.zoom", "Zoom In", []() {});
    reg.register_command("edit.undo", "Undo", []() {});

    auto results = reg.search("Reset");
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].command->id, "view.reset");
}

TEST(CommandRegistry, SearchFuzzyMatch)
{
    CommandRegistry reg;
    reg.register_command("view.reset", "Reset View", []() {});
    reg.register_command("view.zoom", "Zoom In", []() {});
    reg.register_command("edit.undo", "Undo", []() {});

    auto results = reg.search("rv");   // Fuzzy: R-eset V-iew
    ASSERT_GE(results.size(), 1u);
    // Reset View should match (has 'r' and 'v')
    bool found_reset = false;
    for (const auto& r : results)
    {
        if (r.command->id == "view.reset")
            found_reset = true;
    }
    EXPECT_TRUE(found_reset);
}

TEST(CommandRegistry, SearchCaseInsensitive)
{
    CommandRegistry reg;
    reg.register_command("view.reset", "Reset View", []() {});

    auto results = reg.search("reset view");
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].command->id, "view.reset");
}

TEST(CommandRegistry, SearchNoMatch)
{
    CommandRegistry reg;
    reg.register_command("view.reset", "Reset View", []() {});

    auto results = reg.search("zzzzz");
    EXPECT_TRUE(results.empty());
}

TEST(CommandRegistry, SearchMaxResults)
{
    CommandRegistry reg;
    for (int i = 0; i < 100; ++i)
    {
        reg.register_command("cmd." + std::to_string(i), "Command " + std::to_string(i), []() {});
    }

    auto results = reg.search("", 10);
    EXPECT_EQ(results.size(), 10u);
}

// ─── Categories ──────────────────────────────────────────────────────────────

TEST(CommandRegistry, CategoriesReturnsUnique)
{
    CommandRegistry reg;
    reg.register_command("a", "A", []() {}, "", "View");
    reg.register_command("b", "B", []() {}, "", "Edit");
    reg.register_command("c", "C", []() {}, "", "View");
    reg.register_command("d", "D", []() {}, "", "File");

    auto cats = reg.categories();
    EXPECT_EQ(cats.size(), 3u);
}

TEST(CommandRegistry, CommandsInCategory)
{
    CommandRegistry reg;
    reg.register_command("a", "A", []() {}, "", "View");
    reg.register_command("b", "B", []() {}, "", "Edit");
    reg.register_command("c", "C", []() {}, "", "View");

    auto view_cmds = reg.commands_in_category("View");
    EXPECT_EQ(view_cmds.size(), 2u);

    auto edit_cmds = reg.commands_in_category("Edit");
    EXPECT_EQ(edit_cmds.size(), 1u);
}

// ─── Recent commands ─────────────────────────────────────────────────────────

TEST(CommandRegistry, RecentCommandsTracked)
{
    CommandRegistry reg;
    reg.register_command("cmd.a", "A", []() {});
    reg.register_command("cmd.b", "B", []() {});

    reg.execute("cmd.a");
    reg.execute("cmd.b");

    auto recent = reg.recent_commands(10);
    ASSERT_GE(recent.size(), 2u);
    // Most recent first
    EXPECT_EQ(recent[0]->id, "cmd.b");
    EXPECT_EQ(recent[1]->id, "cmd.a");
}

TEST(CommandRegistry, RecentCommandsNoDuplicates)
{
    CommandRegistry reg;
    reg.register_command("cmd.a", "A", []() {});

    reg.execute("cmd.a");
    reg.execute("cmd.a");
    reg.execute("cmd.a");

    auto recent = reg.recent_commands(10);
    EXPECT_EQ(recent.size(), 1u);
}

TEST(CommandRegistry, RecentCommandsMaxCount)
{
    CommandRegistry reg;
    for (int i = 0; i < 30; ++i)
    {
        reg.register_command("cmd." + std::to_string(i), "Cmd " + std::to_string(i), []() {});
        reg.execute("cmd." + std::to_string(i));
    }

    auto recent = reg.recent_commands(5);
    EXPECT_EQ(recent.size(), 5u);
}

TEST(CommandRegistry, ClearRecent)
{
    CommandRegistry reg;
    reg.register_command("cmd.a", "A", []() {});
    reg.execute("cmd.a");
    EXPECT_FALSE(reg.recent_commands(10).empty());

    reg.clear_recent();
    EXPECT_TRUE(reg.recent_commands(10).empty());
}

// ─── Default (empty-query) listing ───────────────────────────────────────────

namespace
{

void register_listing_fixture(CommandRegistry& reg)
{
    reg.register_command(
        "accessibility.sonify",
        "Sonify Active Series",
        []() {},
        "",
        "Accessibility");
    reg.register_command("anim.go_to_end", "Go to End", []() {}, "", "Animation");
    reg.register_command("view.autofit", "Auto-Fit Active Axes", []() {}, "A", "View");
    reg.register_command("view.reset", "Reset View", []() {}, "R", "View");
    reg.register_command("view.toggle_grid", "Toggle Grid", []() {}, "G", "View");
    reg.register_command("file.export_png", "Export PNG", []() {}, "Ctrl+S", "File");
    reg.register_command("app.command_palette", "Command Palette", []() {}, "Ctrl+K", "App");
}

CommandListingOptions listing_options()
{
    CommandListingOptions opts;
    opts.featured_ids       = {"view.autofit", "view.reset", "missing.cmd", "file.export_png"};
    opts.recent_exclude_ids = {"app.command_palette"};
    opts.max_recent         = 2;
    return opts;
}

std::vector<std::string> listing_ids(const std::vector<CommandSearchResult>& results)
{
    std::vector<std::string> ids;
    for (const auto& r : results)
    {
        ids.push_back(r.command->id);
    }
    return ids;
}

}   // namespace

TEST(CommandRegistry, DefaultListingWithoutRecentStartsWithFeatured)
{
    CommandRegistry reg;
    register_listing_fixture(reg);

    auto results = reg.default_listing(listing_options());
    ASSERT_EQ(results.size(), reg.count());

    // Featured commands first, in the given order; unknown ids are skipped.
    EXPECT_EQ(results[0].command->id, "view.autofit");
    EXPECT_EQ(results[1].command->id, "view.reset");
    EXPECT_EQ(results[2].command->id, "file.export_png");
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(results[i].section, CommandRegistry::SUGGESTED_SECTION);
    }

    // The rest follow by category then label, grouped under their own category.
    auto                     ids = listing_ids(results);
    std::vector<std::string> rest(ids.begin() + 3, ids.end());
    EXPECT_EQ(rest,
              (std::vector<std::string>{"accessibility.sonify",
                                        "anim.go_to_end",
                                        "app.command_palette",
                                        "view.toggle_grid"}));
    for (size_t i = 3; i < results.size(); ++i)
    {
        EXPECT_TRUE(results[i].section.empty());
    }
}

TEST(CommandRegistry, DefaultListingPutsRecentFirst)
{
    CommandRegistry reg;
    register_listing_fixture(reg);
    reg.execute("anim.go_to_end");
    reg.execute("view.reset");

    auto results = reg.default_listing(listing_options());
    auto ids     = listing_ids(results);
    ASSERT_EQ(ids.size(), reg.count());

    // Most recent first, under the "Recent" section.
    EXPECT_EQ(ids[0], "view.reset");
    EXPECT_EQ(ids[1], "anim.go_to_end");
    EXPECT_EQ(results[0].section, CommandRegistry::RECENT_SECTION);
    EXPECT_EQ(results[1].section, CommandRegistry::RECENT_SECTION);

    // Featured follow, without repeating the recent "view.reset".
    EXPECT_EQ(ids[2], "view.autofit");
    EXPECT_EQ(ids[3], "file.export_png");
    EXPECT_EQ(results[2].section, CommandRegistry::SUGGESTED_SECTION);

    // No command is listed twice.
    std::vector<std::string> sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end());
}

TEST(CommandRegistry, DefaultListingRecentHonoursLimitAndExclusions)
{
    CommandRegistry reg;
    register_listing_fixture(reg);
    reg.execute("accessibility.sonify");
    reg.execute("view.toggle_grid");
    reg.execute("anim.go_to_end");
    reg.execute("app.command_palette");   // Opening the palette must not crowd out recents

    auto results = reg.default_listing(listing_options());
    ASSERT_GE(results.size(), 2u);
    EXPECT_EQ(results[0].command->id, "anim.go_to_end");
    EXPECT_EQ(results[1].command->id, "view.toggle_grid");
    EXPECT_NE(results[2].section, CommandRegistry::RECENT_SECTION);
}

TEST(CommandRegistry, DefaultListingRecentUpdatesAfterExecute)
{
    CommandRegistry reg;
    register_listing_fixture(reg);
    reg.execute("view.toggle_grid");
    EXPECT_EQ(reg.default_listing(listing_options())[0].command->id, "view.toggle_grid");

    reg.execute("file.export_png");
    auto results = reg.default_listing(listing_options());
    EXPECT_EQ(results[0].command->id, "file.export_png");
    EXPECT_EQ(results[1].command->id, "view.toggle_grid");

    reg.clear_recent();
    results = reg.default_listing(listing_options());
    EXPECT_EQ(results[0].command->id, "view.autofit");
    EXPECT_EQ(results[0].section, CommandRegistry::SUGGESTED_SECTION);
}

TEST(CommandRegistry, DefaultListingIsNotTruncated)
{
    CommandRegistry reg;
    for (int i = 0; i < 120; ++i)
    {
        reg.register_command("cmd." + std::to_string(i), "Command " + std::to_string(i), []() {});
    }
    EXPECT_EQ(reg.default_listing(CommandListingOptions{}).size(), 120u);
}

// ─── All commands ────────────────────────────────────────────────────────────

TEST(CommandRegistry, AllCommandsSorted)
{
    CommandRegistry reg;
    reg.register_command("z", "Zeta", []() {}, "", "B");
    reg.register_command("a", "Alpha", []() {}, "", "A");
    reg.register_command("m", "Mu", []() {}, "", "A");

    auto all = reg.all_commands();
    ASSERT_EQ(all.size(), 3u);
    // Sorted by category then label
    EXPECT_EQ(all[0]->id, "a");   // A/Alpha
    EXPECT_EQ(all[1]->id, "m");   // A/Mu
    EXPECT_EQ(all[2]->id, "z");   // B/Zeta
}

// ─── Thread safety ───────────────────────────────────────────────────────────

TEST(CommandRegistry, ConcurrentRegisterAndSearch)
{
    CommandRegistry  reg;
    std::atomic<int> exec_count{0};

    // Register some initial commands
    for (int i = 0; i < 20; ++i)
    {
        reg.register_command("cmd." + std::to_string(i),
                             "Command " + std::to_string(i),
                             [&exec_count]() { exec_count++; });
    }

    std::thread writer(
        [&reg, &exec_count]()
        {
            for (int i = 20; i < 40; ++i)
            {
                reg.register_command("cmd." + std::to_string(i),
                                     "Command " + std::to_string(i),
                                     [&exec_count]() { exec_count++; });
            }
        });

    std::thread reader(
        [&reg]()
        {
            for (int i = 0; i < 50; ++i)
            {
                auto results = reg.search("Command");
                (void)results;
            }
        });

    writer.join();
    reader.join();

    EXPECT_EQ(reg.count(), 40u);
}

// ─── Register with full Command struct ───────────────────────────────────────

TEST(CommandRegistry, RegisterFullStruct)
{
    CommandRegistry reg;
    Command         cmd;
    cmd.id       = "test.full";
    cmd.label    = "Full Command";
    cmd.category = "Test";
    cmd.shortcut = "Ctrl+F";
    cmd.icon     = 42;
    cmd.callback = []() {};
    cmd.enabled  = true;

    reg.register_command(std::move(cmd));

    const Command* found = reg.find("test.full");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->label, "Full Command");
    EXPECT_EQ(found->category, "Test");
    EXPECT_EQ(found->shortcut, "Ctrl+F");
    EXPECT_EQ(found->icon, 42);
}

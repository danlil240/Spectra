#include <atomic>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "ui/command_registry.hpp"

using namespace plotix;

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
    int value = 0;
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
    int value = 0;
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
    int value = 0;
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
    int value = 0;
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
    reg.register_command(
        "test.cmd", "Test Command", []() {}, "Ctrl+T", "Testing");

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

    auto results = reg.search("rv");  // Fuzzy: R-eset V-iew
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
    reg.register_command(
        "a", "A", []() {}, "", "View");
    reg.register_command(
        "b", "B", []() {}, "", "Edit");
    reg.register_command(
        "c", "C", []() {}, "", "View");
    reg.register_command(
        "d", "D", []() {}, "", "File");

    auto cats = reg.categories();
    EXPECT_EQ(cats.size(), 3u);
}

TEST(CommandRegistry, CommandsInCategory)
{
    CommandRegistry reg;
    reg.register_command(
        "a", "A", []() {}, "", "View");
    reg.register_command(
        "b", "B", []() {}, "", "Edit");
    reg.register_command(
        "c", "C", []() {}, "", "View");

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

// ─── All commands ────────────────────────────────────────────────────────────

TEST(CommandRegistry, AllCommandsSorted)
{
    CommandRegistry reg;
    reg.register_command(
        "z", "Zeta", []() {}, "", "B");
    reg.register_command(
        "a", "Alpha", []() {}, "", "A");
    reg.register_command(
        "m", "Mu", []() {}, "", "A");

    auto all = reg.all_commands();
    ASSERT_EQ(all.size(), 3u);
    // Sorted by category then label
    EXPECT_EQ(all[0]->id, "a");  // A/Alpha
    EXPECT_EQ(all[1]->id, "m");  // A/Mu
    EXPECT_EQ(all[2]->id, "z");  // B/Zeta
}

// ─── Thread safety ───────────────────────────────────────────────────────────

TEST(CommandRegistry, ConcurrentRegisterAndSearch)
{
    CommandRegistry reg;
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
    Command cmd;
    cmd.id = "test.full";
    cmd.label = "Full Command";
    cmd.category = "Test";
    cmd.shortcut = "Ctrl+F";
    cmd.icon = 42;
    cmd.callback = []() {};
    cmd.enabled = true;

    reg.register_command(std::move(cmd));

    const Command* found = reg.find("test.full");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->label, "Full Command");
    EXPECT_EQ(found->category, "Test");
    EXPECT_EQ(found->shortcut, "Ctrl+F");
    EXPECT_EQ(found->icon, 42);
}

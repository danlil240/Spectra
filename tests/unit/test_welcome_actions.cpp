#include <gtest/gtest.h>

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/welcome_actions.hpp"

using namespace spectra;

namespace
{

void register_defaults(CommandRegistry& reg)
{
    reg.register_command("figure.new", "New Figure", []() {}, "Ctrl+T", "Figure");
    reg.register_command("app.command_palette", "Command Palette", []() {}, "Ctrl+K", "App");
}

}   // namespace

TEST(WelcomeActions, NullRegistryOffersOpenFileAndTopics)
{
    auto actions = build_welcome_actions(nullptr);
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].id, WELCOME_OPEN_FILE_ID);
    EXPECT_FALSE(actions[0].primary);
    EXPECT_EQ(actions[1].id, WELCOME_TOPICS_ID);
    EXPECT_TRUE(build_welcome_hint(actions).empty());
}

TEST(WelcomeActions, StartColumnOrder)
{
    CommandRegistry reg;
    register_defaults(reg);
    reg.register_command("file.open", "Open File...", []() {}, "Ctrl+O", "File");
    reg.register_command("file.open_layout", "Open Layout...", []() {}, "", "File");
    reg.register_command("panel.toggle_topics", "Toggle Topics", []() {}, "Ctrl+Shift+T", "Panel");
    auto actions = build_welcome_actions(&reg);
    ASSERT_EQ(actions.size(), 4u);
    EXPECT_EQ(actions[0].id, "figure.new");
    EXPECT_TRUE(actions[0].primary);
    EXPECT_EQ(actions[1].id, "file.open");
    EXPECT_EQ(actions[1].shortcut, "Ctrl+O");
    EXPECT_EQ(actions[2].id, "file.open_layout");
    EXPECT_TRUE(actions[2].shortcut.empty());
    EXPECT_EQ(actions[3].id, WELCOME_TOPICS_ID);
    EXPECT_EQ(actions[3].shortcut, "Ctrl+Shift+T");
    for (const auto& a : actions)
        EXPECT_NE(a.icon, 0u) << a.id;
}

TEST(WelcomeActions, OpenFileFallsBackToPseudoAction)
{
    CommandRegistry reg;
    register_defaults(reg);
    auto actions = build_welcome_actions(&reg);
    ASSERT_EQ(actions.size(), 3u);
    EXPECT_EQ(actions[0].id, "figure.new");
    EXPECT_TRUE(actions[0].primary);
    EXPECT_EQ(actions[1].id, WELCOME_OPEN_FILE_ID);
    EXPECT_EQ(actions[2].id, WELCOME_TOPICS_ID);
}

TEST(WelcomeActions, UsesStartPageLabels)
{
    CommandRegistry reg;
    reg.register_command("figure.new", "Create Figure", []() {}, "", "Figure");
    auto actions = build_welcome_actions(&reg);
    ASSERT_FALSE(actions.empty());
    EXPECT_EQ(actions[0].label, "New figure");
}

TEST(WelcomeActions, DisabledOrMissingCommandIsOmitted)
{
    CommandRegistry reg;
    register_defaults(reg);
    reg.set_enabled("figure.new", false);
    auto actions = build_welcome_actions(&reg);
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].id, WELCOME_OPEN_FILE_ID);
}

TEST(WelcomeActions, FallsBackToDeclaredShortcutWithoutBinding)
{
    CommandRegistry reg;
    register_defaults(reg);
    auto actions = build_welcome_actions(&reg, nullptr);
    ASSERT_FALSE(actions.empty());
    EXPECT_EQ(actions[0].shortcut, "Ctrl+T");
}

TEST(WelcomeActions, UsesLiveBindingWhenDeclaredShortcutIsUnbound)
{
    CommandRegistry reg;
    register_defaults(reg);
    ShortcutManager sm;
    sm.set_command_registry(&reg);
    sm.register_defaults();   // binds figure.new to Ctrl+N, not the declared Ctrl+T
    auto actions = build_welcome_actions(&reg, &sm);
    ASSERT_FALSE(actions.empty());
    EXPECT_EQ(actions[0].shortcut, "Ctrl+N");
}

TEST(WelcomeActions, PrefersDeclaredShortcutWhenBound)
{
    CommandRegistry reg;
    register_defaults(reg);
    ShortcutManager sm;
    sm.set_command_registry(&reg);
    sm.register_defaults();
    sm.bind(Shortcut::from_string("Ctrl+T"), "figure.new");
    auto actions = build_welcome_actions(&reg, &sm);
    ASSERT_FALSE(actions.empty());
    EXPECT_EQ(actions[0].shortcut, "Ctrl+T");
}

TEST(WelcomeActions, NoShortcutWhenCommandIsUnbound)
{
    CommandRegistry reg;
    register_defaults(reg);
    ShortcutManager sm;
    sm.set_command_registry(&reg);
    auto actions = build_welcome_actions(&reg, &sm);
    ASSERT_FALSE(actions.empty());
    EXPECT_TRUE(actions[0].shortcut.empty());
    EXPECT_TRUE(build_welcome_hint(actions).empty());
}

TEST(WelcomeActions, HintListsShortcutsAndSkipsActionsWithout)
{
    std::vector<WelcomeAction> actions = {
        {"figure.new", "New Figure", "Ctrl+N", true},
        {std::string(WELCOME_OPEN_FILE_ID), "Open file...", "", false},
        {"app.command_palette", "Command Palette", "Ctrl+K", false},
    };
    EXPECT_EQ(build_welcome_hint(actions), "Ctrl+N  New Figure  \xC2\xB7  Ctrl+K  Command Palette");
}

TEST(WelcomeActions, HintSingleShortcut)
{
    CommandRegistry reg;
    register_defaults(reg);
    EXPECT_EQ(build_welcome_hint(build_welcome_actions(&reg)), "Ctrl+T  New figure");
}

TEST(WelcomeActions, CommandShortcutHelper)
{
    CommandRegistry reg;
    register_defaults(reg);
    EXPECT_EQ(welcome_command_shortcut(&reg, nullptr, "app.command_palette"), "Ctrl+K");
    EXPECT_TRUE(welcome_command_shortcut(&reg, nullptr, "missing.command").empty());
    EXPECT_TRUE(welcome_command_shortcut(nullptr, nullptr, "figure.new").empty());
}

// ─── Recent column ──────────────────────────────────────────────────────────

TEST(WelcomeRecent, FilesFirstThenLayouts)
{
    const std::vector<std::string> files   = {"/data/run1/a.csv", "/logs/flight.ulg"};
    const std::vector<std::string> layouts = {"/home/u/dash.spectra"};
    auto                           items   = build_welcome_recent(files, layouts);
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].name, "a.csv");
    EXPECT_EQ(items[0].folder, "run1");
    EXPECT_EQ(items[0].kind, "CSV");
    EXPECT_FALSE(items[0].layout);
    EXPECT_EQ(items[1].kind, "ULog");
    EXPECT_EQ(items[2].kind, "Layout");
    EXPECT_TRUE(items[2].layout);
    EXPECT_EQ(items[2].path, "/home/u/dash.spectra");
}

TEST(WelcomeRecent, CapsEachListAndSkipsDuplicates)
{
    std::vector<std::string> files;
    for (int i = 0; i < 12; ++i)
        files.push_back("/d/f" + std::to_string(i) + ".csv");
    const std::vector<std::string> layouts = {"/d/f0.csv", "/l/a.spectra", "/l/b.spectra"};
    auto                           items   = build_welcome_recent(files, layouts, 8, 1);
    ASSERT_EQ(items.size(), 9u);
    EXPECT_EQ(items[7].path, "/d/f7.csv");
    EXPECT_EQ(items[8].path, "/l/a.spectra");
}

TEST(WelcomeRecent, EmptyListsGiveNoItems)
{
    EXPECT_TRUE(build_welcome_recent({}, {}).empty());
}

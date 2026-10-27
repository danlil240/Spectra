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

TEST(WelcomeActions, NullRegistryOffersOnlyOpenCsv)
{
    auto actions = build_welcome_actions(nullptr);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].id, WELCOME_OPEN_CSV_ID);
    EXPECT_FALSE(actions[0].primary);
    EXPECT_TRUE(build_welcome_hint(actions).empty());
}

TEST(WelcomeActions, NewFigureIsPrimaryAndFirst)
{
    CommandRegistry reg;
    register_defaults(reg);
    auto actions = build_welcome_actions(&reg);
    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0].id, "figure.new");
    EXPECT_EQ(actions[0].label, "New Figure");
    EXPECT_TRUE(actions[0].primary);
    EXPECT_EQ(actions[1].id, WELCOME_OPEN_CSV_ID);
}

TEST(WelcomeActions, UsesRegistryLabel)
{
    CommandRegistry reg;
    reg.register_command("figure.new", "Create Figure", []() {}, "", "Figure");
    auto actions = build_welcome_actions(&reg);
    ASSERT_FALSE(actions.empty());
    EXPECT_EQ(actions[0].label, "Create Figure");
}

TEST(WelcomeActions, DisabledOrMissingCommandIsOmitted)
{
    CommandRegistry reg;
    register_defaults(reg);
    reg.set_enabled("figure.new", false);
    auto actions = build_welcome_actions(&reg);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].id, WELCOME_OPEN_CSV_ID);
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
        {std::string(WELCOME_OPEN_CSV_ID), "Open CSV...", "", false},
        {"app.command_palette", "Command Palette", "Ctrl+K", false},
    };
    EXPECT_EQ(build_welcome_hint(actions), "Ctrl+N  New Figure  \xC2\xB7  Ctrl+K  Command Palette");
}

TEST(WelcomeActions, HintSingleShortcut)
{
    CommandRegistry reg;
    register_defaults(reg);
    EXPECT_EQ(build_welcome_hint(build_welcome_actions(&reg)), "Ctrl+T  New Figure");
}

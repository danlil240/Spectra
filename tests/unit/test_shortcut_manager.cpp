#include <gtest/gtest.h>
#include <string>

#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"

using namespace spectra;

// ─── Shortcut string conversion ──────────────────────────────────────────────

TEST(Shortcut, ToStringSimpleKey)
{
    Shortcut s;
    s.key  = 65;   // 'A'
    s.mods = KeyMod::None;
    EXPECT_EQ(s.to_string(), "A");
}

TEST(Shortcut, ToStringWithCtrl)
{
    Shortcut s;
    s.key  = 75;   // 'K'
    s.mods = KeyMod::Control;
    EXPECT_EQ(s.to_string(), "Ctrl+K");
}

TEST(Shortcut, ToStringWithCtrlShift)
{
    Shortcut s;
    s.key  = 90;   // 'Z'
    s.mods = KeyMod::Control | KeyMod::Shift;
    EXPECT_EQ(s.to_string(), "Ctrl+Shift+Z");
}

TEST(Shortcut, ToStringSpecialKey)
{
    Shortcut s;
    s.key  = 256;   // Escape
    s.mods = KeyMod::None;
    EXPECT_EQ(s.to_string(), "Escape");
}

TEST(Shortcut, FromStringSimple)
{
    Shortcut s = Shortcut::from_string("A");
    EXPECT_EQ(s.key, 65);
    EXPECT_EQ(s.mods, KeyMod::None);
}

TEST(Shortcut, FromStringCtrlK)
{
    Shortcut s = Shortcut::from_string("Ctrl+K");
    EXPECT_EQ(s.key, 75);
    EXPECT_TRUE(has_mod(s.mods, KeyMod::Control));
}

TEST(Shortcut, FromStringCtrlShiftZ)
{
    Shortcut s = Shortcut::from_string("Ctrl+Shift+Z");
    EXPECT_EQ(s.key, 90);
    EXPECT_TRUE(has_mod(s.mods, KeyMod::Control));
    EXPECT_TRUE(has_mod(s.mods, KeyMod::Shift));
}

TEST(Shortcut, FromStringEscape)
{
    Shortcut s = Shortcut::from_string("Escape");
    EXPECT_EQ(s.key, 256);
}

TEST(Shortcut, FromStringF1)
{
    Shortcut s = Shortcut::from_string("F1");
    EXPECT_EQ(s.key, 290);
}

TEST(Shortcut, RoundTrip)
{
    Shortcut original;
    original.key  = 83;   // 'S'
    original.mods = KeyMod::Control | KeyMod::Shift;

    std::string str    = original.to_string();
    Shortcut    parsed = Shortcut::from_string(str);

    EXPECT_EQ(parsed.key, original.key);
    EXPECT_EQ(parsed.mods, original.mods);
}

TEST(Shortcut, ValidCheck)
{
    Shortcut empty;
    EXPECT_FALSE(empty.valid());

    Shortcut valid;
    valid.key = 65;
    EXPECT_TRUE(valid.valid());
}

TEST(Shortcut, Equality)
{
    Shortcut a{65, KeyMod::Control};
    Shortcut b{65, KeyMod::Control};
    Shortcut c{66, KeyMod::Control};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ─── ShortcutManager binding ─────────────────────────────────────────────────

TEST(ShortcutManager, InitiallyEmpty)
{
    ShortcutManager mgr;
    EXPECT_EQ(mgr.count(), 0u);
}

TEST(ShortcutManager, BindIncrementsCount)
{
    ShortcutManager mgr;
    mgr.bind({65, KeyMod::None}, "test.cmd");
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(ShortcutManager, BindInvalidShortcutIgnored)
{
    ShortcutManager mgr;
    mgr.bind({0, KeyMod::None}, "test.cmd");
    EXPECT_EQ(mgr.count(), 0u);
}

TEST(ShortcutManager, CommandForShortcut)
{
    ShortcutManager mgr;
    mgr.bind({65, KeyMod::Control}, "test.cmd");
    EXPECT_EQ(mgr.command_for_shortcut({65, KeyMod::Control}), "test.cmd");
}

TEST(ShortcutManager, CommandForUnboundShortcut)
{
    ShortcutManager mgr;
    EXPECT_EQ(mgr.command_for_shortcut({65, KeyMod::None}), "");
}

TEST(ShortcutManager, ShortcutForCommand)
{
    ShortcutManager mgr;
    mgr.bind({75, KeyMod::Control}, "app.palette");

    Shortcut sc = mgr.shortcut_for_command("app.palette");
    EXPECT_EQ(sc.key, 75);
    EXPECT_TRUE(has_mod(sc.mods, KeyMod::Control));
}

TEST(ShortcutManager, ShortcutForUnboundCommand)
{
    ShortcutManager mgr;
    Shortcut        sc = mgr.shortcut_for_command("nonexistent");
    EXPECT_FALSE(sc.valid());
}

TEST(ShortcutManager, UnbindRemoves)
{
    ShortcutManager mgr;
    Shortcut        sc{65, KeyMod::None};
    mgr.bind(sc, "test.cmd");
    EXPECT_EQ(mgr.count(), 1u);

    mgr.unbind(sc);
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_EQ(mgr.command_for_shortcut(sc), "");
}

TEST(ShortcutManager, UnbindCommand)
{
    ShortcutManager mgr;
    mgr.bind({65, KeyMod::None}, "test.cmd");
    mgr.bind({66, KeyMod::None}, "test.cmd");
    mgr.bind({67, KeyMod::None}, "other.cmd");
    EXPECT_EQ(mgr.count(), 3u);

    mgr.unbind_command("test.cmd");
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(ShortcutManager, BindOverwritesExisting)
{
    ShortcutManager mgr;
    Shortcut        sc{65, KeyMod::Control};
    mgr.bind(sc, "cmd.a");
    mgr.bind(sc, "cmd.b");

    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.command_for_shortcut(sc), "cmd.b");
}

TEST(ShortcutManager, AllBindings)
{
    ShortcutManager mgr;
    mgr.bind({65, KeyMod::None}, "cmd.a");
    mgr.bind({66, KeyMod::None}, "cmd.b");

    auto bindings = mgr.all_bindings();
    EXPECT_EQ(bindings.size(), 2u);
}

TEST(ShortcutManager, Clear)
{
    ShortcutManager mgr;
    mgr.bind({65, KeyMod::None}, "cmd.a");
    mgr.bind({66, KeyMod::None}, "cmd.b");
    mgr.clear();
    EXPECT_EQ(mgr.count(), 0u);
}

// ─── Key dispatch ────────────────────────────────────────────────────────────

TEST(ShortcutManager, OnKeyExecutesCommand)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Test", [&value]() { value = 42; });

    ShortcutManager mgr;
    mgr.set_command_registry(&reg);
    mgr.bind({65, KeyMod::None}, "test.cmd");

    // action=1 is GLFW_PRESS
    bool handled = mgr.on_key(65, 1, 0);
    EXPECT_TRUE(handled);
    EXPECT_EQ(value, 42);
}

TEST(ShortcutManager, OnKeyIgnoresRelease)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Test", [&value]() { value = 42; });

    ShortcutManager mgr;
    mgr.set_command_registry(&reg);
    mgr.bind({65, KeyMod::None}, "test.cmd");

    // action=0 is GLFW_RELEASE
    bool handled = mgr.on_key(65, 0, 0);
    EXPECT_FALSE(handled);
    EXPECT_EQ(value, 0);
}

TEST(ShortcutManager, OnKeyWithModifiers)
{
    CommandRegistry reg;
    int             value = 0;
    reg.register_command("test.cmd", "Test", [&value]() { value = 42; });

    ShortcutManager mgr;
    mgr.set_command_registry(&reg);
    mgr.bind({75, KeyMod::Control}, "test.cmd");   // Ctrl+K

    // mods=0x02 is GLFW_MOD_CONTROL
    bool handled = mgr.on_key(75, 1, 0x02);
    EXPECT_TRUE(handled);
    EXPECT_EQ(value, 42);
}

TEST(ShortcutManager, OnKeyUnboundReturnsFalse)
{
    CommandRegistry reg;
    ShortcutManager mgr;
    mgr.set_command_registry(&reg);

    EXPECT_FALSE(mgr.on_key(65, 1, 0));
}

TEST(ShortcutManager, OnKeyWithoutRegistryReturnsFalse)
{
    ShortcutManager mgr;
    mgr.bind({65, KeyMod::None}, "test.cmd");
    EXPECT_FALSE(mgr.on_key(65, 1, 0));
}

// ─── Register defaults ──────────────────────────────────────────────────────

TEST(ShortcutManager, RegisterDefaultsPopulatesBindings)
{
    ShortcutManager mgr;
    mgr.register_defaults();
    EXPECT_GT(mgr.count(), 20u);   // Should have 20+ default bindings

    // Check a few specific defaults
    EXPECT_EQ(mgr.command_for_shortcut({75, KeyMod::Control}), "app.command_palette");
    EXPECT_EQ(mgr.command_for_shortcut({82, KeyMod::None}), "view.reset");         // R
    EXPECT_EQ(mgr.command_for_shortcut({71, KeyMod::None}), "view.toggle_grid");   // G
}

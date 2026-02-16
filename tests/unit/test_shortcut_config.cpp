#include <filesystem>
#include <gtest/gtest.h>

#include "ui/command_registry.hpp"
#include "ui/shortcut_config.hpp"
#include "ui/shortcut_manager.hpp"

using namespace spectra;

// ─── Override Management ─────────────────────────────────────────────────────

TEST(ShortcutConfigOverrides, InitiallyEmpty)
{
    ShortcutConfig config;
    EXPECT_EQ(config.override_count(), 0u);
    EXPECT_TRUE(config.overrides().empty());
}

TEST(ShortcutConfigOverrides, SetOverride)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    EXPECT_EQ(config.override_count(), 1u);
    EXPECT_TRUE(config.has_override("view.reset"));
    EXPECT_FALSE(config.has_override("view.zoom"));
}

TEST(ShortcutConfigOverrides, UpdateOverride)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.reset", "Ctrl+Shift+R");
    EXPECT_EQ(config.override_count(), 1u);
    auto overrides = config.overrides();
    EXPECT_EQ(overrides[0].shortcut_str, "Ctrl+Shift+R");
}

TEST(ShortcutConfigOverrides, RemoveOverride)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.zoom", "Ctrl+Z");
    EXPECT_EQ(config.override_count(), 2u);
    config.remove_override("view.reset");
    EXPECT_EQ(config.override_count(), 1u);
    EXPECT_FALSE(config.has_override("view.reset"));
    EXPECT_TRUE(config.has_override("view.zoom"));
}

TEST(ShortcutConfigOverrides, RemoveNonexistent)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.remove_override("nonexistent");
    EXPECT_EQ(config.override_count(), 1u);
}

TEST(ShortcutConfigOverrides, ResetAll)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.zoom", "Ctrl+Z");
    config.set_override("edit.undo", "Ctrl+U");
    config.reset_all();
    EXPECT_EQ(config.override_count(), 0u);
}

TEST(ShortcutConfigOverrides, UnbindOverride)
{
    ShortcutConfig config;
    config.set_override("view.reset", "");
    EXPECT_EQ(config.override_count(), 1u);
    auto overrides = config.overrides();
    EXPECT_TRUE(overrides[0].removed);
    EXPECT_TRUE(overrides[0].shortcut_str.empty());
}

TEST(ShortcutConfigOverrides, MultipleOverrides)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.zoom", "Ctrl+Plus");
    config.set_override("edit.undo", "Ctrl+Z");
    config.set_override("edit.redo", "Ctrl+Shift+Z");
    EXPECT_EQ(config.override_count(), 4u);
}

// ─── Apply Overrides ─────────────────────────────────────────────────────────

TEST(ShortcutConfigApply, ApplyRebind)
{
    CommandRegistry registry;
    bool called = false;
    registry.register_command("test.cmd", "Test", [&]() { called = true; });

    ShortcutManager mgr;
    mgr.set_command_registry(&registry);
    mgr.bind(Shortcut::from_string("Ctrl+T"), "test.cmd");

    ShortcutConfig config;
    config.set_shortcut_manager(&mgr);
    config.set_override("test.cmd", "Ctrl+R");
    config.apply_overrides();

    // Old shortcut should no longer work
    EXPECT_TRUE(mgr.command_for_shortcut(Shortcut::from_string("Ctrl+T")).empty());
    // New shortcut should work
    EXPECT_EQ(mgr.command_for_shortcut(Shortcut::from_string("Ctrl+R")), "test.cmd");
}

TEST(ShortcutConfigApply, ApplyUnbind)
{
    CommandRegistry registry;
    registry.register_command("test.cmd", "Test", []() {});

    ShortcutManager mgr;
    mgr.set_command_registry(&registry);
    mgr.bind(Shortcut::from_string("Ctrl+T"), "test.cmd");

    ShortcutConfig config;
    config.set_shortcut_manager(&mgr);
    config.set_override("test.cmd", "");
    config.apply_overrides();

    // Shortcut should be removed
    EXPECT_TRUE(mgr.shortcut_for_command("test.cmd").key == 0);
}

TEST(ShortcutConfigApply, ApplyWithNullManager)
{
    ShortcutConfig config;
    config.set_override("test.cmd", "Ctrl+R");
    // Should not crash
    config.apply_overrides();
}

TEST(ShortcutConfigApply, ApplyMultipleOverrides)
{
    CommandRegistry registry;
    registry.register_command("cmd.a", "A", []() {});
    registry.register_command("cmd.b", "B", []() {});

    ShortcutManager mgr;
    mgr.set_command_registry(&registry);
    mgr.bind(Shortcut::from_string("Ctrl+A"), "cmd.a");
    mgr.bind(Shortcut::from_string("Ctrl+B"), "cmd.b");

    ShortcutConfig config;
    config.set_shortcut_manager(&mgr);
    config.set_override("cmd.a", "Ctrl+1");
    config.set_override("cmd.b", "Ctrl+2");
    config.apply_overrides();

    EXPECT_EQ(mgr.command_for_shortcut(Shortcut::from_string("Ctrl+1")), "cmd.a");
    EXPECT_EQ(mgr.command_for_shortcut(Shortcut::from_string("Ctrl+2")), "cmd.b");
}

// ─── Serialization ───────────────────────────────────────────────────────────

TEST(ShortcutConfigSerialize, EmptyConfig)
{
    ShortcutConfig config;
    std::string json = config.serialize();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"version\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"bindings\""), std::string::npos);
}

TEST(ShortcutConfigSerialize, RoundTrip)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.zoom", "Ctrl+Plus");
    config.set_override("edit.undo", "");

    std::string json = config.serialize();

    ShortcutConfig config2;
    EXPECT_TRUE(config2.deserialize(json));
    EXPECT_EQ(config2.override_count(), 3u);

    auto overrides = config2.overrides();
    bool found_reset = false, found_zoom = false, found_undo = false;
    for (const auto& o : overrides)
    {
        if (o.command_id == "view.reset")
        {
            EXPECT_EQ(o.shortcut_str, "Ctrl+R");
            EXPECT_FALSE(o.removed);
            found_reset = true;
        }
        if (o.command_id == "view.zoom")
        {
            EXPECT_EQ(o.shortcut_str, "Ctrl+Plus");
            found_zoom = true;
        }
        if (o.command_id == "edit.undo")
        {
            EXPECT_TRUE(o.removed);
            found_undo = true;
        }
    }
    EXPECT_TRUE(found_reset);
    EXPECT_TRUE(found_zoom);
    EXPECT_TRUE(found_undo);
}

TEST(ShortcutConfigSerialize, DeserializeEmpty)
{
    ShortcutConfig config;
    EXPECT_FALSE(config.deserialize(""));
}

TEST(ShortcutConfigSerialize, DeserializeFutureVersion)
{
    ShortcutConfig config;
    EXPECT_FALSE(config.deserialize("{\"version\": 99, \"bindings\": []}"));
}

TEST(ShortcutConfigSerialize, DeserializeNoBindings)
{
    ShortcutConfig config;
    EXPECT_TRUE(config.deserialize("{\"version\": 1}"));
    EXPECT_EQ(config.override_count(), 0u);
}

TEST(ShortcutConfigSerialize, SpecialCharacters)
{
    ShortcutConfig config;
    config.set_override("plugin.my\"cmd", "Ctrl+Shift+A");

    std::string json = config.serialize();
    ShortcutConfig config2;
    EXPECT_TRUE(config2.deserialize(json));
    EXPECT_EQ(config2.override_count(), 1u);
}

// ─── File I/O ────────────────────────────────────────────────────────────────

TEST(ShortcutConfigFile, SaveAndLoad)
{
    auto path = std::filesystem::temp_directory_path() / "plotix_test_keybindings.json";
    auto path_str = path.string();

    // Cleanup
    std::filesystem::remove(path);

    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");
    config.set_override("view.zoom", "Ctrl+Plus");
    EXPECT_TRUE(config.save(path_str));
    EXPECT_TRUE(std::filesystem::exists(path));

    ShortcutConfig config2;
    EXPECT_TRUE(config2.load(path_str));
    EXPECT_EQ(config2.override_count(), 2u);

    // Cleanup
    std::filesystem::remove(path);
}

TEST(ShortcutConfigFile, LoadNonexistent)
{
    ShortcutConfig config;
    EXPECT_FALSE(config.load("/nonexistent/path/keybindings.json"));
}

TEST(ShortcutConfigFile, SaveToInvalidPath)
{
    ShortcutConfig config;
    // This might succeed on some systems (creates dirs), might fail on others
    // Just ensure it doesn't crash
    config.save("/dev/null/impossible/path/keybindings.json");
}

TEST(ShortcutConfigFile, DefaultPath)
{
    std::string path = ShortcutConfig::default_path();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("keybindings.json"), std::string::npos);
}

// ─── Callback ────────────────────────────────────────────────────────────────

TEST(ShortcutConfigCallback, OnChangeCalledOnSet)
{
    ShortcutConfig config;
    int change_count = 0;
    config.set_on_change([&]() { ++change_count; });

    config.set_override("view.reset", "Ctrl+R");
    EXPECT_EQ(change_count, 1);

    config.set_override("view.zoom", "Ctrl+Z");
    EXPECT_EQ(change_count, 2);
}

TEST(ShortcutConfigCallback, OnChangeCalledOnRemove)
{
    ShortcutConfig config;
    int change_count = 0;
    config.set_override("view.reset", "Ctrl+R");

    config.set_on_change([&]() { ++change_count; });
    config.remove_override("view.reset");
    EXPECT_EQ(change_count, 1);
}

TEST(ShortcutConfigCallback, OnChangeCalledOnReset)
{
    ShortcutConfig config;
    config.set_override("view.reset", "Ctrl+R");

    int change_count = 0;
    config.set_on_change([&]() { ++change_count; });
    config.reset_all();
    EXPECT_EQ(change_count, 1);
}

TEST(ShortcutConfigCallback, NoCallbackNoCrash)
{
    ShortcutConfig config;
    // No callback set — should not crash
    config.set_override("view.reset", "Ctrl+R");
    config.remove_override("view.reset");
    config.reset_all();
}

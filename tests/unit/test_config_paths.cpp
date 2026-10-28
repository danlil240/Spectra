// Tests for Spectra's per-user config directory resolution
// (src/core/config_paths.hpp): SPECTRA_CONFIG_DIR -> XDG_CONFIG_HOME/spectra ->
// $HOME/.config/spectra, and that every per-user file follows it.

#include <gtest/gtest.h>

#include "core/config_paths.hpp"
#include "env_guard.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef SPECTRA_USE_IMGUI
    #include "ui/commands/shortcut_config.hpp"
    #include "ui/settings/settings_store.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/workspace/plugin_api.hpp"
    #include "ui/workspace/workspace.hpp"
    #include "ui/workspace/workspace_autosave.hpp"
#endif

namespace fs = std::filesystem;
using spectra::ConfigDirEnv;
using spectra::resolve_config_dir;
using spectra::test::EnvGuard;

namespace
{

fs::path make_temp_dir(const std::string& tag)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path   dir =
        fs::temp_directory_path() / ("spectra_config_test_" + tag + "_" + std::to_string(stamp));
    fs::create_directories(dir);
    return dir;
}

// Saves and restores every variable the resolution reads, so a test can set
// any combination without leaking into later tests.
struct ConfigEnvScope
{
    EnvGuard spectra_dir{"SPECTRA_CONFIG_DIR"};
    EnvGuard xdg{"XDG_CONFIG_HOME"};
    EnvGuard home{"HOME"};
    EnvGuard userprofile{"USERPROFILE"};
};

}   // namespace

// ─── Pure resolution order ───────────────────────────────────────────────────

TEST(ConfigDirResolve, SpectraConfigDirWinsAndIsUsedAsIs)
{
    ConfigDirEnv env;
    env.spectra_config_dir = "/iso/cfg";
    env.xdg_config_home    = "/xdg";
    env.home               = "/home/u";
    EXPECT_EQ(resolve_config_dir(env, false), fs::path("/iso/cfg"));
    EXPECT_EQ(resolve_config_dir(env, true), fs::path("/iso/cfg"));
}

TEST(ConfigDirResolve, EmptySpectraConfigDirIsIgnored)
{
    ConfigDirEnv env;
    env.spectra_config_dir = "";
    env.xdg_config_home    = "/xdg";
    env.home               = "/home/u";
    EXPECT_EQ(resolve_config_dir(env, false), fs::path("/xdg") / "spectra");
}

TEST(ConfigDirResolve, AbsoluteXdgConfigHomeBeatsHome)
{
    ConfigDirEnv env;
    env.xdg_config_home = "/xdg";
    env.home            = "/home/u";
    EXPECT_EQ(resolve_config_dir(env, false), fs::path("/xdg") / "spectra");
}

TEST(ConfigDirResolve, RelativeOrEmptyXdgConfigHomeIsIgnored)
{
    ConfigDirEnv env;
    env.home                = "/home/u";
    const fs::path expected = fs::path("/home/u") / ".config" / "spectra";

    env.xdg_config_home = "relative/cfg";
    EXPECT_EQ(resolve_config_dir(env, false), expected);
    env.xdg_config_home = "";
    EXPECT_EQ(resolve_config_dir(env, false), expected);
}

TEST(ConfigDirResolve, XdgConfigHomeIgnoredOnWindows)
{
    ConfigDirEnv env;
    env.xdg_config_home = "/xdg";
    env.userprofile     = "C:/Users/u";
    EXPECT_EQ(resolve_config_dir(env, true), fs::path("C:/Users/u") / ".config" / "spectra");
}

TEST(ConfigDirResolve, HomeDefaultThenUserProfile)
{
    ConfigDirEnv env;
    env.home        = "/home/u";
    env.userprofile = "/profile";
    EXPECT_EQ(resolve_config_dir(env, false), fs::path("/home/u") / ".config" / "spectra");

    env.home = nullptr;
    EXPECT_EQ(resolve_config_dir(env, false), fs::path("/profile") / ".config" / "spectra");

    env.home = "";
    EXPECT_EQ(resolve_config_dir(env, false), fs::path("/profile") / ".config" / "spectra");
}

TEST(ConfigDirResolve, NothingKnownGivesEmpty)
{
    EXPECT_TRUE(resolve_config_dir(ConfigDirEnv{}, false).empty());
    EXPECT_TRUE(resolve_config_dir(ConfigDirEnv{}, true).empty());
}

// ─── Process environment ─────────────────────────────────────────────────────

TEST(ConfigDirEnvironment, SpectraConfigDirOverride)
{
    ConfigEnvScope scope;
    scope.spectra_dir.set("/iso/spectra");
    scope.xdg.set("/xdg");
    scope.home.set("/home/u");
    EXPECT_EQ(spectra::user_config_dir(), fs::path("/iso/spectra"));
    EXPECT_TRUE(spectra::user_config_dir_overridden());
}

#ifndef _WIN32
TEST(ConfigDirEnvironment, XdgConfigHome)
{
    ConfigEnvScope scope;
    scope.spectra_dir.unset();
    scope.xdg.set("/xdg");
    scope.home.set("/home/u");
    EXPECT_EQ(spectra::user_config_dir(), fs::path("/xdg") / "spectra");
    EXPECT_TRUE(spectra::user_config_dir_overridden());
}
#endif

TEST(ConfigDirEnvironment, BackwardCompatibleHomeDefault)
{
    ConfigEnvScope scope;
    scope.spectra_dir.unset();
    scope.xdg.unset();
    scope.home.set("/home/u");
    EXPECT_EQ(spectra::user_config_dir(), fs::path("/home/u") / ".config" / "spectra");
    EXPECT_FALSE(spectra::user_config_dir_overridden());
}

TEST(ConfigDirEnvironment, RestoredAfterScope)
{
    const char*       before = std::getenv("SPECTRA_CONFIG_DIR");
    const bool        had    = before != nullptr;
    const std::string saved  = had ? before : "";
    {
        ConfigEnvScope scope;
        scope.spectra_dir.set("/tmp/spectra_config_scope_probe");
    }
    const char* after = std::getenv("SPECTRA_CONFIG_DIR");
    if (had)
    {
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(saved, after);
    }
    else
    {
        // _putenv_s(name, "") removes the variable on Windows.
        EXPECT_TRUE(after == nullptr || after[0] == '\0');
    }
}

// ─── Every per-user file follows the config dir ──────────────────────────────

#ifdef SPECTRA_USE_IMGUI

TEST(ConfigDirFiles, AllDefaultPathsUseSpectraConfigDir)
{
    ConfigEnvScope scope;
    const fs::path dir = make_temp_dir("all");
    scope.spectra_dir.set(dir.string());

    EXPECT_EQ(fs::path(spectra::ui::settings::SettingsStore::default_path()),
              dir / "settings.json");
    EXPECT_EQ(fs::path(spectra::ShortcutConfig::default_path()), dir / "keybindings.json");
    EXPECT_EQ(fs::path(spectra::Workspace::default_path()), dir / "workspace.spectra");
    EXPECT_EQ(fs::path(spectra::WorkspaceAutosave::default_autosave_path()),
              dir / "autosave.spectra");
    EXPECT_EQ(fs::path(spectra::PluginManager::default_plugin_dir()), dir / "plugins");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

    #ifndef _WIN32
TEST(ConfigDirFiles, SettingsFollowXdgConfigHome)
{
    ConfigEnvScope scope;
    const fs::path xdg = make_temp_dir("xdg");
    scope.spectra_dir.unset();
    scope.xdg.set(xdg.string());
    EXPECT_EQ(fs::path(spectra::ui::settings::SettingsStore::default_path()),
              xdg / "spectra" / "settings.json");

    std::error_code ec;
    fs::remove_all(xdg, ec);
}
    #endif

TEST(ConfigDirFiles, SettingsStoreLoadReadsIsolatedDir)
{
    ConfigEnvScope scope;
    const fs::path dir = make_temp_dir("load");
    scope.spectra_dir.set(dir.string());

    spectra::ui::settings::SettingsStore empty_store;
    EXPECT_FALSE(empty_store.load());   // nothing in the isolated dir

    {
        std::ofstream f(dir / "settings.json");
        f << R"({"default_theme": "light", "default_data_palette": "colorblind"})";
    }
    spectra::ui::settings::SettingsStore store;
    ASSERT_TRUE(store.load());
    EXPECT_EQ(store.data().default_theme, "light");
    EXPECT_EQ(store.data().default_data_palette, "colorblind");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ConfigDirFiles, DefaultThemeSavedInConfigDir)
{
    ConfigEnvScope scope;
    const fs::path dir = make_temp_dir("theme");
    scope.spectra_dir.set(dir.string());

    spectra::ui::ThemeManager tm;
    tm.ensure_initialized();
    tm.set_theme("light");
    tm.save_current_as_default();
    EXPECT_TRUE(fs::exists(dir / "default_theme.json"));

    spectra::ui::ThemeManager reloaded;
    reloaded.ensure_initialized();
    reloaded.set_theme("dark");
    reloaded.load_default();
    EXPECT_EQ(reloaded.current_theme_name(), "light");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ConfigDirFiles, LegacyDefaultThemeStillFoundWithoutOverride)
{
    ConfigEnvScope scope;
    const fs::path home = make_temp_dir("legacy_home");
    scope.spectra_dir.unset();
    scope.xdg.unset();
    scope.home.set(home.string());

    {
        spectra::ui::ThemeManager src;
        src.ensure_initialized();
        src.set_theme("light");
        fs::create_directories(home / ".spectra");
        ASSERT_TRUE(src.export_theme((home / ".spectra" / "default_theme.json").string()));
    }

    spectra::ui::ThemeManager tm;
    tm.ensure_initialized();
    tm.set_theme("dark");
    tm.load_default();
    EXPECT_EQ(tm.current_theme_name(), "light");

    // ...but an isolated config dir never reads it.
    const fs::path iso = make_temp_dir("legacy_iso");
    scope.spectra_dir.set(iso.string());
    spectra::ui::ThemeManager isolated;
    isolated.ensure_initialized();
    isolated.set_theme("dark");
    isolated.load_default();
    EXPECT_EQ(isolated.current_theme_name(), "dark");

    std::error_code ec;
    fs::remove_all(home, ec);
    fs::remove_all(iso, ec);
}

#endif   // SPECTRA_USE_IMGUI

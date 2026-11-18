// Glass / transparency persistence: Settings -> Appearance saves glass into
// settings.json, and the startup path (SettingsStore::load + migrate_legacy_glass
// + apply_startup_appearance, as App::run and the multiproc window process do)
// restores it.  default_theme.json is only read once, to migrate old glass.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "env_guard.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"

namespace fs = std::filesystem;
using spectra::test::EnvGuard;
using spectra::ui::ThemeGlassSettings;
using spectra::ui::ThemeManager;
using namespace spectra::ui::settings;

namespace
{

class GlassPersistence : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir_ = fs::temp_directory_path() / ("spectra_glass_test_" + std::to_string(stamp));
        fs::create_directories(dir_);
        spectra_dir_.set(dir_.string());
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    // One window-process start: what src/agent/main.cpp does (App::run passes
    // the same SettingsData to apply_startup_appearance()).
    struct Session
    {
        ThemeManager  tm;
        SettingsStore store;
    };
    static void start(Session& s, const std::string& theme_override = {})
    {
        s.tm.ensure_initialized();
        s.store.load();
        s.store.set_on_change([&store = s.store]() { store.save(); });
        s.store.migrate_legacy_glass(s.tm);
        apply_startup_appearance(s.tm, &s.store.data(), theme_override, {});
    }

    // What dragging a slider in Settings -> Appearance -> Glass does.
    static void change_glass(Session& s, const ThemeGlassSettings& g)
    {
        s.tm.set_glass_settings(g, true);
        remember_glass(s.tm, s.store);
    }
    // What clicking a theme card does.
    static void pick_theme(Session& s, const std::string& id)
    {
        ASSERT_TRUE(select_theme(s.tm, s.store, id));
        s.tm.reset_glass_defaults();
        remember_glass(s.tm, s.store);
    }

    static ThemeGlassSettings custom_glass()
    {
        ThemeGlassSettings g;
        g.master_intensity = 0.9f;
        g.panel_alpha      = 0.5f;
        g.plot_alpha       = 0.75f;
        g.toolbar_alpha    = 0.45f;
        g.glow_strength    = 0.1f;
        g.blur_enabled     = true;
        return g;
    }

    fs::path settings_file() const { return dir_ / "settings.json"; }

    fs::path dir_;
    EnvGuard spectra_dir_{"SPECTRA_CONFIG_DIR"};
};

}   // namespace

TEST_F(GlassPersistence, CustomGlassSurvivesRestart)
{
    {
        Session s;
        start(s);
        EXPECT_EQ(s.tm.current_theme_name(), "night");
        change_glass(s, custom_glass());
        EXPECT_TRUE(fs::exists(settings_file()));
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.current_theme_name(), "night");
    EXPECT_EQ(s.tm.glass(), custom_glass());
    EXPECT_TRUE(s.store.has_saved_glass());
    // The legacy snapshot file is no longer written.
    EXPECT_FALSE(fs::exists(dir_ / "default_theme.json"));
}

TEST_F(GlassPersistence, CustomGlassFollowsPickedTheme)
{
    {
        Session s;
        start(s);
        pick_theme(s, "dark");
        change_glass(s, custom_glass());
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.current_theme_name(), "dark");
    EXPECT_EQ(s.tm.glass(), custom_glass());
}

TEST_F(GlassPersistence, ResetToThemeDefaultsIsRestored)
{
    {
        Session s;
        start(s);
        change_glass(s, custom_glass());
        s.tm.reset_glass_defaults();   // "Reset Theme Defaults"
        remember_glass(s.tm, s.store);
        EXPECT_FALSE(s.store.data().glass.has_value());
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.glass(), ThemeGlassSettings::night_defaults());
}

TEST_F(GlassPersistence, ThemeSwitchDropsGlassOfPreviousTheme)
{
    {
        Session s;
        start(s);
        change_glass(s, custom_glass());
        pick_theme(s, "light");
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.current_theme_name(), "light");
    EXPECT_EQ(s.tm.glass(), ThemeGlassSettings::light_defaults());
}

TEST_F(GlassPersistence, StartupThemeGetsItsOwnGlassDefaults)
{
    // A light startup theme used to start with the night glass defaults.
    {
        Session s;
        start(s);
        pick_theme(s, "light");
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.current_theme_name(), "light");
    EXPECT_EQ(s.tm.glass(), ThemeGlassSettings::light_defaults());
}

TEST_F(GlassPersistence, ExplicitStartupThemeIgnoresOtherThemesGlass)
{
    {
        Session s;
        start(s);
        change_glass(s, custom_glass());   // for "night"
    }
    Session s;
    start(s, "dark");   // e.g. AppConfig::theme
    EXPECT_EQ(s.tm.current_theme_name(), "dark");
    EXPECT_EQ(s.tm.glass(), ThemeGlassSettings::dark_defaults());
}

TEST_F(GlassPersistence, MakeDefaultKeepsVisibleGlass)
{
    {
        Session s;
        start(s);
        s.tm.set_theme("dark");   // a theme command: active only
        change_glass(s, custom_glass());
        make_active_theme_default(s.tm, s.store);
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.current_theme_name(), "dark");
    EXPECT_EQ(s.tm.glass(), custom_glass());
}

TEST_F(GlassPersistence, ToggleThemesRestored)
{
    {
        Session s;
        start(s);
        pick_theme(s, "dark");
        pick_theme(s, "high_contrast");
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.last_dark_theme(), "dark");
}

TEST_F(GlassPersistence, WithoutUserSettingsGlassIsUntouched)
{
    // Headless apps (no settings.json): theme only, glass stays as constructed.
    ThemeManager tm;
    tm.ensure_initialized();
    tm.set_glass_settings(custom_glass(), false);
    apply_startup_appearance(tm, nullptr, "light", {});
    EXPECT_EQ(tm.current_theme_name(), "light");
    EXPECT_EQ(tm.glass(), custom_glass());
}

TEST_F(GlassPersistence, LegacyDefaultThemeGlassMigratedOnce)
{
    {
        // An older build: glass saved only to default_theme.json.
        ThemeManager old;
        old.ensure_initialized();
        old.set_glass_settings(custom_glass(), false);
        old.save_current_as_default();
        ASSERT_TRUE(fs::exists(dir_ / "default_theme.json"));
        std::ofstream(settings_file()) << R"({"version":1,"ui":{"default_theme":"night"}})";
    }
    {
        Session s;
        start(s);
        EXPECT_EQ(s.tm.glass(), custom_glass());
        // Migrated into settings.json ...
        SettingsStore reread;
        ASSERT_TRUE(reread.load(settings_file().string()));
        EXPECT_TRUE(reread.has_saved_glass());
        ASSERT_TRUE(reread.data().glass.has_value());
        EXPECT_EQ(*reread.data().glass, custom_glass());
        EXPECT_EQ(reread.data().glass_theme, "night");
        // ... so a reset is not undone by the old file.
        s.tm.reset_glass_defaults();
        remember_glass(s.tm, s.store);
    }
    Session s;
    start(s);
    EXPECT_EQ(s.tm.glass(), ThemeGlassSettings::night_defaults());
}

TEST_F(GlassPersistence, SerializedValuesAreClamped)
{
    std::ofstream(settings_file())
        << R"({"version":1,"ui":{"default_theme":"night"},"glass":{"glass_custom":true,)"
           R"("glass_theme":"night","glass_master_intensity":4,"glass_panel_alpha":0.0,)"
           R"("glass_plot_alpha":0.5,"glass_toolbar_alpha":0.5,"glass_glow_strength":-1,)"
           R"("glass_blur_enabled":false}})";
    Session s;
    start(s);
    EXPECT_FLOAT_EQ(s.tm.glass().master_intensity, 1.0f);
    EXPECT_FLOAT_EQ(s.tm.glass().panel_alpha, 0.35f);
    EXPECT_FLOAT_EQ(s.tm.glass().plot_alpha, 0.5f);
    EXPECT_FLOAT_EQ(s.tm.glass().glow_strength, 0.0f);
}

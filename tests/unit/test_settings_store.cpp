#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

#include "ui/settings/settings_store.hpp"

using spectra::ui::settings::reads_user_settings;
using spectra::ui::settings::resolve_startup_theme;
using spectra::ui::settings::SettingsData;
using spectra::ui::settings::SettingsStore;

namespace
{

SettingsData light_user_settings()
{
    SettingsData d;
    d.default_theme        = "light";
    d.default_data_palette = "colorblind";
    return d;
}

}   // namespace

// ─── reads_user_settings ─────────────────────────────────────────────────────

TEST(StartupTheme, WindowedAppsReadUserSettings)
{
    EXPECT_TRUE(reads_user_settings(/*headless=*/false, /*use_user_settings=*/false));
    EXPECT_TRUE(reads_user_settings(/*headless=*/false, /*use_user_settings=*/true));
}

TEST(StartupTheme, HeadlessAppsIgnoreUserSettingsUnlessAsked)
{
    EXPECT_FALSE(reads_user_settings(/*headless=*/true, /*use_user_settings=*/false));
    EXPECT_TRUE(reads_user_settings(/*headless=*/true, /*use_user_settings=*/true));
}

// ─── resolve_startup_theme ───────────────────────────────────────────────────

TEST(StartupTheme, NoUserSettingsUsesBuiltInDefaults)
{
    const auto t = resolve_startup_theme(nullptr, "", "");
    EXPECT_EQ(t.theme, "night");
    EXPECT_EQ(t.data_palette, "default");
}

TEST(StartupTheme, UserSettingsUsedWhenConfigEmpty)
{
    const SettingsData user = light_user_settings();
    const auto         t    = resolve_startup_theme(&user, "", "");
    EXPECT_EQ(t.theme, "light");
    EXPECT_EQ(t.data_palette, "colorblind");
}

TEST(StartupTheme, ExplicitConfigOverridesUserSettings)
{
    const SettingsData user = light_user_settings();
    const auto         t    = resolve_startup_theme(&user, "dark", "tol_bright");
    EXPECT_EQ(t.theme, "dark");
    EXPECT_EQ(t.data_palette, "tol_bright");
}

TEST(StartupTheme, ExplicitConfigOverridesDefaultsWithoutUserSettings)
{
    const auto t = resolve_startup_theme(nullptr, "light", "");
    EXPECT_EQ(t.theme, "light");
    EXPECT_EQ(t.data_palette, "default");
}

TEST(StartupTheme, EachFieldResolvesIndependently)
{
    const SettingsData user = light_user_settings();
    const auto         t    = resolve_startup_theme(&user, "", "wong");
    EXPECT_EQ(t.theme, "light");
    EXPECT_EQ(t.data_palette, "wong");
}

TEST(StartupTheme, EmptyUserValuesFallBackToDefaults)
{
    SettingsData user;
    user.default_theme        = "";
    user.default_data_palette = "";
    const auto t              = resolve_startup_theme(&user, "", "");
    EXPECT_EQ(t.theme, "night");
    EXPECT_EQ(t.data_palette, "default");
}

// ─── SettingsStore file round trip feeding the resolver ─────────────────────

TEST(StartupTheme, LoadedSettingsFileFeedsResolver)
{
    const auto path =
        std::filesystem::temp_directory_path()
        / ("spectra_settings_store_test_"
           + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".json");
    {
        std::ofstream f(path);
        f << R"({"version":1,"ui":{"default_theme":"light","default_data_palette":"ibm"}})";
    }

    SettingsStore store;
    ASSERT_TRUE(store.load(path.string()));
    const auto from_user = resolve_startup_theme(&store.data(), "", "");
    EXPECT_EQ(from_user.theme, "light");
    EXPECT_EQ(from_user.data_palette, "ibm");

    const auto overridden = resolve_startup_theme(&store.data(), "high_contrast", "");
    EXPECT_EQ(overridden.theme, "high_contrast");
    EXPECT_EQ(overridden.data_palette, "ibm");

    std::filesystem::remove(path);
}

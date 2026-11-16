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

// ─── Recent data files (start page) ──────────────────────────────────────────

TEST(RecentFiles, PushMovesToFrontAndDeduplicates)
{
    std::vector<std::string> list;
    spectra::ui::settings::push_recent_file(list, "/d/a.csv");
    spectra::ui::settings::push_recent_file(list, "/d/b.ulg");
    spectra::ui::settings::push_recent_file(list, "/d/./a.csv");
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], std::filesystem::path("/d/a.csv").lexically_normal().string());
    EXPECT_EQ(list[1], std::filesystem::path("/d/b.ulg").string());
}

TEST(RecentFiles, CappedAndIgnoresEmptyPaths)
{
    std::vector<std::string> list;
    for (int i = 0; i < 20; ++i)
        spectra::ui::settings::push_recent_file(list, "/d/f" + std::to_string(i) + ".csv");
    spectra::ui::settings::push_recent_file(list, "");
    ASSERT_EQ(list.size(), spectra::ui::settings::MAX_RECENT_FILES);
    EXPECT_EQ(list.front(), std::filesystem::path("/d/f19.csv").string());
}

TEST(RecentFiles, RelativePathsAreStoredAbsolute)
{
    std::vector<std::string> list;
    spectra::ui::settings::push_recent_file(list, "rel.csv");
    ASSERT_EQ(list.size(), 1u);
    EXPECT_TRUE(std::filesystem::path(list[0]).is_absolute());
    spectra::ui::settings::remove_recent_file(list, "rel.csv");
    EXPECT_TRUE(list.empty());
}

TEST(RecentFiles, StoreNotifiesAndRoundTrips)
{
    const auto path =
        std::filesystem::temp_directory_path()
        / ("spectra_recent_files_test_"
           + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".json");
    SettingsStore store;
    int           changes = 0;
    store.set_on_change([&]() { ++changes; });
    store.add_recent_file("/data/with \"quote\".csv");
    store.add_recent_file("/data/b.mcap");
    EXPECT_EQ(changes, 2);
    store.data_mut().default_theme = "light";
    ASSERT_TRUE(store.save(path.string()));

    SettingsStore loaded;
    ASSERT_TRUE(loaded.load(path.string()));
    ASSERT_EQ(loaded.data().recent_files.size(), 2u);
    EXPECT_EQ(loaded.data().recent_files[0], "/data/b.mcap");
    EXPECT_EQ(loaded.data().recent_files[1], "/data/with \"quote\".csv");
    EXPECT_EQ(loaded.data().default_theme, "light");

    loaded.forget_recent_file("/data/b.mcap");
    EXPECT_EQ(loaded.data().recent_files.size(), 1u);
    loaded.clear_recent_files();
    EXPECT_TRUE(loaded.data().recent_files.empty());
    std::filesystem::remove(path);
}

TEST(RecentFiles, OldSettingsFilesHaveNoRecentFiles)
{
    const auto path =
        std::filesystem::temp_directory_path()
        / ("spectra_recent_files_old_"
           + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".json");
    {
        std::ofstream f(path);
        f << R"({"version":1,"ui":{"default_theme":"dark"}})";
    }
    SettingsStore store;
    ASSERT_TRUE(store.load(path.string()));
    EXPECT_TRUE(store.data().recent_files.empty());
    std::filesystem::remove(path);
}

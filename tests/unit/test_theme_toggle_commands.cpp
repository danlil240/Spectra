// Theme commands: "Toggle Dark/Light Theme" switches between the last-used dark
// and light themes (high contrast is separate) and remembers the pair in
// settings; theme.<id> / palette.<id> commands are generated from the registry.

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "ui/app/commands/command_context.hpp"
#include "ui/app/commands/command_descriptor.hpp"
#include "ui/app/commands/command_groups.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_registry.hpp"
#include "ui/settings/settings_store.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;
using namespace spectra::ui::settings;

namespace
{

class ThemeToggleCommands : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        tm_.ensure_initialized();
        ui::ThemeManager::set_current(&tm_);
        tm_.set_theme("night");
        tm_.set_data_palette("default");
        store_.set_on_change([this]() { ++saves_; });
        ui_.theme_mgr      = &tm_;
        ui_.settings_store = &store_;
        CommandContext ctx{ui_, registry_, &active_fig_, &active_id_};
        commands_ = make_theme_commands(ctx);
    }

    void TearDown() override { ui::ThemeManager::set_current(nullptr); }

    const CommandDescriptor* find(const std::string& id) const
    {
        auto it = std::find_if(commands_.begin(),
                               commands_.end(),
                               [&](const CommandDescriptor& c) { return c.id == id; });
        return it == commands_.end() ? nullptr : &*it;
    }

    void run(const std::string& id)
    {
        const CommandDescriptor* c = find(id);
        ASSERT_NE(c, nullptr) << id;
        c->action();
    }

    ui::ThemeManager               tm_;
    SettingsStore                  store_;
    int                            saves_ = 0;
    WindowUIContext                ui_;
    FigureRegistry                 registry_;
    Figure*                        active_fig_ = nullptr;
    FigureId                       active_id_  = INVALID_FIGURE_ID;
    std::vector<CommandDescriptor> commands_;
};

}   // namespace

TEST_F(ThemeToggleCommands, ThemeKinds)
{
    EXPECT_EQ(tm_.theme_kind("night"), ui::ThemeKind::Dark);
    EXPECT_EQ(tm_.theme_kind("dark"), ui::ThemeKind::Dark);
    EXPECT_EQ(tm_.theme_kind("light"), ui::ThemeKind::Light);
    EXPECT_EQ(tm_.theme_kind("high_contrast"), ui::ThemeKind::HighContrast);
}

TEST_F(ThemeToggleCommands, ToggleFromDefaultNightGoesToLightAndBack)
{
    run("theme.toggle");
    EXPECT_EQ(tm_.current_theme_name(), "light");
    run("theme.toggle");
    EXPECT_EQ(tm_.current_theme_name(), "night");   // not "dark"
}

TEST_F(ThemeToggleCommands, ToggleReturnsToLastUsedDarkTheme)
{
    run("theme.dark");
    run("theme.toggle");
    EXPECT_EQ(tm_.current_theme_name(), "light");
    run("theme.toggle");
    EXPECT_EQ(tm_.current_theme_name(), "dark");
}

TEST_F(ThemeToggleCommands, HighContrastIsNotRememberedAsDark)
{
    run("theme.high_contrast");
    EXPECT_EQ(tm_.current_theme_name(), "high_contrast");
    EXPECT_EQ(tm_.last_dark_theme(), "night");
    run("theme.toggle");   // high contrast -> light
    EXPECT_EQ(tm_.current_theme_name(), "light");
    run("theme.toggle");   // light -> night, not high contrast
    EXPECT_EQ(tm_.current_theme_name(), "night");
}

TEST_F(ThemeToggleCommands, ToggleIsUndoable)
{
    run("theme.toggle");
    ASSERT_TRUE(ui_.undo_mgr.undo());
    EXPECT_EQ(tm_.current_theme_name(), "night");
    ASSERT_TRUE(ui_.undo_mgr.redo());
    EXPECT_EQ(tm_.current_theme_name(), "light");
}

TEST_F(ThemeToggleCommands, TogglePersistsPairButNotStartupDefault)
{
    run("theme.dark");
    EXPECT_EQ(saves_, 0);   // plain theme commands do not save settings
    run("theme.toggle");
    EXPECT_EQ(store_.data().last_dark_theme, "dark");
    EXPECT_EQ(store_.data().last_light_theme, "light");
    EXPECT_EQ(store_.data().default_theme, "night");   // startup default untouched (S16)
    EXPECT_EQ(saves_, 1);
}

TEST_F(ThemeToggleCommands, PairRoundTripsThroughSettingsFile)
{
    const auto path =
        (std::filesystem::temp_directory_path() / "spectra_test_theme_toggle_settings.json")
            .string();
    store_.data_mut().last_dark_theme  = "dark";
    store_.data_mut().last_light_theme = "light";
    ASSERT_TRUE(store_.save(path));

    SettingsStore loaded;
    ASSERT_TRUE(loaded.load(path));
    EXPECT_EQ(loaded.data().last_dark_theme, "dark");

    ui::ThemeManager tm;
    loaded.apply_to(tm);   // startup theme "night" (default) ...
    EXPECT_EQ(tm.current_theme_name(), "night");
    // ... is itself the last dark theme now; the light one comes from the file.
    EXPECT_EQ(tm.last_light_theme(), "light");
    std::filesystem::remove(path);
}

TEST_F(ThemeToggleCommands, LastThemesRejectWrongKindOrUnknown)
{
    tm_.set_last_themes("light", "night");
    EXPECT_EQ(tm_.last_dark_theme(), "night");
    EXPECT_EQ(tm_.last_light_theme(), "light");
    tm_.set_last_themes("no_such", "high_contrast");
    EXPECT_EQ(tm_.last_dark_theme(), "night");
    EXPECT_EQ(tm_.last_light_theme(), "light");
}

TEST_F(ThemeToggleCommands, EveryThemeAndPaletteHasACommand)
{
    for (const std::string& id : tm_.available_themes())
        EXPECT_NE(find("theme." + id), nullptr) << id;
    for (const std::string& id : tm_.available_data_palettes())
        EXPECT_NE(find("palette." + id), nullptr) << id;
    ASSERT_NE(find("theme.high_contrast"), nullptr);
    EXPECT_EQ(find("theme.high_contrast")->label, "Switch to High Contrast Theme");
    ASSERT_NE(find("palette.viridis"), nullptr);
    EXPECT_EQ(find("palette.viridis")->label, "Use Viridis Data Palette");
    EXPECT_NE(find("palette.colorblind"), nullptr);
    EXPECT_NE(find("palette.monochrome"), nullptr);
    // Built-ins are listed first, in a fixed order.
    const auto themes = tm_.available_themes();
    ASSERT_GE(themes.size(), 4u);
    EXPECT_EQ(themes[0], "night");
    EXPECT_EQ(themes[3], "high_contrast");
}

TEST_F(ThemeToggleCommands, PaletteCommandSwitchesAndUndoes)
{
    run("palette.viridis");
    EXPECT_EQ(tm_.current_data_palette_name(), "viridis");
    EXPECT_EQ(saves_, 0);
    ASSERT_TRUE(ui_.undo_mgr.undo());
    EXPECT_EQ(tm_.current_data_palette_name(), "default");
}

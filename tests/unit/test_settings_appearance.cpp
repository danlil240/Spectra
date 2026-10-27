// Settings -> Appearance contract (audit S16): the panel shows the ACTIVE
// theme / data palette, a panel selection also saves the startup default, and
// theme commands + undo/redo change only the active choice.

#include <algorithm>
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

class SettingsAppearance : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        tm_.ensure_initialized();
        ui::ThemeManager::set_current(&tm_);
        store_.data_mut().default_theme = "night";
        store_.set_on_change([this]() { ++saves_; });
        ASSERT_TRUE(select_theme(tm_, store_, "night"));
        saves_ = 0;

        ui_.theme_mgr = &tm_;
        CommandContext ctx{ui_, registry_, &active_fig_, &active_id_};
        commands_ = make_theme_commands(ctx);
    }

    void TearDown() override { ui::ThemeManager::set_current(nullptr); }

    void run(const std::string& id)
    {
        auto it = std::find_if(commands_.begin(),
                               commands_.end(),
                               [&](const CommandDescriptor& c) { return c.id == id; });
        ASSERT_NE(it, commands_.end()) << id;
        it->action();
    }

    AppearanceState state() const { return appearance_state(tm_, store_.data()); }

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

TEST_F(SettingsAppearance, CommandSwitchReportsActiveThemeAndKeepsStartupDefault)
{
    run("theme.light");

    const AppearanceState s = state();
    EXPECT_EQ(s.active_theme, "light");
    EXPECT_EQ(s.startup_theme, "night");
    EXPECT_FALSE(s.theme_matches_startup());   // panel shows the "Startup default" hint
    EXPECT_EQ(store_.data().default_theme, "night");
    EXPECT_EQ(saves_, 0);
}

TEST_F(SettingsAppearance, ToggleCommandReportsActiveTheme)
{
    run("theme.toggle");   // night -> dark
    EXPECT_EQ(state().active_theme, "dark");
    run("theme.toggle");   // dark -> light
    EXPECT_EQ(state().active_theme, "light");
    EXPECT_EQ(state().startup_theme, "night");
}

TEST_F(SettingsAppearance, UndoRedoKeepsPanelTruthful)
{
    run("theme.light");
    ASSERT_TRUE(ui_.undo_mgr.undo());
    EXPECT_EQ(state().active_theme, "night");
    EXPECT_TRUE(state().theme_matches_startup());

    ASSERT_TRUE(ui_.undo_mgr.redo());
    EXPECT_EQ(state().active_theme, "light");
    EXPECT_FALSE(state().theme_matches_startup());
    EXPECT_EQ(store_.data().default_theme, "night");
}

TEST_F(SettingsAppearance, PanelThemeSelectionPersistsStartupDefault)
{
    run("theme.dark");
    ASSERT_TRUE(select_theme(tm_, store_, "light"));

    EXPECT_EQ(tm_.current_theme_name(), "light");
    EXPECT_EQ(store_.data().default_theme, "light");
    EXPECT_TRUE(state().theme_matches_startup());
    EXPECT_EQ(saves_, 1);

    // Undoing the earlier command changes the active theme only.
    ASSERT_TRUE(ui_.undo_mgr.undo());
    EXPECT_EQ(state().active_theme, "night");
    EXPECT_EQ(state().startup_theme, "light");
}

TEST_F(SettingsAppearance, UnknownThemeSelectionChangesNothing)
{
    EXPECT_FALSE(select_theme(tm_, store_, "no_such_theme"));
    EXPECT_EQ(tm_.current_theme_name(), "night");
    EXPECT_EQ(store_.data().default_theme, "night");
    EXPECT_EQ(saves_, 0);
}

TEST_F(SettingsAppearance, MakeDefaultSavesActiveTheme)
{
    run("theme.light");
    make_active_theme_default(tm_, store_);
    EXPECT_EQ(store_.data().default_theme, "light");
    EXPECT_TRUE(state().theme_matches_startup());
    EXPECT_EQ(saves_, 1);
}

TEST_F(SettingsAppearance, PaletteStateReportsActivePalette)
{
    store_.data_mut().default_data_palette = "default";
    tm_.set_data_palette("viridis");   // e.g. automation / external change

    AppearanceState s = state();
    EXPECT_EQ(s.active_data_palette, "viridis");
    EXPECT_EQ(s.startup_data_palette, "default");
    EXPECT_FALSE(s.data_palette_matches_startup());

    make_active_data_palette_default(tm_, store_);
    EXPECT_EQ(store_.data().default_data_palette, "viridis");
    EXPECT_TRUE(state().data_palette_matches_startup());
}

TEST_F(SettingsAppearance, PanelPaletteSelectionPersistsStartupDefault)
{
    ASSERT_TRUE(select_data_palette(tm_, store_, "wong"));
    EXPECT_EQ(tm_.current_data_palette_name(), "wong");
    EXPECT_EQ(store_.data().default_data_palette, "wong");
    EXPECT_EQ(saves_, 1);

    EXPECT_FALSE(select_data_palette(tm_, store_, "no_such_palette"));
    EXPECT_EQ(tm_.current_data_palette_name(), "wong");
    EXPECT_EQ(store_.data().default_data_palette, "wong");
    EXPECT_EQ(saves_, 1);
}

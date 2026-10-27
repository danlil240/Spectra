#include <gtest/gtest.h>
#include <spectra/theme_api.hpp>
#include <vector>

#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{
std::vector<ThemeSnapshot> g_calls;

void record_call(const ThemeSnapshot& snap)
{
    g_calls.push_back(snap);
}

void expect_rgba_eq(const float got[4], const ui::Color& want)
{
    EXPECT_FLOAT_EQ(got[0], want.r);
    EXPECT_FLOAT_EQ(got[1], want.g);
    EXPECT_FLOAT_EQ(got[2], want.b);
    EXPECT_FLOAT_EQ(got[3], want.a);
}

class ThemeChangeCallbackTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ui::ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
        tm_.set_theme("night");
        g_calls.clear();
        register_theme_change_callback(&record_call);
    }

    void TearDown() override
    {
        register_theme_change_callback(nullptr);
        g_calls.clear();
        ui::ThemeManager::set_current(nullptr);
    }

    ui::ThemeManager tm_;
};
}   // namespace

TEST_F(ThemeChangeCallbackTest, SwitchNightToLightDeliversLightSnapshot)
{
    const ui::Color night_canvas = tm_.colors().bg_canvas;
    tm_.set_theme("light");

    ASSERT_EQ(g_calls.size(), 1u);
    const ThemeSnapshot&   snap  = g_calls[0];
    const ui::ThemeColors& light = tm_.colors();
    ASSERT_EQ(tm_.current_theme_name(), "light");
    EXPECT_NE(light.bg_canvas, night_canvas);
    expect_rgba_eq(snap.bg_canvas, light.bg_canvas);
    expect_rgba_eq(snap.bg_primary, light.bg_primary);
    expect_rgba_eq(snap.text_primary, light.text_primary);
    expect_rgba_eq(snap.accent, light.accent);
    expect_rgba_eq(snap.crosshair, light.crosshair);
    EXPECT_EQ(snap.version, tm_.theme_version());

    ThemeSnapshot polled{};
    EXPECT_EQ(get_theme_snapshot(polled), snap.version);
}

TEST_F(ThemeChangeCallbackTest, UnregisterStopsCalls)
{
    register_theme_change_callback(nullptr);
    tm_.set_theme("light");
    tm_.set_theme("night");
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(ThemeChangeCallbackTest, ReapplyingSameThemeDoesNotRepeat)
{
    tm_.set_theme("night");   // already active
    EXPECT_TRUE(g_calls.empty());

    tm_.set_theme("light");
    tm_.set_theme("light");
    tm_.set_theme("light");
    EXPECT_EQ(g_calls.size(), 1u);

    tm_.set_theme("night");
    EXPECT_EQ(g_calls.size(), 2u);
}

TEST_F(ThemeChangeCallbackTest, UnknownThemeDoesNotFire)
{
    tm_.set_theme("does-not-exist");
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(ThemeChangeCallbackTest, TransitionFiresOnceOnCompletion)
{
    tm_.transition_to("light", 0.2f);
    tm_.update(0.05f);
    tm_.update(0.05f);
    EXPECT_TRUE(g_calls.empty()) << "intermediate frames must not be reported";

    tm_.update(0.2f);   // completes
    ASSERT_EQ(g_calls.size(), 1u);
    EXPECT_EQ(tm_.current_theme_name(), "light");
    expect_rgba_eq(g_calls[0].bg_canvas, tm_.colors().bg_canvas);
}

TEST_F(ThemeChangeCallbackTest, PaletteAndGlassChangesDoNotFire)
{
    tm_.set_data_palette("colorblind");
    tm_.reset_glass_defaults();
    EXPECT_TRUE(g_calls.empty());
}

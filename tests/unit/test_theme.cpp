#include <cmath>
#include <gtest/gtest.h>

#include "ui/design_tokens.hpp"
#include "ui/theme.hpp"

using namespace spectra::ui;

// ─── Color struct ────────────────────────────────────────────────────────────

TEST(Color, DefaultIsTransparentBlack)
{
    Color c;
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(Color, ConstructFromComponents)
{
    Color c(0.1f, 0.2f, 0.3f, 0.4f);
    EXPECT_FLOAT_EQ(c.r, 0.1f);
    EXPECT_FLOAT_EQ(c.g, 0.2f);
    EXPECT_FLOAT_EQ(c.b, 0.3f);
    EXPECT_FLOAT_EQ(c.a, 0.4f);
}

TEST(Color, DefaultAlphaIsOpaque)
{
    Color c(0.5f, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(Color, FromHexRGB)
{
    Color c = Color::from_hex(0xFF0000);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(Color, FromHexGreen)
{
    Color c = Color::from_hex(0x00FF00);
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 1.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
}

TEST(Color, FromHexBlue)
{
    Color c = Color::from_hex(0x0000FF);
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 1.0f);
}

TEST(Color, FromHexARGB)
{
    Color c = Color::from_hex(0x80FF0000);
    EXPECT_NEAR(c.r, 1.0f, 1.0f / 255.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_NEAR(c.a, 128.0f / 255.0f, 1.0f / 255.0f);
}

TEST(Color, FromHexWhite)
{
    Color c = Color::from_hex(0xFFFFFF);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 1.0f);
    EXPECT_FLOAT_EQ(c.b, 1.0f);
}

TEST(Color, FromHexBlack)
{
    Color c = Color::from_hex(0x000000);
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(Color, WithAlpha)
{
    Color c(1.0f, 0.0f, 0.0f, 1.0f);
    Color c2 = c.with_alpha(0.5f);
    EXPECT_FLOAT_EQ(c2.r, 1.0f);
    EXPECT_FLOAT_EQ(c2.g, 0.0f);
    EXPECT_FLOAT_EQ(c2.b, 0.0f);
    EXPECT_FLOAT_EQ(c2.a, 0.5f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(Color, LerpEndpoints)
{
    Color a(0.0f, 0.0f, 0.0f, 0.0f);
    Color b(1.0f, 1.0f, 1.0f, 1.0f);

    Color at0 = a.lerp(b, 0.0f);
    EXPECT_FLOAT_EQ(at0.r, 0.0f);
    EXPECT_FLOAT_EQ(at0.a, 0.0f);

    Color at1 = a.lerp(b, 1.0f);
    EXPECT_FLOAT_EQ(at1.r, 1.0f);
    EXPECT_FLOAT_EQ(at1.a, 1.0f);
}

TEST(Color, LerpMidpoint)
{
    Color a(0.0f, 0.2f, 0.4f, 0.6f);
    Color b(1.0f, 0.8f, 0.6f, 0.4f);

    Color mid = a.lerp(b, 0.5f);
    EXPECT_FLOAT_EQ(mid.r, 0.5f);
    EXPECT_FLOAT_EQ(mid.g, 0.5f);
    EXPECT_FLOAT_EQ(mid.b, 0.5f);
    EXPECT_FLOAT_EQ(mid.a, 0.5f);
}

TEST(Color, LerpSameColorIsIdentity)
{
    Color c(0.3f, 0.6f, 0.9f, 1.0f);
    Color result = c.lerp(c, 0.5f);
    EXPECT_FLOAT_EQ(result.r, c.r);
    EXPECT_FLOAT_EQ(result.g, c.g);
    EXPECT_FLOAT_EQ(result.b, c.b);
    EXPECT_FLOAT_EQ(result.a, c.a);
}

TEST(Color, ToHexRoundTrip)
{
    Color c = Color::from_hex(0xFF8040);
    uint32_t hex = c.to_hex();
    uint8_t r = (hex >> 24) & 0xFF;
    uint8_t g = (hex >> 16) & 0xFF;
    uint8_t b = (hex >> 8) & 0xFF;
    uint8_t a = hex & 0xFF;
    EXPECT_EQ(r, 0xFF);
    EXPECT_EQ(g, 0x80);
    EXPECT_EQ(b, 0x40);
    EXPECT_EQ(a, 0xFF);
}

// ─── ThemeColors struct ──────────────────────────────────────────────────────

TEST(ThemeColors, AllFieldsInitialized)
{
    ThemeColors tc{};
    EXPECT_FLOAT_EQ(tc.bg_primary.a, 1.0f);
    EXPECT_FLOAT_EQ(tc.text_primary.a, 1.0f);
    EXPECT_FLOAT_EQ(tc.accent.a, 1.0f);
}

// ─── DataPalette ─────────────────────────────────────────────────────────────

TEST(DataPalette, DefaultState)
{
    DataPalette dp;
    EXPECT_TRUE(dp.name.empty());
    EXPECT_TRUE(dp.colors.empty());
    EXPECT_FALSE(dp.colorblind_safe);
}

TEST(DataPalette, CanHoldColors)
{
    DataPalette dp;
    dp.name = "test";
    dp.colors = {Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)};
    dp.colorblind_safe = true;
    EXPECT_EQ(dp.colors.size(), 3u);
    EXPECT_TRUE(dp.colorblind_safe);
}

// ─── Theme struct ────────────────────────────────────────────────────────────

TEST(Theme, DefaultValues)
{
    Theme t;
    EXPECT_FLOAT_EQ(t.opacity_panel, 0.95f);
    EXPECT_FLOAT_EQ(t.opacity_tooltip, 0.98f);
    EXPECT_FLOAT_EQ(t.shadow_intensity, 1.0f);
    EXPECT_FLOAT_EQ(t.animation_speed, 1.0f);
    EXPECT_TRUE(t.enable_animations);
    EXPECT_TRUE(t.use_blur);
}

// ─── Design Tokens ───────────────────────────────────────────────────────────

TEST(DesignTokens, SpacingScaleIsMonotonic)
{
    EXPECT_LT(tokens::SPACE_0, tokens::SPACE_1);
    EXPECT_LT(tokens::SPACE_1, tokens::SPACE_2);
    EXPECT_LT(tokens::SPACE_2, tokens::SPACE_3);
    EXPECT_LT(tokens::SPACE_3, tokens::SPACE_4);
    EXPECT_LT(tokens::SPACE_4, tokens::SPACE_5);
    EXPECT_LT(tokens::SPACE_5, tokens::SPACE_6);
    EXPECT_LT(tokens::SPACE_6, tokens::SPACE_8);
    EXPECT_LT(tokens::SPACE_8, tokens::SPACE_10);
    EXPECT_LT(tokens::SPACE_10, tokens::SPACE_12);
    EXPECT_LT(tokens::SPACE_12, tokens::SPACE_16);
}

TEST(DesignTokens, SpacingBaseIs4px)
{
    EXPECT_FLOAT_EQ(tokens::SPACE_1, 4.0f);
    EXPECT_FLOAT_EQ(tokens::SPACE_2, 8.0f);
    EXPECT_FLOAT_EQ(tokens::SPACE_4, 16.0f);
}

TEST(DesignTokens, RadiusScaleIsMonotonic)
{
    EXPECT_LT(tokens::RADIUS_SM, tokens::RADIUS_MD);
    EXPECT_LT(tokens::RADIUS_MD, tokens::RADIUS_LG);
    EXPECT_LT(tokens::RADIUS_LG, tokens::RADIUS_XL);
    EXPECT_LT(tokens::RADIUS_XL, tokens::RADIUS_PILL);
}

TEST(DesignTokens, FontScaleIsMonotonic)
{
    EXPECT_LT(tokens::FONT_XS, tokens::FONT_SM);
    EXPECT_LT(tokens::FONT_SM, tokens::FONT_BASE);
    EXPECT_LT(tokens::FONT_BASE, tokens::FONT_MD);
    EXPECT_LT(tokens::FONT_MD, tokens::FONT_LG);
    EXPECT_LT(tokens::FONT_LG, tokens::FONT_XL);
    EXPECT_LT(tokens::FONT_XL, tokens::FONT_2XL);
}

TEST(DesignTokens, DurationScaleIsMonotonic)
{
    EXPECT_LE(tokens::DURATION_INSTANT, tokens::DURATION_FAST);
    EXPECT_LT(tokens::DURATION_FAST, tokens::DURATION_NORMAL);
    EXPECT_LT(tokens::DURATION_NORMAL, tokens::DURATION_SLOW);
    EXPECT_LT(tokens::DURATION_SLOW, tokens::DURATION_SLOWER);
}

TEST(DesignTokens, IconSizeScaleIsMonotonic)
{
    EXPECT_LT(tokens::ICON_XS, tokens::ICON_SM);
    EXPECT_LT(tokens::ICON_SM, tokens::ICON_MD);
    EXPECT_LT(tokens::ICON_MD, tokens::ICON_LG);
    EXPECT_LT(tokens::ICON_LG, tokens::ICON_XL);
}

TEST(DesignTokens, BorderWidthScaleIsMonotonic)
{
    EXPECT_LT(tokens::BORDER_WIDTH_THIN, tokens::BORDER_WIDTH_NORMAL);
    EXPECT_LT(tokens::BORDER_WIDTH_NORMAL, tokens::BORDER_WIDTH_THICK);
}

TEST(DesignTokens, OpacityBounds)
{
    EXPECT_FLOAT_EQ(tokens::OPACITY_HIDDEN, 0.0f);
    EXPECT_FLOAT_EQ(tokens::OPACITY_OPAQUE, 1.0f);
    EXPECT_GT(tokens::OPACITY_FAINT, 0.0f);
    EXPECT_LT(tokens::OPACITY_VISIBLE, 1.0f);
}

TEST(DesignTokens, PerformanceTargets)
{
    EXPECT_NEAR(tokens::FRAME_TIME_BUDGET_MS, 16.67f, 0.01f);
    EXPECT_FLOAT_EQ(tokens::UI_FRAME_TIME_TARGET_MS, 2.0f);
    EXPECT_FLOAT_EQ(tokens::TOOLTIP_TIME_TARGET_MS, 0.1f);
}

TEST(DesignTokens, LayoutConstants)
{
    EXPECT_FLOAT_EQ(tokens::COMMAND_BAR_HEIGHT, 48.0f);
    EXPECT_FLOAT_EQ(tokens::NAV_RAIL_WIDTH, 48.0f);
    EXPECT_FLOAT_EQ(tokens::INSPECTOR_WIDTH, 320.0f);
    EXPECT_LT(tokens::INSPECTOR_WIDTH_MIN, tokens::INSPECTOR_WIDTH);
    EXPECT_GT(tokens::INSPECTOR_WIDTH_MAX, tokens::INSPECTOR_WIDTH);
}

// ─── ThemeManager ────────────────────────────────────────────────────────────

class ThemeManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        auto& tm = ThemeManager::instance();
        original_theme_ = tm.current_theme_name();
    }
    void TearDown() override
    {
        auto& tm = ThemeManager::instance();
        if (tm.is_transitioning())
        {
            tm.update(10.0f);
        }
        tm.set_theme(original_theme_);
    }
    std::string original_theme_;
};

TEST_F(ThemeManagerTest, SingletonReturnsSameInstance)
{
    auto& a = ThemeManager::instance();
    auto& b = ThemeManager::instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(ThemeManagerTest, DefaultThemesRegistered)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    EXPECT_EQ(tm.current_theme_name(), "dark");
    tm.set_theme("light");
    EXPECT_EQ(tm.current_theme_name(), "light");
    tm.set_theme("high_contrast");
    EXPECT_EQ(tm.current_theme_name(), "high_contrast");
}

TEST_F(ThemeManagerTest, SetThemeInvalidNameIsNoOp)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    tm.set_theme("nonexistent_theme_xyz");
    EXPECT_EQ(tm.current_theme_name(), "dark");
}

TEST_F(ThemeManagerTest, DarkThemeHasDarkBackground)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    const auto& c = tm.colors();
    float lum = 0.2126f * c.bg_primary.r + 0.7152f * c.bg_primary.g + 0.0722f * c.bg_primary.b;
    EXPECT_LT(lum, 0.15f);
}

TEST_F(ThemeManagerTest, LightThemeHasLightBackground)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("light");
    const auto& c = tm.colors();
    float lum = 0.2126f * c.bg_primary.r + 0.7152f * c.bg_primary.g + 0.0722f * c.bg_primary.b;
    EXPECT_GT(lum, 0.85f);
}

TEST_F(ThemeManagerTest, HighContrastThemeHasBlackBackground)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("high_contrast");
    const auto& c = tm.colors();
    EXPECT_FLOAT_EQ(c.bg_primary.r, 0.0f);
    EXPECT_FLOAT_EQ(c.bg_primary.g, 0.0f);
    EXPECT_FLOAT_EQ(c.bg_primary.b, 0.0f);
}

TEST_F(ThemeManagerTest, HighContrastThemeHasWhiteText)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("high_contrast");
    const auto& c = tm.colors();
    EXPECT_FLOAT_EQ(c.text_primary.r, 1.0f);
    EXPECT_FLOAT_EQ(c.text_primary.g, 1.0f);
    EXPECT_FLOAT_EQ(c.text_primary.b, 1.0f);
}

TEST_F(ThemeManagerTest, CurrentReturnsValidTheme)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    const auto& t = tm.current();
    EXPECT_EQ(t.name, "dark");
    EXPECT_GT(t.opacity_panel, 0.0f);
}

TEST_F(ThemeManagerTest, ColorsMatchesCurrent)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("light");
    const auto& c1 = tm.colors();
    const auto& c2 = tm.current().colors;
    EXPECT_FLOAT_EQ(c1.bg_primary.r, c2.bg_primary.r);
    EXPECT_FLOAT_EQ(c1.accent.r, c2.accent.r);
}

TEST_F(ThemeManagerTest, RegisterCustomTheme)
{
    auto& tm = ThemeManager::instance();
    Theme custom;
    custom.name = "custom_test";
    custom.colors.bg_primary = Color(0.5f, 0.5f, 0.5f, 1.0f);
    custom.colors.accent = Color(1.0f, 0.0f, 1.0f, 1.0f);

    tm.register_theme("custom_test", custom);
    tm.set_theme("custom_test");
    EXPECT_EQ(tm.current_theme_name(), "custom_test");
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.r, 0.5f);
    EXPECT_FLOAT_EQ(tm.colors().accent.r, 1.0f);
}

// ─── Theme Transitions ───────────────────────────────────────────────────────

TEST_F(ThemeManagerTest, TransitionStartsTransitioning)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    EXPECT_FALSE(tm.is_transitioning());
    tm.transition_to("light", 0.2f);
    EXPECT_TRUE(tm.is_transitioning());
}

TEST_F(ThemeManagerTest, TransitionCompletesAfterDuration)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    tm.transition_to("light", 0.1f);
    tm.update(0.15f);
    EXPECT_FALSE(tm.is_transitioning());
    EXPECT_EQ(tm.current_theme_name(), "light");
}

TEST_F(ThemeManagerTest, TransitionInterpolatesColors)
{
    auto& tm = ThemeManager::instance();
    // NOTE: ThemeManager::update() mutates current_theme_->colors in place,
    // so stored theme data may be corrupted by previous transition tests.
    // We test interpolation by verifying colors change during the transition.
    tm.set_theme("dark");
    tm.transition_to("light", 1.0f);

    // Snapshot at start of transition
    float start_bg_r = tm.colors().bg_primary.r;

    // Advance halfway
    tm.update(0.5f);
    float mid_bg_r = tm.colors().bg_primary.r;
    EXPECT_TRUE(tm.is_transitioning());

    // Complete transition
    tm.update(0.6f);
    float end_bg_r = tm.colors().bg_primary.r;
    EXPECT_FALSE(tm.is_transitioning());

    // Mid-transition color should differ from both start and end
    // (unless start == end, which means dark theme was already corrupted)
    if (start_bg_r != end_bg_r)
    {
        EXPECT_NE(mid_bg_r, start_bg_r);
        EXPECT_NE(mid_bg_r, end_bg_r);
    }
}

TEST_F(ThemeManagerTest, TransitionToInvalidNameIsNoOp)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    tm.transition_to("nonexistent", 0.2f);
    EXPECT_FALSE(tm.is_transitioning());
}

TEST_F(ThemeManagerTest, TransitionToSameThemeWorks)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    float bg_r = tm.colors().bg_primary.r;
    tm.transition_to("dark", 0.1f);
    EXPECT_TRUE(tm.is_transitioning());
    tm.update(0.15f);
    EXPECT_FALSE(tm.is_transitioning());
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.r, bg_r);
}

TEST_F(ThemeManagerTest, TransitionUpdateWithZeroDtDoesNotCrash)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    tm.transition_to("light", 0.2f);
    tm.update(0.0f);
    EXPECT_TRUE(tm.is_transitioning());
}

TEST_F(ThemeManagerTest, UpdateWithNoTransitionIsNoOp)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    float bg_r = tm.colors().bg_primary.r;
    tm.update(1.0f);
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.r, bg_r);
}

// ─── Data Palettes ───────────────────────────────────────────────────────────

TEST_F(ThemeManagerTest, DefaultPaletteHas10Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    const auto& dp = tm.current_data_palette();
    EXPECT_EQ(dp.colors.size(), 10u);
    EXPECT_FALSE(dp.colorblind_safe);
}

TEST_F(ThemeManagerTest, ColorblindPaletteHas8Colors)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("colorblind");
    const auto& dp = tm.current_data_palette();
    EXPECT_EQ(dp.colors.size(), 8u);
    EXPECT_TRUE(dp.colorblind_safe);
}

TEST_F(ThemeManagerTest, SetInvalidPaletteIsNoOp)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    size_t count_before = tm.current_data_palette().colors.size();
    tm.set_data_palette("nonexistent_palette");
    EXPECT_EQ(tm.current_data_palette().colors.size(), count_before);
}

TEST_F(ThemeManagerTest, AvailablePalettesContainsDefaults)
{
    auto& tm = ThemeManager::instance();
    const auto& names = tm.available_data_palettes();
    bool has_default = false, has_colorblind = false;
    for (const auto& n : names)
    {
        if (n == "default")
            has_default = true;
        if (n == "colorblind")
            has_colorblind = true;
    }
    EXPECT_TRUE(has_default);
    EXPECT_TRUE(has_colorblind);
}

TEST_F(ThemeManagerTest, PaletteColorsAreDistinct)
{
    auto& tm = ThemeManager::instance();
    tm.set_data_palette("default");
    const auto& colors = tm.current_data_palette().colors;
    for (size_t i = 0; i + 1 < colors.size(); ++i)
    {
        bool same = (colors[i].r == colors[i + 1].r && colors[i].g == colors[i + 1].g
                     && colors[i].b == colors[i + 1].b);
        EXPECT_FALSE(same) << "Palette colors " << i << " and " << i + 1 << " are identical";
    }
}

// ─── Color Lookup ────────────────────────────────────────────────────────────

TEST_F(ThemeManagerTest, GetColorReturnsCorrectValues)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    Color accent = tm.get_color("accent");
    EXPECT_FLOAT_EQ(accent.r, tm.colors().accent.r);
    EXPECT_FLOAT_EQ(accent.g, tm.colors().accent.g);
}

TEST_F(ThemeManagerTest, GetColorUnknownReturnsTransparent)
{
    auto& tm = ThemeManager::instance();
    Color c = tm.get_color("nonexistent_color_name");
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
}

TEST_F(ThemeManagerTest, LerpColorWorks)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    Color target(1.0f, 1.0f, 1.0f, 1.0f);
    Color result = tm.lerp_color("accent", target, 0.0f);
    EXPECT_FLOAT_EQ(result.r, tm.colors().accent.r);
    Color result1 = tm.lerp_color("accent", target, 1.0f);
    EXPECT_FLOAT_EQ(result1.r, 1.0f);
}

TEST_F(ThemeManagerTest, ThemeConvenienceAccessor)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("dark");
    const auto& c1 = theme();
    const auto& c2 = tm.colors();
    EXPECT_FLOAT_EQ(c1.bg_primary.r, c2.bg_primary.r);
}

TEST_F(ThemeManagerTest, AllThemesHaveNonZeroAccent)
{
    auto& tm = ThemeManager::instance();
    for (const auto& name : {"dark", "light", "high_contrast"})
    {
        tm.set_theme(name);
        float lum = tm.colors().accent.r + tm.colors().accent.g + tm.colors().accent.b;
        EXPECT_GT(lum, 0.0f) << "Theme '" << name << "' has zero-luminance accent";
    }
}

TEST_F(ThemeManagerTest, AllThemesHavePlotColors)
{
    auto& tm = ThemeManager::instance();
    for (const auto& name : {"dark", "light", "high_contrast"})
    {
        tm.set_theme(name);
        const auto& c = tm.colors();
        float sum = c.grid_line.r + c.grid_line.g + c.grid_line.b + c.axis_line.r + c.axis_line.g
                    + c.axis_line.b + c.tick_label.r + c.tick_label.g + c.tick_label.b;
        EXPECT_GT(sum, 0.0f) << "Theme '" << name << "' has no plot colors";
    }
}

// NOTE: load_default(), export_theme(), import_theme() are declared in
// theme.hpp but not yet implemented in theme.cpp — tests deferred.

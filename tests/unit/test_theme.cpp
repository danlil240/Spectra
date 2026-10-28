#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>

#include "ui/theme/design_tokens.hpp"
#include "ui/theme/glass_tokens.hpp"
#include "ui/theme/svg_style.hpp"
#include "ui/theme/theme.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_THEME_IMGUI 1
    #include <imgui.h>
#endif

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
    Color    c   = Color::from_hex(0xFF8040);
    uint32_t hex = c.to_hex();
    uint8_t  r   = (hex >> 24) & 0xFF;
    uint8_t  g   = (hex >> 16) & 0xFF;
    uint8_t  b   = (hex >> 8) & 0xFF;
    uint8_t  a   = hex & 0xFF;
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
    dp.name            = "test";
    dp.colors          = {Color(1, 0, 0), Color(0, 1, 0), Color(0, 0, 1)};
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
    EXPECT_FLOAT_EQ(tokens::NAV_RAIL_WIDTH, 56.0f);
    EXPECT_FLOAT_EQ(tokens::INSPECTOR_WIDTH, 320.0f);
    EXPECT_LT(tokens::INSPECTOR_WIDTH_MIN, tokens::INSPECTOR_WIDTH);
    EXPECT_GT(tokens::INSPECTOR_WIDTH_MAX, tokens::INSPECTOR_WIDTH);

    // Layout rhythm (8px grid)
    EXPECT_FLOAT_EQ(tokens::PANEL_PADDING, 16.0f);
    EXPECT_FLOAT_EQ(tokens::SECTION_GAP, 12.0f);
    EXPECT_FLOAT_EQ(tokens::SERIES_ROW_HEIGHT, 30.0f);
    EXPECT_FLOAT_EQ(tokens::ICON_BUTTON_HITBOX, 32.0f);
    EXPECT_FLOAT_EQ(tokens::INSPECTOR_HEADER_H, 32.0f);
}

// ─── ThemeManager ────────────────────────────────────────────────────────────

class ThemeManagerTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
        original_theme_ = tm_.current_theme_name();
    }
    void TearDown() override
    {
        if (tm_.is_transitioning())
        {
            tm_.update(10.0f);
        }
        tm_.set_theme(original_theme_);
        ThemeManager::set_current(nullptr);
    }
    ThemeManager tm_;
    std::string  original_theme_;
};

// Startup sequence used by embedders (Qt adapter): a fresh manager is made current and a
// theme is requested before anything has registered the built-ins.  The request must stick,
// including across the ensure_initialized() call ImGui init makes later.
TEST(ThemeManagerStartup, SetThemeOnFreshManagerIsHonoured)
{
    ThemeManager tm;
    ThemeManager::set_current(&tm);
    tm.set_theme("dark");
    EXPECT_EQ(tm.current_theme_name(), "dark");
    tm.ensure_initialized();   // what ImGuiIntegration::init() does
    EXPECT_EQ(tm.current_theme_name(), "dark");
    EXPECT_EQ(ThemeManager::instance().current_theme_name(), "dark");
    ThemeManager::set_current(nullptr);
}

TEST(ThemeManagerStartup, UnknownThemeOnFreshManagerFallsBackToDefault)
{
    ThemeManager tm;
    tm.set_theme("nonexistent_theme_xyz");
    EXPECT_EQ(tm.current_theme_name(), "night");
}

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
    float lum     = 0.2126f * c.bg_primary.r + 0.7152f * c.bg_primary.g + 0.0722f * c.bg_primary.b;
    EXPECT_LT(lum, 0.15f);
}

TEST_F(ThemeManagerTest, LightThemeHasLightBackground)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("light");
    const auto& c = tm.colors();
    float lum     = 0.2126f * c.bg_primary.r + 0.7152f * c.bg_primary.g + 0.0722f * c.bg_primary.b;
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

namespace
{
// WCAG relative luminance of `fg` composited over the opaque `bg`.
float composited_luminance(const Color& fg, const Color& bg)
{
    auto lin = [](float c)
    { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    float r = fg.r * fg.a + bg.r * (1.0f - fg.a);
    float g = fg.g * fg.a + bg.g * (1.0f - fg.a);
    float b = fg.b * fg.a + bg.b * (1.0f - fg.a);
    return 0.2126f * lin(r) + 0.7152f * lin(g) + 0.0722f * lin(b);
}

float contrast_on(const Color& fg, const Color& bg)
{
    float l_fg = composited_luminance(fg, bg);
    float l_bg = composited_luminance(bg, bg);
    return (std::max(l_fg, l_bg) + 0.05f) / (std::min(l_fg, l_bg) + 0.05f);
}
}   // namespace

TEST_F(ThemeManagerTest, HighContrastGridIsDimmerThanAxesAndText)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("high_contrast");
    const auto& c      = tm.colors();
    const Color canvas = c.bg_canvas;

    float major = contrast_on(c.grid_major, canvas);
    float minor = contrast_on(c.grid_minor, canvas);

    // Grid is a reference, so it must sit well below the axis, tick labels and text.
    EXPECT_LT(major, contrast_on(c.axis_line, canvas) / 6.0f);
    EXPECT_LT(major, contrast_on(c.tick_label, canvas) / 6.0f);
    EXPECT_LT(major, contrast_on(c.text_primary, canvas) / 6.0f);
    EXPECT_LT(contrast_on(c.grid_line, canvas), contrast_on(c.axis_line, canvas) / 6.0f);

    // The grid recedes behind the data even in dense scenes: the major grid is a
    // quiet reference (well under the former ~2.1:1) but still clearly visible.
    EXPECT_GE(major, 1.5f);
    EXPECT_LE(major, 1.8f);
    // Minor stays visible but faint, and clearly below major so the two levels
    // remain distinguishable.
    EXPECT_GT(minor, 1.05f);
    EXPECT_LE(minor, 1.2f);
    EXPECT_GE(major - 1.0f, 4.0f * (minor - 1.0f));
    EXPECT_GE(c.grid_major.a, 2.5f * c.grid_minor.a);

    // Text, axes and ticks keep full high-contrast levels.
    EXPECT_GE(contrast_on(c.axis_line, canvas), 15.0f);
    EXPECT_GE(contrast_on(c.tick_label, canvas), 15.0f);
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
    custom.name              = "custom_test";
    custom.colors.bg_primary = Color(0.5f, 0.5f, 0.5f, 1.0f);
    custom.colors.accent     = Color(1.0f, 0.0f, 1.0f, 1.0f);

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

// S4-C01: the rendered mid-transition frame dropped text to ~1:1, because text
// and surfaces were interpolated through the same mid-grey.  Text now keeps
// 4.5:1 on the interpolated panel surface (or the best black / white can do on
// it) on every frame of every theme pair, and the endpoints are unchanged.
TEST_F(ThemeManagerTest, TransitionKeepsTextReadableOnEveryFrame)
{
    auto&             tm      = ThemeManager::instance();
    const char* const names[] = {"dark", "night", "light", "high_contrast"};
    auto              same    = [](const Color& a, const Color& b)
    { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
    for (const char* from : names)
        for (const char* to : names)
        {
            if (std::string(from) == to)
                continue;
            tm.set_theme(to);
            const ThemeColors target = tm.colors();
            for (int step = 1; step < 20; ++step)
            {
                tm.set_theme(from);
                tm.transition_to(to, 1.0f);
                tm.update(static_cast<float>(step) / 20.0f);
                ASSERT_TRUE(tm.is_transitioning());
                const ThemeColors& c       = tm.colors();
                const Color        surface = c.bg_secondary.with_alpha(1.0f);
                const float        best    = std::max(Color(0, 0, 0).contrast_ratio(surface),
                                            Color(1, 1, 1).contrast_ratio(surface));
                const float        need    = std::min(4.5f, best) - 0.01f;
                const auto         where =
                    std::string(from) + " -> " + to + " step " + std::to_string(step);
                EXPECT_GE(c.text_primary.contrast_ratio(surface), need) << where;
                EXPECT_GE(c.text_secondary.contrast_ratio(surface), need) << where;
                EXPECT_GE(c.text_tertiary.contrast_ratio(surface), need) << where;
                EXPECT_GE(c.tick_label.contrast_ratio(c.bg_canvas.with_alpha(1.0f)),
                          std::min(4.5f,
                                   std::max(Color(0, 0, 0).contrast_ratio(c.bg_canvas),
                                            Color(1, 1, 1).contrast_ratio(c.bg_canvas)))
                              - 0.01f)
                    << where;
                // The crossover frame is still far above the old ~1:1.
                EXPECT_GE(c.text_primary.contrast_ratio(surface), 4.0f) << where;
            }
            tm.update(1.0f);
            EXPECT_FALSE(tm.is_transitioning());
            EXPECT_TRUE(same(tm.colors().text_primary, target.text_primary)) << from << "->" << to;
            EXPECT_TRUE(same(tm.colors().text_secondary, target.text_secondary))
                << from << "->" << to;
        }
    tm.set_theme("dark");
}

TEST_F(ThemeManagerTest, SetThemeCancelsActiveTransition)
{
    auto& tm = ThemeManager::instance();
    tm.set_theme("light");
    const ThemeColors light = tm.colors();
    tm.set_theme("dark");
    const ThemeColors dark = tm.colors();
    ASSERT_NE(dark.bg_primary.r, light.bg_primary.r);

    tm.transition_to("light", 1.0f);
    tm.update(0.5f);
    ASSERT_TRUE(tm.is_transitioning());
    ASSERT_NE(tm.colors().bg_primary.r, dark.bg_primary.r);   // mid-transition blend

    // A direct choice must win immediately: no interpolated colours left over.
    tm.set_theme("dark");
    EXPECT_FALSE(tm.is_transitioning());
    EXPECT_EQ(tm.current_theme_name(), "dark");
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.r, dark.bg_primary.r);
    EXPECT_FLOAT_EQ(tm.colors().text_primary.r, dark.text_primary.r);
    EXPECT_FLOAT_EQ(tm.colors().accent.g, dark.accent.g);

    // ...and the stale transition target must never take over later.
    tm.update(0.6f);
    tm.update(1.0f);
    EXPECT_FALSE(tm.is_transitioning());
    EXPECT_EQ(tm.current_theme_name(), "dark");
    EXPECT_FLOAT_EQ(tm.colors().bg_primary.r, dark.bg_primary.r);
}

#ifdef SPECTRA_TEST_THEME_IMGUI
TEST_F(ThemeManagerTest, ApplyToImguiFollowsTransitionColors)
{
    ImGuiContext* ctx = ImGui::CreateContext();
    auto&         tm  = ThemeManager::instance();
    tm.set_theme("light");
    const ThemeColors light = tm.colors();
    tm.set_theme("dark");
    const ThemeColors dark = tm.colors();
    ASSERT_NE(dark.text_primary.r, light.text_primary.r);

    const bool do_lin   = tm.current().linearize_colors;
    auto       expected = [do_lin](const Color& c) { return do_lin ? c.to_linear() : c; };

    tm.transition_to("light", 1.0f);
    tm.update(0.5f);   // update() calls apply_to_imgui()
    ASSERT_TRUE(tm.is_transitioning());

    const Color   mid_text   = expected(tm.colors().text_primary);
    const Color   dark_text  = expected(dark.text_primary);
    const ImVec4& imgui_text = ImGui::GetStyle().Colors[ImGuiCol_Text];
    ASSERT_NE(mid_text.r, dark_text.r);
    EXPECT_NEAR(imgui_text.x, mid_text.r, 1e-5f);
    EXPECT_NEAR(imgui_text.y, mid_text.g, 1e-5f);
    EXPECT_NEAR(imgui_text.z, mid_text.b, 1e-5f);

    const Color   mid_check   = expected(tm.colors().accent);
    const ImVec4& imgui_check = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
    EXPECT_NEAR(imgui_check.x, mid_check.r, 1e-5f);
    EXPECT_NEAR(imgui_check.y, mid_check.g, 1e-5f);
    EXPECT_NEAR(imgui_check.z, mid_check.b, 1e-5f);

    tm.update(10.0f);
    ImGui::DestroyContext(ctx);
}
#endif

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
    auto&       tm          = ThemeManager::instance();
    const auto& names       = tm.available_data_palettes();
    bool        has_default = false, has_colorblind = false;
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
    Color c  = tm.get_color("nonexistent_color_name");
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

TEST(GlassTokens, MasterZeroIsNearlyOpaque)
{
    ThemeGlassSettings g;
    g.master_intensity = 0.0f;
    g.panel_alpha      = 0.62f;
    EXPECT_NEAR(glass_surface_alpha_value(g, GlassSurface::Panel), 0.92f, 1e-5f);
}

TEST(GlassTokens, MasterOneUsesTargetAlpha)
{
    ThemeGlassSettings g;
    g.master_intensity = 1.0f;
    g.panel_alpha      = 0.62f;
    EXPECT_NEAR(glass_surface_alpha_value(g, GlassSurface::Panel), 0.62f, 1e-5f);
}

TEST(GlassTokens, PlotAlphaHasReadabilityFloor)
{
    ThemeGlassSettings g;
    g.master_intensity = 1.0f;
    g.plot_alpha       = 0.05f;
    EXPECT_GE(glass_surface_alpha_value(g, GlassSurface::Plot),
              glass_tokens::MIN_PLOT_READABILITY_ALPHA);
}

TEST_F(ThemeManagerTest, GlassSettingsClampAndApply)
{
    ThemeGlassSettings g;
    g.master_intensity = 2.0f;
    g.panel_alpha      = -0.5f;
    g.clamp();
    EXPECT_FLOAT_EQ(g.master_intensity, 1.0f);
    EXPECT_FLOAT_EQ(g.panel_alpha, 0.35f);

    tm_.set_theme("night");
    tm_.set_glass_settings(ThemeGlassSettings::night_defaults(), true);
    EXPECT_NEAR(tm_.glass_surface_alpha(GlassSurface::Panel), 0.585f, 0.03f);
}

TEST_F(ThemeManagerTest, ResetGlassDefaultsPerTheme)
{
    tm_.set_theme("dark");
    tm_.glass_mut().master_intensity = 0.95f;
    tm_.reset_glass_defaults();
    EXPECT_FLOAT_EQ(tm_.glass().master_intensity,
                    ThemeGlassSettings::dark_defaults().master_intensity);
}

TEST_F(ThemeManagerTest, NightGlassAccentIsCyanFamily)
{
    tm_.set_theme("night");
    const auto& accent = tm_.colors().accent;
    EXPECT_GT(accent.g, accent.r);
    EXPECT_GT(accent.b, accent.r * 0.5f);
}

TEST_F(ThemeManagerTest, ExportImportGlassSettings)
{
    tm_.set_theme("night");
    ThemeGlassSettings custom;
    custom.master_intensity = 0.42f;
    custom.panel_alpha      = 0.51f;
    custom.plot_alpha       = 0.33f;
    custom.toolbar_alpha    = 0.47f;
    custom.glow_strength    = 0.61f;
    custom.blur_enabled     = true;
    tm_.set_glass_settings(custom, true);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "spectra_glass_theme_test.json";
    ASSERT_TRUE(tm_.export_theme(path.string()));

    ThemeGlassSettings before_import = tm_.glass();
    before_import.master_intensity   = 0.0f;
    tm_.set_glass_settings(before_import, true);

    ASSERT_TRUE(tm_.import_theme(path.string()));
    EXPECT_NEAR(tm_.glass().master_intensity, 0.42f, 1e-4f);
    EXPECT_NEAR(tm_.glass().panel_alpha, 0.51f, 1e-4f);
    EXPECT_NEAR(tm_.glass().plot_alpha, 0.33f, 1e-4f);
    EXPECT_NEAR(tm_.glass().toolbar_alpha, 0.47f, 1e-4f);
    EXPECT_NEAR(tm_.glass().glow_strength, 0.61f, 1e-4f);
    EXPECT_TRUE(tm_.glass().blur_enabled);
    std::filesystem::remove(path);
}

// ─── Theme display names ─────────────────────────────────────────────────────

TEST(ThemeDisplayName, FallbackTitleCasesIds)
{
    EXPECT_EQ(theme_display_name("light"), "Light");
    EXPECT_EQ(theme_display_name("high_contrast"), "High Contrast");
    EXPECT_EQ(theme_display_name("my-custom_theme"), "My Custom Theme");
    EXPECT_EQ(theme_display_name("_odd__id_"), "Odd Id");
    EXPECT_EQ(theme_display_name(""), "");
}

TEST_F(ThemeManagerTest, DisplayNamesForBuiltInThemes)
{
    EXPECT_EQ(tm_.display_name("night"), "Night");
    EXPECT_EQ(tm_.display_name("dark"), "Dark");
    EXPECT_EQ(tm_.display_name("light"), "Light");
    EXPECT_EQ(tm_.display_name("high_contrast"), "High Contrast");
}

TEST_F(ThemeManagerTest, DisplayNameFallsBackForUnknownOrUnnamedThemes)
{
    EXPECT_EQ(tm_.display_name("not_registered"), "Not Registered");

    Theme custom;   // registered without a display_name
    tm_.register_theme("solar_flare", custom);
    EXPECT_EQ(tm_.display_name("solar_flare"), "Solar Flare");
}

// ─── Effective figure background ─────────────────────────────────────────────

TEST(EffectiveFigureBackground, DefaultFollowsThemeCanvas)
{
    spectra::FigureStyle style;   // background defaults to white, but not overridden
    EXPECT_FALSE(style.background_override);

    ThemeColors colors{};
    colors.bg_canvas = Color(0.05f, 0.07f, 0.11f, 1.0f);

    const spectra::Color bg = effective_figure_background(style, colors);
    EXPECT_FLOAT_EQ(bg.r, 0.05f);
    EXPECT_FLOAT_EQ(bg.g, 0.07f);
    EXPECT_FLOAT_EQ(bg.b, 0.11f);
    EXPECT_FLOAT_EQ(bg.a, 1.0f);
}

TEST(EffectiveFigureBackground, OverrideUsesFigureColour)
{
    spectra::FigureStyle style;
    style.background          = spectra::Color(0.9f, 0.2f, 0.1f, 1.0f);
    style.background_override = true;

    ThemeColors colors{};
    colors.bg_canvas = Color(0.05f, 0.07f, 0.11f, 1.0f);

    const spectra::Color bg = effective_figure_background(style, colors);
    EXPECT_FLOAT_EQ(bg.r, 0.9f);
    EXPECT_FLOAT_EQ(bg.g, 0.2f);
    EXPECT_FLOAT_EQ(bg.b, 0.1f);

    // Back to auto: the stored colour is kept but the theme wins again.
    style.background_override = false;
    EXPECT_FLOAT_EQ(effective_figure_background(style, colors).r, 0.05f);
}

TEST_F(ThemeManagerTest, EffectiveFigureBackgroundTracksThemeSwitch)
{
    spectra::FigureStyle style;
    tm_.set_theme("light");
    const spectra::Color light_bg = effective_figure_background(style, tm_.colors());
    tm_.set_theme("night");
    const spectra::Color night_bg = effective_figure_background(style, tm_.colors());
    EXPECT_GT(light_bg.r + light_bg.g + light_bg.b, 2.0f);   // light canvas
    EXPECT_LT(night_bg.r + night_bg.g + night_bg.b, 0.5f);   // dark canvas, not white
}

// ─── Semantic status tokens (used by ROS2 / UI panels instead of literals) ───

TEST_F(ThemeManagerTest, CriticalTokenPresentAndDistinctInAllThemes)
{
    for (const char* name : {"dark", "night", "light", "high_contrast"})
    {
        SCOPED_TRACE(name);
        tm_.set_theme(name);
        const ThemeColors& c = tm_.colors();
        EXPECT_FLOAT_EQ(c.critical.a, 1.0f);
        EXPECT_NE(c.critical, Color{});
        EXPECT_NE(c.critical, c.error);
        EXPECT_NE(c.critical, c.warning);
        EXPECT_NE(c.critical, c.success);
        EXPECT_NE(c.critical, c.info);
        EXPECT_NE(c.critical, c.accent);
        EXPECT_NE(c.critical, c.text_primary);
        EXPECT_EQ(tm_.get_color("critical"), c.critical);
    }
}

TEST_F(ThemeManagerTest, StatusTokensReadableOnPanelsInAllThemes)
{
    // Log levels, diagnostics badges and error messages draw these on panel
    // surfaces; each must stay readable in every built-in theme.
    for (const char* name : {"dark", "night", "light", "high_contrast"})
    {
        SCOPED_TRACE(name);
        tm_.set_theme(name);
        const ThemeColors& c  = tm_.colors();
        const Color        bg = c.bg_secondary;
        EXPECT_GE(c.text_primary.contrast_ratio(bg), 4.5f);
        EXPECT_GE(c.warning.contrast_ratio(bg), 3.0f);
        EXPECT_GE(c.error.contrast_ratio(bg), 3.0f);
        EXPECT_GE(c.success.contrast_ratio(bg), 3.0f);
        EXPECT_GE(c.critical.contrast_ratio(bg), 3.0f);
    }
}

TEST_F(ThemeManagerTest, CriticalTokenSurvivesExportImport)
{
    tm_.set_theme("light");
    const Color                 expected = tm_.colors().critical;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "spectra_critical_theme_test.json";
    ASSERT_TRUE(tm_.export_theme(path.string()));
    tm_.set_theme("dark");
    ASSERT_TRUE(tm_.import_theme(path.string()));
    tm_.set_theme("light");
    EXPECT_NEAR(tm_.colors().critical.r, expected.r, 1e-3f);
    EXPECT_NEAR(tm_.colors().critical.g, expected.g, 1e-3f);
    EXPECT_NEAR(tm_.colors().critical.b, expected.b, 1e-3f);
    std::filesystem::remove(path);
}

TEST_F(ThemeManagerTest, ImportWithoutCriticalFallsBackToError)
{
    // Themes exported before the `critical` token existed must not import it as black.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "spectra_legacy_theme_test.json";
    {
        std::ofstream f(path);
        f << "{\n  \"name\": \"legacy_no_critical\",\n  \"colors\": {\n"
             "    \"error\": [0.8, 0.1, 0.1, 1.0]\n  }\n}\n";
    }
    ASSERT_TRUE(tm_.import_theme(path.string()));
    tm_.set_theme("legacy_no_critical");
    ASSERT_EQ(tm_.current_theme_name(), "legacy_no_critical");
    EXPECT_EQ(tm_.colors().critical, tm_.colors().error);
    EXPECT_NEAR(tm_.colors().critical.r, 0.8f, 1e-4f);
    std::filesystem::remove(path);
}

// ─── Plot surface colours for figure background overrides (S10) ──────────────

namespace
{
float surface_luma(const Color& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

bool same_color(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Every token plot_surface_colors() may change.
bool same_plot_tokens(const ThemeColors& a, const ThemeColors& b)
{
    return same_color(a.bg_canvas, b.bg_canvas) && same_color(a.text_primary, b.text_primary)
           && same_color(a.text_secondary, b.text_secondary)
           && same_color(a.text_tertiary, b.text_tertiary)
           && same_color(a.border_default, b.border_default)
           && same_color(a.border_subtle, b.border_subtle)
           && same_color(a.border_strong, b.border_strong) && same_color(a.grid_major, b.grid_major)
           && same_color(a.grid_minor, b.grid_minor) && same_color(a.grid_line, b.grid_line)
           && same_color(a.axis_line, b.axis_line) && same_color(a.tick_label, b.tick_label)
           && same_color(a.crosshair, b.crosshair) && same_color(a.tooltip_bg, b.tooltip_bg)
           && same_color(a.tooltip_border, b.tooltip_border)
           && same_color(a.bg_elevated, b.bg_elevated)
           && same_color(a.annotation_bg, b.annotation_bg);
}

spectra::FigureStyle override_style(const spectra::Color& bg)
{
    spectra::FigureStyle style;
    style.background          = bg;
    style.background_override = true;
    return style;
}
}   // namespace

TEST_F(ThemeManagerTest, PlotSurfaceWithoutOverrideEqualsTheme)
{
    for (const char* name : {"dark", "night", "light", "high_contrast"})
    {
        tm_.set_theme(name);
        spectra::FigureStyle style;   // background white, but not overridden
        EXPECT_TRUE(same_plot_tokens(tm_.plot_surface_colors(style), tm_.colors())) << name;
        EXPECT_EQ(&tm_.plot_surface_data_palette(style), &tm_.current_data_palette()) << name;
    }
}

TEST_F(ThemeManagerTest, PlotSurfaceOverrideEqualToCanvasEqualsTheme)
{
    for (const char* name : {"dark", "night", "light", "high_contrast"})
    {
        tm_.set_theme(name);
        const Color& c     = tm_.colors().bg_canvas;
        const auto   style = override_style(spectra::Color{c.r, c.g, c.b, c.a});
        EXPECT_TRUE(same_plot_tokens(tm_.plot_surface_colors(style), tm_.colors())) << name;
        EXPECT_EQ(&tm_.plot_surface_data_palette(style), &tm_.current_data_palette()) << name;
    }
}

TEST_F(ThemeManagerTest, WhiteOverrideOnDarkThemesUsesDarkInk)
{
    const auto white = override_style(spectra::colors::white);
    for (const char* name : {"dark", "night", "high_contrast"})
    {
        tm_.set_theme(name);
        const ThemeColors s = tm_.plot_surface_colors(white);
        EXPECT_TRUE(same_color(s.bg_canvas, Color(1.0f, 1.0f, 1.0f, 1.0f))) << name;
        EXPECT_TRUE(is_light_plot_background(s)) << name;
        // Titles, labels and ticks are dark ink, readable on white.
        EXPECT_LT(surface_luma(s.text_primary), 0.3f) << name;
        EXPECT_LT(surface_luma(s.text_secondary), 0.4f) << name;
        EXPECT_LT(surface_luma(s.tick_label), 0.4f) << name;
        EXPECT_LT(surface_luma(s.axis_line), 0.5f) << name;
        // Pure white ink is remapped to the dark surface ink; explicit black stays black.
        const Color w = adapt_series_color_to_theme(Color(1.0f, 1.0f, 1.0f, 1.0f), s);
        EXPECT_LT(surface_luma(w), 0.3f) << name;
        const Color k = adapt_series_color_to_theme(Color(0.0f, 0.0f, 0.0f, 1.0f), s);
        EXPECT_TRUE(same_color(k, Color(0.0f, 0.0f, 0.0f, 1.0f))) << name;
        // "default" palette follows the surface: the light theme's palette.
        tm_.set_theme("light");
        const std::vector<Color> light_palette = tm_.current_data_palette().colors;
        tm_.set_theme(name);
        EXPECT_EQ(tm_.plot_surface_data_palette(white).colors.size(), light_palette.size());
        EXPECT_TRUE(same_color(tm_.plot_surface_data_palette(white).colors[0], light_palette[0]))
            << name;
    }
}

TEST_F(ThemeManagerTest, BlackOverrideOnLightThemeUsesLightInk)
{
    tm_.set_theme("light");
    const auto        black = override_style(spectra::colors::black);
    const ThemeColors s     = tm_.plot_surface_colors(black);
    EXPECT_FALSE(is_light_plot_background(s));
    EXPECT_GT(surface_luma(s.text_primary), 0.6f);
    EXPECT_GT(surface_luma(s.tick_label), 0.4f);
    const Color k = adapt_series_color_to_theme(Color(0.0f, 0.0f, 0.0f, 1.0f), s);
    EXPECT_GT(surface_luma(k), 0.6f);
    const Color w = adapt_series_color_to_theme(Color(1.0f, 1.0f, 1.0f, 1.0f), s);
    EXPECT_TRUE(same_color(w, Color(1.0f, 1.0f, 1.0f, 1.0f)));
}

TEST_F(ThemeManagerTest, SameLightnessOverrideKeepsThemeInk)
{
    tm_.set_theme("light");
    const auto        pale = override_style(spectra::Color{0.92f, 0.92f, 0.88f, 1.0f});
    const ThemeColors s    = tm_.plot_surface_colors(pale);
    EXPECT_FLOAT_EQ(s.bg_canvas.r, 0.92f);
    EXPECT_TRUE(same_color(s.text_primary, tm_.colors().text_primary));
    EXPECT_TRUE(same_color(s.tick_label, tm_.colors().tick_label));
}

TEST_F(ThemeManagerTest, ExplicitPaletteIsNotSwappedBySurface)
{
    tm_.set_theme("dark");
    tm_.set_data_palette("tol_bright");
    const auto white = override_style(spectra::colors::white);
    EXPECT_EQ(&tm_.plot_surface_data_palette(white), &tm_.current_data_palette());
    tm_.set_data_palette("default");
}

TEST_F(ThemeManagerTest, FormatLettersResolveForOverrideSurface)
{
    tm_.set_theme("dark");
    spectra::Figure fig;
    fig.style().background_override = true;
    fig.style().background          = spectra::colors::white;
    auto&             ax            = fig.subplot(1, 1, 1);
    const float       x[]           = {0.0f, 1.0f};
    auto&             w_line        = ax.plot(x, x, "w-");
    auto&             k_line        = ax.plot(x, x, "k-");
    const ThemeColors s             = tm_.plot_surface_colors(fig.style());
    resolve_auto_series_colors(fig,
                               tm_.plot_surface_data_palette(fig.style()),
                               is_light_plot_background(s));
    // Light-surface shades: 'w' dark gray, 'k' black — both visible on white.
    const auto wc = adapt_series_color_to_theme(w_line.color(), s);
    const auto kc = adapt_series_color_to_theme(k_line.color(), s);
    EXPECT_LT(0.2126f * wc.r + 0.7152f * wc.g + 0.0722f * wc.b, 0.3f);
    EXPECT_LT(0.2126f * kc.r + 0.7152f * kc.g + 0.0722f * kc.b, 0.1f);
}

TEST_F(ThemeManagerTest, SvgExportUsesOverrideSurfaceInk)
{
    auto hex = [](const Color& c)
    {
        char buf[8];
        std::snprintf(buf,
                      sizeof(buf),
                      "#%02x%02x%02x",
                      static_cast<int>(std::lround(c.r * 255.0f)),
                      static_cast<int>(std::lround(c.g * 255.0f)),
                      static_cast<int>(std::lround(c.b * 255.0f)));
        return std::string(buf);
    };
    tm_.set_theme("light");
    const Color light_tick = tm_.colors().tick_label;
    tm_.set_theme("dark");
    const Color dark_tick = tm_.colors().tick_label;
    ASSERT_NE(hex(light_tick), hex(dark_tick));

    spectra::Figure fig;
    fig.style().background_override = true;
    fig.style().background          = spectra::colors::white;
    auto&       ax                  = fig.subplot(1, 1, 1);
    const float x[]                 = {0.0f, 1.0f, 2.0f};
    ax.plot(x, x, "w-");
    ax.title("Title");
    fig.compute_layout();
    resolve_auto_series_colors(fig,
                               tm_.plot_surface_data_palette(fig.style()),
                               is_light_plot_background(tm_.plot_surface_colors(fig.style())));
    const std::string svg =
        spectra::SvgExporter::to_string(fig,
                                        svg_style_from_theme(tm_.plot_surface_colors(fig.style())));
    EXPECT_NE(svg.find("#ffffff"), std::string::npos);         // white surface painted
    EXPECT_NE(svg.find(hex(light_tick)), std::string::npos);   // dark tick ink
    EXPECT_EQ(svg.find(hex(dark_tick)), std::string::npos);    // no pale dark-theme ink
    EXPECT_NE(svg.find("#292929"), std::string::npos);         // 'w' light-surface shade
}

// ─── Per-export theme (ExportOptions::theme) ─────────────────────────────────

TEST_F(ThemeManagerTest, ExportThemeOverridesAndRestores)
{
    tm_.set_theme("dark");
    const Color dark_bg            = tm_.colors().bg_canvas;
    const auto  dark_palette_first = tm_.current_data_palette().colors.at(0);
    {
        ScopedExportTheme guard(&tm_, "light");
        ASSERT_TRUE(guard.active());
        EXPECT_TRUE(tm_.export_theme_active());
        EXPECT_EQ(tm_.current_theme_name(), "light");
        EXPECT_GT(tm_.colors().bg_canvas.luminance(), 0.5f);
        EXPECT_NE(tm_.colors().bg_canvas, dark_bg);
        // The "default" data palette follows the export theme too.
        EXPECT_NE(tm_.current_data_palette().colors.at(0), dark_palette_first);
        // Nested overrides are refused.
        EXPECT_FALSE(tm_.push_export_theme("night"));
    }
    EXPECT_FALSE(tm_.export_theme_active());
    EXPECT_EQ(tm_.current_theme_name(), "dark");
    EXPECT_EQ(tm_.colors().bg_canvas, dark_bg);
    EXPECT_EQ(tm_.current_data_palette().colors.at(0), dark_palette_first);
}

TEST_F(ThemeManagerTest, UnknownOrEmptyExportThemeKeepsActiveTheme)
{
    tm_.set_theme("dark");
    {
        ScopedExportTheme unknown(&tm_, "no_such_theme");
        EXPECT_FALSE(unknown.active());
        EXPECT_EQ(tm_.current_theme_name(), "dark");
        ScopedExportTheme empty(&tm_, "");
        EXPECT_FALSE(empty.active());
    }
    ScopedExportTheme null_mgr(nullptr, "light");
    EXPECT_FALSE(null_mgr.active());
    // Names are matched case-insensitively.
    ScopedExportTheme upper(&tm_, "LIGHT");
    EXPECT_TRUE(upper.active());
    EXPECT_EQ(tm_.current_theme_name(), "light");
}

TEST_F(ThemeManagerTest, ExportThemeSuspendsAndResumesTransition)
{
    tm_.set_theme("dark");
    tm_.transition_to("light", 1.0f);
    tm_.update(0.5f);
    ASSERT_TRUE(tm_.is_transitioning());
    const Color mid = tm_.colors().bg_canvas;
    {
        ScopedExportTheme guard(&tm_, "night");
        ASSERT_TRUE(guard.active());
        EXPECT_FALSE(tm_.is_transitioning());
        EXPECT_EQ(tm_.current_theme_name(), "night");
    }
    EXPECT_TRUE(tm_.is_transitioning());
    EXPECT_EQ(tm_.colors().bg_canvas, mid);
}

#ifdef SPECTRA_TEST_THEME_IMGUI
// App UI scale (Settings -> Appearance) scales the ImGui style once.
TEST(UiScale, ScalesImGuiStyleWithoutCompounding)
{
    ImGuiContext* ctx = ImGui::CreateContext();
    ThemeManager  tm;
    tm.ensure_initialized();
    tm.set_theme("dark");
    tm.apply_to_imgui();
    const ImGuiStyle base = ImGui::GetStyle();
    EXPECT_FLOAT_EQ(base.FontScaleMain, 1.0f);

    tm.set_ui_scale(1.5f);
    EXPECT_FLOAT_EQ(ImGui::GetStyle().FontScaleMain, 1.5f);
    EXPECT_NEAR(ImGui::GetStyle().WindowPadding.x, base.WindowPadding.x * 1.5f, 1.0f);
    EXPECT_NEAR(ImGui::GetStyle().FramePadding.y, base.FramePadding.y * 1.5f, 1.0f);

    // Re-applying (theme change, transition frame) does not scale again.
    tm.apply_to_imgui();
    tm.apply_to_imgui();
    EXPECT_NEAR(ImGui::GetStyle().WindowPadding.x, base.WindowPadding.x * 1.5f, 1.0f);

    tm.set_ui_scale(1.0f);
    EXPECT_FLOAT_EQ(ImGui::GetStyle().WindowPadding.x, base.WindowPadding.x);
    EXPECT_FLOAT_EQ(ImGui::GetStyle().FontScaleMain, 1.0f);
    ImGui::DestroyContext(ctx);
}
#endif

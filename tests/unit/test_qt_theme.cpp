// Qt adapter theming (issue #110): the native Qt chrome around an embedded canvas
// (status bar, window background) must follow the Spectra theme instead of the
// platform's light default.  Pure colour conversion — no display needed.

#include <gtest/gtest.h>

#include <QtGui/QColor>
#include <QtGui/QPalette>

#include "adapters/qt/qt_theme.hpp"
#include "ui/theme/theme.hpp"

using spectra::adapters::qt::palette_from_theme;
using spectra::adapters::qt::to_qcolor;

namespace
{

const spectra::ui::ThemeColors& colors_of(spectra::ui::ThemeManager& tm, const char* name)
{
    tm.ensure_initialized();
    tm.set_theme(name);
    return tm.colors();
}

TEST(QtTheme, ToQColorRoundsChannels)
{
    const QColor c = to_qcolor(spectra::ui::Color::from_hex(0x12161A));
    EXPECT_EQ(c.red(), 0x12);
    EXPECT_EQ(c.green(), 0x16);
    EXPECT_EQ(c.blue(), 0x1A);
    EXPECT_EQ(c.alpha(), 255);

    const QColor clamped = to_qcolor(spectra::ui::Color(-0.5f, 1.5f, 0.5f, 1.0f));
    EXPECT_EQ(clamped.red(), 0);
    EXPECT_EQ(clamped.green(), 255);
    EXPECT_EQ(clamped.blue(), 128);
}

TEST(QtTheme, DarkPaletteUsesThemeSurfacesAndText)
{
    spectra::ui::ThemeManager tm;
    const auto&               c = colors_of(tm, "dark");
    const QPalette            p = palette_from_theme(c);

    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive})
    {
        EXPECT_EQ(p.color(g, QPalette::Window), to_qcolor(c.bg_primary));
        EXPECT_EQ(p.color(g, QPalette::WindowText), to_qcolor(c.text_primary));
        EXPECT_EQ(p.color(g, QPalette::Base), to_qcolor(c.bg_secondary));
        EXPECT_EQ(p.color(g, QPalette::Highlight), to_qcolor(c.accent));
    }
    EXPECT_EQ(p.color(QPalette::Disabled, QPalette::WindowText), to_qcolor(c.text_tertiary));

    // The status bar inherits Window/WindowText: dark background, light text.
    EXPECT_LT(p.color(QPalette::Window).lightnessF(), 0.25);
    EXPECT_GT(p.color(QPalette::WindowText).lightnessF(), 0.75);
}

TEST(QtTheme, LightPaletteIsLight)
{
    spectra::ui::ThemeManager tm;
    const QPalette            p = palette_from_theme(colors_of(tm, "light"));
    EXPECT_GT(p.color(QPalette::Window).lightnessF(), 0.75);
    EXPECT_LT(p.color(QPalette::WindowText).lightnessF(), 0.35);
}

TEST(QtTheme, TranslucentColoursAreCompositedOverWindow)
{
    spectra::ui::ThemeManager tm;
    const auto&               c = colors_of(tm, "dark");
    ASSERT_LT(c.border_default.a, 1.0f);   // dark theme borders are translucent rims
    const QPalette p = palette_from_theme(c);

    const QColor expected = to_qcolor(c.border_default.composite_over(c.bg_primary));
    EXPECT_EQ(p.color(QPalette::Light), expected);
    EXPECT_EQ(p.color(QPalette::Light).alpha(), 255);
}

}   // namespace

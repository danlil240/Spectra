// Auto-assigned series colours stay visible on the plot canvas (audit S07):
// every theme x palette combination, after visible_data_palette(), has each
// auto slot at >= 3:1 contrast against the canvas, with hue preserved, no
// slots collapsing together (also under simulated CVD), and explicit colours
// left untouched.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <spectra/axes.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/theme/theme.hpp"

using spectra::ui::CVDType;
using spectra::ui::DataPalette;
using spectra::ui::ThemeManager;
using UiColor = spectra::ui::Color;

namespace
{

constexpr const char* kThemes[] = {"dark", "night", "light", "high_contrast"};
constexpr const char* kPalettes[] =
    {"default", "colorblind", "tol_bright", "tol_muted", "ibm", "wong", "viridis", "monochrome"};

double lin(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

template <typename C>
double luminance(const C& c)
{
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

template <typename A, typename B>
double contrast(const A& a, const B& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// 8-bit quantised, as the colour reaches the framebuffer / SVG.
UiColor quantise(const UiColor& c)
{
    auto q = [](float v) { return std::round(v * 255.0f) / 255.0f; };
    return UiColor(q(c.r), q(c.g), q(c.b), c.a);
}

struct Lab
{
    double L, a, b;
};

Lab oklab(const UiColor& c)
{
    const double r = lin(c.r), g = lin(c.g), b = lin(c.b);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

double cvd_distance(const UiColor& x, const UiColor& y)
{
    double best = 1e9;
    for (CVDType t :
         {CVDType::None, CVDType::Protanopia, CVDType::Deuteranopia, CVDType::Tritanopia})
    {
        const Lab a = oklab(spectra::ui::simulate_cvd(x, t));
        const Lab b = oklab(spectra::ui::simulate_cvd(y, t));
        best        = std::min(best, std::hypot(a.L - b.L, a.a - b.a, a.b - b.b));
    }
    return best;
}

double min_pairwise(const std::vector<UiColor>& cs)
{
    double best = 1e9;
    for (size_t i = 0; i < cs.size(); ++i)
        for (size_t j = i + 1; j < cs.size(); ++j)
            best = std::min(best, cvd_distance(cs[i], cs[j]));
    return best;
}

bool same_rgb(const UiColor& a, const UiColor& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool ink_slot(const UiColor& c, const UiColor& canvas)
{
    const float luma  = 0.2126f * canvas.r + 0.7152f * canvas.g + 0.0722f * canvas.b;
    const bool  black = c.r == 0.0f && c.g == 0.0f && c.b == 0.0f;
    const bool  white = c.r == 1.0f && c.g == 1.0f && c.b == 1.0f;
    return (black && luma < 0.35f) || (white && luma > 0.65f);
}

class PaletteVisibilityTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ThemeManager::set_current(&tm_);
        tm_.ensure_initialized();
    }
    void TearDown() override { ThemeManager::set_current(nullptr); }

    ThemeManager tm_;
};

}   // namespace

TEST_F(PaletteVisibilityTest, EveryAutoSlotReachesThreeToOneOnAllCombos)
{
    int combos   = 0;
    int adjusted = 0;
    for (const char* theme : kThemes)
    {
        tm_.set_theme(theme);
        const UiColor canvas = tm_.colors().bg_canvas;
        for (const char* name : kPalettes)
        {
            tm_.set_data_palette(name);
            const DataPalette& src = tm_.current_data_palette();
            ASSERT_FALSE(src.colors.empty()) << theme << "/" << name;
            const DataPalette vis = spectra::ui::visible_data_palette(
                src,
                spectra::Color(canvas.r, canvas.g, canvas.b, canvas.a));
            ASSERT_EQ(vis.colors.size(), src.colors.size());
            ++combos;
            for (size_t i = 0; i < src.colors.size(); ++i)
            {
                const UiColor& s = src.colors[i];
                const UiColor& v = vis.colors[i];
                const auto     where =
                    std::string(theme) + "/" + name + " slot " + std::to_string(i + 1);
                if (ink_slot(s, canvas))
                {
                    // Left for adapt_series_color_to_theme() (drawn in text_primary).
                    EXPECT_TRUE(same_rgb(s, v)) << where;
                    EXPECT_GE(
                        contrast(spectra::ui::adapt_series_color_to_theme(v, tm_.colors()), canvas),
                        3.0)
                        << where;
                    continue;
                }
                EXPECT_GE(contrast(v, canvas), 3.0) << where;
                EXPECT_GE(contrast(quantise(v), canvas), 3.0) << where << " (8-bit)";
                EXPECT_EQ(v.a, s.a) << where;
                if (contrast(s, canvas) >= 3.0)
                {
                    EXPECT_TRUE(same_rgb(s, v)) << where << " passing slot must be unchanged";
                    continue;
                }
                ++adjusted;
                // Lightness moves only away from the canvas.
                const bool light_canvas = luminance(canvas) > 0.18;
                const Lab  ls           = oklab(s);
                const Lab  lv           = oklab(v);
                if (light_canvas)
                    EXPECT_LT(lv.L, ls.L) << where;
                else
                    EXPECT_GT(lv.L, ls.L) << where;
                // Hue preserved (only meaningful for chromatic colours).
                const double cs = std::hypot(ls.a, ls.b);
                const double cv = std::hypot(lv.a, lv.b);
                if (cs > 0.03 && cv > 0.03)
                {
                    const double hs = std::atan2(ls.b, ls.a);
                    const double hv = std::atan2(lv.b, lv.a);
                    double       dh = std::abs(hs - hv) * 180.0 / M_PI;
                    dh              = std::min(dh, 360.0 - dh);
                    EXPECT_LT(dh, 2.0) << where;
                }
            }
        }
    }
    EXPECT_EQ(combos, 32);
    // The audit counted 50 slots below 3:1 across the four themes.
    EXPECT_GE(adjusted, 40);
}

TEST_F(PaletteVisibilityTest, AdjustedSlotsStayDistinctIncludingUnderCVD)
{
    for (const char* theme : kThemes)
    {
        tm_.set_theme(theme);
        const UiColor canvas = tm_.colors().bg_canvas;
        for (const char* name : kPalettes)
        {
            tm_.set_data_palette(name);
            const DataPalette& src = tm_.current_data_palette();
            const DataPalette  vis =
                spectra::ui::visible_data_palette(src,
                                                  spectra::Color(canvas.r, canvas.g, canvas.b));
            const double before = min_pairwise(src.colors);
            const double after  = min_pairwise(vis.colors);
            // No pair closer than 0.04 (OKLab, worst of normal / P / D / T) unless the
            // source palette already had them closer.
            EXPECT_GE(after, std::min(before, 0.04) - 1e-3)
                << theme << "/" << name << " before " << before << " after " << after;

            // The colorblind-safety check of test_theme_colorblind.cpp also holds for
            // the colours actually drawn.
            if (!src.colorblind_safe)
                continue;
            for (CVDType t : src.safe_for)
                for (size_t i = 0; i < vis.colors.size(); ++i)
                    for (size_t j = i + 1; j < vis.colors.size(); ++j)
                    {
                        const UiColor a  = spectra::ui::simulate_cvd(vis.colors[i], t);
                        const UiColor b  = spectra::ui::simulate_cvd(vis.colors[j], t);
                        const float   dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
                        EXPECT_GT(std::sqrt(dr * dr + dg * dg + db * db), 0.02f)
                            << theme << "/" << name << " " << i << "," << j;
                    }
        }
    }
}

TEST_F(PaletteVisibilityTest, DefaultDarkPalettesAreUnchanged)
{
    for (const char* theme : {"dark", "night", "high_contrast"})
    {
        tm_.set_theme(theme);
        tm_.set_data_palette("default");
        const UiColor      canvas = tm_.colors().bg_canvas;
        const DataPalette& src    = tm_.current_data_palette();
        const DataPalette  vis =
            spectra::ui::visible_data_palette(src, spectra::Color(canvas.r, canvas.g, canvas.b));
        for (size_t i = 0; i < src.colors.size(); ++i)
            EXPECT_TRUE(same_rgb(src.colors[i], vis.colors[i])) << theme << " " << i;
    }
}

TEST_F(PaletteVisibilityTest, EmptyPaletteUsesDefaultCycle)
{
    const DataPalette vis =
        spectra::ui::visible_data_palette(DataPalette{}, spectra::Color(0.1f, 0.1f, 0.1f));
    ASSERT_EQ(vis.colors.size(), spectra::palette::default_cycle_size);
}

TEST_F(PaletteVisibilityTest, ResolveAdjustsAutoSeriesButNotExplicitColours)
{
    tm_.set_theme("light");
    tm_.set_data_palette("viridis");
    const UiColor  c = tm_.colors().bg_canvas;
    spectra::Color canvas(c.r, c.g, c.b);

    const std::vector<float>      x = {0.0f, 1.0f, 2.0f};
    const std::vector<float>      y = {1.0f, 2.0f, 3.0f};
    spectra::Figure               fig;
    auto&                         ax = fig.subplot(1, 1, 1);
    std::vector<spectra::Series*> autos;
    for (int i = 0; i < 10; ++i)
        autos.push_back(&ax.line(x, y));
    // Viridis slot 10 yellow, chosen explicitly on purpose.
    const spectra::Color yellow = spectra::rgb(0xFD / 255.0f, 0xE7 / 255.0f, 0x25 / 255.0f);
    auto&                explicit_yellow = ax.line(x, y);
    explicit_yellow.color(yellow);
    auto& letter = ax.plot(x, y, "y--");

    spectra::ui::resolve_auto_series_colors(fig, tm_.current_data_palette(), true, canvas);

    const DataPalette vis = spectra::ui::visible_data_palette(tm_.current_data_palette(), canvas);
    for (size_t i = 0; i < autos.size(); ++i)
    {
        ASSERT_TRUE(autos[i]->has_auto_color());
        const auto slot = static_cast<size_t>(autos[i]->auto_color_index());
        const auto got  = autos[i]->color();
        const auto want = vis.colors[slot % vis.colors.size()];
        EXPECT_FLOAT_EQ(got.r, want.r) << i;
        EXPECT_FLOAT_EQ(got.g, want.g) << i;
        EXPECT_FLOAT_EQ(got.b, want.b) << i;
        EXPECT_GE(contrast(got, c), 3.0) << i;
    }
    // Explicit RGB colours are the user's choice and are never adjusted.
    const auto ey = explicit_yellow.color();
    EXPECT_EQ(ey.r, yellow.r);
    EXPECT_EQ(ey.g, yellow.g);
    EXPECT_EQ(ey.b, yellow.b);
    // Format letters keep their tuned light-background shade.
    const auto shade = spectra::format_colors::shade('y', true);
    EXPECT_EQ(letter.color().r, shade.r);
    EXPECT_EQ(letter.color().g, shade.g);
    EXPECT_EQ(letter.color().b, shade.b);
}

TEST_F(PaletteVisibilityTest, UsesEffectiveSurfaceNotThemeCanvas)
{
    // Dark theme, but the figure paints a light background: yellow must darken.
    tm_.set_theme("dark");
    tm_.set_data_palette("colorblind");
    spectra::FigureStyle style;
    style.background_override    = true;
    style.background             = spectra::rgb(1.0f, 1.0f, 1.0f);
    const spectra::Color surface = spectra::ui::effective_figure_background(style, tm_.colors());
    const DataPalette vis = spectra::ui::visible_data_palette(tm_.current_data_palette(), surface);
    const UiColor     yellow = vis.colors[3];   // Okabe-Ito yellow #F0E442
    EXPECT_GE(contrast(yellow, surface), 3.0);
    EXPECT_LT(oklab(yellow).L, oklab(UiColor::from_hex(0xF0E442)).L);

    // The same palette on the dark canvas leaves yellow alone.
    const UiColor     dark = tm_.colors().bg_canvas;
    const DataPalette on_dark =
        spectra::ui::visible_data_palette(tm_.current_data_palette(),
                                          spectra::Color(dark.r, dark.g, dark.b));
    EXPECT_TRUE(same_rgb(on_dark.colors[3], tm_.current_data_palette().colors[3]));
}

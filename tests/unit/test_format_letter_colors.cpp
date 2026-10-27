// MATLAB format-string colour letters map to tuned, theme-aware shades (P1-5).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include <spectra/axes.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ipc/codec_fb.hpp"
#include "ipc/message.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/commands/undoable_property.hpp"
#include "ui/theme/theme.hpp"

using spectra::Color;
namespace fc = spectra::format_colors;

namespace
{

bool same_rgb(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// HSV hue in degrees, -1 for a gray.
float hue_deg(const Color& c)
{
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d  = mx - mn;
    if (d < 1e-4f)
        return -1.0f;
    float h = 0.0f;
    if (mx == c.r)
        h = 60.0f * std::fmod((c.g - c.b) / d, 6.0f);
    else if (mx == c.g)
        h = 60.0f * ((c.b - c.r) / d + 2.0f);
    else
        h = 60.0f * ((c.r - c.g) / d + 4.0f);
    return h < 0.0f ? h + 360.0f : h;
}

float hue_distance(float a, float b)
{
    const float d = std::fabs(a - b);
    return std::min(d, 360.0f - d);
}

float lightness(const Color& c)
{
    return 0.5f * (std::max({c.r, c.g, c.b}) + std::min({c.r, c.g, c.b}));
}

float luminance(const Color& c)
{
    auto lin = [](float v)
    { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

float contrast(const Color& a, const Color& b)
{
    float la = luminance(a), lb = luminance(b);
    if (la < lb)
        std::swap(la, lb);
    return (la + 0.05f) / (lb + 0.05f);
}

const std::vector<float> kX = {0.0f, 1.0f, 2.0f};
const std::vector<float> kY = {1.0f, 2.0f, 3.0f};

}   // namespace

// ─── Shade tables ───────────────────────────────────────────────────────────

TEST(FormatLetterColors, LettersAreRecognised)
{
    for (char l : std::string("rgbcmykw"))
    {
        EXPECT_TRUE(fc::is_letter(l)) << l;
    }
    for (char l : std::string("xoRK-\0", 6))
    {
        EXPECT_FALSE(fc::is_letter(l)) << static_cast<int>(l);
    }
}

TEST(FormatLetterColors, ChromaticLettersKeepTheirHue)
{
    const struct
    {
        char  letter;
        Color primary;
    } cases[] = {{'r', spectra::colors::red},
                 {'g', spectra::colors::green},
                 {'b', spectra::colors::blue},
                 {'c', spectra::colors::cyan},
                 {'m', spectra::colors::magenta},
                 {'y', spectra::colors::yellow}};
    for (const auto& tc : cases)
    {
        for (bool light : {false, true})
        {
            const Color s = fc::shade(tc.letter, light);
            EXPECT_FALSE(same_rgb(s, tc.primary)) << tc.letter << " is still a raw primary";
            EXPECT_LT(hue_distance(hue_deg(s), hue_deg(tc.primary)), 22.0f)
                << tc.letter << (light ? " light" : " dark");
        }
    }
}

TEST(FormatLetterColors, BlackAndWhiteStayGrayAndDistinct)
{
    for (bool light : {false, true})
    {
        const Color k = fc::shade('k', light);
        const Color w = fc::shade('w', light);
        EXPECT_LT(hue_deg(k), 0.0f);
        EXPECT_LT(hue_deg(w), 0.0f);
        EXPECT_GT(std::fabs(lightness(w) - lightness(k)), 0.1f);
    }
    // Dark backgrounds: w is white, k a light gray.  Light: the mirror.
    EXPECT_TRUE(same_rgb(fc::shade('w', false), spectra::colors::white));
    EXPECT_GT(lightness(fc::shade('k', false)), 0.75f);
    EXPECT_TRUE(same_rgb(fc::shade('k', true), spectra::colors::black));
    EXPECT_LT(lightness(fc::shade('w', true)), 0.25f);
}

TEST(FormatLetterColors, ShadesReadOnTheirBackground)
{
    // Representative plot canvases (dark theme / light theme).
    const Color dark_bg{0.11f, 0.12f, 0.15f};
    const Color light_bg{0.97f, 0.98f, 1.0f};
    for (char l : std::string("rgbcmykw"))
    {
        EXPECT_GT(contrast(fc::shade(l, false), dark_bg), 4.0f) << l << " on dark";
        EXPECT_GT(contrast(fc::shade(l, true), light_bg), 2.5f) << l << " on light";
    }
    // The raw primaries that motivated this: blue on dark, yellow on light.
    EXPECT_LT(contrast(spectra::colors::blue, dark_bg), 2.5f);
    EXPECT_LT(contrast(spectra::colors::yellow, light_bg), 1.2f);
}

// ─── Parsing and Series state ───────────────────────────────────────────────

TEST(FormatLetterColors, ParseRecordsLetterAndRoundTrips)
{
    for (char l : std::string("rgbcmykw"))
    {
        const std::string fmt = std::string(1, l) + "--o";
        const auto        ps  = spectra::parse_format_string(fmt);
        ASSERT_TRUE(ps.color.has_value());
        EXPECT_EQ(ps.color_letter, l);
        EXPECT_TRUE(same_rgb(*ps.color, fc::shade(l)));
        EXPECT_EQ(spectra::to_format_string(ps), fmt);
    }
    EXPECT_EQ(spectra::parse_format_string("--o").color_letter, '\0');
}

TEST(FormatLetterColors, ResolveFollowsThemeBackground)
{
    spectra::Figure fig;
    auto&           ax  = fig.subplot(1, 1, 1);
    auto&           b   = ax.plot(kX, kY, "b:");
    auto&           y   = ax.plot(kX, kY, "y-");
    auto&           hex = ax.line(kX, kY);
    hex.color(spectra::colors::blue);   // explicit pure blue stays exactly as given
    EXPECT_EQ(b.color_letter(), 'b');
    EXPECT_FALSE(b.has_auto_color());

    spectra::ui::resolve_auto_series_colors(fig, spectra::ui::DataPalette{}, true);
    EXPECT_TRUE(same_rgb(b.color(), fc::shade('b', true)));
    EXPECT_TRUE(same_rgb(y.color(), fc::shade('y', true)));
    EXPECT_TRUE(same_rgb(hex.color(), spectra::colors::blue));

    spectra::ui::resolve_auto_series_colors(fig, spectra::ui::DataPalette{}, false);
    EXPECT_TRUE(same_rgb(b.color(), fc::shade('b', false)));
    EXPECT_TRUE(same_rgb(y.color(), fc::shade('y', false)));
    EXPECT_TRUE(same_rgb(hex.color(), spectra::colors::blue));
    EXPECT_EQ(b.color_letter(), 'b');   // still lettered after resolving
}

TEST(FormatLetterColors, ExplicitColorClearsLetter)
{
    spectra::LineSeries s(kX, kY);
    s.format("r--");
    EXPECT_EQ(s.color_letter(), 'r');
    s.color(spectra::rgb(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(s.color_letter(), '\0');
    EXPECT_FALSE(s.resolve_letter_color(fc::shade('r', true)));

    // A PlotStyle whose colour was edited after parsing is explicit too.
    auto ps  = spectra::parse_format_string("g-");
    ps.color = spectra::colors::orange;
    s.plot_style(ps);
    EXPECT_EQ(s.color_letter(), '\0');
    EXPECT_TRUE(same_rgb(s.color(), spectra::colors::orange));

    s.plot_style(spectra::parse_format_string("g-"));
    EXPECT_EQ(s.color_letter(), 'g');

    spectra::LineSeries copy(s);
    EXPECT_EQ(copy.color_letter(), 'g');
}

TEST(FormatLetterColors, UndoRestoresLetter)
{
    spectra::UndoManager mgr;
    spectra::LineSeries  s(kX, kY);
    s.format("m-");
    spectra::undoable_set_series_color(&mgr, s, spectra::colors::orange);
    EXPECT_EQ(s.color_letter(), '\0');
    mgr.undo();
    EXPECT_EQ(s.color_letter(), 'm');
}

TEST(FormatLetterColors, ThemeBackgroundDetection)
{
    spectra::ui::ThemeManager tm;
    tm.ensure_initialized();
    tm.set_theme("light");
    EXPECT_TRUE(spectra::ui::is_light_plot_background(tm.colors()));
    for (const char* name : {"dark", "night", "high_contrast"})
    {
        tm.set_theme(name);
        EXPECT_FALSE(spectra::ui::is_light_plot_background(tm.colors())) << name;
    }
}

// ─── IPC: letter travels to the window ──────────────────────────────────────

TEST(FormatLetterColors, SnapshotColorLetterRoundTrips)
{
    spectra::ipc::StateSnapshotPayload p;
    spectra::ipc::SnapshotFigureState  fig;
    fig.figure_id = 1;
    spectra::ipc::SnapshotSeriesState lettered;
    lettered.type         = "line";
    lettered.color_letter = 'k';
    spectra::ipc::SnapshotSeriesState plain;
    plain.type = "line";
    fig.series = {lettered, plain};
    p.figures.push_back(fig);

    auto buf = spectra::ipc::encode_fb_state_snapshot(p);
    auto dec = spectra::ipc::decode_fb_state_snapshot(spectra::ipc::strip_fb_prefix(buf));
    ASSERT_TRUE(dec.has_value());
    ASSERT_EQ(dec->figures[0].series.size(), 2u);
    EXPECT_EQ(dec->figures[0].series[0].color_letter, 'k');
    EXPECT_EQ(dec->figures[0].series[1].color_letter, 0);
}

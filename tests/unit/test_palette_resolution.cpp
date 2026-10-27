// Auto-coloured series follow the theme's data palette (P0-6, P1-1, P1-4).

#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes3d.hpp>
#include <vector>

#include "daemon/figure_model.hpp"
#include "ipc/codec_fb.hpp"
#include "ipc/message.hpp"
#include "ui/theme/theme.hpp"

using spectra::Color;
using spectra::ui::DataPalette;
using spectra::ui::ThemeManager;

namespace
{

bool same_rgb(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool same_rgb(const Color& a, const spectra::ui::Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

DataPalette make_palette(std::vector<spectra::ui::Color> colors)
{
    DataPalette p;
    p.name   = "test";
    p.colors = std::move(colors);
    return p;
}

const std::vector<float> kX = {0.0f, 1.0f, 2.0f};
const std::vector<float> kY = {1.0f, 2.0f, 3.0f};
const std::vector<float> kZ = {0.5f, 1.5f, 2.5f};

class ThemePaletteTest : public ::testing::Test
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

// ─── Series auto-colour state ───────────────────────────────────────────────

TEST(PaletteResolution, AxesAssignsAutoColorSlots)
{
    spectra::Figure fig;
    auto&           ax = fig.subplot(1, 1, 1);
    auto&           a  = ax.line(kX, kY);
    auto&           b  = ax.scatter(kX, kY);

    EXPECT_TRUE(a.has_auto_color());
    EXPECT_TRUE(b.has_auto_color());
    EXPECT_EQ(a.auto_color_index(), 0);
    EXPECT_EQ(b.auto_color_index(), 1);
    // Before any draw, color() returns the provisional default-cycle colour.
    EXPECT_TRUE(same_rgb(a.color(), spectra::palette::default_cycle[0]));
    EXPECT_TRUE(same_rgb(b.color(), spectra::palette::default_cycle[1]));
}

TEST(PaletteResolution, ExplicitColorDisablesAutoColor)
{
    spectra::Figure fig;
    auto&           ax = fig.subplot(1, 1, 1);
    auto&           a  = ax.line(kX, kY);
    a.color(spectra::colors::red);
    EXPECT_FALSE(a.has_auto_color());
    EXPECT_EQ(a.auto_color_index(), -1);

    auto& b = ax.line(kX, kY);
    b.set_color(spectra::colors::green);
    EXPECT_FALSE(b.has_auto_color());
}

TEST(PaletteResolution, FormatStringColorIsExplicitButStyleOnlyKeepsAuto)
{
    spectra::Figure fig;
    auto&           ax         = fig.subplot(1, 1, 1);
    auto&           colored    = ax.plot(kX, kY, "r--");
    auto&           style_only = ax.plot(kX, kY, "--o");
    EXPECT_FALSE(colored.has_auto_color());
    EXPECT_TRUE(same_rgb(colored.color(), spectra::format_colors::shade('r')));
    EXPECT_TRUE(style_only.has_auto_color());
}

TEST(PaletteResolution, ResolveRecolorsOnlyAutoSeries)
{
    spectra::Figure fig;
    auto&           ax        = fig.subplot(1, 1, 1);
    auto&           auto0     = ax.line(kX, kY);
    auto&           explicit1 = ax.line(kX, kY);
    auto&           auto2     = ax.line(kX, kY);
    explicit1.color(spectra::colors::red);

    auto pal = make_palette({spectra::ui::Color(0.1f, 0.2f, 0.3f),
                             spectra::ui::Color(0.4f, 0.5f, 0.6f),
                             spectra::ui::Color(0.7f, 0.8f, 0.9f)});
    spectra::ui::resolve_auto_series_colors(fig, pal);

    EXPECT_TRUE(same_rgb(auto0.color(), pal.colors[0]));
    EXPECT_TRUE(same_rgb(explicit1.color(), spectra::colors::red));
    EXPECT_TRUE(same_rgb(auto2.color(), pal.colors[2]));
    EXPECT_TRUE(auto0.has_auto_color());   // still follows the palette

    // Switching palette recolours existing auto series; slots wrap around.
    auto pal2 =
        make_palette({spectra::ui::Color(1.0f, 0.0f, 0.0f), spectra::ui::Color(0.0f, 1.0f, 0.0f)});
    spectra::ui::resolve_auto_series_colors(fig, pal2);
    EXPECT_TRUE(same_rgb(auto0.color(), pal2.colors[0]));
    EXPECT_TRUE(same_rgb(auto2.color(), pal2.colors[0]));   // slot 2 % 2
    EXPECT_TRUE(same_rgb(explicit1.color(), spectra::colors::red));
}

TEST(PaletteResolution, EmptyPaletteFallsBackToDefaultCycle)
{
    spectra::Figure fig;
    auto&           ax = fig.subplot(1, 1, 1);
    ax.line(kX, kY);
    auto& s = ax.line(kX, kY);
    spectra::ui::resolve_auto_series_colors(fig, DataPalette{});
    EXPECT_TRUE(same_rgb(s.color(), spectra::palette::default_cycle[1]));
}

TEST(PaletteResolution, Axes3DSeriesAreAutoColoredNotBlue)
{
    spectra::Figure       fig;
    auto&                 ax    = fig.subplot3d(1, 1, 1);
    auto&                 sc    = ax.scatter3d(kX, kY, kZ);
    auto&                 ln    = ax.line3d(kX, kY, kZ);
    std::vector<float>    grid  = {0.0f, 1.0f};
    std::vector<float>    zv    = {0.0f, 1.0f, 1.0f, 0.0f};
    auto&                 sf    = ax.surface(grid, grid, zv);
    std::vector<float>    verts = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1};
    std::vector<uint32_t> idx   = {0, 1, 2};
    auto&                 ms    = ax.mesh(verts, idx);
    auto&                 sh0   = ax.shapes3d();
    sh0.box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    auto& sh1 = ax.shapes3d();
    sh1.sphere(0.0f, 0.0f, 0.0f, 0.5f);

    for (const spectra::Series* s : {static_cast<spectra::Series*>(&sc),
                                     static_cast<spectra::Series*>(&ln),
                                     static_cast<spectra::Series*>(&sf),
                                     static_cast<spectra::Series*>(&ms),
                                     static_cast<spectra::Series*>(&sh0),
                                     static_cast<spectra::Series*>(&sh1)})
    {
        EXPECT_TRUE(s->has_auto_color());
        EXPECT_FALSE(same_rgb(s->color(), spectra::colors::blue));
    }
    EXPECT_EQ(sc.auto_color_index(), 0);
    EXPECT_EQ(ms.auto_color_index(), 3);
    EXPECT_EQ(sh0.auto_color_index(), 4);
    EXPECT_EQ(sh1.auto_color_index(), 5);
    EXPECT_FALSE(same_rgb(sh0.color(), sh1.color()));

    auto pal = make_palette({spectra::ui::Color(0.1f, 0.2f, 0.3f),
                             spectra::ui::Color(0.4f, 0.5f, 0.6f),
                             spectra::ui::Color(0.7f, 0.8f, 0.9f)});
    spectra::ui::resolve_auto_series_colors(fig, pal);
    EXPECT_TRUE(same_rgb(sc.color(), pal.colors[0]));
    EXPECT_TRUE(same_rgb(ln.color(), pal.colors[1]));
    EXPECT_TRUE(same_rgb(sh0.color(), pal.colors[1]));   // slot 4 % 3
    EXPECT_TRUE(same_rgb(sh1.color(), pal.colors[2]));   // slot 5 % 3

    // A palette/theme change recolours the shapes again.
    auto pal2 =
        make_palette({spectra::ui::Color(1.0f, 0.0f, 0.0f), spectra::ui::Color(0.0f, 1.0f, 0.0f)});
    spectra::ui::resolve_auto_series_colors(fig, pal2);
    EXPECT_TRUE(same_rgb(sh0.color(), pal2.colors[0]));   // slot 4 % 2
    EXPECT_TRUE(same_rgb(sh1.color(), pal2.colors[1]));   // slot 5 % 2

    // Explicit colours stay stable across palette changes.
    ln.color(spectra::colors::red);
    sh1.color(spectra::colors::cyan);
    spectra::ui::resolve_auto_series_colors(fig, pal);
    EXPECT_TRUE(same_rgb(ln.color(), spectra::colors::red));
    EXPECT_TRUE(same_rgb(sh1.color(), spectra::colors::cyan));
    EXPECT_FALSE(sh1.has_auto_color());
    EXPECT_TRUE(same_rgb(sh0.color(), pal.colors[1]));
}

// S18 + S19 + S20: an auto-coloured ShapeSeries3D (Axes3D::shapes3d()) with a
// mix of explicit and unspecified per-shape colours.  Draw ranges are built
// lazily with the geometry (no render, no explicit rebuild), shapes without a
// colour inherit the series colour, which is the palette's auto colour.
TEST(PaletteResolution, Shape3DAutoColorFlowsIntoLazyDrawRanges)
{
    spectra::Figure fig;
    auto&           ax = fig.subplot3d(1, 1, 1);
    auto&           sh = ax.shapes3d();
    sh.box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f).shape_color(spectra::colors::red);
    sh.sphere(3.0f, 0.0f, 0.0f, 0.5f);   // no shape colour: inherits the series colour
    ASSERT_TRUE(sh.has_auto_color());

    auto ranges = sh.draw_ranges();   // first access builds geometry + ranges
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_FALSE(ranges[0].inherits_series_color());
    EXPECT_TRUE(ranges[1].inherits_series_color());
    EXPECT_EQ(ranges[1].first_index + ranges[1].index_count, sh.indices().size());

    auto pal = make_palette({spectra::ui::Color(0.1f, 0.2f, 0.3f)});
    spectra::ui::resolve_auto_series_colors(fig, pal);
    EXPECT_TRUE(same_rgb(sh.color(), pal.colors[0]));
    EXPECT_FALSE(same_rgb(sh.color(), spectra::colors::blue));

    // Adding a shape after the first build refreshes the ranges lazily too.
    sh.cone(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f);
    ranges = sh.draw_ranges();
    ASSERT_EQ(ranges.size(), 2u);   // sphere + cone merge into one inheriting range
    EXPECT_TRUE(ranges[1].inherits_series_color());
    EXPECT_EQ(ranges[1].first_index + ranges[1].index_count, sh.indices().size());

    // Bounds (S20) are available without a render as well.
    spectra::vec3 lo{}, hi{};
    sh.get_bounds(lo, hi);
    EXPECT_LT(lo.x, hi.x);
}

TEST(PaletteResolution, CopyKeepsAutoColorAndIndexCanBeRestored)
{
    spectra::LineSeries a(kX, kY);
    a.set_auto_color_index(4);
    spectra::LineSeries b(a);
    EXPECT_EQ(b.auto_color_index(), 4);

    b.color(spectra::colors::red);
    EXPECT_FALSE(b.has_auto_color());
    b.set_auto_color_index(4);   // e.g. undo of a colour change
    EXPECT_TRUE(b.has_auto_color());
    EXPECT_FALSE(b.resolve_auto_color(b.color()));   // unchanged colour: no-op
}

// ─── ThemeManager palette selection ─────────────────────────────────────────

TEST_F(ThemePaletteTest, DefaultPaletteFollowsTheme)
{
    tm_.set_data_palette("default");

    tm_.set_theme("night");
    const auto& night = tm_.current_data_palette();
    ASSERT_FALSE(night.colors.empty());
    EXPECT_TRUE(same_rgb(spectra::palette::default_cycle[0], night.colors[0]));

    tm_.set_theme("light");
    const auto& light = tm_.current_data_palette();
    ASSERT_EQ(light.colors.size(), spectra::palette::light_cycle_size);
    EXPECT_TRUE(same_rgb(spectra::palette::light_cycle[0], light.colors[0]));
}

TEST_F(ThemePaletteTest, LightDefaultPaletteIsReadableOnLightCanvas)
{
    tm_.set_theme("light");
    tm_.set_data_palette("default");
    const auto& canvas = tm_.colors().bg_canvas;
    const auto& colors = tm_.current_data_palette().colors;
    ASSERT_FALSE(colors.empty());
    float sum = 0.0f;
    for (const auto& c : colors)
    {
        const float cr = c.contrast_ratio(canvas);
        EXPECT_GT(cr, 1.7f) << "palette colour too light for the light canvas";
        sum += cr;
    }
    // The dark-theme vivid palette averages below 2:1 on the light canvas.
    EXPECT_GT(sum / static_cast<float>(colors.size()), 3.0f);
}

TEST_F(ThemePaletteTest, SelectedPaletteIsKeptAcrossThemeSwitches)
{
    tm_.set_theme("night");
    tm_.set_data_palette("colorblind");
    const auto expected = tm_.get_data_palette("colorblind").colors;

    tm_.set_theme("light");
    EXPECT_EQ(tm_.current_data_palette_name(), "colorblind");
    ASSERT_EQ(tm_.current_data_palette().colors.size(), expected.size());
    EXPECT_EQ(tm_.current_data_palette().colors[0].to_hex(), expected[0].to_hex());

    // Selecting a palette must not overwrite the theme's own default palette.
    tm_.set_data_palette("default");
    EXPECT_TRUE(same_rgb(spectra::palette::light_cycle[0], tm_.current_data_palette().colors[0]));
}

TEST_F(ThemePaletteTest, PaletteSwitchRecolorsExistingFigure)
{
    spectra::Figure fig;
    auto&           ax         = fig.subplot(1, 1, 1);
    auto&           s          = ax.line(kX, kY);
    auto&           explicit_s = ax.line(kX, kY);
    explicit_s.color(spectra::colors::red);

    for (const char* name : {"default", "colorblind", "tol_bright", "tol_muted", "ibm"})
    {
        tm_.set_data_palette(name);
        spectra::ui::resolve_auto_series_colors(fig, tm_.current_data_palette());
        EXPECT_TRUE(same_rgb(s.color(), tm_.current_data_palette().colors[0])) << name;
        EXPECT_TRUE(same_rgb(explicit_s.color(), spectra::colors::red)) << name;
    }
}

// ─── IPC: palette slot travels to the window ────────────────────────────────

TEST(PaletteResolution, SnapshotColorIndexRoundTrips)
{
    spectra::ipc::StateSnapshotPayload p;
    spectra::ipc::SnapshotFigureState  fig;
    fig.figure_id = 1;
    spectra::ipc::SnapshotSeriesState auto_s;
    auto_s.type        = "line";
    auto_s.color_index = 3;
    spectra::ipc::SnapshotSeriesState explicit_s;
    explicit_s.type = "line";
    fig.series      = {auto_s, explicit_s};
    p.figures.push_back(fig);

    auto buf = spectra::ipc::encode_fb_state_snapshot(p);
    auto dec = spectra::ipc::decode_fb_state_snapshot(spectra::ipc::strip_fb_prefix(buf));
    ASSERT_TRUE(dec.has_value());
    ASSERT_EQ(dec->figures[0].series.size(), 2u);
    EXPECT_EQ(dec->figures[0].series[0].color_index, 3);
    EXPECT_EQ(dec->figures[0].series[1].color_index, -1);
}

TEST(PaletteResolution, FigureModelAssignsPerAxesSlotsAndExplicitColorClearsThem)
{
    spectra::daemon::FigureModel model;
    auto                         fid = model.create_figure("f");
    model.add_axes(fid);
    model.add_axes(fid);
    uint32_t i0 = 0, i1 = 0, i2 = 0;
    model.add_series_with_diff(fid, "a", "line", 0, i0);
    model.add_series_with_diff(fid, "b", "line", 0, i1);
    model.add_series_with_diff(fid, "c", "line", 1, i2);
    model.set_series_color(fid, i1, 1.0f, 0.0f, 0.0f, 1.0f);

    auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    const auto& ser = snap.figures[0].series;
    ASSERT_EQ(ser.size(), 3u);
    EXPECT_EQ(ser[0].color_index, 0);
    EXPECT_EQ(ser[1].color_index, -1);   // explicit after set_series_color
    EXPECT_EQ(ser[2].color_index, 0);    // first series of axes 1
}

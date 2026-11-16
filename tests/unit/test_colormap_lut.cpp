// Colormap lookup tables: values against matplotlib, reversed maps, user
// tables, name parsing, and one colour for one t across every series type.

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <set>
#include <span>
#include <spectra/colormap.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_image.hpp>
#include <string>
#include <vector>

#include "core/colorbar_layout.hpp"
#include "core/image_colormap.hpp"

namespace spectra
{
namespace
{

// Tables are 8-bit: within half a step of matplotlib's float values.
constexpr float TOL = 0.5f / 255.0f + 1e-5f;

void expect_rgb_near(const Color& c, const std::array<float, 3>& ref, const std::string& what)
{
    EXPECT_NEAR(c.r, ref[0], TOL) << what;
    EXPECT_NEAR(c.g, ref[1], TOL) << what;
    EXPECT_NEAR(c.b, ref[2], TOL) << what;
}

struct Reference
{
    const char*                         name;
    std::array<std::array<float, 3>, 4> rgb;   // matplotlib cmap(t) for T below
};

constexpr std::array<float, 4> T = {0.0f, 0.5f, 0.75f, 1.0f};

// matplotlib 3.11: matplotlib.colormaps[name](t)[:3] for t in T (256-entry maps).
const std::vector<Reference>& references()
{
    static const std::vector<Reference> refs = {
        {"viridis",
         {{{0.267004f, 0.004874f, 0.329415f},
           {0.127568f, 0.566949f, 0.550556f},
           {0.369214f, 0.788888f, 0.382914f},
           {0.993248f, 0.906157f, 0.143936f}}}},
        {"plasma",
         {{{0.050383f, 0.029803f, 0.527975f},
           {0.798216f, 0.280197f, 0.469538f},
           {0.973416f, 0.585761f, 0.25154f},
           {0.940015f, 0.975158f, 0.131326f}}}},
        {"inferno",
         {{{0.001462f, 0.000466f, 0.013866f},
           {0.735683f, 0.215906f, 0.330245f},
           {0.978422f, 0.557937f, 0.034931f},
           {0.988362f, 0.998364f, 0.644924f}}}},
        {"magma",
         {{{0.001462f, 0.000466f, 0.013866f},
           {0.716387f, 0.214982f, 0.47529f},
           {0.9867f, 0.535582f, 0.38221f},
           {0.987053f, 0.991438f, 0.749504f}}}},
        {"cividis",
         {{{0.0f, 0.135112f, 0.304751f},
           {0.488697f, 0.485318f, 0.471008f},
           {0.736488f, 0.680629f, 0.424028f},
           {0.995737f, 0.909344f, 0.217772f}}}},
        {"turbo",
         {{{0.18995f, 0.07176f, 0.23217f},
           {0.64362f, 0.98999f, 0.23356f},
           {0.9836f, 0.49291f, 0.12849f},
           {0.4796f, 0.01583f, 0.01055f}}}},
        // twilight has 510 entries in matplotlib: twilight.resampled(256)(t).
        {"twilight",
         {{{0.88575f, 0.850009f, 0.887974f},
           {0.187745f, 0.077252f, 0.213874f},
           {0.703162f, 0.347187f, 0.324089f},
           {0.885712f, 0.850022f, 0.885725f}}}},
        {"coolwarm",
         {{{0.229806f, 0.298718f, 0.753683f},
           {0.867428f, 0.864377f, 0.862602f},
           {0.956653f, 0.598034f, 0.477302f},
           {0.705673f, 0.015556f, 0.150233f}}}},
        {"RdBu",
         {{{0.403922f, 0.0f, 0.121569f},
           {0.965705f, 0.967243f, 0.968089f},
           {0.408535f, 0.668743f, 0.814533f},
           {0.019608f, 0.188235f, 0.380392f}}}},
        {"PuOr",
         {{{0.498039f, 0.231373f, 0.031373f},
           {0.966244f, 0.966398f, 0.967705f},
           {0.594233f, 0.554325f, 0.744637f},
           {0.176471f, 0.0f, 0.294118f}}}},
        {"jet",
         {{{0.0f, 0.0f, 0.5f},
           {0.490196f, 1.0f, 0.477546f},
           {1.0f, 0.581699f, 0.0f},
           {0.5f, 0.0f, 0.0f}}}},
        {"gray",
         {{{0.0f, 0.0f, 0.0f},
           {0.501961f, 0.501961f, 0.501961f},
           {0.752941f, 0.752941f, 0.752941f},
           {1.0f, 1.0f, 1.0f}}}},
        {"hot",
         {{{0.0416f, 0.0f, 0.0f},
           {1.0f, 0.359314f, 0.0f},
           {1.0f, 1.0f, 0.027205f},
           {1.0f, 1.0f, 1.0f}}}},
        {"viridis_r",
         {{{0.993248f, 0.906157f, 0.143936f},
           {0.128729f, 0.563265f, 0.551229f},
           {0.231674f, 0.318106f, 0.544834f},
           {0.267004f, 0.004874f, 0.329415f}}}},
        {"RdBu_r",
         {{{0.019608f, 0.188235f, 0.380392f},
           {0.969089f, 0.966474f, 0.964937f},
           {0.894579f, 0.503806f, 0.399769f},
           {0.403922f, 0.0f, 0.121569f}}}},
    };
    return refs;
}

TEST(ColormapLut, MatchesMatplotlibReference)
{
    for (const auto& ref : references())
    {
        const auto cm = Colormap::from_name(ref.name);
        ASSERT_TRUE(cm.has_value()) << ref.name;
        for (size_t i = 0; i < T.size(); ++i)
            expect_rgb_near(cm->sample(T[i]),
                            ref.rgb[i],
                            std::string(ref.name) + " t=" + std::to_string(T[i]));
    }
}

TEST(ColormapLut, ViridisAtThreeQuartersIsBrightGreen)
{
    // The old scatter ramp gave olive (131, 163, 71) here.
    const Color c = Colormap(ColormapType::Viridis).sample(0.75f);
    EXPECT_NEAR(c.r * 255.0f, 94.0f, 0.6f);
    EXPECT_NEAR(c.g * 255.0f, 201.0f, 0.6f);
    EXPECT_NEAR(c.b * 255.0f, 98.0f, 0.6f);
}

TEST(ColormapLut, SampleClampsAndHandlesNaN)
{
    const Colormap cm(ColormapType::Viridis);
    const Color    lo = cm.sample(0.0f);
    const Color    hi = cm.sample(1.0f);
    EXPECT_EQ(cm.sample(-3.0f).r, lo.r);
    EXPECT_EQ(cm.sample(7.0f).g, hi.g);
    EXPECT_EQ(cm.sample(std::numeric_limits<float>::quiet_NaN()).b, lo.b);
}

TEST(ColormapLut, ReversedMirrorsTheTable)
{
    for (ColormapType type : builtin_colormaps())
    {
        const Colormap fwd(type);
        const Colormap rev = fwd.reversed(true);
        EXPECT_TRUE(rev.is_reversed());
        const auto a = fwd.rgba8();
        const auto b = rev.rgba8();
        for (size_t i = 0; i < Colormap::LUT_SIZE; ++i)
            for (size_t ch = 0; ch < 4; ++ch)
                ASSERT_EQ(a[i * 4 + ch], b[(Colormap::LUT_SIZE - 1 - i) * 4 + ch])
                    << colormap_name(type) << " entry " << i;
        EXPECT_FALSE(rev.reversed(false).is_reversed());
        EXPECT_EQ(rev.reversed(false), fwd);
        EXPECT_FALSE(rev == fwd);
    }
}

TEST(ColormapLut, Rgba8MatchesSample)
{
    const Colormap cm(ColormapType::Turbo, true);
    const auto     lut = cm.rgba8();
    for (size_t i = 0; i < Colormap::LUT_SIZE; ++i)
    {
        const Color c = cm.sample((static_cast<float>(i) + 0.5f) / Colormap::LUT_SIZE);
        EXPECT_NEAR(lut[i * 4 + 0] / 255.0f, c.r, 1e-6f);
        EXPECT_NEAR(lut[i * 4 + 1] / 255.0f, c.g, 1e-6f);
        EXPECT_NEAR(lut[i * 4 + 2] / 255.0f, c.b, 1e-6f);
        EXPECT_EQ(lut[i * 4 + 3], 255);
    }
}

TEST(ColormapLut, ParsesMatplotlibNames)
{
    struct Case
    {
        const char*  name;
        ColormapType type;
        bool         reversed;
    };
    const Case cases[] = {
        {"viridis", ColormapType::Viridis, false},
        {"VIRIDIS", ColormapType::Viridis, false},
        {"viridis_r", ColormapType::Viridis, true},
        {"cividis", ColormapType::Cividis, false},
        {"turbo", ColormapType::Turbo, false},
        {"twilight", ColormapType::Twilight, false},
        {"RdBu", ColormapType::RdBu, false},
        {"rdbu_R", ColormapType::RdBu, true},
        {"PuOr_r", ColormapType::PuOr, true},
        {"coolwarm", ColormapType::Coolwarm, false},
        {"gray", ColormapType::Grayscale, false},
        {"grey", ColormapType::Grayscale, false},
        {"grayscale", ColormapType::Grayscale, false},
        {"hot", ColormapType::Hot, false},
        {"jet_r", ColormapType::Jet, true},
        {"none", ColormapType::None, false},
    };
    for (const auto& c : cases)
    {
        const auto cm = Colormap::from_name(c.name);
        ASSERT_TRUE(cm.has_value()) << c.name;
        EXPECT_EQ(cm->type(), c.type) << c.name;
        EXPECT_EQ(cm->is_reversed(), c.reversed) << c.name;
    }
    EXPECT_FALSE(Colormap::from_name("bogus").has_value());
    EXPECT_FALSE(Colormap::from_name("_r").has_value());
    EXPECT_FALSE(Colormap::from_name("").has_value());
}

TEST(ColormapLut, NamesRoundTrip)
{
    std::set<ColormapType> seen;
    for (ColormapType type : builtin_colormaps())
    {
        EXPECT_TRUE(seen.insert(type).second) << "duplicate map";
        for (bool rev : {false, true})
        {
            const Colormap cm(type, rev);
            const auto     parsed = Colormap::from_name(cm.name());
            ASSERT_TRUE(parsed.has_value()) << cm.name();
            EXPECT_EQ(*parsed, cm) << cm.name();
        }
    }
    EXPECT_EQ(seen.size(), 13u);
    EXPECT_EQ(seen.count(ColormapType::None), 0u);
    EXPECT_EQ(seen.count(ColormapType::Custom), 0u);
    EXPECT_EQ(Colormap(ColormapType::RdBu, true).name(), "RdBu_r");
}

TEST(ColormapLut, FromColorsInterpolates)
{
    const std::vector<Color> stops = {{1.0f, 0.0f, 0.0f, 1.0f},
                                      {0.0f, 0.0f, 1.0f, 1.0f},
                                      {0.0f, 1.0f, 0.0f, 1.0f}};
    const Colormap           cm    = Colormap::from_colors(stops);
    EXPECT_TRUE(cm.is_custom());
    EXPECT_EQ(cm.type(), ColormapType::Custom);
    expect_rgb_near(cm.sample(0.0f), {1.0f, 0.0f, 0.0f}, "start");
    expect_rgb_near(cm.sample(1.0f), {0.0f, 1.0f, 0.0f}, "end");
    // Entry 128 sits at 128/255 of the way: just past the middle stop.
    const Color mid = cm.sample(0.5f);
    EXPECT_NEAR(mid.b, 1.0f, 0.01f);
    EXPECT_NEAR(mid.g, 0.0f, 0.01f);
    // Quarter: half way between red and blue.
    const Color q = cm.sample(0.25f);
    EXPECT_NEAR(q.r, 0.5f, 0.01f);
    EXPECT_NEAR(q.b, 0.5f, 0.01f);

    // Reversed user table.
    expect_rgb_near(cm.reversed(true).sample(0.0f), {0.0f, 1.0f, 0.0f}, "reversed start");

    // Copies compare equal; different colours do not.
    EXPECT_EQ(Colormap::from_colors(stops), cm);
    const std::vector<Color> other = {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    EXPECT_FALSE(Colormap::from_colors(other) == cm);

    // One colour: flat.  None: no colours.
    const Color only{0.2f, 0.4f, 0.6f, 1.0f};
    const auto  flat = Colormap::from_colors(std::span<const Color>(&only, 1));
    expect_rgb_near(flat.sample(0.9f), {0.2f, 0.4f, 0.6f}, "flat");
    EXPECT_TRUE(Colormap::from_colors({}).is_none());
}

TEST(ColormapLut, UserTableAlphaReachesTheLut)
{
    const std::vector<Color> stops = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};
    const Colormap           cm    = Colormap::from_colors(stops);
    EXPECT_TRUE(cm.has_transparency());
    EXPECT_NEAR(cm.sample(0.0f).a, 0.0f, 1e-6f);
    EXPECT_NEAR(cm.sample(1.0f).a, 1.0f, 1e-6f);
    EXPECT_NEAR(cm.sample(0.5f).a, 128.0f / 255.0f, 1e-3f);

    // The texture every shader reads carries the alpha (reversed too).
    const auto lut = cm.rgba8();
    EXPECT_EQ(lut[3], 0);
    EXPECT_EQ(lut[(Colormap::LUT_SIZE - 1) * 4 + 3], 255);
    const auto rev = cm.reversed(true).rgba8();
    EXPECT_EQ(rev[3], 255);
    EXPECT_EQ(rev[(Colormap::LUT_SIZE - 1) * 4 + 3], 0);
    const auto img = image_colormap::build_lut(cm, {});
    EXPECT_EQ(img[3], 0);

    // Opaque tables are not transparent.
    for (ColormapType type : builtin_colormaps())
        EXPECT_FALSE(Colormap(type).has_transparency()) << colormap_name(type);
    const std::vector<Color> opaque = {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};
    EXPECT_FALSE(Colormap::from_colors(opaque).has_transparency());

    // A surface with a translucent colormap takes the transparent (sorted) path.
    SurfaceSeries surface;
    EXPECT_FALSE(surface.colormap(ColormapType::Viridis).is_transparent());
    EXPECT_TRUE(surface.colormap(cm).is_transparent());
}

// One t gives one colour for scatter, surface, image, contour and the colorbar.
TEST(ColormapLut, SameColourAcrossSeriesTypes)
{
    for (const char* name : {"viridis", "coolwarm", "RdBu_r", "turbo"})
    {
        const Colormap cm = *Colormap::from_name(name);

        ScatterSeries scatter;
        scatter.colormap(cm);
        SurfaceSeries surface;
        surface.colormap(cm);
        ImageSeries              image;
        const std::vector<float> grid = {0.0f, 1.0f};
        image.set_data(grid, 2, 1).colormap(cm).colormap_range(0.0f, 1.0f);
        ContourSeries contour;
        contour.colormap(cm).colormap_range(0.0f, 1.0f);

        for (float t : {0.0f, 0.1f, 0.33f, 0.5f, 0.75f, 0.9f, 1.0f})
        {
            const Color ref = cm.sample(t);
            const auto  msg = std::string(name) + " t=" + std::to_string(t);
            expect_rgb_near(scatter.cmap().sample(t), {ref.r, ref.g, ref.b}, msg + " scatter");
            expect_rgb_near(surface.cmap().sample(t), {ref.r, ref.g, ref.b}, msg + " surface");
            expect_rgb_near(image.color_for_value(t), {ref.r, ref.g, ref.b}, msg + " image");
            expect_rgb_near(contour.level_color(t), {ref.r, ref.g, ref.b}, msg + " contour");
            for (auto mapping : {colorbar_layout::Mapping::Scatter,
                                 colorbar_layout::Mapping::Surface,
                                 colorbar_layout::Mapping::Image})
                expect_rgb_near(colorbar_layout::sample(cm, mapping, t),
                                {ref.r, ref.g, ref.b},
                                msg + " colorbar");
        }
        // The image LUT texture holds the same table.
        const auto lut = image_colormap::build_lut(image.cmap(), {});
        const auto ref = cm.rgba8();
        ASSERT_EQ(lut.size(), ref.size());
        for (size_t i = 0; i < lut.size(); ++i)
            ASSERT_EQ(lut[i], ref[i]) << name << " byte " << i;
    }
}

TEST(ColormapLut, SeriesReversedSetter)
{
    ScatterSeries scatter;
    scatter.colormap(ColormapType::Viridis).colormap_reversed(true);
    EXPECT_TRUE(scatter.colormap_reversed());
    // Changing the type keeps the direction.
    scatter.colormap(ColormapType::Plasma);
    EXPECT_EQ(scatter.colormap(), ColormapType::Plasma);
    EXPECT_TRUE(scatter.colormap_reversed());
    scatter.colormap(std::string("cividis"));
    EXPECT_EQ(scatter.colormap(), ColormapType::Cividis);
    EXPECT_FALSE(scatter.colormap_reversed());
    scatter.colormap(std::string("magma_r"));
    EXPECT_EQ(scatter.colormap(), ColormapType::Magma);
    EXPECT_TRUE(scatter.colormap_reversed());

    SurfaceSeries surface;
    surface.colormap("RdBu_r");
    EXPECT_EQ(surface.colormap_type(), ColormapType::RdBu);
    EXPECT_TRUE(surface.colormap_reversed());
    surface.colormap_reversed(false);
    EXPECT_FALSE(surface.colormap_reversed());

    ImageSeries    image;
    const uint64_t gen = image.lut_generation();
    image.colormap_reversed(true);
    EXPECT_TRUE(image.colormap_reversed());
    EXPECT_EQ(image.colormap(), ColormapType::Viridis);
    EXPECT_NE(image.lut_generation(), gen);   // GPU lookup table re-upload

    ContourSeries contour;
    contour.colormap(std::string("twilight_r"));
    EXPECT_EQ(contour.colormap(), ColormapType::Twilight);
    EXPECT_TRUE(contour.colormap_reversed());
}

}   // namespace
}   // namespace spectra

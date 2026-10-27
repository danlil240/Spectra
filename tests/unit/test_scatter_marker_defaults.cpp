// Default scatter marker style and size (visual polish finding P1-10).
#include <gtest/gtest.h>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include "render/scatter_marker.hpp"

using namespace spectra;

namespace
{
constexpr float DARK_CANVAS  = 0.05f;   // night / dark themes
constexpr float LIGHT_CANVAS = 0.97f;   // light theme
}   // namespace

TEST(ScatterMarkerDefaults, DefaultSizeIsSixPixels)
{
    ScatterSeries s;
    EXPECT_FLOAT_EQ(s.size(), 6.0f);
    ScatterSeries3D s3;
    EXPECT_FLOAT_EQ(s3.size(), 6.0f);
}

TEST(ScatterMarkerDefaults, ExplicitSizeIsKept)
{
    ScatterSeries s;
    s.size(3.0f);
    EXPECT_FLOAT_EQ(s.size(), 3.0f);
    ScatterSeries3D s3;
    s3.size(11.0f);
    EXPECT_FLOAT_EQ(s3.size(), 11.0f);
}

TEST(ScatterMarkerDefaults, DefaultMarkerStyleIsNone)
{
    ScatterSeries s;
    EXPECT_EQ(s.marker_style(), MarkerStyle::None);
    ScatterSeries3D s3;
    EXPECT_EQ(s3.marker_style(), MarkerStyle::None);
}

TEST(ScatterMarkerDefaults, NoneResolvesToFilledDotWithLightEdgeOnDarkCanvas)
{
    auto p = resolve_scatter_marker(MarkerStyle::None, false, DARK_CANVAS);
    EXPECT_EQ(p.marker_type, DEFAULT_SCATTER_MARKER_CODE);
    EXPECT_FLOAT_EQ(p.edge_mix, DEFAULT_SCATTER_EDGE_MIX);
    EXPECT_GT(p.edge_mix, 0.0f);
}

TEST(ScatterMarkerDefaults, NoneResolvesToDarkEdgeOnLightCanvas)
{
    auto p = resolve_scatter_marker(MarkerStyle::None, false, LIGHT_CANVAS);
    EXPECT_EQ(p.marker_type, DEFAULT_SCATTER_MARKER_CODE);
    EXPECT_FLOAT_EQ(p.edge_mix, -DEFAULT_SCATTER_EDGE_MIX);
}

TEST(ScatterMarkerDefaults, ColormapGetsStrongerContrastEdge)
{
    auto dark = resolve_scatter_marker(MarkerStyle::None, true, DARK_CANVAS);
    EXPECT_EQ(dark.marker_type, DEFAULT_SCATTER_MARKER_CODE);
    EXPECT_FLOAT_EQ(dark.edge_mix, DEFAULT_SCATTER_COLORMAP_EDGE_MIX);
    EXPECT_GT(DEFAULT_SCATTER_COLORMAP_EDGE_MIX, DEFAULT_SCATTER_EDGE_MIX);

    auto light = resolve_scatter_marker(MarkerStyle::None, true, LIGHT_CANVAS);
    EXPECT_FLOAT_EQ(light.edge_mix, -DEFAULT_SCATTER_COLORMAP_EDGE_MIX);
}

TEST(ScatterMarkerDefaults, DefaultCodeIsNotAPublicMarkerStyle)
{
    EXPECT_GT(DEFAULT_SCATTER_MARKER_CODE, static_cast<uint32_t>(MarkerStyle::FilledTriangleUp));
}

TEST(ScatterMarkerDefaults, ExplicitStylesAreUnchanged)
{
    const MarkerStyle styles[] = {MarkerStyle::Point,
                                  MarkerStyle::Circle,
                                  MarkerStyle::Square,
                                  MarkerStyle::FilledCircle,
                                  MarkerStyle::Cross};
    for (MarkerStyle ms : styles)
    {
        for (bool cm : {false, true})
        {
            auto p = resolve_scatter_marker(ms, cm, DARK_CANVAS);
            EXPECT_EQ(p.marker_type, static_cast<uint32_t>(ms));
            EXPECT_FLOAT_EQ(p.edge_mix, 0.0f);
        }
    }
}

TEST(ScatterMarkerDefaults, FormatStringOKeepsHollowCircle)
{
    ScatterSeries s;
    s.format("o");
    EXPECT_EQ(s.marker_style(), MarkerStyle::Circle);
    auto p = resolve_scatter_marker(s.marker_style(), false, DARK_CANVAS);
    EXPECT_EQ(p.marker_type, static_cast<uint32_t>(MarkerStyle::Circle));
    EXPECT_FLOAT_EQ(s.size(), 6.0f);
}

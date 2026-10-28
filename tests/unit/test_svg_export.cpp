#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_shapes3d.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <random>

#include <spectra/custom_series.hpp>
#include <spectra/logger.hpp>

#include "core/label_layout.hpp"
#include "io/font_subset.hpp"
#include "io/svg_export_internal.hpp"
#include "io/svg_label_metrics.hpp"
#include "io/text_metrics.hpp"
#include "render/text_fit.hpp"

// The test's own (internal-linkage) stb_truetype, to parse the embedded font subset.
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace spectra
{
namespace
{

int count_of(const std::string& haystack, const std::string& needle)
{
    int    n   = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++n;
        pos += needle.size();
    }
    return n;
}

std::string hex_of(const Color& c)
{
    char buf[8];
    std::snprintf(buf,
                  sizeof(buf),
                  "#%02x%02x%02x",
                  static_cast<int>(std::lround(c.r * 255.0f)),
                  static_cast<int>(std::lround(c.g * 255.0f)),
                  static_cast<int>(std::lround(c.b * 255.0f)));
    return buf;
}

// Helper: create a simple figure with one line series
Figure make_line_figure()
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);

    std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
    ax.line(x, y).label("test-line").color(rgb(1.0f, 0.0f, 0.0f));
    ax.title("Test Title");
    ax.xlabel("X Label");
    ax.ylabel("Y Label");

    fig.compute_layout();
    return fig;
}

// Helper: create a figure with scatter series
Figure make_scatter_figure()
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax = fig.subplot(1, 1, 1);

    std::vector<float> x = {1.0f, 2.0f, 3.0f};
    std::vector<float> y = {1.0f, 4.0f, 2.0f};
    ax.scatter(x, y).label("points").color(rgb(0.0f, 0.0f, 1.0f));

    fig.compute_layout();
    return fig;
}

TEST(SvgExport, ToStringProducesValidSvg)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Must start with XML declaration and SVG root
    EXPECT_NE(svg.find("<?xml"), std::string::npos);
    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("</svg>"), std::string::npos);
}

TEST(SvgExport, ContainsViewBoxDimensions)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("width=\"800\""), std::string::npos);
    EXPECT_NE(svg.find("height=\"600\""), std::string::npos);
    EXPECT_NE(svg.find("viewBox=\"0 0 800 600\""), std::string::npos);
}

TEST(SvgExport, ContainsPathForLineSeries)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<path class=\"line\""), std::string::npos);
    // Red series colour
    EXPECT_NE(svg.find("stroke=\"#ff0000\""), std::string::npos);
}

TEST(SvgExport, ContainsCirclesForScatterSeries)
{
    auto        fig = make_scatter_figure();
    std::string svg = SvgExporter::to_string(fig);

    // One shared marker <symbol> for the points (the renderer's default dot
    // with its edge tint) plus the legend's plain disc, instanced with <use>.
    EXPECT_EQ(count_of(svg, "<symbol"), 2);
    EXPECT_GE(count_of(svg, "<use "), 3);
}

TEST(SvgExport, ContainsTitleText)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("Test Title"), std::string::npos);
}

TEST(SvgExport, ContainsAxisLabels)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("X Label"), std::string::npos);
    EXPECT_NE(svg.find("Y Label"), std::string::npos);
}

TEST(SvgExport, ContainsLegendEntry)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("test-line"), std::string::npos);
}

TEST(SvgExport, SqrtAxisPlacesMarkersAtSquareRootPositions)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.scatter(std::vector<float>{0.0f, 25.0f, 100.0f}, std::vector<float>{0.0f, 4.0f, 16.0f});
    ax.xscale(ScaleType::Sqrt);
    ax.yscale(ScaleType::Sqrt);
    ax.xlim(0.0, 100.0);
    ax.ylim(0.0, 16.0);
    fig.compute_layout();
    const Rect vp = ax.viewport();

    const std::string                      svg = SvgExporter::to_string(fig);
    std::vector<std::pair<double, double>> pts;
    const std::string                      key = "        <use xlink:href=\"#mk";
    for (size_t pos = svg.find(key); pos != std::string::npos; pos = svg.find(key, pos + 1))
    {
        const size_t xs = svg.find(" x=\"", pos) + 4;
        const size_t ys = svg.find(" y=\"", pos) + 4;
        pts.emplace_back(std::stod(svg.substr(xs)), std::stod(svg.substr(ys)));
    }
    ASSERT_EQ(pts.size(), 3u);
    // x = 25 is half way along sqrt [0, 10]; y = 4 is half way up sqrt [0, 4].
    EXPECT_NEAR(pts[0].first, vp.x, 0.05);
    EXPECT_NEAR(pts[1].first, vp.x + 0.5 * vp.w, 0.05);
    EXPECT_NEAR(pts[2].first, vp.x + vp.w, 0.05);
    EXPECT_NEAR(pts[0].second, vp.y + vp.h, 0.05);
    EXPECT_NEAR(pts[1].second, vp.y + 0.5 * vp.h, 0.05);
    EXPECT_NEAR(pts[2].second, vp.y, 0.05);
}

TEST(SvgExport, ContainsGridLines)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Grid group should exist
    EXPECT_NE(svg.find("class=\"grid\""), std::string::npos);
}

TEST(SvgExport, ContainsBorderRect)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    // Border is drawn in the style's axis colour
    EXPECT_NE(svg.find("class=\"axis\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"" + hex_of(SvgStyle{}.axis_line) + "\""), std::string::npos);
}

TEST(SvgExport, ContainsTickLabels)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("class=\"tick-labels\""), std::string::npos);
}

TEST(SvgExport, ContainsClipPath)
{
    auto        fig = make_line_figure();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<clipPath"), std::string::npos);
    EXPECT_NE(svg.find("clip-path="), std::string::npos);
}

TEST(SvgExport, WriteToFile)
{
    auto fig = make_line_figure();

    // Use cross-platform temporary file
    std::filesystem::path temp_dir    = std::filesystem::temp_directory_path();
    std::string           random_name = "plot_" + std::to_string(std::random_device{}());
    std::string           path        = random_name + ".svg";

    bool ok = SvgExporter::write_svg(path, fig);
    EXPECT_TRUE(ok);

    // Verify file exists and has content
    std::ifstream file(path);
    EXPECT_TRUE(file.good());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_GT(content.size(), 100u);
    EXPECT_NE(content.find("<svg"), std::string::npos);

    // Clean up
    std::remove(path.c_str());
}

TEST(SvgExport, MultiSubplotProducesMultipleAxesGroups)
{
    Figure fig({.width = 1200, .height = 600});
    auto&  ax1 = fig.subplot(1, 2, 1);
    auto&  ax2 = fig.subplot(1, 2, 2);

    std::vector<float> x  = {0.0f, 1.0f, 2.0f};
    std::vector<float> y1 = {0.0f, 1.0f, 0.5f};
    std::vector<float> y2 = {1.0f, 0.5f, 1.5f};

    ax1.line(x, y1).label("series1");
    ax2.line(x, y2).label("series2");
    ax1.title("Plot 1");
    ax2.title("Plot 2");

    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    // Should have two axes groups
    size_t pos        = 0;
    int    axes_count = 0;
    while ((pos = svg.find("class=\"axes\"", pos)) != std::string::npos)
    {
        ++axes_count;
        ++pos;
    }
    EXPECT_EQ(axes_count, 2);

    // Both titles present
    EXPECT_NE(svg.find("Plot 1"), std::string::npos);
    EXPECT_NE(svg.find("Plot 2"), std::string::npos);
}

TEST(SvgExport, EmptyFigureProducesMinimalSvg)
{
    Figure fig({.width = 400, .height = 300});
    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("</svg>"), std::string::npos);
    // No axes groups since no subplots created
    EXPECT_EQ(svg.find("class=\"axes\""), std::string::npos);
}

TEST(SvgExport, XmlEscapesSpecialCharacters)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.title("A < B & C > D");

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    ax.line(x, y);

    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    // Special chars should be escaped
    EXPECT_NE(svg.find("A &lt; B &amp; C &gt; D"), std::string::npos);
    // Raw special chars should NOT appear in text content
    // (they do appear in SVG markup itself, so just check the title is escaped)
}

TEST(SvgExport, GridDisabledOmitsGridGroup)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.grid(false);

    std::vector<float> x = {0.0f, 1.0f};
    std::vector<float> y = {0.0f, 1.0f};
    ax.line(x, y);

    fig.compute_layout();

    std::string svg = SvgExporter::to_string(fig);

    // Grid group should NOT exist
    EXPECT_EQ(svg.find("class=\"grid\""), std::string::npos);
}

// ─── Theme, styles, markers, stat/shape series, 3D (P0-5) ───────────────────

TEST(SvgExport, BackgroundAndChromeFollowStyle)
{
    auto     fig = make_line_figure();
    SvgStyle st;
    st.background   = Color{0.1f, 0.2f, 0.3f, 1.0f};
    st.grid_major   = Color{0.0f, 1.0f, 0.0f, 0.5f};
    st.tick_label   = Color{1.0f, 0.0f, 1.0f, 1.0f};
    std::string svg = SvgExporter::to_string(fig, st);

    EXPECT_NE(svg.find("class=\"background\" width=\"100%\" height=\"100%\" fill=\"#1a334d\""),
              std::string::npos);
    EXPECT_EQ(svg.find("fill=\"white\""), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"#00ff00\" stroke-width=\"1\" stroke-opacity=\"0.5\""),
              std::string::npos);
    EXPECT_NE(svg.find("fill=\"#ff00ff\""), std::string::npos);
}

TEST(SvgExport, BlackSeriesFollowsForegroundOnDarkBackground)
{
    Figure             fig({.width = 400, .height = 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    ax.plot(x, y, "-").color(Color{0.0f, 0.0f, 0.0f, 1.0f});
    fig.compute_layout();
    SvgStyle st;
    st.background   = Color{0.05f, 0.05f, 0.08f, 1.0f};
    st.text_primary = Color{0.9f, 0.9f, 0.9f, 1.0f};
    std::string svg = SvgExporter::to_string(fig, st);
    EXPECT_NE(svg.find("<path class=\"line\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"#e6e6e6\""), std::string::npos);
}

TEST(SvgExport, FormatLetterUsesShadeForBackground)
{
    Figure             fig({.width = 400, .height = 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    ax.plot(x, y, "k-");
    fig.compute_layout();
    SvgStyle dark;
    dark.background = Color{0.05f, 0.05f, 0.08f, 1.0f};
    EXPECT_NE(SvgExporter::to_string(fig, dark).find("stroke=\"#d6d6d6\""), std::string::npos);
    SvgStyle light;
    light.background = Color{1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_NE(SvgExporter::to_string(fig, light).find("stroke=\"#000000\""), std::string::npos);
}

TEST(SvgExport, LineStylesEmitRendererDashPatterns)
{
    Figure             fig({.width = 800, .height = 600});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f}, y = {0.0f, 1.0f, 0.0f};
    ax.plot(x, y, "--").width(2.0f);
    ax.plot(x, y, ":").width(2.0f);
    ax.plot(x, y, "-.").width(2.0f);
    ax.plot(x, y, "-").width(2.0f);
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);

    // get_dash_pattern() at width 2: dashed 8w/4w, dotted 2w/4w, dash-dot 8w/3.5w/2w/3.5w
    EXPECT_NE(svg.find("stroke-dasharray=\"16,8\""), std::string::npos);
    EXPECT_NE(svg.find("stroke-dasharray=\"4,8\""), std::string::npos);
    EXPECT_NE(svg.find("stroke-dasharray=\"16,7,4,7\""), std::string::npos);
    // Solid line has no dash array: 3 dashed series in the plot (+3 legend swatches would
    // need labels, there are none)
    EXPECT_EQ(count_of(svg, "stroke-dasharray"), 3);
}

TEST(SvgExport, ReferenceLinesAreClippedToSaneCoordinates)
{
    Figure             fig({.width = 640, .height = 480});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    ax.line(x, y);
    ax.hline(0.5, "k--");
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);
    // hline spans +-1e12 in data space; no coordinate may explode.
    EXPECT_EQ(svg.find("00000000"), std::string::npos);
    EXPECT_NE(svg.find("stroke-dasharray"), std::string::npos);
}

TEST(SvgExport, EveryMarkerStyleBecomesOneSymbol)
{
    Figure             fig({.width = 800, .height = 600});
    auto&              ax     = fig.subplot(1, 1, 1);
    std::vector<float> x      = {0.0f, 1.0f, 2.0f};
    int                k      = 0;
    int                styles = 0;
    for (auto ms : ALL_MARKER_STYLES)
    {
        if (ms == MarkerStyle::None)
        {
            continue;
        }
        ++styles;
        std::vector<float> y = {static_cast<float>(k),
                                static_cast<float>(k) + 0.5f,
                                static_cast<float>(k)};
        ax.line(x, y).marker_style(ms).marker_size(8.0f);
        ++k;
    }
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_EQ(count_of(svg, "<symbol"), styles);
    EXPECT_EQ(count_of(svg, "<use xlink:href=\"#mk"), styles * 3);
    EXPECT_NE(svg.find("xmlns:xlink=\"http://www.w3.org/1999/xlink\""), std::string::npos);
}

TEST(SvgExport, ColormappedScatterHasPerPointColours)
{
    Figure             fig({.width = 640, .height = 480});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f, 2.0f}, y = {0.0f, 1.0f, 2.0f}, v = {0.0f, 0.5f, 1.0f};
    ax.scatter(x, y).color_values(v).colormap(ColormapType::Grayscale).colormap_range(0.0f, 1.0f);
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find("color=\"#000000\"/>"), std::string::npos);
    EXPECT_NE(svg.find("color=\"#808080\"/>"), std::string::npos);
    EXPECT_NE(svg.find("color=\"#ffffff\"/>"), std::string::npos);
}

TEST(SvgExport, BarsEmitFillAndOutline)
{
    Figure             fig({.width = 800, .height = 600});
    auto&              v = fig.subplot(1, 2, 1);
    auto&              h = fig.subplot(1, 2, 2);
    std::vector<float> p = {1.0f, 2.0f, 3.0f}, hh = {3.0f, 1.0f, 2.0f};
    v.bar(p, hh).label("vertical");
    h.bar(p, hh).orientation(BarOrientation::Horizontal).label("horizontal");
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);
    // Gradient fills (one per bar) + one outline path per series.
    EXPECT_GE(count_of(svg, "<path class=\"bar\" d=\"M"), 2 + 2 * 3);
    EXPECT_GE(count_of(svg, "<linearGradient"), 6);
    EXPECT_NE(svg.find("fill=\"url(#fg"), std::string::npos);
    // Legend swatch is a filled rectangle for bars.
    EXPECT_EQ(count_of(svg, "class=\"swatch-fill\""), 2);
}

TEST(SvgExport, StatisticalSeriesAreNotEmpty)
{
    Figure             fig({.width = 1200, .height = 800});
    std::vector<float> vals(200);
    for (size_t i = 0; i < vals.size(); ++i)
        vals[i] = std::sin(static_cast<float>(i) * 0.37f) * 10.0f + 50.0f;
    vals[0] = 120.0f;   // outlier
    fig.subplot(2, 3, 1).histogram(vals, 20);
    fig.subplot(2, 3, 2).box_plot().add_box(1.0f, vals);
    fig.subplot(2, 3, 3).violin().add_violin(1.0f, vals);
    std::vector<float> x = {0.0f, 1.0f, 2.0f}, lo = {0.0f, 0.5f, 0.0f}, hi = {1.0f, 1.5f, 1.0f};
    fig.subplot(2, 3, 4).band(x, lo, hi);
    fig.subplot(2, 3, 5).stem(x, hi);
    fig.subplot(2, 3, 6)
        .shapes()
        .rect(0, 0, 1, 1)
        .circle(2, 2, 1)
        .ellipse(4, 2, 1, 0.5f)
        .line(0, 0, 4, 4)
        .arrow(0, 4, 2, 3)
        .ring(2, 5, 1, 0.5f);
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<path class=\"histogram\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("<path class=\"box\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("<path class=\"violin\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("<path class=\"band\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("<path class=\"stem\" d=\"M"), std::string::npos);
    EXPECT_GE(count_of(svg, "<path class=\"shape\" d=\"M"), 2);
    // Box outliers and stem heads are markers.
    EXPECT_GE(count_of(svg, "<use xlink:href"), 1 + 3);
}

TEST(SvgExport, ThreeDAxesAreProjectedNotBlank)
{
    Figure             fig({.width = 800, .height = 600});
    auto&              ax = fig.subplot3d(1, 1, 1);
    std::vector<float> x = {-1.0f, 0.0f, 1.0f}, y = {-1.0f, 0.5f, 1.0f}, z = {0.0f, 1.0f, 0.5f};
    ax.scatter3d(x, y, z).label("pts");
    ax.line3d(x, y, z);
    std::vector<float> g  = {-1.0f, 0.0f, 1.0f};
    std::vector<float> zz = {0.0f, 0.2f, 0.0f, 0.2f, 1.0f, 0.2f, 0.0f, 0.2f, 0.0f};
    ax.surface(g, g, zz).colormap(ColormapType::Viridis);
    ax.title("3D");
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("class=\"axes axes3d\""), std::string::npos);
    EXPECT_NE(svg.find("<g class=\"box\""), std::string::npos);
    EXPECT_NE(svg.find("<g class=\"line3d\""), std::string::npos);
    EXPECT_NE(svg.find("<g class=\"scatter3d\""), std::string::npos);
    EXPECT_NE(svg.find("<g class=\"surface\""), std::string::npos);
    // 2x2 grid cells -> 8 surface triangles, 3 scatter points
    EXPECT_GE(count_of(svg, "Z\" fill=\"#"), 8);
    EXPECT_GE(count_of(svg, "<use xlink:href=\"#mk"), 3);
    EXPECT_NE(svg.find(">3D</text>"), std::string::npos);
    EXPECT_NE(svg.find("class=\"axis-arrow\""), std::string::npos);
}

// Issue #105: lighting_enabled(false) exports flat base colours, like the raster path.
TEST(SvgExport, ThreeDLightingDisabledIsFlat)
{
    auto make_svg = [](bool lit)
    {
        Figure             fig({.width = 640, .height = 480});
        auto&              ax = fig.subplot3d(1, 1, 1);
        std::vector<float> g  = {-1.0f, 0.0f, 1.0f};
        std::vector<float> zz = {0.0f, 0.4f, 0.0f, 0.4f, 1.0f, 0.4f, 0.0f, 0.4f, 0.0f};
        ax.surface(g, g, zz).color(rgb(1.0f, 0.0f, 0.0f));
        ax.lighting_enabled(lit);
        fig.compute_layout();
        return SvgExporter::to_string(fig);
    };
    const std::string flat = make_svg(false);
    EXPECT_EQ(count_of(flat, "Z\" fill=\"#ff0000\""), 8);
    const std::string lit = make_svg(true);
    EXPECT_LT(count_of(lit, "Z\" fill=\"#ff0000\""), 8);
}

TEST(SvgExport, UnicodeTextIsKeptAndControlCharsDropped)
{
    Figure             fig({.width = 640, .height = 480});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    ax.line(x, y).label("\xce\xbc \xc2\xb1 \xcf\x83 \xe6\xb8\xa9\xe5\xba\xa6 <&>\x01");
    ax.title("\xce\x94t [\xc2\xb5s] \"q\"");
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find(">\xce\xbc \xc2\xb1 \xcf\x83 \xe6\xb8\xa9\xe5\xba\xa6 &lt;&amp;&gt;</text>"),
              std::string::npos);
    EXPECT_NE(svg.find(">\xce\x94t [\xc2\xb5s] &quot;q&quot;</text>"), std::string::npos);
    EXPECT_EQ(svg.find('\x01'), std::string::npos);
}

TEST(SvgExport, LegendWidthFitsLongestLabel)
{
    Figure             fig({.width = 1000, .height = 600});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    ax.line(x, y).label("a");
    ax.line(x, y).label("a much much longer legend label for this series");
    fig.compute_layout();
    std::string svg = SvgExporter::to_string(fig);

    const size_t g = svg.find("<g class=\"legend\">");
    ASSERT_NE(g, std::string::npos);
    const size_t w = svg.find("width=\"", g);
    ASSERT_NE(w, std::string::npos);
    const float width = std::stof(svg.substr(w + 7));
    EXPECT_GT(width, 250.0f);   // the old fixed 120 px box cut long labels off
    EXPECT_LT(width, 450.0f);
}

// Audit S04: a narrow subplot's legend stays inside its plot, long labels are
// ellipsized on UTF-8 boundaries, and entries that cannot fit are summarised
// by an explicit "+N more" cell (same policy as the GPU/PNG legend).
namespace
{
struct SvgRect
{
    float x = 0, y = 0, w = 0, h = 0;
};

float attr(const std::string& s, size_t from, const char* name)
{
    const std::string key = std::string(" ") + name + "=\"";
    const size_t      p   = s.find(key, from);
    return p == std::string::npos ? NAN : std::stof(s.substr(p + key.size()));
}

// The plot area: the first clipPath rect.
SvgRect plot_rect(const std::string& svg)
{
    const size_t p = svg.find("<clipPath");
    return {attr(svg, p, "x"), attr(svg, p, "y"), attr(svg, p, "width"), attr(svg, p, "height")};
}
}   // namespace

TEST(SvgExport, FortyEntryLegendStaysInsideNarrowPlot)
{
    Figure             fig({.width = 320, .height = 260});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    for (int i = 0; i < 40; ++i)
        ax.line(x, y).label("Series " + std::to_string(i + 1));
    fig.legend().position = LegendPosition::TopLeft;
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    const SvgRect plot = plot_rect(svg);
    const size_t  g    = svg.find("<g class=\"legend\">");
    ASSERT_NE(g, std::string::npos);
    const size_t  end = svg.find("</g>", g);
    const SvgRect box{attr(svg, g, "x"),
                      attr(svg, g, "y"),
                      attr(svg, g, "width"),
                      attr(svg, g, "height")};
    EXPECT_GE(box.x, plot.x);
    EXPECT_GE(box.y, plot.y);
    EXPECT_LE(box.x + box.w, plot.x + plot.w + 0.01f);
    EXPECT_LE(box.y + box.h, plot.y + plot.h + 0.01f);

    // Every label is inside the box, and shown + hidden == 40.
    int    shown = 0;
    int    more  = 0;
    size_t p     = g;
    while ((p = svg.find("<text ", p + 1)) != std::string::npos && p < end)
    {
        const float ty = attr(svg, p, "y");
        EXPECT_GE(ty, box.y);
        EXPECT_LE(ty, box.y + box.h);
        const size_t gt  = svg.find('>', p);
        const auto   txt = svg.substr(gt + 1, svg.find('<', gt) - gt - 1);
        if (txt.rfind("Series ", 0) == 0)
            ++shown;
        else if (txt.front() == '+')
            more = std::stoi(txt.substr(1));
    }
    EXPECT_GT(more, 0) << "overflow must be reported, not dropped";
    EXPECT_EQ(shown + more, 40);

    // The "+N more" cell names every hidden entry (uncapped) in its <title>.
    const size_t ov = svg.find("<g class=\"legend-overflow\"><title>", g);
    ASSERT_NE(ov, std::string::npos);
    const size_t      ts    = svg.find("<title>", ov) + 7;
    const std::string title = svg.substr(ts, svg.find("</title>", ts) - ts);
    EXPECT_EQ(title.substr(0, title.find('\n')), "Series " + std::to_string(shown + 1));
    EXPECT_EQ(std::count(title.begin(), title.end(), '\n'), more - 1) << title;
    EXPECT_NE(title.find("Series 40"), std::string::npos) << title;
    EXPECT_EQ(title.find(" more"), std::string::npos) << title;
}

// A crowded Best legend (50 entries, small panel) moves to the strip right of
// the plot instead of covering it; summarised entries are all listed in the
// "+N more" cell's <title>.
TEST(SvgExport, CrowdedBestLegendIsPlacedOutsideThePlot)
{
    Figure             fig({.width = 900, .height = 600});
    auto&              ax = fig.subplot(2, 2, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    for (int i = 0; i < 50; ++i)
        ax.line(x, y).label("Series " + std::to_string(i + 1));
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    const SvgRect plot = plot_rect(svg);
    const size_t  g    = svg.find("<g class=\"legend\">");
    ASSERT_NE(g, std::string::npos);
    const float box_x = attr(svg, g, "x");
    EXPECT_GE(box_x, plot.x + plot.w) << "legend must not cover the plot";
    EXPECT_LE(box_x + attr(svg, g, "width"), 900.0f);

    const size_t ov = svg.find("<g class=\"legend-overflow\"><title>", g);
    ASSERT_NE(ov, std::string::npos);
    const size_t      ts    = svg.find("<title>", ov) + 7;
    const std::string title = svg.substr(ts, svg.find("</title>", ts) - ts);
    EXPECT_NE(title.find("Series 50"), std::string::npos) << title;
    EXPECT_EQ(title.find(" more"), std::string::npos) << title;
}

TEST(SvgExport, LongUtf8LegendLabelIsEllipsizedInsidePlot)
{
    Figure             fig({.width = 300, .height = 240});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
    std::string        label = "Temperature ";
    for (int i = 0; i < 30; ++i)
        label += "\xce\xbc\xe6\xb8\xa9";   // "μ温"
    ax.line(x, y).label(label);
    fig.legend().position = LegendPosition::TopRight;
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    const SvgRect plot = plot_rect(svg);
    const size_t  g    = svg.find("<g class=\"legend\">");
    ASSERT_NE(g, std::string::npos);
    const float bx = attr(svg, g, "x");
    const float bw = attr(svg, g, "width");
    EXPECT_GE(bx, plot.x);
    EXPECT_LE(bx + bw, plot.x + plot.w + 0.01f);
    // Truncated label ends with an ellipsis, keeps whole codepoints, and the
    // full text is kept as a <title> tooltip.
    EXPECT_NE(svg.find("<title>" + label + "</title>", g), std::string::npos);
    const size_t t   = svg.find("<text ", g);
    const size_t gt  = svg.find('>', t);
    const auto   txt = svg.substr(gt + 1, svg.find('<', gt) - gt - 1);
    ASSERT_GE(txt.size(), 3u);
    EXPECT_EQ(txt.substr(txt.size() - 3), "\xE2\x80\xA6");
    EXPECT_LT(txt.size(), label.size());
    const std::string body = txt.substr(0, txt.size() - 3);
    EXPECT_EQ(label.compare(0, body.size(), body), 0);
    const auto last = static_cast<unsigned char>(label[body.size()]);
    EXPECT_NE(last & 0xC0u, 0x80u) << "cut inside a UTF-8 sequence";
}

// Audit S06: long titles / axis labels are fitted to their subplot in SVG exactly as in
// the GPU/raster path (shared rules in core/label_layout), with or without tick offsets.
namespace
{
struct SvgLabel
{
    float       x = 0, y = 0, font_px = 0;
    std::string anchor = "start";
    std::string content;
    bool        rotated = false;
};

std::vector<SvgLabel> parse_labels(const std::string& svg)
{
    std::vector<SvgLabel> out;
    size_t                pos = 0;
    while ((pos = svg.find("<text ", pos)) != std::string::npos)
    {
        const size_t gt  = svg.find('>', pos);
        const size_t end = svg.find("</text>", gt);
        if (gt == std::string::npos || end == std::string::npos)
            break;
        const std::string tag = svg.substr(pos, gt - pos);
        SvgLabel          t;
        t.x            = attr(tag, 0, "x");
        t.y            = attr(tag, 0, "y");
        t.font_px      = attr(tag, 0, "font-size");
        const size_t a = tag.find(" text-anchor=\"");
        if (a != std::string::npos)
            t.anchor = tag.substr(a + 14, tag.find('"', a + 14) - (a + 14));
        const size_t r = tag.find("rotate(");
        if (r != std::string::npos)
        {
            // "rotate(<angle> <cx> <cy>)": keep the rotation centre.
            t.rotated       = true;
            const size_t cx = tag.find(' ', r) + 1;
            const size_t cy = tag.find(' ', cx) + 1;
            t.x             = std::stof(tag.substr(cx));
            t.y             = std::stof(tag.substr(cy));
        }
        t.content = svg.substr(gt + 1, end - gt - 1);
        out.push_back(t);
        pos = end;
    }
    return out;
}

LabelFont font_of(float px)
{
    if (std::abs(px - svg_plot_label_font_px(LabelFont::Title)) < 0.01f)
        return LabelFont::Title;
    if (std::abs(px - svg_plot_label_font_px(LabelFont::Label)) < 0.01f)
        return LabelFont::Label;
    return LabelFont::Tick;
}

float svg_width(const SvgLabel& t)
{
    return svg_plot_label_width(t.content, font_of(t.font_px));
}

const SvgLabel* find_prefixed(const std::vector<SvgLabel>& labels, const std::string& prefix)
{
    for (const auto& t : labels)
        if (t.content.rfind(prefix, 0) == 0)
            return &t;
    return nullptr;
}

bool ends_with_ellipsis(const std::string& s)
{
    return s.size() >= LABEL_ELLIPSIS.size()
           && s.compare(s.size() - LABEL_ELLIPSIS.size(), LABEL_ELLIPSIS.size(), LABEL_ELLIPSIS)
                  == 0;
}

std::string long_text(const std::string& head)
{
    return head + " with a very long description that cannot possibly fit inside one subplot";
}
}   // namespace

TEST(SvgExport, LongLabelsStayInsideTheirSubplot)
{
    const uint32_t     fig_w = 800, fig_h = 600;
    Figure             fig({.width = fig_w, .height = fig_h});
    std::vector<float> x = {0.0f, 10.0f}, y = {0.0f, 1.0f};
    Axes*              axes[4];
    for (int i = 0; i < 4; ++i)
    {
        auto& ax = fig.subplot(2, 2, i + 1);
        ax.line(x, y);
        ax.xlim(0.0, 10.0);
        ax.ylim(0.0, 1.0);
        const std::string tag = "P" + std::to_string(i);
        ax.title(long_text(tag + " title"));
        ax.xlabel(long_text(tag + " xlabel"));
        ax.ylabel(long_text(tag + " ylabel"));
        axes[i] = &ax;
    }
    fig.compute_layout();
    const std::string svg    = SvgExporter::to_string(fig);
    const auto        labels = parse_labels(svg);

    for (int i = 0; i < 4; ++i)
    {
        const Axes& ax = *axes[i];
        ASSERT_TRUE(ax.compute_x_ticks().offset_label.empty());
        ASSERT_TRUE(ax.compute_y_ticks().offset_label.empty());
        const auto&       vp  = ax.viewport();
        const std::string tag = "P" + std::to_string(i);
        SCOPED_TRACE(tag);

        for (const char* kind : {" title", " xlabel"})
        {
            const SvgLabel* t = find_prefixed(labels, tag + kind);
            ASSERT_NE(t, nullptr) << kind;
            ASSERT_EQ(t->anchor, "middle");
            EXPECT_TRUE(ends_with_ellipsis(t->content)) << t->content;
            const float w = svg_width(*t);
            EXPECT_GE(t->x - w * 0.5f, vp.x - 0.5f) << kind;
            EXPECT_LE(t->x + w * 0.5f, vp.x + vp.w + 0.5f) << kind;
            EXPECT_GE(t->x - w * 0.5f, 0.0f);
            EXPECT_LE(t->x + w * 0.5f, static_cast<float>(fig_w));
            // The full text stays available as a tooltip.
            EXPECT_NE(svg.find("<title>" + long_text(tag + kind) + "</title>"), std::string::npos);
        }
        // Long titles step down to the label size before being ellipsized (GPU rule).
        EXPECT_FLOAT_EQ(find_prefixed(labels, tag + " title")->font_px,
                        svg_plot_label_font_px(LabelFont::Label));

        const SvgLabel* yl = find_prefixed(labels, tag + " ylabel");
        ASSERT_NE(yl, nullptr);
        ASSERT_TRUE(yl->rotated);
        EXPECT_TRUE(ends_with_ellipsis(yl->content)) << yl->content;
        const float h = svg_width(*yl);   // runs vertically
        EXPECT_GE(yl->y - h * 0.5f, vp.y - 0.5f);
        EXPECT_LE(yl->y + h * 0.5f, vp.y + vp.h + 0.5f);
        EXPECT_GE(yl->y - h * 0.5f, 0.0f);
        EXPECT_LE(yl->y + h * 0.5f, static_cast<float>(fig_h));
    }
}

TEST(SvgExport, ShortLabelsAreNotShortened)
{
    Figure             fig({.width = 800, .height = 600});
    std::vector<float> x = {0.0f, 10.0f}, y = {0.0f, 1.0f};
    for (int i = 0; i < 4; ++i)
    {
        auto& ax = fig.subplot(2, 2, i + 1);
        ax.line(x, y);
        ax.title("Response");
        ax.xlabel("Time (s)");
        ax.ylabel("Amplitude");
    }
    fig.compute_layout();
    const std::string svg    = SvgExporter::to_string(fig);
    const auto        labels = parse_labels(svg);
    int               title = 0, xl = 0, yl = 0;
    for (const auto& t : labels)
    {
        if (t.content == "Response")
        {
            ++title;
            EXPECT_FLOAT_EQ(t.font_px, svg_plot_label_font_px(LabelFont::Title));
        }
        xl += t.content == "Time (s)";
        yl += t.content == "Amplitude";
    }
    EXPECT_EQ(title, 4);
    EXPECT_EQ(xl, 4);
    EXPECT_EQ(yl, 4);
    EXPECT_EQ(svg.find("\xE2\x80\xA6"), std::string::npos);
    EXPECT_EQ(svg.find("<title>"), std::string::npos);
}

TEST(SvgExport, Long3DTitleStaysInsideItsSubplot)
{
    Figure  fig({.width = 640, .height = 320});
    Axes3D* axes[2];
    for (int i = 0; i < 2; ++i)
    {
        auto& ax             = fig.subplot3d(1, 2, i + 1);
        axes[i]              = &ax;
        std::vector<float> v = {0.0f, 1.0f};
        ax.line3d(v, v, v);
        ax.title(long_text("P" + std::to_string(i) + " 3d title"));
    }
    fig.compute_layout();
    const auto labels = parse_labels(SvgExporter::to_string(fig));
    for (int i = 0; i < 2; ++i)
    {
        const auto&     vp = axes[i]->viewport();
        const SvgLabel* t  = find_prefixed(labels, "P" + std::to_string(i) + " 3d title");
        ASSERT_NE(t, nullptr);
        EXPECT_TRUE(ends_with_ellipsis(t->content)) << t->content;
        const float w = svg_width(*t);
        EXPECT_GE(t->x - w * 0.5f, vp.x - 0.5f);
        EXPECT_LE(t->x + w * 0.5f, vp.x + vp.w + 0.5f);
    }
}

// The SVG exporter and the GPU/raster path (render_geometry.cpp: fit_text_to_width /
// to_fitted_text over the same core rules) produce the same fitted strings and sizes for the
// same metrics, with and without tick offsets.
TEST(SvgExport, FittedLabelsMatchRasterHelper)
{
    // Raster-side measure in the renderer's own types, fed with the SVG metrics.
    const TextWidthFn raster_measure = [](const std::string& t, FontSize fs)
    { return svg_plot_label_width(t, static_cast<LabelFont>(fs)); };
    const LabelWidthFn raster_label_measure = to_label_width_fn(raster_measure);

    struct Case
    {
        double xlo, xhi, ylo, yhi;
    };
    const Case cases[] = {
        {0.0, 10.0, 0.0, 1.0},                                      // no offsets
        {1720000000.0, 1720000001.2, 1.0e7 - 0.02, 1.0e7 + 0.02},   // offsets
        {0.0, 3.0e-7, 0.0, 4.0e9},                                  // multipliers
    };
    const std::string titles[] = {"Short", long_text("A title"), "Medium length plot title"};
    for (uint32_t fig_w : {500u, 900u})
        for (const auto& c : cases)
            for (const auto& text : titles)
            {
                SCOPED_TRACE("w=" + std::to_string(fig_w) + " x=" + std::to_string(c.xhi) + " "
                             + text);
                Figure fig({.width = fig_w, .height = 400});
                auto&  ax = fig.subplot(2, 2, 1);
                fig.subplot(2, 2, 2);
                ax.xlim(c.xlo, c.xhi);
                ax.ylim(c.ylo, c.yhi);
                ax.title("T:" + text);
                ax.xlabel("X:" + text);
                ax.ylabel("Y:" + text);
                fig.compute_layout();
                const auto  labels = parse_labels(SvgExporter::to_string(fig));
                const auto& vp     = ax.viewport();

                const auto  xo = ax.compute_x_ticks().offset_label;
                const auto  yo = ax.compute_y_ticks().offset_label;
                const float x_res =
                    xo.empty()
                        ? 0.0f
                        : offset_annotation_reserve(svg_plot_label_width(xo, LabelFont::Tick));
                const float y_res =
                    yo.empty()
                        ? 0.0f
                        : offset_annotation_reserve(svg_plot_label_width(yo, LabelFont::Tick));

                const FittedText rt =
                    to_fitted_text(fit_axes_title("T:" + text, vp.w, y_res, raster_label_measure));
                const FittedText rx =
                    to_fitted_text(fit_axes_xlabel("X:" + text, vp.w, x_res, raster_label_measure));
                const FittedText ry =
                    to_fitted_text(fit_axes_ylabel("Y:" + text, vp.h, raster_label_measure));

                const std::pair<const FittedText*, const char*> expect[] = {{&rt, "T:"},
                                                                            {&rx, "X:"},
                                                                            {&ry, "Y:"}};
                for (const auto& [fit, prefix] : expect)
                {
                    const SvgLabel* t = find_prefixed(labels, prefix);
                    if (fit->text.empty())
                    {
                        EXPECT_EQ(t, nullptr) << prefix;
                        continue;
                    }
                    ASSERT_NE(t, nullptr) << prefix;
                    EXPECT_EQ(t->content, fit->text) << prefix;
                    EXPECT_FLOAT_EQ(t->font_px,
                                    svg_plot_label_font_px(static_cast<LabelFont>(fit->size)))
                        << prefix;
                }
            }
}

}   // namespace

// ─── Issue #106: measured text, renderer marker sizes, lit 3D, ShapeSeries3D ──

namespace
{
// Body of the first <symbol> whose content contains `needle` ("" if none).
std::string symbol_with(const std::string& svg, const std::string& needle)
{
    size_t p = 0;
    while ((p = svg.find("<symbol", p)) != std::string::npos)
    {
        const size_t e    = svg.find("</symbol>", p);
        std::string  body = svg.substr(p, e - p);
        if (body.find(needle) != std::string::npos)
            return body;
        p = e;
    }
    return {};
}

// Every triangle fill colour, in document order.
std::vector<Color> triangle_fills(const std::string& svg)
{
    std::vector<Color> out;
    const std::string  key = "Z\" fill=\"#";
    size_t             p   = 0;
    while ((p = svg.find(key, p)) != std::string::npos)
    {
        p += key.size();
        const unsigned v = static_cast<unsigned>(std::stoul(svg.substr(p, 6), nullptr, 16));
        out.push_back(Color{static_cast<float>((v >> 16) & 0xFF) / 255.0f,
                            static_cast<float>((v >> 8) & 0xFF) / 255.0f,
                            static_cast<float>(v & 0xFF) / 255.0f,
                            1.0f});
    }
    return out;
}

float max_channel_diff(const Color& a, const Color& b)
{
    return std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)});
}
}   // namespace

TEST(SvgExport, TextIsMeasuredWithInterMetrics)
{
    ASSERT_TRUE(inter_metrics_available());
    EXPECT_GT(inter_text_width("W", 10.0f), 2.0f * inter_text_width("i", 10.0f));
    EXPECT_FLOAT_EQ(inter_text_width("abc", 20.0f), 2.0f * inter_text_width("abc", 10.0f));
    // Glyphs Inter lacks fall back to a full-width estimate.
    EXPECT_FLOAT_EQ(inter_text_width("\xe6\xbc\xa2", 10.0f), 10.0f);
}

TEST(SvgExport, LegendBoxGrowsByMeasuredLabelWidth)
{
    auto legend_width = [](const std::string& label)
    {
        Figure             fig({.width = 1000, .height = 600});
        auto&              ax = fig.subplot(1, 1, 1);
        std::vector<float> x = {0.0f, 1.0f}, y = {0.0f, 1.0f};
        ax.line(x, y).label(label);
        fig.compute_layout();
        const std::string svg = SvgExporter::to_string(fig);
        const size_t      g   = svg.find("<g class=\"legend\">");
        return attr(svg, g, "width");
    };
    const std::string narrow(24, 'i'), wide(24, 'W');
    // The box is exactly as much wider as the measured label (font 12.5 px).
    EXPECT_NEAR(legend_width(wide) - legend_width(narrow),
                inter_text_width(wide, 12.5f) - inter_text_width(narrow, 12.5f),
                0.05f);
}

TEST(SvgExport, MarkerSizeIsADiameterLikeTheRenderer)
{
    // scatter.vert: quad half size = size / 2 + 1; scatter.frag: circle radius 0.85 UV.
    Figure             fig({.width = 640, .height = 480});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x = {1.0f, 2.0f}, y = {1.0f, 2.0f};
    ax.scatter(x, y).size(10.0f).marker_style(MarkerStyle::FilledCircle);
    ax.line(x, y).marker_style(MarkerStyle::FilledCircle).marker_size(16.0f);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(symbol_with(svg, "<circle r=\"5.1\""), "");    // 0.85 * (10/2 + 1)
    EXPECT_NE(symbol_with(svg, "<circle r=\"7.65\""), "");   // 0.85 * (16/2 + 1)
}

TEST(SvgExport, DefaultScatterDotHasRendererEdgeTint)
{
    auto svg_for = [](const SvgStyle& st, bool three_d)
    {
        Figure             fig({.width = 640, .height = 480});
        std::vector<float> x = {1.0f, 2.0f}, y = {1.0f, 2.0f}, z = {0.0f, 1.0f};
        if (three_d)
            fig.subplot3d(1, 1, 1).scatter3d(x, y, z);
        else
            fig.subplot(1, 1, 1).scatter(x, y);
        fig.compute_layout();
        return SvgExporter::to_string(fig, st);
    };
    SvgStyle dark;
    dark.background = {0.05f, 0.06f, 0.08f, 1.0f};

    // Filled dot (never a ring) with a rim tinted to black on light, white on dark.
    for (bool three_d : {false, true})
    {
        const std::string light_dot = symbol_with(svg_for(SvgStyle{}, three_d), "stroke=");
        EXPECT_NE(light_dot.find("fill=\"currentColor\""), std::string::npos);
        EXPECT_NE(light_dot.find("stroke=\"#000000\""), std::string::npos) << three_d;
        EXPECT_NE(light_dot.find("opacity=\"0.3\""), std::string::npos);
        const std::string dark_dot = symbol_with(svg_for(dark, three_d), "stroke=");
        EXPECT_NE(dark_dot.find("fill=\"currentColor\""), std::string::npos);
        EXPECT_NE(dark_dot.find("stroke=\"#ffffff\""), std::string::npos) << three_d;
    }
}

TEST(SvgExport, MeshShadingUsesVertexNormals)
{
    // Two triangles facing different directions.
    auto fills_for = [](float nz)
    {
        Figure                fig({.width = 600, .height = 500});
        auto&                 ax   = fig.subplot3d(1, 1, 1);
        std::vector<float>    v    = {0, 0, 0, 0, 0, nz, 1, 0, 0, 0, 0, nz, 0, 1, 0, 0, 0, nz,
                                      2, 0, 0, 0, 0, nz, 3, 0, 1, 0, 0, nz, 2, 1, 1, 0, 0, nz};
        std::vector<uint32_t> idx  = {0, 1, 2, 3, 4, 5};
        auto&                 mesh = ax.mesh(v, idx);
        mesh.color(rgb(0.2f, 0.5f, 0.8f));
        mesh.specular(0.002f);
        ax.show_bounding_box(false);
        fig.compute_layout();
        return triangle_fills(SvgExporter::to_string(fig));
    };
    const auto smooth = fills_for(1.0f);
    ASSERT_EQ(smooth.size(), 2u);
    // Same vertex normals -> same lighting, whatever the face orientation.
    EXPECT_LE(max_channel_diff(smooth[0], smooth[1]), 2.0f / 255.0f);
    // Without normals, the face normal is used: the tilted face shades differently.
    const auto faceted = fills_for(0.0f);
    ASSERT_EQ(faceted.size(), 2u);
    EXPECT_GT(max_channel_diff(faceted[0], faceted[1]), 8.0f / 255.0f);
}

TEST(SvgExport, SurfaceShadingFollowsTheRendererMaterial)
{
    // A flat surface facing +Z is lit uniformly: shade = min(0.40 + diffuse + fill, 1)
    // with the default material, so the base colour is never brightened past itself.
    Figure             fig({.width = 600, .height = 500});
    auto&              ax = fig.subplot3d(1, 1, 1);
    std::vector<float> g  = {0.0f, 1.0f, 2.0f};
    std::vector<float> z(9, 0.0f);
    const Color        base = rgb(0.2f, 0.6f, 0.4f);
    auto&              surf = ax.surface(g, g, z);
    surf.color(base);
    surf.specular(0.002f);
    ax.zlim(-1.0f, 1.0f);
    fig.compute_layout();
    const auto fills = triangle_fills(SvgExporter::to_string(fig));
    ASSERT_EQ(fills.size(), 8u);
    for (const auto& f : fills)
    {
        EXPECT_LE(max_channel_diff(f, fills[0]), 2.0f / 255.0f);
        EXPECT_LE(f.g, base.g + 1.0f / 255.0f);
        EXPECT_GE(f.g, base.g * 0.40f);
        // Hue preserved: shading scales all channels alike.
        EXPECT_NEAR(f.r / f.g, base.r / base.g, 0.03f);
    }
}

TEST(SvgExport, ShapeSeries3DIsExportedWithPerShapeColours)
{
    Figure fig({.width = 700, .height = 500});
    auto&  ax = fig.subplot3d(1, 1, 1);
    auto&  sh = ax.shapes3d();
    sh.color(rgb(0.1f, 0.2f, 0.9f));
    sh.box(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f).shape_color(rgb(0.9f, 0.1f, 0.1f));
    sh.sphere(2.0f, 0.0f, 0.0f, 0.5f).segments(12);
    sh.box(4.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f).shape_color(Color{0.1f, 0.8f, 0.1f, 0.5f});
    ax.xlim(-1.0f, 5.0f);
    ax.ylim(-1.0f, 1.0f);
    ax.zlim(-1.0f, 1.0f);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    EXPECT_NE(svg.find("<g class=\"shape3d\""), std::string::npos);
    EXPECT_NE(svg.find("<g class=\"shape3d\" fill-opacity=\"0.5\""), std::string::npos);
    int red = 0, blue = 0, green = 0;
    for (const auto& f : triangle_fills(svg))
    {
        red += f.r > 2.0f * f.g && f.r > 2.0f * f.b;
        blue += f.b > 2.0f * f.r && f.b > 2.0f * f.g;
        green += f.g > 2.0f * f.r && f.g > 2.0f * f.b;
    }
    EXPECT_GE(red, 12);     // box: 6 faces x 2 triangles
    EXPECT_GE(blue, 20);    // sphere
    EXPECT_GE(green, 12);   // translucent box
}

TEST(SvgExport, ExportOptionsCarryThePerExportTheme)
{
    const auto dir = std::filesystem::temp_directory_path();
    Figure     fig({.width = 320, .height = 240});
    fig.subplot(1, 1, 1);
    ASSERT_TRUE(fig.save_svg((dir / "themed.svg").string(), {.theme = "light"}));
    EXPECT_EQ(fig.export_req_.svg_theme, "light");
    ASSERT_TRUE(fig.save_png((dir / "themed.png").string(), {.width = 64, .theme = "dark"}));
    EXPECT_EQ(fig.export_req_.png_theme, "dark");
    EXPECT_EQ(fig.export_req_.png_width, 64u);
    // The original signatures keep working and use the app theme.
    ASSERT_TRUE(fig.save_png((dir / "plain.png").string(), 32, 32));
    EXPECT_TRUE(fig.export_req_.png_theme.empty());
    ASSERT_TRUE(fig.save_svg((dir / "plain.svg").string()));
    EXPECT_TRUE(fig.export_req_.svg_theme.empty());
}

// write_svg()/to_string() without an SvgStyle must still honour the figure's
// background override (round-2 follow-up).
TEST(SvgExport, DefaultStyleHonoursBackgroundOverride)
{
    auto fig                        = make_line_figure();
    fig.style().background          = Color{0.2f, 0.4f, 0.6f, 1.0f};
    fig.style().background_override = true;
    std::string svg                 = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find("class=\"background\" width=\"100%\" height=\"100%\" fill=\"#336699\""),
              std::string::npos);

    const auto path = (std::filesystem::temp_directory_path() / "spectra_bg_override.svg").string();
    ASSERT_TRUE(SvgExporter::write_svg(path, fig));
    std::ifstream     in(path);
    const std::string file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::remove(path.c_str());
    EXPECT_NE(file.find("fill=\"#336699\""), std::string::npos);
}

TEST(SvgExport, DefaultStyleUsesLightInkOnDarkOverride)
{
    auto fig                        = make_line_figure();
    fig.style().background          = Color{0.05f, 0.05f, 0.05f, 1.0f};
    fig.style().background_override = true;
    std::string svg                 = SvgExporter::to_string(fig);
    // Title uses the dark theme's primary text colour, not the light default.
    EXPECT_NE(svg.find("#f1f5f9"), std::string::npos);
    EXPECT_EQ(svg.find(hex_of(SvgStyle{}.text_primary)), std::string::npos);
}

TEST(SvgExport, DefaultStyleUnchangedWithoutOverride)
{
    auto fig = make_line_figure();
    EXPECT_EQ(SvgExporter::to_string(fig), SvgExporter::to_string(fig, SvgStyle{}));
}

// ─── Font-independent text metrics (textLength) ─────────────────────────────

// Value of attribute `name` in the opening tag of `elem`, or NaN.
float attr_or_nan(const std::string& elem, const std::string& name)
{
    const size_t a = elem.find(" " + name + "=\"");
    if (a == std::string::npos)
        return std::nanf("");
    return std::stof(elem.substr(a + name.size() + 3));
}

// Opening tags of every <text> element, paired with its content.
std::vector<std::pair<std::string, std::string>> text_elements(const std::string& svg)
{
    std::vector<std::pair<std::string, std::string>> out;
    size_t                                           pos = 0;
    while ((pos = svg.find("<text ", pos)) != std::string::npos)
    {
        const size_t gt  = svg.find('>', pos);
        const size_t end = svg.find("</text>", gt);
        if (gt == std::string::npos || end == std::string::npos)
            break;
        out.emplace_back(svg.substr(pos, gt - pos), svg.substr(gt + 1, end - gt - 1));
        pos = end;
    }
    return out;
}

TEST(SvgExport, FittedLabelsPinTheirLayoutWidthWithTextLength)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "Inter not embedded in this build";
    Figure             fig({.width = 400, .height = 300});
    std::vector<float> x = {0.0f, 10.0f}, y = {0.0f, 1.0f};
    auto&              ax = fig.subplot(1, 1, 1);
    ax.line(x, y).label("series");
    ax.title(long_text("P0 title"));
    ax.xlabel(long_text("P0 xlabel"));
    ax.ylabel(long_text("P0 ylabel"));
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    // Every fitted (here: shortened) label is pinned to the Inter width the
    // fitting used, so a fallback font cannot overflow the space it was given.
    int fitted = 0;
    for (const auto& t : parse_labels(svg))
    {
        if (t.content.rfind("P0 ", 0) != 0)
            continue;
        ++fitted;
        ASSERT_TRUE(ends_with_ellipsis(t.content)) << t.content;
    }
    EXPECT_EQ(fitted, 3);

    int pinned = 0;
    for (const auto& [tag, content] : text_elements(svg))
    {
        SCOPED_TRACE(content);
        const float tl = attr_or_nan(tag, "textLength");
        ASSERT_FALSE(std::isnan(tl)) << tag;   // ticks and legend too, not only fitted labels
        EXPECT_NE(tag.find(" lengthAdjust=\"spacingAndGlyphs\""), std::string::npos);
        const float font_px = attr_or_nan(tag, "font-size");
        EXPECT_NEAR(tl, inter_text_width(content, font_px), 0.01f);
        if (content.rfind("P0 ", 0) == 0)
        {
            const SvgLabel l{0, 0, font_px, "", content, false};
            EXPECT_NEAR(tl, svg_width(l), 0.01f);
            ++pinned;
        }
    }
    EXPECT_EQ(pinned, 3);
}

TEST(SvgExport, TextLengthCanBeTurnedOff)
{
    auto     fig = make_line_figure();
    SvgStyle st;
    st.fit_text_length    = false;
    const std::string svg = SvgExporter::to_string(fig, st);
    EXPECT_GT(count_of(svg, "<text "), 0);
    EXPECT_EQ(svg.find("textLength"), std::string::npos);
    EXPECT_EQ(svg.find("lengthAdjust"), std::string::npos);
}

TEST(SvgExport, TextOutsideInterCoverageIsNotStretched)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "Inter not embedded in this build";
    // Inter has no CJK glyphs: the measured width is only an estimate, so the
    // viewer's own font metrics are kept.
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.line(std::vector<float>{0.0f, 1.0f}, std::vector<float>{0.0f, 1.0f});
    ax.title("\xE6\xB8\xA9\xE5\xBA\xA6");   // 温度
    fig.compute_layout();
    const std::string svg   = SvgExporter::to_string(fig);
    bool              found = false;
    for (const auto& [tag, content] : text_elements(svg))
    {
        if (content != "\xE6\xB8\xA9\xE5\xBA\xA6")
            continue;
        found = true;
        EXPECT_EQ(tag.find("textLength"), std::string::npos) << tag;
    }
    EXPECT_TRUE(found);
}

// ─── Embedded font ──────────────────────────────────────────────────────────

std::vector<uint8_t> base64_decode(const std::string& s)
{
    auto val = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    uint32_t             acc = 0;
    int                  n   = 0;
    for (char c : s)
    {
        const int v = val(c);
        if (v < 0)
            continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        if (++n == 4)
        {
            out.push_back(static_cast<uint8_t>(acc >> 16));
            out.push_back(static_cast<uint8_t>(acc >> 8));
            out.push_back(static_cast<uint8_t>(acc));
            acc = 0;
            n   = 0;
        }
    }
    if (n == 3)
    {
        out.push_back(static_cast<uint8_t>(acc >> 10));
        out.push_back(static_cast<uint8_t>(acc >> 2));
    }
    else if (n == 2)
        out.push_back(static_cast<uint8_t>(acc >> 4));
    return out;
}

TEST(SvgExport, Base64MatchesRfc4648Vectors)
{
    auto enc = [](std::string_view s)
    { return base64_encode({reinterpret_cast<const uint8_t*>(s.data()), s.size()}); };
    EXPECT_EQ(enc(""), "");
    EXPECT_EQ(enc("f"), "Zg==");
    EXPECT_EQ(enc("fo"), "Zm8=");
    EXPECT_EQ(enc("foo"), "Zm9v");
    EXPECT_EQ(enc("foob"), "Zm9vYg==");
    EXPECT_EQ(enc("fooba"), "Zm9vYmE=");
    EXPECT_EQ(enc("foobar"), "Zm9vYmFy");
}

TEST(SvgExport, FontIsNotEmbeddedByDefault)
{
    auto fig = make_line_figure();
    EXPECT_EQ(SvgExporter::to_string(fig).find("@font-face"), std::string::npos);
}

TEST(SvgExport, EmbeddedFontIsAnInterSubsetOfTheUsedGlyphs)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "Inter not embedded in this build";
    auto     fig = make_line_figure();   // "Test Title", "X Label", "Y Label", "test-line"
    SvgStyle st;
    st.embed_font         = true;
    const std::string svg = SvgExporter::to_string(fig, st);

    const std::string head = "@font-face{font-family:'Inter';";
    const size_t      ff   = svg.find(head);
    ASSERT_NE(ff, std::string::npos);
    EXPECT_LT(ff, svg.find("</defs>"));
    const std::string b64_start = "base64,";
    const size_t      b         = svg.find(b64_start, ff);
    ASSERT_NE(b, std::string::npos);
    const size_t e = svg.find(')', b);
    ASSERT_NE(e, std::string::npos);
    const std::vector<uint8_t> ttf = base64_decode(svg.substr(b + 7, e - b - 7));

    // A small subset of the ~400 KB face.
    const auto full = embedded_inter_font();
    EXPECT_GT(ttf.size(), 1000u);
    EXPECT_LT(ttf.size(), 64u * 1024u);
    EXPECT_LT(svg.size(), SvgExporter::to_string(fig).size() + 96u * 1024u);

    stbtt_fontinfo sub{}, orig{};
    ASSERT_TRUE(stbtt_InitFont(&sub, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)));
    ASSERT_TRUE(stbtt_InitFont(&orig, full.data(), 0));

    // Every used character keeps its outline and advance; unused ones are gone.
    for (const char ch : std::string("TestiXLabY-01"))
    {
        SCOPED_TRACE(std::string(1, ch));
        const int g = stbtt_FindGlyphIndex(&sub, ch);
        ASSERT_GT(g, 0);
        EXPECT_EQ(g, stbtt_FindGlyphIndex(&orig, ch));
        EXPECT_FALSE(stbtt_IsGlyphEmpty(&sub, g));
        int adv_s = 0, adv_o = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&sub, g, &adv_s, &lsb);
        stbtt_GetGlyphHMetrics(&orig, g, &adv_o, &lsb);
        EXPECT_EQ(adv_s, adv_o);
        stbtt_vertex* vs = nullptr;
        stbtt_vertex* vo = nullptr;
        const int     ns = stbtt_GetGlyphShape(&sub, g, &vs);
        const int     no = stbtt_GetGlyphShape(&orig, g, &vo);
        EXPECT_EQ(ns, no);
        stbtt_FreeShape(&sub, vs);
        stbtt_FreeShape(&orig, vo);
    }
    for (const char ch : std::string("QZqz@"))
    {
        SCOPED_TRACE(std::string(1, ch));
        EXPECT_EQ(stbtt_FindGlyphIndex(&sub, ch), 0);
    }
}

TEST(SvgExport, FontSubsetKeepsCompositeComponentsAndNonBmpMapping)
{
    if (!inter_metrics_available())
        GTEST_SKIP() << "Inter not embedded in this build";
    // U+00E9 (é) is a composite glyph in Inter: its components must survive.
    const std::vector<uint32_t> cps = {0xE9, 'A'};
    const auto                  ttf = inter_font_subset(cps);
    ASSERT_FALSE(ttf.empty());
    stbtt_fontinfo sub{};
    ASSERT_TRUE(stbtt_InitFont(&sub, ttf.data(), 0));
    const int g = stbtt_FindGlyphIndex(&sub, 0xE9);
    ASSERT_GT(g, 0);
    stbtt_vertex* v = nullptr;
    EXPECT_GT(stbtt_GetGlyphShape(&sub, g, &v), 0);
    stbtt_FreeShape(&sub, v);

    // A non-BMP codepoint is mapped through a format 12 subtable.
    const auto                         full = embedded_inter_font();
    const std::vector<FontSubsetGlyph> glyphs{{0x1F600u, 5}, {'A', 6}};
    const auto                         t2 = subset_truetype(full, glyphs);
    ASSERT_FALSE(t2.empty());
    stbtt_fontinfo s2{};
    ASSERT_TRUE(stbtt_InitFont(&s2, t2.data(), 0));
    EXPECT_EQ(stbtt_FindGlyphIndex(&s2, 0x1F600), 5);
    EXPECT_EQ(stbtt_FindGlyphIndex(&s2, 'A'), 6);

    // Not a TrueType font: nothing.
    const uint8_t junk[16] = {'O', 'T', 'T', 'O'};
    EXPECT_TRUE(subset_truetype(junk, glyphs).empty());
}

// ─── Plugin series ──────────────────────────────────────────────────────────

struct WarningCapture
{
    std::shared_ptr<std::vector<std::string>> messages =
        std::make_shared<std::vector<std::string>>();
    Logger::SinkId id = 0;
    WarningCapture()
    {
        auto m = messages;
        id     = Logger::instance().add_sink(
            [m](const Logger::LogEntry& e)
            {
                if (e.level == LogLevel::Warning && e.category == "svg")
                    m->push_back(e.message);
            });
    }
    ~WarningCapture() { Logger::instance().remove_sink(id); }
};

Figure make_plugin_figure(int series_count = 1)
{
    Figure fig({.width = 640, .height = 480});
    auto&  ax = fig.subplot(1, 1, 1);
    for (int i = 0; i < series_count; ++i)
    {
        auto        cs   = std::make_unique<CustomSeries>("grid--cells");
        const float blob = 0.0f;
        cs->set_data(&blob, sizeof(blob), 1);
        cs->label("cells " + std::to_string(i));
        ax.series_mut().push_back(std::move(cs));
    }
    ax.xlim(0.0, 4.0);
    ax.ylim(0.0, 2.0);
    fig.compute_layout();
    return fig;
}

TEST(SvgExport, PluginSeriesWithoutHookBecomesCommentAndWarning)
{
    auto           fig = make_plugin_figure(2);
    WarningCapture warn;
    const auto     svg = SvgExporter::to_string(fig);
    // One comment per series ("--" is not allowed in an XML comment) ...
    EXPECT_EQ(count_of(svg,
                       "<!-- plugin series 'grid-\xE2\x80\x90"
                       "cells' \"cells "),
              2);
    EXPECT_NE(svg.find("not exported: no plugin series registry attached"), std::string::npos);
    EXPECT_EQ(svg.find("plugin-series"), std::string::npos);
    // ... and one warning per type.
    ASSERT_EQ(warn.messages->size(), 1u);
    EXPECT_NE((*warn.messages)[0].find("grid--cells"), std::string::npos);

    // A host whose registry has no export callback for the type says so.
    SvgExportHost host;
    host.custom_series = [](const CustomSeries&, const float*, float, CustomVectorSink&)
    { return "its plugin has no vector export callback"; };
    const auto svg2 = svg_export_to_string(fig, SvgStyle{}, host);
    EXPECT_EQ(count_of(svg2, "not exported: its plugin has no vector export callback"), 2);
}

TEST(SvgExport, PluginSeriesIsDrawnThroughTheHostHook)
{
    auto          fig = make_plugin_figure();
    const Rect    vp  = fig.axes()[0]->viewport();
    SvgExportHost host;
    float         seen_alpha = -1.0f;
    host.custom_series       = [&](const CustomSeries& cs,
                             const float*        rgba,
                             float               opacity,
                             CustomVectorSink&   sink) -> const char*
    {
        EXPECT_EQ(cs.type_name(), "grid--cells");
        EXPECT_FLOAT_EQ(opacity, 1.0f);
        seen_alpha          = rgba[3];
        const float red[4]  = {1.0f, 0.0f, 0.0f, 0.5f};
        const float xy[6]   = {0.0f, 0.0f, 4.0f, 0.0f, 4.0f, 2.0f};
        const float line[4] = {0.0f, 0.0f, 4.0f, 2.0f};
        sink.fill_rect(0.0f, 0.0f, 2.0f, 1.0f, red);   // lower-left quarter
        sink.fill_polygon(xy, 3, red);
        sink.stroke_polyline(line, 2, rgba, 2.0f);
        return nullptr;
    };
    WarningCapture warn;
    const auto     svg = svg_export_to_string(fig, SvgStyle{}, host);
    EXPECT_TRUE(warn.messages->empty());
    EXPECT_FLOAT_EQ(seen_alpha, 1.0f);
    const size_t g = svg.find("<g class=\"plugin-series\" data-type=\"grid--cells\">");
    ASSERT_NE(g, std::string::npos);
    // Inside the clipped series group.
    EXPECT_LT(svg.rfind("<g class=\"series\" clip-path=", g), g);

    const size_t r = svg.find("<rect ", g);
    ASSERT_NE(r, std::string::npos);
    const std::string rect = svg.substr(r, svg.find("/>", r) - r);
    EXPECT_NEAR(attr_or_nan(rect, "x"), vp.x, 0.01f);
    EXPECT_NEAR(attr_or_nan(rect, "y"), vp.y + vp.h * 0.5f, 0.01f);
    EXPECT_NEAR(attr_or_nan(rect, "width"), vp.w * 0.5f, 0.01f);
    EXPECT_NEAR(attr_or_nan(rect, "height"), vp.h * 0.5f, 0.01f);
    EXPECT_NE(rect.find("fill=\"#ff0000\" fill-opacity=\"0.5\""), std::string::npos);
    EXPECT_NE(svg.find("Z\" fill=\"#ff0000\"", g), std::string::npos);
    EXPECT_NE(svg.find("fill=\"none\" stroke=", g), std::string::npos);

    // A failing hook leaves nothing behind but the comment.
    host.custom_series = [](const CustomSeries&, const float*, float, CustomVectorSink& sink)
    {
        const float c[4] = {1, 0, 0, 1};
        sink.fill_rect(0, 0, 1, 1, c);
        return "its vector export callback failed";
    };
    const auto svg2 = svg_export_to_string(fig, SvgStyle{}, host);
    EXPECT_EQ(svg2.find("plugin-series"), std::string::npos);
    EXPECT_NE(svg2.find("not exported: its vector export callback failed"), std::string::npos);
}

}   // namespace spectra

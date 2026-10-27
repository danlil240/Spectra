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
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <random>

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

    // One shared marker <symbol>, instanced once per point (+1 legend swatch)
    EXPECT_EQ(count_of(svg, "<symbol"), 1);
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
    ax.plot(x, y, "k-");
    fig.compute_layout();
    SvgStyle st;
    st.background   = Color{0.05f, 0.05f, 0.08f, 1.0f};
    st.text_primary = Color{0.9f, 0.9f, 0.9f, 1.0f};
    std::string svg = SvgExporter::to_string(fig, st);
    EXPECT_NE(svg.find("<path class=\"line\" d=\"M"), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"#e6e6e6\""), std::string::npos);
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

}   // namespace
}   // namespace spectra

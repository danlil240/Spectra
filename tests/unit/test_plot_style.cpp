#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <vector>

using namespace spectra;

// ─── Helper: compare colors with tolerance ───────────────────────────────────

static bool color_eq(const Color& a, const Color& b, float eps = 0.01f)
{
    return std::abs(a.r - b.r) < eps && std::abs(a.g - b.g) < eps && std::abs(a.b - b.b) < eps
           && std::abs(a.a - b.a) < eps;
}

// ═══════════════════════════════════════════════════════════════════════════════
// LineStyle enum
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LineStyleTest, EnumValues)
{
    EXPECT_EQ(static_cast<int>(LineStyle::None), 0);
    EXPECT_EQ(static_cast<int>(LineStyle::Solid), 1);
    EXPECT_EQ(static_cast<int>(LineStyle::Dashed), 2);
    EXPECT_EQ(static_cast<int>(LineStyle::Dotted), 3);
    EXPECT_EQ(static_cast<int>(LineStyle::DashDot), 4);
    EXPECT_EQ(static_cast<int>(LineStyle::DashDotDot), 5);
}

TEST(LineStyleTest, Names)
{
    EXPECT_STREQ(line_style_name(LineStyle::None), "None");
    EXPECT_STREQ(line_style_name(LineStyle::Solid), "Solid");
    EXPECT_STREQ(line_style_name(LineStyle::Dashed), "Dashed");
    EXPECT_STREQ(line_style_name(LineStyle::Dotted), "Dotted");
    EXPECT_STREQ(line_style_name(LineStyle::DashDot), "Dash-Dot");
    EXPECT_STREQ(line_style_name(LineStyle::DashDotDot), "Dash-Dot-Dot");
}

TEST(LineStyleTest, Symbols)
{
    EXPECT_STREQ(line_style_symbol(LineStyle::None), "");
    EXPECT_STREQ(line_style_symbol(LineStyle::Solid), "-");
    EXPECT_STREQ(line_style_symbol(LineStyle::Dashed), "--");
    EXPECT_STREQ(line_style_symbol(LineStyle::Dotted), ":");
    EXPECT_STREQ(line_style_symbol(LineStyle::DashDot), "-.");
    EXPECT_STREQ(line_style_symbol(LineStyle::DashDotDot), "-..");
}

TEST(LineStyleTest, Count)
{
    EXPECT_EQ(LINE_STYLE_COUNT, 6);
    int count = 0;
    for (auto ls : ALL_LINE_STYLES)
    {
        (void)ls;
        ++count;
    }
    EXPECT_EQ(count, LINE_STYLE_COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MarkerStyle enum
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MarkerStyleTest, EnumValues)
{
    EXPECT_EQ(static_cast<int>(MarkerStyle::None), 0);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Point), 1);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Circle), 2);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Plus), 3);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Cross), 4);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Star), 5);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Square), 6);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Diamond), 7);
    EXPECT_EQ(static_cast<int>(MarkerStyle::TriangleUp), 8);
    EXPECT_EQ(static_cast<int>(MarkerStyle::TriangleDown), 9);
    EXPECT_EQ(static_cast<int>(MarkerStyle::TriangleLeft), 10);
    EXPECT_EQ(static_cast<int>(MarkerStyle::TriangleRight), 11);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Pentagon), 12);
    EXPECT_EQ(static_cast<int>(MarkerStyle::Hexagon), 13);
    EXPECT_EQ(static_cast<int>(MarkerStyle::FilledCircle), 14);
    EXPECT_EQ(static_cast<int>(MarkerStyle::FilledSquare), 15);
    EXPECT_EQ(static_cast<int>(MarkerStyle::FilledDiamond), 16);
    EXPECT_EQ(static_cast<int>(MarkerStyle::FilledTriangleUp), 17);
}

TEST(MarkerStyleTest, Names)
{
    EXPECT_STREQ(marker_style_name(MarkerStyle::None), "None");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Point), "Point");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Circle), "Circle");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Star), "Star");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Square), "Square");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Diamond), "Diamond");
    EXPECT_STREQ(marker_style_name(MarkerStyle::TriangleUp), "Triangle Up");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Pentagon), "Pentagon");
    EXPECT_STREQ(marker_style_name(MarkerStyle::Hexagon), "Hexagon");
    EXPECT_STREQ(marker_style_name(MarkerStyle::FilledCircle), "Filled Circle");
    EXPECT_STREQ(marker_style_name(MarkerStyle::FilledTriangleUp), "Filled Triangle Up");
}

TEST(MarkerStyleTest, Symbols)
{
    EXPECT_EQ(marker_style_symbol(MarkerStyle::None), '\0');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Point), '.');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Circle), 'o');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Plus), '+');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Cross), 'x');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Star), '*');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Square), 's');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Diamond), 'd');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::TriangleUp), '^');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::TriangleDown), 'v');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::TriangleLeft), '<');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::TriangleRight), '>');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Pentagon), 'p');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::Hexagon), 'h');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::FilledCircle), 'O');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::FilledSquare), 'S');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::FilledDiamond), 'D');
    EXPECT_EQ(marker_style_symbol(MarkerStyle::FilledTriangleUp), 'A');
}

TEST(MarkerStyleTest, Count)
{
    EXPECT_EQ(MARKER_STYLE_COUNT, 18);
    int count = 0;
    for (auto ms : ALL_MARKER_STYLES)
    {
        (void)ms;
        ++count;
    }
    EXPECT_EQ(count, MARKER_STYLE_COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PlotStyle struct
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PlotStyleTest, Defaults)
{
    PlotStyle ps;
    EXPECT_EQ(ps.line_style, LineStyle::Solid);
    EXPECT_EQ(ps.marker_style, MarkerStyle::None);
    EXPECT_FALSE(ps.color.has_value());
    EXPECT_FLOAT_EQ(ps.line_width, 2.0f);
    EXPECT_FLOAT_EQ(ps.marker_size, 6.0f);
    EXPECT_FLOAT_EQ(ps.opacity, 1.0f);
}

TEST(PlotStyleTest, HasLine)
{
    PlotStyle ps;
    ps.line_style = LineStyle::Solid;
    EXPECT_TRUE(ps.has_line());
    ps.line_style = LineStyle::Dashed;
    EXPECT_TRUE(ps.has_line());
    ps.line_style = LineStyle::None;
    EXPECT_FALSE(ps.has_line());
}

TEST(PlotStyleTest, HasMarker)
{
    PlotStyle ps;
    ps.marker_style = MarkerStyle::None;
    EXPECT_FALSE(ps.has_marker());
    ps.marker_style = MarkerStyle::Circle;
    EXPECT_TRUE(ps.has_marker());
    ps.marker_style = MarkerStyle::Star;
    EXPECT_TRUE(ps.has_marker());
}

// ═══════════════════════════════════════════════════════════════════════════════
// DashPattern
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DashPatternTest, SolidHasNoPattern)
{
    DashPattern dp = get_dash_pattern(LineStyle::Solid);
    EXPECT_EQ(dp.count, 0);
    EXPECT_FLOAT_EQ(dp.total, 0.0f);
}

TEST(DashPatternTest, NoneHasNoPattern)
{
    DashPattern dp = get_dash_pattern(LineStyle::None);
    EXPECT_EQ(dp.count, 0);
}

TEST(DashPatternTest, DashedPattern)
{
    DashPattern dp = get_dash_pattern(LineStyle::Dashed, 2.0f);
    EXPECT_EQ(dp.count, 2);
    EXPECT_GT(dp.total, 0.0f);
    EXPECT_GT(dp.segments[0], 0.0f);  // dash
    EXPECT_GT(dp.segments[1], 0.0f);  // gap
}

TEST(DashPatternTest, DottedPattern)
{
    DashPattern dp = get_dash_pattern(LineStyle::Dotted, 2.0f);
    EXPECT_EQ(dp.count, 2);
    EXPECT_GT(dp.total, 0.0f);
}

TEST(DashPatternTest, DashDotPattern)
{
    DashPattern dp = get_dash_pattern(LineStyle::DashDot, 2.0f);
    EXPECT_EQ(dp.count, 4);
    EXPECT_GT(dp.total, 0.0f);
}

TEST(DashPatternTest, DashDotDotPattern)
{
    DashPattern dp = get_dash_pattern(LineStyle::DashDotDot, 2.0f);
    EXPECT_EQ(dp.count, 6);
    EXPECT_GT(dp.total, 0.0f);
}

TEST(DashPatternTest, ScalesWithLineWidth)
{
    DashPattern dp1 = get_dash_pattern(LineStyle::Dashed, 1.0f);
    DashPattern dp2 = get_dash_pattern(LineStyle::Dashed, 3.0f);
    EXPECT_GT(dp2.total, dp1.total);
    EXPECT_FLOAT_EQ(dp2.total / dp1.total, 3.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Format String Parser — Colors
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatParserTest, ColorOnly_Red)
{
    auto ps = parse_format_string("r");
    EXPECT_TRUE(ps.color.has_value());
    EXPECT_TRUE(color_eq(*ps.color, colors::red));
    EXPECT_EQ(ps.line_style, LineStyle::Solid);  // default when only color
    EXPECT_EQ(ps.marker_style, MarkerStyle::None);
}

TEST(FormatParserTest, ColorOnly_Green)
{
    auto ps = parse_format_string("g");
    EXPECT_TRUE(color_eq(*ps.color, colors::green));
}

TEST(FormatParserTest, ColorOnly_Blue)
{
    auto ps = parse_format_string("b");
    EXPECT_TRUE(color_eq(*ps.color, colors::blue));
}

TEST(FormatParserTest, ColorOnly_Cyan)
{
    auto ps = parse_format_string("c");
    EXPECT_TRUE(color_eq(*ps.color, colors::cyan));
}

TEST(FormatParserTest, ColorOnly_Magenta)
{
    auto ps = parse_format_string("m");
    EXPECT_TRUE(color_eq(*ps.color, colors::magenta));
}

TEST(FormatParserTest, ColorOnly_Yellow)
{
    auto ps = parse_format_string("y");
    EXPECT_TRUE(color_eq(*ps.color, colors::yellow));
}

TEST(FormatParserTest, ColorOnly_Black)
{
    auto ps = parse_format_string("k");
    EXPECT_TRUE(color_eq(*ps.color, colors::black));
}

TEST(FormatParserTest, ColorOnly_White)
{
    auto ps = parse_format_string("w");
    EXPECT_TRUE(color_eq(*ps.color, colors::white));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Format String Parser — Line Styles
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatParserTest, LineStyle_Solid)
{
    auto ps = parse_format_string("-");
    EXPECT_EQ(ps.line_style, LineStyle::Solid);
    EXPECT_EQ(ps.marker_style, MarkerStyle::None);
    EXPECT_FALSE(ps.color.has_value());
}

TEST(FormatParserTest, LineStyle_Dashed)
{
    auto ps = parse_format_string("--");
    EXPECT_EQ(ps.line_style, LineStyle::Dashed);
}

TEST(FormatParserTest, LineStyle_Dotted)
{
    auto ps = parse_format_string(":");
    EXPECT_EQ(ps.line_style, LineStyle::Dotted);
}

TEST(FormatParserTest, LineStyle_DashDot)
{
    auto ps = parse_format_string("-.");
    EXPECT_EQ(ps.line_style, LineStyle::DashDot);
}

TEST(FormatParserTest, LineStyle_DashDotDot)
{
    auto ps = parse_format_string("-..");
    EXPECT_EQ(ps.line_style, LineStyle::DashDotDot);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Format String Parser — Markers
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatParserTest, MarkerOnly_Circle)
{
    auto ps = parse_format_string("o");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Circle);
    EXPECT_EQ(ps.line_style, LineStyle::None);  // marker-only = no line
}

TEST(FormatParserTest, MarkerOnly_Point)
{
    auto ps = parse_format_string(".");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Point);
    EXPECT_EQ(ps.line_style, LineStyle::None);
}

TEST(FormatParserTest, MarkerOnly_Plus)
{
    auto ps = parse_format_string("+");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Plus);
}

TEST(FormatParserTest, MarkerOnly_Cross)
{
    auto ps = parse_format_string("x");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Cross);
}

TEST(FormatParserTest, MarkerOnly_Star)
{
    auto ps = parse_format_string("*");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Star);
}

TEST(FormatParserTest, MarkerOnly_Square)
{
    auto ps = parse_format_string("s");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Square);
}

TEST(FormatParserTest, MarkerOnly_Diamond)
{
    auto ps = parse_format_string("d");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Diamond);
}

TEST(FormatParserTest, MarkerOnly_TriangleUp)
{
    auto ps = parse_format_string("^");
    EXPECT_EQ(ps.marker_style, MarkerStyle::TriangleUp);
}

TEST(FormatParserTest, MarkerOnly_TriangleDown)
{
    auto ps = parse_format_string("v");
    EXPECT_EQ(ps.marker_style, MarkerStyle::TriangleDown);
}

TEST(FormatParserTest, MarkerOnly_TriangleLeft)
{
    auto ps = parse_format_string("<");
    EXPECT_EQ(ps.marker_style, MarkerStyle::TriangleLeft);
}

TEST(FormatParserTest, MarkerOnly_TriangleRight)
{
    auto ps = parse_format_string(">");
    EXPECT_EQ(ps.marker_style, MarkerStyle::TriangleRight);
}

TEST(FormatParserTest, MarkerOnly_Pentagon)
{
    auto ps = parse_format_string("p");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Pentagon);
}

TEST(FormatParserTest, MarkerOnly_Hexagon)
{
    auto ps = parse_format_string("h");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Hexagon);
}

TEST(FormatParserTest, MarkerOnly_FilledCircle)
{
    auto ps = parse_format_string("O");
    EXPECT_EQ(ps.marker_style, MarkerStyle::FilledCircle);
}

TEST(FormatParserTest, MarkerOnly_FilledSquare)
{
    auto ps = parse_format_string("S");
    EXPECT_EQ(ps.marker_style, MarkerStyle::FilledSquare);
}

TEST(FormatParserTest, MarkerOnly_FilledDiamond)
{
    auto ps = parse_format_string("D");
    EXPECT_EQ(ps.marker_style, MarkerStyle::FilledDiamond);
}

TEST(FormatParserTest, MarkerOnly_FilledTriangleUp)
{
    auto ps = parse_format_string("A");
    EXPECT_EQ(ps.marker_style, MarkerStyle::FilledTriangleUp);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Format String Parser — Combinations (MATLAB-style)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatParserTest, RedDashedCircle)
{
    auto ps = parse_format_string("r--o");
    EXPECT_TRUE(color_eq(*ps.color, colors::red));
    EXPECT_EQ(ps.line_style, LineStyle::Dashed);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Circle);
}

TEST(FormatParserTest, BlueDottedStar)
{
    auto ps = parse_format_string("b:*");
    EXPECT_TRUE(color_eq(*ps.color, colors::blue));
    EXPECT_EQ(ps.line_style, LineStyle::Dotted);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Star);
}

TEST(FormatParserTest, GreenDashDotSquare)
{
    auto ps = parse_format_string("g-.s");
    EXPECT_TRUE(color_eq(*ps.color, colors::green));
    EXPECT_EQ(ps.line_style, LineStyle::DashDot);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Square);
}

TEST(FormatParserTest, BlackSolidDiamond)
{
    auto ps = parse_format_string("k-d");
    EXPECT_TRUE(color_eq(*ps.color, colors::black));
    EXPECT_EQ(ps.line_style, LineStyle::Solid);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Diamond);
}

TEST(FormatParserTest, CyanDashDotDotTriangle)
{
    auto ps = parse_format_string("c-..^");
    EXPECT_TRUE(color_eq(*ps.color, colors::cyan));
    EXPECT_EQ(ps.line_style, LineStyle::DashDotDot);
    EXPECT_EQ(ps.marker_style, MarkerStyle::TriangleUp);
}

TEST(FormatParserTest, ColorAndMarkerNoLine)
{
    auto ps = parse_format_string("ro");
    EXPECT_TRUE(color_eq(*ps.color, colors::red));
    EXPECT_EQ(ps.marker_style, MarkerStyle::Circle);
    EXPECT_EQ(ps.line_style, LineStyle::None);  // marker-only
}

TEST(FormatParserTest, LineAndMarkerNoColor)
{
    auto ps = parse_format_string("--o");
    EXPECT_FALSE(ps.color.has_value());
    EXPECT_EQ(ps.line_style, LineStyle::Dashed);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Circle);
}

TEST(FormatParserTest, FlexibleOrder_LineFirst)
{
    auto ps = parse_format_string("--r");
    EXPECT_TRUE(color_eq(*ps.color, colors::red));
    EXPECT_EQ(ps.line_style, LineStyle::Dashed);
}

TEST(FormatParserTest, FlexibleOrder_MarkerColorLine)
{
    auto ps = parse_format_string("or--");
    EXPECT_TRUE(color_eq(*ps.color, colors::red));
    EXPECT_EQ(ps.line_style, LineStyle::Dashed);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Circle);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Format String Parser — Edge Cases
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatParserTest, EmptyString)
{
    auto ps = parse_format_string("");
    EXPECT_EQ(ps.line_style, LineStyle::Solid);  // default
    EXPECT_EQ(ps.marker_style, MarkerStyle::None);
    EXPECT_FALSE(ps.color.has_value());
}

TEST(FormatParserTest, UnknownCharsIgnored)
{
    auto ps = parse_format_string("r!@#--o");
    EXPECT_TRUE(color_eq(*ps.color, colors::red));
    EXPECT_EQ(ps.line_style, LineStyle::Dashed);
    EXPECT_EQ(ps.marker_style, MarkerStyle::Circle);
}

TEST(FormatParserTest, LastColorWins)
{
    auto ps = parse_format_string("rb");
    EXPECT_TRUE(color_eq(*ps.color, colors::blue));
}

TEST(FormatParserTest, LastMarkerWins)
{
    auto ps = parse_format_string("o*");
    EXPECT_EQ(ps.marker_style, MarkerStyle::Star);
}

// ═══════════════════════════════════════════════════════════════════════════════
// to_format_string (round-trip)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(FormatStringTest, RoundTrip_RedDashedCircle)
{
    auto ps = parse_format_string("r--o");
    std::string fmt = to_format_string(ps);
    EXPECT_EQ(fmt, "r--o");
}

TEST(FormatStringTest, RoundTrip_BlueDotted)
{
    auto ps = parse_format_string("b:");
    std::string fmt = to_format_string(ps);
    EXPECT_EQ(fmt, "b:");
}

TEST(FormatStringTest, RoundTrip_BlackStar)
{
    auto ps = parse_format_string("k*");
    std::string fmt = to_format_string(ps);
    EXPECT_EQ(fmt, "k*");
}

TEST(FormatStringTest, NoColor)
{
    auto ps = parse_format_string("--o");
    std::string fmt = to_format_string(ps);
    EXPECT_EQ(fmt, "--o");
}

TEST(FormatStringTest, SolidOnly)
{
    auto ps = parse_format_string("-");
    std::string fmt = to_format_string(ps);
    EXPECT_EQ(fmt, "-");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Series integration — LineSeries::format()
// ═══════════════════════════════════════════════════════════════════════════════

TEST(LineSeriesFormatTest, ApplyFormatString)
{
    LineSeries ls;
    ls.format("r--o");
    EXPECT_TRUE(color_eq(ls.color(), colors::red));
    EXPECT_EQ(ls.line_style(), LineStyle::Dashed);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Circle);
}

TEST(LineSeriesFormatTest, FormatPreservesData)
{
    std::vector<float> x = {1, 2, 3};
    std::vector<float> y = {4, 5, 6};
    LineSeries ls(x, y);
    ls.format("b:*");
    EXPECT_EQ(ls.point_count(), 3u);
    EXPECT_TRUE(color_eq(ls.color(), colors::blue));
    EXPECT_EQ(ls.line_style(), LineStyle::Dotted);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Star);
}

TEST(LineSeriesFormatTest, FluentChaining)
{
    LineSeries ls;
    ls.format("r--o").width(3.0f).label("test");
    EXPECT_FLOAT_EQ(ls.width(), 3.0f);
    EXPECT_EQ(ls.label(), "test");
    EXPECT_EQ(ls.line_style(), LineStyle::Dashed);
}

TEST(LineSeriesFormatTest, RuntimeStyleChange)
{
    LineSeries ls;
    ls.format("r-");
    EXPECT_EQ(ls.line_style(), LineStyle::Solid);
    // Change at runtime
    ls.line_style(LineStyle::Dotted);
    EXPECT_EQ(ls.line_style(), LineStyle::Dotted);
    ls.marker_style(MarkerStyle::Diamond);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Diamond);
}

TEST(LineSeriesFormatTest, MarkerSizeAdjustment)
{
    LineSeries ls;
    ls.marker_style(MarkerStyle::Circle).marker_size(12.0f);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Circle);
    EXPECT_FLOAT_EQ(ls.marker_size(), 12.0f);
}

TEST(LineSeriesFormatTest, OpacityAdjustment)
{
    LineSeries ls;
    ls.opacity(0.5f);
    EXPECT_FLOAT_EQ(ls.opacity(), 0.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Series integration — ScatterSeries::format()
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ScatterSeriesFormatTest, ApplyFormatString)
{
    ScatterSeries ss;
    ss.format("go");
    EXPECT_TRUE(color_eq(ss.color(), colors::green));
    EXPECT_EQ(ss.marker_style(), MarkerStyle::Circle);
}

TEST(ScatterSeriesFormatTest, FluentChaining)
{
    ScatterSeries ss;
    ss.format("r*").size(10.0f).label("scatter");
    EXPECT_FLOAT_EQ(ss.size(), 10.0f);
    EXPECT_EQ(ss.label(), "scatter");
    EXPECT_EQ(ss.marker_style(), MarkerStyle::Star);
}

TEST(ScatterSeriesFormatTest, RuntimeStyleChange)
{
    ScatterSeries ss;
    ss.marker_style(MarkerStyle::Square);
    EXPECT_EQ(ss.marker_style(), MarkerStyle::Square);
    ss.marker_style(MarkerStyle::Pentagon);
    EXPECT_EQ(ss.marker_style(), MarkerStyle::Pentagon);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Series integration — PlotStyle application
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SeriesPlotStyleTest, ApplyPlotStyle)
{
    PlotStyle ps;
    ps.line_style = LineStyle::DashDot;
    ps.marker_style = MarkerStyle::Diamond;
    ps.color = colors::cyan;
    ps.marker_size = 10.0f;
    ps.opacity = 0.7f;

    LineSeries ls;
    ls.plot_style(ps);
    EXPECT_EQ(ls.line_style(), LineStyle::DashDot);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Diamond);
    EXPECT_TRUE(color_eq(ls.color(), colors::cyan));
    EXPECT_FLOAT_EQ(ls.marker_size(), 10.0f);
    EXPECT_FLOAT_EQ(ls.opacity(), 0.7f);
}

TEST(SeriesPlotStyleTest, PlotStyleWithoutColor)
{
    PlotStyle ps;
    ps.line_style = LineStyle::Dashed;
    // No color set — should not change existing color

    LineSeries ls;
    ls.color(colors::orange);
    ls.plot_style(ps);
    EXPECT_TRUE(color_eq(ls.color(), colors::orange));
    EXPECT_EQ(ls.line_style(), LineStyle::Dashed);
}

TEST(SeriesPlotStyleTest, PlotStyleMutAccess)
{
    LineSeries ls;
    ls.plot_style_mut().line_style = LineStyle::Dotted;
    ls.plot_style_mut().marker_style = MarkerStyle::Star;
    EXPECT_EQ(ls.line_style(), LineStyle::Dotted);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Star);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Axes::plot() convenience
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AxesPlotTest, PlotWithFormatString)
{
    Axes ax;
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 4};
    auto& ls = ax.plot(x, y, "r--o");
    EXPECT_TRUE(color_eq(ls.color(), colors::red));
    EXPECT_EQ(ls.line_style(), LineStyle::Dashed);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Circle);
    EXPECT_EQ(ls.point_count(), 3u);
    EXPECT_EQ(ax.series().size(), 1u);
}

TEST(AxesPlotTest, PlotWithPlotStyle)
{
    Axes ax;
    std::vector<float> x = {0, 1};
    std::vector<float> y = {0, 1};
    PlotStyle ps;
    ps.line_style = LineStyle::Dotted;
    ps.marker_style = MarkerStyle::Star;
    ps.color = colors::magenta;
    auto& ls = ax.plot(x, y, ps);
    EXPECT_TRUE(color_eq(ls.color(), colors::magenta));
    EXPECT_EQ(ls.line_style(), LineStyle::Dotted);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::Star);
}

TEST(AxesPlotTest, PlotDefaultIsSolid)
{
    Axes ax;
    std::vector<float> x = {0, 1};
    std::vector<float> y = {0, 1};
    auto& ls = ax.plot(x, y);
    EXPECT_EQ(ls.line_style(), LineStyle::Solid);
    EXPECT_EQ(ls.marker_style(), MarkerStyle::None);
}

TEST(AxesPlotTest, MultiplePlots)
{
    Axes ax;
    std::vector<float> x = {0, 1};
    std::vector<float> y = {0, 1};
    ax.plot(x, y, "r-");
    ax.plot(x, y, "b--o");
    ax.plot(x, y, "g:*");
    EXPECT_EQ(ax.series().size(), 3u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Dirty flag tracking
// ═══════════════════════════════════════════════════════════════════════════════

TEST(DirtyFlagTest, StyleChangeMarksDirty)
{
    LineSeries ls;
    ls.clear_dirty();
    EXPECT_FALSE(ls.is_dirty());

    ls.line_style(LineStyle::Dashed);
    EXPECT_TRUE(ls.is_dirty());

    ls.clear_dirty();
    ls.marker_style(MarkerStyle::Circle);
    EXPECT_TRUE(ls.is_dirty());

    ls.clear_dirty();
    ls.marker_size(10.0f);
    EXPECT_TRUE(ls.is_dirty());

    ls.clear_dirty();
    ls.opacity(0.5f);
    EXPECT_TRUE(ls.is_dirty());
}

TEST(DirtyFlagTest, FormatMarksDirty)
{
    LineSeries ls;
    ls.clear_dirty();
    ls.format("r--o");
    EXPECT_TRUE(ls.is_dirty());
}

TEST(DirtyFlagTest, PlotStyleMarksDirty)
{
    LineSeries ls;
    ls.clear_dirty();
    PlotStyle ps;
    ps.line_style = LineStyle::Dotted;
    ls.plot_style(ps);
    EXPECT_TRUE(ls.is_dirty());
}

// ═══════════════════════════════════════════════════════════════════════════════
// All MATLAB format string combinations (comprehensive)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(MatlabCompatTest, AllColorLineMarkerCombinations)
{
    // Test a representative set of all 3-component combos
    struct TestCase
    {
        const char* fmt;
        Color expected_color;
        LineStyle expected_line;
        MarkerStyle expected_marker;
    };

    TestCase cases[] = {
        {"r-o", colors::red, LineStyle::Solid, MarkerStyle::Circle},
        {"g--s", colors::green, LineStyle::Dashed, MarkerStyle::Square},
        {"b:d", colors::blue, LineStyle::Dotted, MarkerStyle::Diamond},
        {"c-.^", colors::cyan, LineStyle::DashDot, MarkerStyle::TriangleUp},
        {"m-..v", colors::magenta, LineStyle::DashDotDot, MarkerStyle::TriangleDown},
        {"y-+", colors::yellow, LineStyle::Solid, MarkerStyle::Plus},
        {"k--x", colors::black, LineStyle::Dashed, MarkerStyle::Cross},
        {"w:*", colors::white, LineStyle::Dotted, MarkerStyle::Star},
        {"r-.<", colors::red, LineStyle::DashDot, MarkerStyle::TriangleLeft},
        {"g-..>", colors::green, LineStyle::DashDotDot, MarkerStyle::TriangleRight},
        {"b-p", colors::blue, LineStyle::Solid, MarkerStyle::Pentagon},
        {"c--h", colors::cyan, LineStyle::Dashed, MarkerStyle::Hexagon},
    };

    for (const auto& tc : cases)
    {
        auto ps = parse_format_string(tc.fmt);
        EXPECT_TRUE(ps.color.has_value()) << "fmt=" << tc.fmt;
        EXPECT_TRUE(color_eq(*ps.color, tc.expected_color)) << "fmt=" << tc.fmt;
        EXPECT_EQ(ps.line_style, tc.expected_line) << "fmt=" << tc.fmt;
        EXPECT_EQ(ps.marker_style, tc.expected_marker) << "fmt=" << tc.fmt;
    }
}

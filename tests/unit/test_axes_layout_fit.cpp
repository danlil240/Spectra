// Issue #108: category tick names, date-time axes, measured left margins and
// legend placement (better "Best", OutsideRight).

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <regex>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "core/datetime_ticks.hpp"
#include "core/figure_layout.hpp"
#include "io/text_metrics.hpp"
#include "ui/overlay/legend_layout.hpp"

using namespace spectra;

namespace
{

// 2024-03-05 14:30:00 UTC
constexpr double T0 = 1709649000.0;

LinearTickOptions x_opts(double px = 800.0)
{
    LinearTickOptions o;
    o.axis_px             = px;
    o.target_px           = X_TICK_TARGET_PX;
    o.labels_side_by_side = true;
    return o;
}

bool is_number(const std::string& s)
{
    return std::regex_match(s, std::regex(R"(-?[0-9]+(\.[0-9]+)?)"));
}

}   // namespace

// ─── Calendar helpers ───────────────────────────────────────────────────────

TEST(DateTimeTicks, CivilConversionRoundTrips)
{
    EXPECT_EQ(days_from_civil(1970, 1, 1), 0);
    EXPECT_EQ(days_from_civil(2000, 3, 1), 11017);
    EXPECT_EQ(days_from_civil(1969, 12, 31), -1);

    const CivilTime c = civil_from_epoch(T0);
    EXPECT_EQ(c.year, 2024);
    EXPECT_EQ(c.month, 3);
    EXPECT_EQ(c.day, 5);
    EXPECT_EQ(c.hour, 14);
    EXPECT_EQ(c.minute, 30);
    EXPECT_NEAR(c.second, 0.0, 1e-6);

    const CivilTime leap = civil_from_epoch(days_from_civil(2024, 2, 29) * 86400.0 + 1.5);
    EXPECT_EQ(leap.month, 2);
    EXPECT_EQ(leap.day, 29);
    EXPECT_NEAR(leap.second, 1.5, 1e-6);
}

// ─── Date-time ticks ────────────────────────────────────────────────────────

TEST(DateTimeTicks, HoursAreClockTimesWithDateInCorner)
{
    const TickResult t = generate_datetime_ticks(T0, T0 + 6 * 3600.0, x_opts());
    ASSERT_GE(t.positions.size(), 3u);
    ASSERT_EQ(t.positions.size(), t.labels.size());
    for (size_t i = 0; i < t.positions.size(); ++i)
    {
        // Whole hours / minutes, labelled HH:MM.
        EXPECT_EQ(std::fmod(t.positions[i], 60.0), 0.0) << t.labels[i];
        EXPECT_TRUE(std::regex_match(t.labels[i], std::regex(R"([0-2][0-9]:[0-5][0-9])")))
            << t.labels[i];
    }
    EXPECT_EQ(t.offset_label, "2024-03-05");
}

TEST(DateTimeTicks, MidnightNamesTheDay)
{
    // 18:00 .. 06:00 next day: the 00:00 tick shows the new date.
    const double     lo = days_from_civil(2024, 3, 5) * 86400.0 + 18 * 3600.0;
    const TickResult t  = generate_datetime_ticks(lo, lo + 12 * 3600.0, x_opts());
    EXPECT_NE(std::find(t.labels.begin(), t.labels.end(), "Mar 06"), t.labels.end());
}

TEST(DateTimeTicks, DaysMonthsAndYears)
{
    // Two weeks: day ticks at midnight labelled "Mon DD", year in the corner.
    TickResult days = generate_datetime_ticks(T0, T0 + 14 * 86400.0, x_opts());
    ASSERT_GE(days.positions.size(), 2u);
    for (size_t i = 0; i < days.positions.size(); ++i)
    {
        EXPECT_EQ(std::fmod(days.positions[i], 86400.0), 0.0);
        EXPECT_TRUE(std::regex_match(days.labels[i], std::regex(R"([A-Z][a-z]{2} [0-3][0-9])")))
            << days.labels[i];
    }
    EXPECT_EQ(days.offset_label, "2024");

    // Fourteen months: month ticks on the 1st; January shows the year.
    TickResult months = generate_datetime_ticks(T0, T0 + 425 * 86400.0, x_opts(1200.0));
    ASSERT_GE(months.positions.size(), 3u);
    for (double p : months.positions)
        EXPECT_EQ(civil_from_epoch(p).day, 1);
    EXPECT_NE(std::find(months.labels.begin(), months.labels.end(), "2025"), months.labels.end());

    // Thirty years: January 1st ticks labelled with the year.
    TickResult years = generate_datetime_ticks(T0, T0 + 30 * 365.25 * 86400.0, x_opts());
    ASSERT_GE(years.positions.size(), 2u);
    for (size_t i = 0; i < years.positions.size(); ++i)
    {
        const CivilTime c = civil_from_epoch(years.positions[i]);
        EXPECT_EQ(c.month, 1);
        EXPECT_EQ(c.day, 1);
        EXPECT_EQ(years.labels[i], std::to_string(c.year));
    }
}

TEST(DateTimeTicks, SubSecondLabelsShowFractionalSeconds)
{
    const TickResult t = generate_datetime_ticks(T0 + 7.0, T0 + 7.5, x_opts());
    ASSERT_GE(t.positions.size(), 2u);
    for (const auto& l : t.labels)
        EXPECT_TRUE(std::regex_match(l, std::regex(R"([0-5][0-9]\.[0-9]+)"))) << l;
    EXPECT_EQ(t.offset_label, "2024-03-05 14:30");
}

TEST(DateTimeTicks, XLabelsDoNotCollide)
{
    // A narrow axis must widen the step until the labels fit side by side.
    for (double range : {60.0, 3600.0, 86400.0 * 3, 86400.0 * 90, 86400.0 * 3650})
    {
        const LinearTickOptions o = x_opts(240.0);
        const TickResult        t = generate_datetime_ticks(T0, T0 + range, o);
        ASSERT_FALSE(t.positions.empty()) << range;
        for (size_t i = 1; i < t.positions.size(); ++i)
        {
            const double gap_px = (t.positions[i] - t.positions[i - 1]) * 240.0 / range;
            const double need   = std::max(inter_text_width(t.labels[i], 13.0f),
                                         inter_text_width(t.labels[i - 1], 13.0f));
            EXPECT_GE(gap_px, need) << range << " " << t.labels[i];
        }
    }
}

TEST(DateTimeTicks, AxesOptionSwitchesTickLabels)
{
    Axes                ax;
    std::vector<double> x = {T0, T0 + 3 * 3600.0};
    std::vector<double> y = {0.0, 1.0};
    ax.plot(std::span<const double>(x), std::span<const double>(y));
    ax.xlim(T0, T0 + 3 * 3600.0);
    ax.set_viewport({0.0f, 0.0f, 800.0f, 400.0f});

    EXPECT_FALSE(ax.xaxis_datetime());
    const TickResult numeric = ax.compute_x_ticks();
    ASSERT_FALSE(numeric.labels.empty());
    EXPECT_TRUE(is_number(numeric.labels.front())) << numeric.labels.front();

    ax.xaxis_datetime(true);
    EXPECT_TRUE(ax.xaxis_datetime());
    const TickResult dt = ax.compute_x_ticks();
    ASSERT_GE(dt.labels.size(), 2u);
    EXPECT_NE(dt.labels.front().find(':'), std::string::npos) << dt.labels.front();
    EXPECT_EQ(dt.offset_label, "2024-03-05");

    // The y axis is independent.
    EXPECT_FALSE(ax.yaxis_datetime());
    EXPECT_TRUE(is_number(ax.compute_y_ticks().labels.front()));
}

// ─── Category bar ticks ─────────────────────────────────────────────────────

TEST(CategoryTicks, IntegerBarsGetOneTickPerCategory)
{
    Axes               ax;
    std::vector<float> pos = {1, 2, 3, 4, 5, 6};
    std::vector<float> h   = {3, 5, 2, 7, 4, 6};
    ax.bar(pos, h);
    ax.auto_fit();
    ax.set_viewport({70.0f, 40.0f, 710.0f, 500.0f});
    const TickResult t = ax.compute_x_ticks();
    EXPECT_EQ(t.positions, std::vector<double>({1, 2, 3, 4, 5, 6}));
}

TEST(CategoryTicks, NamedBarsGetTheirNames)
{
    Axes                     ax;
    std::vector<std::string> names = {"apples", "bananas", "cherries", "dates"};
    std::vector<float>       h     = {3, 5, 2, 7};
    auto&                    bars  = ax.bar(names, h);
    EXPECT_EQ(bars.categories(), names);
    EXPECT_EQ(bars.bar_positions(), std::vector<float>({0, 1, 2, 3}));
    ax.auto_fit();
    ax.set_viewport({70.0f, 40.0f, 710.0f, 500.0f});

    const TickResult t = ax.compute_x_ticks();
    EXPECT_EQ(t.positions, std::vector<double>({0, 1, 2, 3}));
    EXPECT_EQ(t.labels, names);
    EXPECT_TRUE(t.offset_label.empty());
    // The value axis stays numeric.
    EXPECT_TRUE(is_number(ax.compute_y_ticks().labels.front()));
}

TEST(CategoryTicks, HorizontalBarsNameTheYAxis)
{
    Axes               ax;
    std::vector<float> pos = {0, 1, 2};
    std::vector<float> h   = {4, 1, 3};
    ax.bar(pos, h).orientation(BarOrientation::Horizontal).categories({"north", "south", "east"});
    ax.auto_fit();
    ax.set_viewport({70.0f, 40.0f, 710.0f, 500.0f});
    const TickResult t = ax.compute_y_ticks();
    EXPECT_EQ(t.labels, std::vector<std::string>({"north", "south", "east"}));
    EXPECT_TRUE(is_number(ax.compute_x_ticks().labels.front()));
}

TEST(CategoryTicks, CrowdedNamesAreThinned)
{
    Axes                     ax;
    std::vector<std::string> names;
    std::vector<float>       h;
    for (int i = 0; i < 40; ++i)
    {
        names.push_back("category number " + std::to_string(i));
        h.push_back(static_cast<float>(i % 7));
    }
    ax.bar(names, h);
    ax.auto_fit();
    ax.set_viewport({70.0f, 40.0f, 600.0f, 400.0f});
    const TickResult t = ax.compute_x_ticks();
    ASSERT_GE(t.positions.size(), 2u);
    EXPECT_LT(t.positions.size(), 40u);
    EXPECT_EQ(t.labels.front(), "category number 0");
    const double px_per_unit = 600.0 / (ax.x_limits().max - ax.x_limits().min);
    const double gap_px      = (t.positions[1] - t.positions[0]) * px_per_unit;
    EXPECT_GE(gap_px, inter_text_width(t.labels.front(), 13.0f));
}

// ─── Left margin fits the y tick labels ─────────────────────────────────────

TEST(LeftMargin, ShortLabelsKeepTheStyleMargin)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.plot(std::vector<float>{0, 1, 2}, std::vector<float>{0, 1, 0.5f});
    ax.ylabel("value");
    fig.compute_layout();
    EXPECT_FLOAT_EQ(ax.viewport().x, fig.style().margin_left);
}

TEST(LeftMargin, LongLabelsGrowTheMarginAndAlignTheColumn)
{
    Figure fig({.width = 900, .height = 700});
    auto&  top = fig.subplot(2, 1, 1);
    top.bar(std::vector<float>{0, 1, 2}, std::vector<float>{1, 2, 3})
        .orientation(BarOrientation::Horizontal)
        .categories({"a fairly long category name", "short", "another long label"});
    top.ylabel("region");
    auto& bottom = fig.subplot(2, 1, 2);
    bottom.plot(std::vector<float>{0, 1}, std::vector<float>{0, 1});
    fig.compute_layout();

    const float need = left_label_extent(top);
    EXPECT_GT(need, fig.style().margin_left);
    // Room for the labels inside the figure, and both rows share the left edge.
    EXPECT_GE(top.viewport().x + 0.5f, need);
    EXPECT_FLOAT_EQ(bottom.viewport().x, top.viewport().x);
    // The right edge does not move.
    EXPECT_FLOAT_EQ(top.viewport().x + top.viewport().w, 900.0f - fig.style().margin_right);
}

TEST(LeftMargin, SvgTickLabelsStayInsideTheFigure)
{
    Figure fig({.width = 800, .height = 500});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.bar(std::vector<float>{0, 1}, std::vector<float>{2, 3})
        .orientation(BarOrientation::Horizontal)
        .categories({"Extremely long category label", "x"});
    ax.ylabel("category");
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    // <text x="..." ...>Extremely long category label</text>, right-aligned at x.
    const std::regex re(
        R"re(<text x="([0-9.\-]+)"[^>]*text-anchor="end"[^>]*>Extremely long category label<)re");
    std::smatch m;
    ASSERT_TRUE(std::regex_search(svg, m, re)) << "category tick label missing from SVG";
    const float right = std::stof(m[1].str());
    EXPECT_GE(right - inter_text_width("Extremely long category label", 13.0f), 0.0f);
}

// ─── Legend placement ───────────────────────────────────────────────────────

TEST(LegendBest, SparseLineThroughCornerCounts)
{
    // Two points outside the view; the segment between them crosses the
    // top-right corner.  Point sampling alone never saw it.
    Axes ax;
    ax.plot(std::vector<float>{6.0f, 11.0f}, std::vector<float>{11.0f, 6.0f}).label("diag");
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    const Rect vp{0.0f, 0.0f, 500.0f, 500.0f};
    ax.set_viewport(vp);
    const size_t tr =
        legend_layout::overlap_score(ax, vp, LegendPosition::TopRight, 100.0f, 60.0f, 12.0f);
    EXPECT_GT(tr, 10u);
    EXPECT_NE(legend_layout::choose_best_corner(ax, vp, 100.0f, 60.0f, 12.0f),
              LegendPosition::TopRight);
}

TEST(LegendBest, MarkerSizeCountsNearTheBox)
{
    // A large marker just outside the box still covers it.
    Axes ax;
    ax.scatter(std::vector<float>{9.9f}, std::vector<float>{9.9f}).size(40.0f).label("m");
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 10.0);
    const Rect vp{0.0f, 0.0f, 500.0f, 500.0f};
    ax.set_viewport(vp);
    // Box at bottom-right never contains the point; top-right does via radius.
    EXPECT_GT(legend_layout::overlap_score(ax, vp, LegendPosition::TopRight, 60.0f, 40.0f, 30.0f),
              0u);
}

TEST(LegendOutside, ReservesAStripRightOfThePlot)
{
    auto make = [](LegendPosition pos)
    {
        auto  fig = std::make_unique<Figure>(FigureConfig{.width = 800, .height = 600});
        auto& ax  = fig->subplot(1, 1, 1);
        ax.plot(std::vector<float>{0, 1}, std::vector<float>{0, 1}).label("first series");
        ax.plot(std::vector<float>{0, 1}, std::vector<float>{1, 0}).label("second");
        fig->legend().position = pos;
        fig->compute_layout();
        return fig;
    };
    auto        inside  = make(LegendPosition::TopRight);
    auto        outside = make(LegendPosition::OutsideRight);
    const Rect& vin     = inside->axes()[0]->viewport();
    const Rect& vout    = outside->axes()[0]->viewport();
    EXPECT_EQ(inside->axes()[0]->outside_legend_area().w, 0.0f);
    EXPECT_LT(vout.w, vin.w);
    EXPECT_FLOAT_EQ(vout.x, vin.x);

    const AxesBase& ax   = *outside->axes()[0];
    const Rect      area = legend_layout::legend_area(ax, LegendPosition::OutsideRight);
    EXPECT_GE(area.x, vout.x + vout.w);
    EXPECT_LE(area.x + area.w, 800.0f);
    // A one-column legend of the two labels fits the strip.
    const float label_w = inter_text_width("first series", legend_layout::OUTSIDE_AUTO_FONT_PX);
    EXPECT_GE(area.w, 2.0f * (8.0f + 2.0f) + 28.0f + label_w);
    const Rect box = legend_layout::place(area, LegendPosition::OutsideRight, 120.0f, 50.0f);
    EXPECT_FLOAT_EQ(box.x, area.x);
    EXPECT_FLOAT_EQ(box.y, vout.y);

    // No labelled series: nothing is reserved.
    Figure plain({.width = 800, .height = 600});
    auto&  pax = plain.subplot(1, 1, 1);
    pax.plot(std::vector<float>{0, 1}, std::vector<float>{0, 1});
    plain.legend().position = LegendPosition::OutsideRight;
    plain.compute_layout();
    EXPECT_FLOAT_EQ(pax.viewport().w, vin.w);
}

TEST(LegendOutside, SvgDrawsTheLegendOutsideThePlot)
{
    Figure fig({.width = 800, .height = 600});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.plot(std::vector<float>{0, 1}, std::vector<float>{0, 1}).label("outside label");
    fig.legend().position = LegendPosition::OutsideRight;
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    const auto        g   = svg.find("<g class=\"legend\">");
    ASSERT_NE(g, std::string::npos);
    const std::regex  re(R"re(<rect x="([0-9.\-]+)")re");
    std::smatch       m;
    const std::string tail = svg.substr(g);
    ASSERT_TRUE(std::regex_search(tail, m, re));
    EXPECT_GE(std::stof(m[1].str()), ax.viewport().x + ax.viewport().w);
}

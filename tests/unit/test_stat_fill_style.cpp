// Flat stat fills (bars, histograms, box plots, violins), their fill/edge
// style setters, and sticky baselines in autoscale.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/series_stats.hpp>
#include <vector>

#include "render/stat_fill_style.hpp"

using namespace spectra;

namespace
{
constexpr float DARK_CANVAS  = 0.01f;
constexpr float LIGHT_CANVAS = 0.95f;

std::vector<float> samples()
{
    std::vector<float> v;
    for (int i = 0; i < 200; ++i)
        v.push_back(std::sin(static_cast<float>(i) * 0.37f) * 10.0f + 50.0f);
    return v;
}

// True when every vertex of a {x, y, alpha} fill buffer has alpha 1 (flat fill).
bool fill_is_flat(std::span<const float> verts)
{
    for (size_t i = 2; i < verts.size(); i += 3)
    {
        if (verts[i] != 1.0f)
            return false;
    }
    return !verts.empty();
}

template <typename S>
void expect_default_style(const S& s)
{
    EXPECT_FALSE(s.gradient());
    EXPECT_FLOAT_EQ(s.fill_alpha(), STAT_DEFAULT_FILL_ALPHA);
    EXPECT_FLOAT_EQ(s.edge_width(), STAT_DEFAULT_EDGE_WIDTH);
    EXPECT_FALSE(s.edge_color().has_value());
    EXPECT_GE(s.fill_alpha(), 0.85f);
    EXPECT_LE(s.fill_alpha(), 0.9f);
}
}   // namespace

// ─── Defaults ───────────────────────────────────────────────────────────────

TEST(StatFillStyle, DefaultsAreFlatAndOpaque)
{
    const auto         v = samples();
    std::vector<float> pos{1, 2, 3}, h{3, 5, 2};

    BarSeries bars(pos, h);
    expect_default_style(bars);
    EXPECT_TRUE(fill_is_flat(bars.fill_verts()));

    HistogramSeries hist(v, 20);
    expect_default_style(hist);
    EXPECT_TRUE(fill_is_flat(hist.fill_verts()));

    BoxPlotSeries box;
    box.add_box(1.0f, v);
    expect_default_style(box);
    EXPECT_TRUE(fill_is_flat(box.fill_verts()));

    ViolinSeries violin;
    violin.add_violin(1.0f, v);
    expect_default_style(violin);
    EXPECT_TRUE(fill_is_flat(violin.fill_verts()));
}

TEST(StatFillStyle, GradientIsOptIn)
{
    std::vector<float> pos{1, 2}, h{3, 5};
    BarSeries          bars(pos, h);
    bars.gradient(true);
    EXPECT_TRUE(bars.gradient());
    EXPECT_FALSE(fill_is_flat(bars.fill_verts()));

    const auto      v = samples();
    HistogramSeries hist(v, 10);
    hist.gradient(true);
    EXPECT_FALSE(fill_is_flat(hist.fill_verts()));
}

// ─── Setters ────────────────────────────────────────────────────────────────

TEST(StatFillStyle, SettersAreFluentAndClamped)
{
    std::vector<float> pos{1, 2}, h{3, 5};
    BarSeries          bars(pos, h);
    bars.fill_alpha(0.5f).edge_width(2.5f).edge_color(colors::red).label("b");
    EXPECT_FLOAT_EQ(bars.fill_alpha(), 0.5f);
    EXPECT_FLOAT_EQ(bars.edge_width(), 2.5f);
    ASSERT_TRUE(bars.edge_color().has_value());
    EXPECT_FLOAT_EQ(bars.edge_color()->r, 1.0f);
    EXPECT_EQ(bars.label(), "b");

    bars.fill_alpha(4.0f).edge_width(-1.0f);
    EXPECT_FLOAT_EQ(bars.fill_alpha(), 1.0f);
    EXPECT_FLOAT_EQ(bars.edge_width(), 0.0f);
    bars.fill_alpha(-0.2f);
    EXPECT_FLOAT_EQ(bars.fill_alpha(), 0.0f);
    bars.fill_alpha(std::numeric_limits<float>::quiet_NaN())
        .edge_width(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(bars.fill_alpha(), 1.0f);
    EXPECT_FLOAT_EQ(bars.edge_width(), 0.0f);

    bars.edge_color(std::nullopt);
    EXPECT_FALSE(bars.edge_color().has_value());
}

TEST(StatFillStyle, AllStatSeriesShareTheSetters)
{
    const auto      v = samples();
    HistogramSeries hist(v, 10);
    hist.fill_alpha(0.3f).edge_width(0.0f).edge_color(colors::black);
    BoxPlotSeries box;
    box.fill_alpha(0.6f).edge_width(1.5f);
    ViolinSeries violin;
    violin.fill_alpha(0.7f).edge_color(colors::white);

    EXPECT_FLOAT_EQ(hist.fill_alpha(), 0.3f);
    EXPECT_FLOAT_EQ(hist.edge_width(), 0.0f);
    EXPECT_TRUE(hist.edge_color().has_value());
    EXPECT_FLOAT_EQ(box.fill_alpha(), 0.6f);
    EXPECT_FLOAT_EQ(box.edge_width(), 1.5f);
    EXPECT_FLOAT_EQ(violin.fill_alpha(), 0.7f);
    ASSERT_TRUE(violin.edge_color().has_value());
    EXPECT_FLOAT_EQ(violin.edge_color()->g, 1.0f);

    // fill_style() copies the whole style in one call.
    BarSeries copy;
    copy.fill_style(hist.fill_style());
    EXPECT_FLOAT_EQ(copy.fill_alpha(), 0.3f);
    EXPECT_FLOAT_EQ(copy.edge_width(), 0.0f);
    EXPECT_TRUE(copy.edge_color().has_value());
}

TEST(StatFillStyle, StyleSettersMarkSeriesDirty)
{
    std::vector<float> pos{1}, h{1};
    BarSeries          bars(pos, h);
    bars.clear_dirty();
    bars.edge_width(2.0f);
    EXPECT_TRUE(bars.is_dirty());
}

// ─── Geometry ───────────────────────────────────────────────────────────────

TEST(StatFillStyle, BoxMediansComeLastForTheirOwnColour)
{
    const auto    v = samples();
    BoxPlotSeries box;
    box.add_box(1.0f, v).add_box(2.0f, v);
    const size_t first = box.median_first_point();
    ASSERT_LT(first, box.point_count());
    // Two medians: (x0, x1, NaN) each.
    ASSERT_EQ(box.point_count() - first, 6u);
    EXPECT_TRUE(std::isnan(box.x_data()[first - 1]));   // outline ends with a break
    for (size_t k = 0; k < 2; ++k)
    {
        const float y = box.y_data()[first + 3 * k];
        EXPECT_FLOAT_EQ(y, box.stats()[k].median);
        EXPECT_FLOAT_EQ(box.y_data()[first + 3 * k + 1], y);
        EXPECT_FLOAT_EQ(box.x_data()[first + 3 * k + 1] - box.x_data()[first + 3 * k],
                        box.box_width());
    }

    BoxPlotSeries empty;
    EXPECT_EQ(empty.median_first_point(), 0u);
}

TEST(StatFillStyle, HistogramBinsAreSeparatedByEdges)
{
    std::vector<float> data = {0.5f, 1.5f, 1.6f, 2.5f, 2.6f, 2.7f};
    HistogramSeries    hist(data, 3);
    // Outline still ends on the baseline.
    EXPECT_FLOAT_EQ(hist.y_data()[hist.point_count() - 1], 0.0f);

    // One vertical edge per interior bin boundary, from the shorter bin to 0.
    const auto& edges  = hist.bin_edges();
    const auto& counts = hist.bin_counts();
    for (int i = 1; i < hist.bins(); ++i)
    {
        bool found = false;
        for (size_t k = 1; k + 1 < hist.point_count(); ++k)
        {
            if (std::isnan(hist.x_data()[k - 1]) && hist.x_data()[k] == edges[i]
                && hist.x_data()[k + 1] == edges[i] && hist.y_data()[k + 1] == 0.0f)
            {
                EXPECT_FLOAT_EQ(hist.y_data()[k], std::min(counts[i - 1], counts[i]));
                found = true;
            }
        }
        EXPECT_TRUE(found) << "no edge at bin boundary " << i;
    }
}

// ─── Colours ────────────────────────────────────────────────────────────────

TEST(StatFillStyle, DefaultEdgeIsThemeAwareShadeOfTheFill)
{
    const Color blue{0.2f, 0.4f, 0.8f, 1.0f};

    const Color on_light = stat_default_edge_color(blue, LIGHT_CANVAS);
    EXPECT_LT(on_light.r, blue.r);
    EXPECT_LT(on_light.g, blue.g);
    EXPECT_LT(on_light.b, blue.b);

    const Color on_dark = stat_default_edge_color(blue, DARK_CANVAS);
    EXPECT_GT(on_dark.r, blue.r);
    EXPECT_GT(on_dark.g, blue.g);
    EXPECT_GT(on_dark.b, blue.b);

    // A near-black fill on a light canvas gets a lighter edge (a darker one
    // would vanish into the fill), and a near-white fill on a dark canvas a
    // darker one.
    const Color black_edge = stat_default_edge_color(Color{0.02f, 0.02f, 0.02f}, LIGHT_CANVAS);
    EXPECT_GT(black_edge.r, 0.02f);
    const Color white_edge = stat_default_edge_color(Color{0.97f, 0.97f, 0.97f}, DARK_CANVAS);
    EXPECT_LT(white_edge.r, 0.97f);
}

TEST(StatFillStyle, ExplicitEdgeColourWins)
{
    StatFillStyle st;
    st.edge_color   = Color{0.1f, 0.9f, 0.1f, 1.0f};
    const Color out = stat_edge_color(st, Color{0.5f, 0.5f, 0.5f}, LIGHT_CANVAS);
    EXPECT_FLOAT_EQ(out.g, 0.9f);
}

TEST(StatFillStyle, MedianContrastsWithTheBoxFill)
{
    const Color white_bg{1.0f, 1.0f, 1.0f};
    const Color dark_bg{0.05f, 0.05f, 0.07f};

    // Light fill: dark ink.  Dark fill: white ink.
    const Color on_yellow = stat_median_color(Color{0.95f, 0.85f, 0.2f}, 0.88f, white_bg);
    EXPECT_LT(stat_relative_luminance(on_yellow), 0.1f);
    const Color on_navy = stat_median_color(Color{0.1f, 0.1f, 0.4f}, 0.88f, white_bg);
    EXPECT_FLOAT_EQ(on_navy.r, 1.0f);

    // A faint fill shows the canvas: the ink follows the canvas.
    const Color faint_on_dark = stat_median_color(Color{0.9f, 0.9f, 0.9f}, 0.05f, dark_bg);
    EXPECT_FLOAT_EQ(faint_on_dark.r, 1.0f);

    EXPECT_GT(stat_median_width(1.0f), 1.0f);
}

// ─── Sticky baselines ───────────────────────────────────────────────────────

TEST(StatStickyBaseline, BarsStandOnTheirBaseline)
{
    Axes               ax;
    std::vector<float> pos{1, 2, 3}, h{3, 5, 2};
    ax.bar(pos, h);
    const auto y = ax.y_limits();
    EXPECT_DOUBLE_EQ(y.min, 0.0);   // no padding below the baseline
    EXPECT_GT(y.max, 5.0);          // padding above stays
    const auto x = ax.x_limits();
    EXPECT_LT(x.min, 0.6);   // the bar axis keeps its padding
    EXPECT_GT(x.max, 3.4);
}

TEST(StatStickyBaseline, NegativeBarsHangFromTheirBaseline)
{
    Axes               ax;
    std::vector<float> pos{1, 2}, h{-3, -5};
    ax.bar(pos, h);
    const auto y = ax.y_limits();
    EXPECT_DOUBLE_EQ(y.max, 0.0);
    EXPECT_LT(y.min, -5.0);
}

TEST(StatStickyBaseline, ConstantBarsAtBaselineKeepASpan)
{
    Axes               ax;
    std::vector<float> pos{1, 2}, h{0, 0};
    ax.bar(pos, h);
    const auto y = ax.y_limits();
    EXPECT_DOUBLE_EQ(y.min, 0.0);
    EXPECT_GT(y.max, 0.0);
}

TEST(StatStickyBaseline, MixedSignBarsArePaddedBothWays)
{
    Axes               ax;
    std::vector<float> pos{1, 2}, h{-3, 5};
    ax.bar(pos, h);
    const auto y = ax.y_limits();
    EXPECT_LT(y.min, -3.0);
    EXPECT_GT(y.max, 5.0);
}

TEST(StatStickyBaseline, CustomBaselineAndHorizontalBars)
{
    Axes               ax;
    std::vector<float> pos{1, 2}, h{8, 9};
    ax.bar(pos, h).baseline(5.0f);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 5.0);

    Axes ah;
    ah.bar(pos, h).orientation(BarOrientation::Horizontal);
    EXPECT_DOUBLE_EQ(ah.x_limits().min, 0.0);
    EXPECT_GT(ah.x_limits().max, 9.0);
    EXPECT_LT(ah.y_limits().min, 0.6);   // the category axis is padded
}

TEST(StatStickyBaseline, HistogramAndStem)
{
    const auto v = samples();
    Axes       ah;
    ah.histogram(v, 20);
    EXPECT_DOUBLE_EQ(ah.y_limits().min, 0.0);
    EXPECT_GT(ah.y_limits().max, 0.0);

    Axes               as;
    std::vector<float> x{1, 2, 3}, y{2, 4, 3};
    as.stem(x, y).baseline(1.0f);
    EXPECT_DOUBLE_EQ(as.y_limits().min, 1.0);
}

TEST(StatStickyBaseline, BandFilledToAConstant)
{
    Axes               ax;
    std::vector<float> x{0, 1, 2}, lo{0, 0, 0}, hi{1, 3, 2};
    ax.band(x, lo, hi);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, 0.0);
    EXPECT_GT(ax.y_limits().max, 3.0);

    // A varying envelope is padded as usual.
    Axes               ab;
    std::vector<float> lo2{0, 0.5f, 0.2f};
    ab.band(x, lo2, hi);
    EXPECT_LT(ab.y_limits().min, 0.0);
}

TEST(StatStickyBaseline, DataBelowTheBaselineKeepsPadding)
{
    Axes               ax;
    std::vector<float> pos{1, 2}, h{3, 5};
    ax.bar(pos, h);
    std::vector<float> lx{0, 3}, ly{-2, 1};
    ax.line(lx, ly);
    EXPECT_LT(ax.y_limits().min, -2.0);
}

TEST(StatStickyBaseline, OnlyLinearPaddedModesStick)
{
    std::vector<float> pos{1, 2}, h{3, 5};

    Axes tight;
    tight.bar(pos, h);
    tight.autoscale_mode(AutoscaleMode::Tight);
    EXPECT_DOUBLE_EQ(tight.y_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(tight.y_limits().max, 5.0);

    // Box plots have no baseline: padded below their lowest whisker too.
    Axes       bx;
    const auto v = samples();
    auto&      b = bx.box_plot();
    b.add_box(1.0f, v);
    EXPECT_LT(bx.y_limits().min, b.stats()[0].whisker_low);
}

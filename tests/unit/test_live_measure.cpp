// Live pill state, Measure tool maths (snap, deltas on linear / log axes,
// number formatting), data tip stepping / dragging and legend solo.

#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/overlay/axis_mapping.hpp"
#include "ui/overlay/legend_solo.hpp"
#include "ui/overlay/live_status.hpp"
#include "ui/overlay/measure_math.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/overlay/data_marker.hpp"
#endif

using namespace spectra;

namespace
{

constexpr Rect VP{100.0f, 50.0f, 800.0f, 400.0f};

struct Plot
{
    Figure fig{{.width = 1000, .height = 500}};
    Axes&  ax;

    Plot() : ax(fig.subplot(1, 1, 1)) { ax.set_viewport(VP); }
};

}   // namespace

// ─── Snap ───────────────────────────────────────────────────────────────────

TEST(MeasureSnap, SnapsToNearestSampleWithinRadius)
{
    Plot                     p;
    const std::vector<float> x{0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    const std::vector<float> y{0.0f, 1.0f, 4.0f, 9.0f, 16.0f};
    p.ax.line(x, y);
    p.ax.xlim(0.0, 4.0);
    p.ax.ylim(0.0, 16.0);
    const AxisMapping map(p.ax);

    // 5 px right of the sample (2, 4): snaps to it.
    const double sx = map.to_screen_x(2.0) + 5.0;
    const double sy = map.to_screen_y(4.0);
    const auto   s  = measure::snap_to_data(p.ax, map, sx, sy, 12.0);
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.index, 2u);
    EXPECT_DOUBLE_EQ(s.x, 2.0);
    EXPECT_DOUBLE_EQ(s.y, 4.0);
    EXPECT_NEAR(s.distance_px, 5.0, 1e-6);

    // 40 px away from every sample: no snap.
    EXPECT_FALSE(measure::snap_to_data(p.ax, map, sx + 40.0, sy + 40.0, 12.0).found);
}

TEST(MeasureSnap, SkipsHiddenSeries)
{
    Plot                     p;
    const std::vector<float> x{0.0f, 1.0f};
    const std::vector<float> y{0.0f, 1.0f};
    p.ax.line(x, y).visible(false);
    p.ax.xlim(0.0, 1.0);
    p.ax.ylim(0.0, 1.0);
    const AxisMapping map(p.ax);
    EXPECT_FALSE(
        measure::snap_to_data(p.ax, map, map.to_screen_x(1.0), map.to_screen_y(1.0), 12.0).found);
}

TEST(MeasureSnap, LogAxisSnapsToDrawnPosition)
{
    // On a log10 x axis [1, 1000] the sample x = 10 is drawn a third of the
    // way across, not at 1 % as a linear mapping would put it.
    Plot p;
    p.ax.xscale(ScaleType::Log10);
    const std::vector<float> x{1.0f, 10.0f, 100.0f, 1000.0f};
    const std::vector<float> y{1.0f, 2.0f, 3.0f, 4.0f};
    p.ax.line(x, y);
    p.ax.xlim(1.0, 1000.0);
    p.ax.ylim(0.0, 5.0);
    const AxisMapping map(p.ax);
    const double      third = VP.x + VP.w / 3.0;
    const auto        s = measure::snap_to_data(p.ax, map, third + 3.0, map.to_screen_y(2.0), 12.0);
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.index, 1u);
    EXPECT_DOUBLE_EQ(s.x, 10.0);
}

TEST(MeasureSnap, LargeSortedSeriesUsesWindowAndStillFindsPoint)
{
    Plot               p;
    std::vector<float> x(10000);
    std::vector<float> y(10000);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>(i % 7);
    }
    p.ax.line(x, y);
    p.ax.xlim(5000.0, 5010.0);
    p.ax.ylim(0.0, 7.0);
    const AxisMapping map(p.ax);
    const auto        s = measure::snap_to_data(p.ax,
                                         map,
                                         map.to_screen_x(5003.0) + 2.0,
                                         map.to_screen_y(5003 % 7),
                                         12.0);
    ASSERT_TRUE(s.found);
    EXPECT_EQ(s.index, 5003u);
}

// ─── Measurement ────────────────────────────────────────────────────────────

TEST(MeasureMath, AxisUnitFromLabel)
{
    EXPECT_EQ(measure::axis_unit("Time (s)"), "s");
    EXPECT_EQ(measure::axis_unit("Voltage [mV]"), "mV");
    EXPECT_EQ(measure::axis_unit("Speed (m/s)"), "m/s");
    EXPECT_EQ(measure::axis_unit("Force (N (net))"), "N (net)");
    EXPECT_EQ(measure::axis_unit("X Axis"), "");
    EXPECT_EQ(measure::axis_unit("(only brackets)"), "");
    EXPECT_EQ(measure::axis_unit(""), "");
}

TEST(MeasureMath, DifferentUnitsGiveNoDistance)
{
    const auto m = measure::compute_measurement(1.0,
                                                10.0,
                                                4.0,
                                                -2.0,
                                                ScaleType::Linear,
                                                ScaleType::Linear,
                                                "s",
                                                "V");
    EXPECT_DOUBLE_EQ(m.dx, 3.0);
    EXPECT_DOUBLE_EQ(m.dy, -12.0);
    ASSERT_TRUE(m.slope_valid);
    EXPECT_DOUBLE_EQ(m.slope, -4.0);
    EXPECT_FALSE(m.distance_valid);   // seconds and volts must not be mixed

    // Unknown units: no distance either.
    EXPECT_FALSE(
        measure::compute_measurement(0, 0, 3, 4, ScaleType::Linear, ScaleType::Linear, "", "")
            .distance_valid);
}

TEST(MeasureMath, SameUnitGivesDistance)
{
    const auto m = measure::compute_measurement(0.0,
                                                0.0,
                                                3.0,
                                                4.0,
                                                ScaleType::Linear,
                                                ScaleType::Linear,
                                                "m",
                                                "m");
    ASSERT_TRUE(m.distance_valid);
    EXPECT_DOUBLE_EQ(m.distance, 5.0);
}

TEST(MeasureMath, VerticalMeasurementHasNoSlope)
{
    const auto m = measure::compute_measurement(2.0,
                                                0.0,
                                                2.0,
                                                4.0,
                                                ScaleType::Linear,
                                                ScaleType::Linear,
                                                "",
                                                "");
    EXPECT_FALSE(m.slope_valid);
    EXPECT_DOUBLE_EQ(m.dy, 4.0);
}

TEST(MeasureMath, LogAxesMeasureInDataSpaceWithRatio)
{
    // log10 y from 10 to 1000: dy is the data difference (990), plus ×100.
    const auto m = measure::compute_measurement(1.0,
                                                10.0,
                                                2.0,
                                                1000.0,
                                                ScaleType::Linear,
                                                ScaleType::Log10,
                                                "s",
                                                "Pa");
    EXPECT_DOUBLE_EQ(m.dy, 990.0);
    ASSERT_TRUE(m.y_ratio_valid);
    EXPECT_DOUBLE_EQ(m.y_ratio, 100.0);
    EXPECT_FALSE(m.x_ratio_valid);
    EXPECT_FALSE(m.distance_valid);   // never a length across a log axis

    const auto lines = measure::measurement_lines(
        m,
        {.x_unit = "s", .y_unit = "Pa", .xscale = ScaleType::Linear, .yscale = ScaleType::Log10});
    ASSERT_GE(lines.size(), 3u);
    EXPECT_NE(lines[1].find("990 Pa"), std::string::npos) << lines[1];
    EXPECT_NE(lines[1].find("\xC3\x97"
                            "100"),
              std::string::npos)
        << lines[1];
    EXPECT_NE(lines[1].find("2.00 dec"), std::string::npos) << lines[1];
    EXPECT_NE(lines[2].find("Pa/s"), std::string::npos) << lines[2];
}

TEST(MeasureMath, LinesCarryUnitsPerAxis)
{
    const auto m     = measure::compute_measurement(1.0,
                                                10.0,
                                                4.3677,
                                                -193.229,
                                                ScaleType::Linear,
                                                ScaleType::Linear,
                                                "s",
                                                "V");
    const auto lines = measure::measurement_lines(
        m,
        {.x_unit = "s", .y_unit = "V", .x_resolution = 0.01, .y_resolution = 0.5});
    ASSERT_EQ(lines.size(), 3u);   // Δx, Δy, slope — no mixed-unit distance
    EXPECT_EQ(lines[0], "\xCE\x94x  3.37 s");
    EXPECT_EQ(lines[1], "\xCE\x94y  -203 V");
    EXPECT_EQ(lines[2], "slope  -60.35 V/s");
}

TEST(MeasureMath, FormatValueFollowsResolution)
{
    EXPECT_EQ(measure::format_value(3.3677, 0.01), "3.37");
    EXPECT_EQ(measure::format_value(-203.229, 0.5), "-203");
    EXPECT_EQ(measure::format_value(0.000123, 0.0), "0.000123");
    EXPECT_EQ(measure::format_value(1234567.0, 0.0), "1234567");
    EXPECT_EQ(measure::format_value(1.7e9, 100.0), "1700000000");
    EXPECT_EQ(measure::format_value(0.001, 0.1), "0");
    EXPECT_EQ(measure::format_value(2.5e-7, 1e-9), "2.5e-07");
    EXPECT_EQ(measure::format_value(std::nan(""), 0.0), "nan");
}

TEST(MeasureMath, PixelResolutionGrowsOnLogAxis)
{
    // log10 [1, 1000] over 300 px: one decade per 100 px.
    const double at_10  = measure::pixel_resolution(ScaleType::Log10, {1.0, 1000.0}, 300.0, 10.0);
    const double at_100 = measure::pixel_resolution(ScaleType::Log10, {1.0, 1000.0}, 300.0, 100.0);
    EXPECT_NEAR(at_10, 10.0 * (std::pow(10.0, 0.01) - 1.0), 1e-9);
    EXPECT_NEAR(at_100 / at_10, 10.0, 1e-9);
    EXPECT_DOUBLE_EQ(measure::pixel_resolution(ScaleType::Linear, {0.0, 8.0}, 800.0, 3.0), 0.01);
    EXPECT_EQ(measure::pixel_resolution(ScaleType::Log10, {1.0, 1000.0}, 300.0, -1.0), 0.0);
}

TEST(MeasureMath, FormatFromAxesUsesLabelsAndDatetime)
{
    Plot p;
    p.ax.xlabel("Time");
    p.ax.ylabel("Pressure [kPa]");
    p.ax.xaxis_datetime(true);
    p.ax.xlim(0.0, 800.0);
    p.ax.ylim(0.0, 4.0);
    const auto f = measure::measure_format(p.ax, 10.0, 1.0);
    EXPECT_EQ(f.x_unit, "s");
    EXPECT_EQ(f.y_unit, "kPa");
    EXPECT_DOUBLE_EQ(f.x_resolution, 1.0);
    EXPECT_DOUBLE_EQ(f.y_resolution, 0.01);
}

// ─── Data tip navigation ────────────────────────────────────────────────────

TEST(DataTipNav, StepIndexClampsAtEnds)
{
    LineSeries s(std::vector<float>{0, 1, 2, 3}, std::vector<float>{0, 1, 2, 3});
    EXPECT_EQ(measure::step_index(s, 1, 1).value(), 2u);
    EXPECT_EQ(measure::step_index(s, 1, -1).value(), 0u);
    EXPECT_FALSE(measure::step_index(s, 3, 1).has_value());
    EXPECT_FALSE(measure::step_index(s, 0, -1).has_value());
    EXPECT_EQ(measure::step_index(s, 0, 10).value(), 3u);
}

TEST(DataTipNav, DragIndexFollowsCursorX)
{
    Plot                     p;
    const std::vector<float> x{0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    const std::vector<float> y{5.0f, 0.0f, 5.0f, 0.0f, 5.0f};
    auto&                    s = p.ax.line(x, y);
    p.ax.xlim(0.0, 4.0);
    p.ax.ylim(0.0, 5.0);
    const AxisMapping map(p.ax);
    // Anywhere vertically, x = 2.8 picks sample 3.
    EXPECT_EQ(measure::drag_index(s, map, map.to_screen_x(2.8), VP.y).value(), 3u);
    EXPECT_EQ(measure::drag_index(s, map, map.to_screen_x(2.2), VP.y + VP.h).value(), 2u);
    // Past the ends: clamped to the first / last sample.
    EXPECT_EQ(measure::drag_index(s, map, VP.x - 50.0, VP.y).value(), 0u);
    EXPECT_EQ(measure::drag_index(s, map, VP.x + VP.w + 50.0, VP.y).value(), 4u);
}

TEST(DataTipNav, PointDerivative)
{
    LineSeries s(std::vector<float>{0, 1, 2}, std::vector<float>{0, 2, 6});
    float      d = 0.0f;
    ASSERT_TRUE(measure::point_derivative(s, 1, d));
    EXPECT_FLOAT_EQ(d, 3.0f);
    ASSERT_TRUE(measure::point_derivative(s, 0, d));
    EXPECT_FLOAT_EQ(d, 2.0f);
}

TEST(DataTipNav, PointDerivativeNeedsACurveWithMonotonicX)
{
    float d = 0.0f;
    // Scatter samples are unordered: no slope (QA M11).
    ScatterSeries sc(std::vector<float>{0, 1, 2}, std::vector<float>{0, 2, 6});
    EXPECT_FALSE(measure::point_derivative(sc, 1, d));
    // A line that turns back in x at the sample has no dy/dx there...
    LineSeries loop(std::vector<float>{0, 2, 1}, std::vector<float>{0, 1, 2});
    EXPECT_FALSE(measure::point_derivative(loop, 1, d));
    // ...but decreasing x throughout is fine.
    LineSeries rev(std::vector<float>{2, 1, 0}, std::vector<float>{6, 2, 0});
    ASSERT_TRUE(measure::point_derivative(rev, 1, d));
    EXPECT_FLOAT_EQ(d, 3.0f);
}

#ifdef SPECTRA_USE_IMGUI
TEST(DataTipNav, MarkerSelectionAndMove)
{
    LineSeries        s(std::vector<float>{0, 1, 2}, std::vector<float>{0, 10, 20});
    DataMarkerManager mgr;
    mgr.add(0.0, 0.0, &s, 0);
    mgr.add(2.0, 20.0, &s, 2);
    EXPECT_EQ(mgr.selected_count(), 0u);

    mgr.select_only(1);
    EXPECT_TRUE(mgr.markers()[1].selected);
    EXPECT_FALSE(mgr.markers()[0].selected);
    mgr.toggle_selected(0);
    EXPECT_EQ(mgr.selected_count(), 2u);

    ASSERT_TRUE(mgr.move_to_point(0, 1));
    EXPECT_EQ(mgr.markers()[0].point_index, 1u);
    EXPECT_DOUBLE_EQ(mgr.markers()[0].data_x, 1.0);
    EXPECT_DOUBLE_EQ(mgr.markers()[0].data_y, 10.0);
    EXPECT_TRUE(mgr.markers()[0].dy_dx_valid);
    EXPECT_FLOAT_EQ(mgr.markers()[0].dy_dx, 10.0f);
    EXPECT_FALSE(mgr.move_to_point(0, 7));   // no such sample

    EXPECT_TRUE(mgr.clear_selection());
    EXPECT_EQ(mgr.selected_count(), 0u);
    EXPECT_FALSE(mgr.clear_selection());
}
#endif

// ─── Live status ────────────────────────────────────────────────────────────

TEST(LiveStatus, FollowingThenPausedBehind)
{
    Plot  p;
    auto& line = p.ax.line();
    for (int i = 0; i <= 20; ++i)
        line.append(static_cast<float>(i), 0.0f);
    p.ax.presented_buffer(5.0f);

    auto st = ui::live_status(p.ax);
    EXPECT_TRUE(st.live);
    EXPECT_TRUE(st.following);
    EXPECT_EQ(ui::live_pill_label(st), "Following");
    ASSERT_TRUE(p.ax.presented_buffer_head().has_value());
    EXPECT_DOUBLE_EQ(*p.ax.presented_buffer_head(), 20.0);

    // Pause, then 12 more seconds of data arrive.
    ui::pause_live_follow(p.ax);
    for (int i = 21; i <= 32; ++i)
        line.append(static_cast<float>(i), 0.0f);
    st = ui::live_status(p.ax);
    EXPECT_FALSE(st.following);
    EXPECT_DOUBLE_EQ(st.behind_seconds, 12.0);
    EXPECT_EQ(ui::live_pill_label(st), "Paused \xC2\xB7 12 s behind");

    ui::resume_live_follow(p.ax);
    st = ui::live_status(p.ax);
    EXPECT_TRUE(st.following);
    EXPECT_DOUBLE_EQ(p.ax.x_limits().max, 32.0);
}

TEST(LiveStatus, PinnedRightEdgeIsTheHead)
{
    Plot  p;
    auto& line = p.ax.line();
    line.append(0.0f, 0.0f);
    line.append(1.0f, 0.0f);
    p.ax.presented_buffer(10.0f);
    p.ax.set_presented_buffer_right_edge(100.0);
    EXPECT_DOUBLE_EQ(*p.ax.presented_buffer_head(), 100.0);
    p.ax.xlim(40.0, 50.0);   // a pan pauses
    EXPECT_DOUBLE_EQ(ui::live_status(p.ax).behind_seconds, 50.0);
}

TEST(LiveStatus, NotLiveAndPannedAhead)
{
    Plot p;
    EXPECT_FALSE(ui::live_status(p.ax).live);
    EXPECT_EQ(ui::live_pill_label(ui::live_status(p.ax)), "");
    EXPECT_FALSE(p.ax.presented_buffer_head().has_value());

    auto& line = p.ax.line();
    line.append(0.0f, 0.0f);
    line.append(10.0f, 0.0f);
    p.ax.presented_buffer(5.0f);
    p.ax.xlim(8.0, 20.0);   // looking past the newest sample
    const auto st = ui::live_status(p.ax);
    EXPECT_DOUBLE_EQ(st.behind_seconds, 0.0);
    EXPECT_EQ(ui::live_pill_label(st), "Paused");
}

TEST(LiveStatus, FormatBehind)
{
    EXPECT_EQ(ui::format_behind(0.42), "0.4 s");
    EXPECT_EQ(ui::format_behind(12.4), "12 s");
    EXPECT_EQ(ui::format_behind(600.0), "10 min");
    EXPECT_EQ(ui::format_behind(5400.0), "90 min");
    EXPECT_EQ(ui::format_behind(9000.0), "2.5 h");
    EXPECT_EQ(ui::format_behind(-3.0), "0.0 s");
}

// ─── Legend solo ────────────────────────────────────────────────────────────

TEST(LegendSolo, SoloAndRestore)
{
    Plot                     p;
    const std::vector<float> x{0.0f, 1.0f};
    auto&                    a = p.ax.line(x, x).label("a");
    auto&                    b = p.ax.line(x, x).label("b");
    auto&                    c = p.ax.line(x, x).label("c");
    c.visible(false);   // hidden before the solo: stays hidden after restore

    LegendSolo solo;
    EXPECT_TRUE(solo.toggle(p.ax, b));
    EXPECT_FALSE(a.visible());
    EXPECT_TRUE(b.visible());
    EXPECT_FALSE(c.visible());
    EXPECT_EQ(solo.soloed(p.ax), &b);

    EXPECT_FALSE(solo.toggle(p.ax, b));   // double-click again: restore
    EXPECT_TRUE(a.visible());
    EXPECT_TRUE(b.visible());
    EXPECT_FALSE(c.visible());
    EXPECT_EQ(solo.soloed(p.ax), nullptr);
}

TEST(LegendSolo, SwitchSoloKeepsOriginalState)
{
    Plot                     p;
    const std::vector<float> x{0.0f, 1.0f};
    auto&                    a = p.ax.line(x, x).label("a");
    auto&                    b = p.ax.line(x, x).label("b");
    auto&                    c = p.ax.line(x, x).label("c");
    c.visible(false);

    LegendSolo solo;
    solo.toggle(p.ax, a);
    EXPECT_TRUE(solo.toggle(p.ax, c));   // solo a hidden series: it is shown
    EXPECT_FALSE(a.visible());
    EXPECT_FALSE(b.visible());
    EXPECT_TRUE(c.visible());

    solo.toggle(p.ax, c);   // restore the visibility from before the first solo
    EXPECT_TRUE(a.visible());
    EXPECT_TRUE(b.visible());
    EXPECT_FALSE(c.visible());
}

TEST(LegendSolo, SeriesAddedWhileSoloedIsLeftAlone)
{
    Plot                     p;
    const std::vector<float> x{0.0f, 1.0f};
    auto&                    a = p.ax.line(x, x).label("a");
    auto&                    b = p.ax.line(x, x).label("b");
    LegendSolo               solo;
    solo.toggle(p.ax, a);
    auto& d = p.ax.line(x, x).label("d");
    solo.toggle(p.ax, a);
    EXPECT_TRUE(a.visible());
    EXPECT_TRUE(b.visible());
    EXPECT_TRUE(d.visible());
}

// ─── Overlay geometry (connectors between far-apart points) ─────────────────

TEST(MeasureGeometry, ClipSegmentToViewport)
{
    // Fully inside: unchanged.
    double x0 = 200, y0 = 100, x1 = 300, y1 = 200;
    ASSERT_TRUE(measure::clip_segment(VP, x0, y0, x1, y1));
    EXPECT_DOUBLE_EQ(x0, 200);
    EXPECT_DOUBLE_EQ(y1, 200);

    // Horizontal line far beyond both sides: clipped to the viewport edges.
    x0 = -1e9;
    y0 = 200;
    x1 = 1e9;
    y1 = 200;
    ASSERT_TRUE(measure::clip_segment(VP, x0, y0, x1, y1));
    EXPECT_NEAR(x0, VP.x, 1e-3);
    EXPECT_NEAR(x1, VP.x + VP.w, 1e-3);

    // Entirely outside (above the plot) or non-finite: nothing to draw.
    x0 = -1e9;
    y0 = 0;
    x1 = 1e9;
    y1 = 10;
    EXPECT_FALSE(measure::clip_segment(VP, x0, y0, x1, y1));
    x0 = std::nan("");
    EXPECT_FALSE(measure::clip_segment(VP, x0, y0, x1, y1));
}

TEST(MeasureGeometry, DeepZoomTipConnectorStaysBounded)
{
    // Two tips 1000 units apart, view zoomed to a 0.001-unit span between
    // them: unclipped, the connector is ~8e8 px long (1e8 dashes per frame).
    Plot p;
    p.ax.xlim(500.0, 500.001);
    p.ax.ylim(-1.0, 1.0);
    const AxisMapping map(p.ax);
    double            x0 = map.to_screen_x(0.0), y0 = map.to_screen_y(0.0);
    double            x1 = map.to_screen_x(1000.0), y1 = map.to_screen_y(0.0);
    EXPECT_GT(std::hypot(x1 - x0, y1 - y0), 1e8);
    ASSERT_TRUE(measure::clip_segment(VP, x0, y0, x1, y1));
    const double len = std::hypot(x1 - x0, y1 - y0);
    EXPECT_LE(len, std::hypot(VP.w, VP.h) + 1e-6);
    EXPECT_LE(measure::dash_count(len, 8.0), 100);
    // Even an unclipped length is capped.
    EXPECT_EQ(measure::dash_count(8e8, 8.0), measure::MAX_DASHES);
    EXPECT_EQ(measure::dash_count(0.0, 8.0), 1);
}

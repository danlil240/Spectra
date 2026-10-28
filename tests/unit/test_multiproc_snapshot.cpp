// Multiproc snapshot fidelity: what the app sends (figure_to_snapshot) and
// what the window agent rebuilds from it (build_figure_from_snapshot) must
// match the in-process figure — subplot placement, statistical series with
// their fills, and the figure-wide series index used by STATE_DIFF ops.

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "agent/live_series_index.hpp"
#include "agent/snapshot_figure_builder.hpp"
#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ui/app/figure_snapshot.hpp"

using namespace spectra;

namespace
{

std::vector<float> normal_samples(float mean, float sd, int n, unsigned seed)
{
    std::mt19937                    rng(seed);
    std::normal_distribution<float> dist(mean, sd);
    std::vector<float>              v(static_cast<size_t>(n));
    for (auto& x : v)
        x = dist(rng);
    return v;
}

// Send through the real IPC codec, as app → backend → window does.
ipc::SnapshotFigureState round_trip(const ipc::SnapshotFigureState& fig)
{
    ipc::StateSnapshotPayload p;
    p.figures.push_back(fig);
    auto decoded = ipc::decode_state_snapshot(ipc::encode_state_snapshot(p));
    EXPECT_TRUE(decoded.has_value());
    if (!decoded || decoded->figures.empty())
        return {};
    return decoded->figures[0];
}

void expect_spans_eq(std::span<const float> a, std::span<const float> b, const char* what)
{
    ASSERT_EQ(a.size(), b.size()) << what;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::isnan(a[i]))
            EXPECT_TRUE(std::isnan(b[i])) << what << "[" << i << "]";
        else
            EXPECT_FLOAT_EQ(a[i], b[i]) << what << "[" << i << "]";
    }
}

template <typename T>
const T* series_as(const Figure& fig, size_t axes, size_t index)
{
    const AxesBase* ax = snapshot_axes_at(fig, axes);
    if (!ax || index >= ax->series().size())
        return nullptr;
    return dynamic_cast<const T*>(ax->series()[index].get());
}

}   // namespace

// ─── Subplots: axes_index ───────────────────────────────────────────────────

TEST(MultiprocSnapshot, SubplotSeriesCarryTheirAxesIndex)
{
    Figure             fig({.width = 800, .height = 600});
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    for (int i = 1; i <= 4; ++i)
    {
        auto& ax = fig.subplot(2, 2, i);
        ax.line(x, x).label("line" + std::to_string(i));
        if (i == 3)
            ax.scatter(x, x).label("dots3");
    }

    const auto snap = round_trip(figure_to_snapshot(fig, 7));
    ASSERT_EQ(snap.axes.size(), 4u);
    ASSERT_EQ(snap.series.size(), 5u);
    const uint32_t expected_axes[] = {0, 1, 2, 2, 3};
    for (size_t k = 0; k < snap.series.size(); ++k)
        EXPECT_EQ(snap.series[k].axes_index, expected_axes[k]) << snap.series[k].name;

    // The window puts each series (and so its legend entry) in its own subplot.
    auto live = agent::build_figure_from_snapshot(snap);
    ASSERT_EQ(live->axes().size(), 4u);
    EXPECT_EQ(live->axes()[0]->series().size(), 1u);
    EXPECT_EQ(live->axes()[1]->series().size(), 1u);
    ASSERT_EQ(live->axes()[2]->series().size(), 2u);
    EXPECT_EQ(live->axes()[3]->series().size(), 1u);
    EXPECT_EQ(live->axes()[1]->series()[0]->label(), "line2");
    EXPECT_EQ(live->axes()[2]->series()[1]->label(), "dots3");
    EXPECT_EQ(live->axes()[3]->series()[0]->label(), "line4");
}

TEST(MultiprocSnapshot, ForEachSeriesUsesFigureWideIndex)
{
    Figure             fig;
    std::vector<float> x = {0.0f, 1.0f};
    fig.subplot(1, 2, 1).line(x, x);
    fig.subplot(1, 2, 1).line(x, x);
    fig.subplot(1, 2, 2).scatter(x, x);

    std::vector<std::pair<uint32_t, uint32_t>> seen;
    for_each_snapshot_series(fig,
                             [&](uint32_t slot, uint32_t global, const Series&)
                             { seen.emplace_back(slot, global); });
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0], std::make_pair(0u, 0u));
    EXPECT_EQ(seen[1], std::make_pair(0u, 1u));
    EXPECT_EQ(seen[2], std::make_pair(1u, 2u));
}

TEST(MultiprocSnapshot, Mixed2DAnd3DSubplotsKeepTheirSlots)
{
    Figure             fig;
    std::vector<float> x = {0.0f, 1.0f, 2.0f};
    fig.subplot3d(1, 3, 1).scatter3d(x, x, x).label("cloud");
    fig.subplot(1, 3, 2).line(x, x).label("trace");
    std::vector<float> gx = {0.0f, 1.0f, 2.0f};
    std::vector<float> gy = {0.0f, 1.0f};
    std::vector<float> gz = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    fig.subplot3d(1, 3, 3).surface(gx, gy, gz).label("surf");

    const auto snap = round_trip(figure_to_snapshot(fig, 1));
    ASSERT_EQ(snap.axes.size(), 3u);
    EXPECT_TRUE(snap.axes[0].is_3d);
    EXPECT_FALSE(snap.axes[1].is_3d);
    EXPECT_TRUE(snap.axes[2].is_3d);
    ASSERT_EQ(snap.series.size(), 3u);
    EXPECT_EQ(snap.series[0].type, "scatter3d");
    EXPECT_EQ(snap.series[0].axes_index, 0u);
    EXPECT_EQ(snap.series[1].type, "line");
    EXPECT_EQ(snap.series[1].axes_index, 1u);
    EXPECT_EQ(snap.series[2].type, "surface");
    EXPECT_EQ(snap.series[2].axes_index, 2u);

    auto live = agent::build_figure_from_snapshot(snap);
    ASSERT_NE(series_as<ScatterSeries3D>(*live, 0, 0), nullptr);
    ASSERT_NE(series_as<LineSeries>(*live, 1, 0), nullptr);
    const auto* surf = series_as<SurfaceSeries>(*live, 2, 0);
    ASSERT_NE(surf, nullptr);
    expect_spans_eq(surf->z_values(), gz, "surface z");
}

// ─── Statistical series ─────────────────────────────────────────────────────

TEST(MultiprocSnapshot, StatsSeriesRebuildWithFills)
{
    const auto a = normal_samples(50.0f, 12.0f, 300, 1);
    const auto b = normal_samples(65.0f, 8.0f, 300, 2);
    const auto c = normal_samples(40.0f, 18.0f, 300, 3);

    Figure fig({.width = 1000, .height = 800});
    auto&  box = fig.subplot(2, 2, 1).box_plot();
    box.add_box(1.0f, a).add_box(2.0f, b).add_box(3.0f, c);
    box.label("Scores").color({0.15f, 0.78f, 0.75f, 1.0f}).box_width(0.5f).gradient(true);
    box.notched(true);

    auto& vn = fig.subplot(2, 2, 2).violin();
    vn.add_violin(1.0f, a).add_violin(2.0f, b);
    vn.label("Density").violin_width(0.7f).resolution(60).show_box(false);

    auto& hist = fig.subplot(2, 2, 3).histogram(a, 35);
    hist.label("Hist").gradient(false).cumulative(true);

    std::vector<float> months  = {1, 2, 3, 4};
    std::vector<float> revenue = {42, 58, 35, 72};
    auto&              bars    = fig.subplot(2, 2, 4).bar(months, revenue);
    bars.label("Revenue").bar_width(0.6f).baseline(5.0f).orientation(BarOrientation::Horizontal);

    const auto snap = round_trip(figure_to_snapshot(fig, 1));
    ASSERT_EQ(snap.series.size(), 4u);
    EXPECT_EQ(snap.series[0].type, "box");
    EXPECT_EQ(snap.series[1].type, "violin");
    EXPECT_EQ(snap.series[2].type, "histogram");
    EXPECT_EQ(snap.series[3].type, "bar");

    auto live = agent::build_figure_from_snapshot(snap);

    const auto* rbox = series_as<BoxPlotSeries>(*live, 0, 0);
    ASSERT_NE(rbox, nullptr);
    EXPECT_EQ(rbox->label(), "Scores");
    EXPECT_FLOAT_EQ(rbox->box_width(), 0.5f);
    EXPECT_TRUE(rbox->notched());
    EXPECT_TRUE(rbox->gradient());
    ASSERT_EQ(rbox->stats().size(), 3u);
    EXPECT_EQ(rbox->stats()[2].count, box.stats()[2].count);
    expect_spans_eq(rbox->fill_verts(), box.fill_verts(), "box fill");
    expect_spans_eq(rbox->x_data(), box.x_data(), "box outline x");
    expect_spans_eq(rbox->outlier_y(), box.outlier_y(), "box outliers");
    EXPECT_GT(rbox->fill_vertex_count(), 0u);

    const auto* rvn = series_as<ViolinSeries>(*live, 1, 0);
    ASSERT_NE(rvn, nullptr);
    EXPECT_EQ(rvn->resolution(), 60);
    EXPECT_FALSE(rvn->show_box());
    expect_spans_eq(rvn->fill_verts(), vn.fill_verts(), "violin fill");
    expect_spans_eq(rvn->y_data(), vn.y_data(), "violin outline y");
    EXPECT_GT(rvn->fill_vertex_count(), 0u);

    const auto* rh = series_as<HistogramSeries>(*live, 2, 0);
    ASSERT_NE(rh, nullptr);
    EXPECT_EQ(rh->bins(), 35);
    EXPECT_TRUE(rh->cumulative());
    EXPECT_FALSE(rh->gradient());
    expect_spans_eq(rh->bin_counts(), hist.bin_counts(), "hist counts");
    expect_spans_eq(rh->fill_verts(), hist.fill_verts(), "hist fill");
    EXPECT_GT(rh->fill_vertex_count(), 0u);

    const auto* rb = series_as<BarSeries>(*live, 3, 0);
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(rb->label(), "Revenue");
    EXPECT_FLOAT_EQ(rb->bar_width(), 0.6f);
    EXPECT_FLOAT_EQ(rb->baseline(), 5.0f);
    EXPECT_EQ(rb->orientation(), BarOrientation::Horizontal);
    expect_spans_eq(rb->fill_verts(), bars.fill_verts(), "bar fill");
    EXPECT_GT(rb->fill_vertex_count(), 0u);
}

TEST(MultiprocSnapshot, MalformedStatsPayloadKeepsSlotWithoutDrawing)
{
    ipc::SnapshotFigureState snap;
    snap.axes.resize(1);
    ipc::SnapshotSeriesState bad;
    bad.type = "bar";
    bad.data = {0.8f, 0.0f, 0.0f, 1.0f, 1e9f, 1.0f};   // count far beyond payload
    snap.series.push_back(bad);
    ipc::SnapshotSeriesState hostile_box;
    hostile_box.type = "box";
    hostile_box.data = {0.5f, 1.0f, 0.0f, 1.0f, 1.0f, 1, 2, 1, 3, 0, 4, 9, -3.0f};
    snap.series.push_back(hostile_box);
    ipc::SnapshotSeriesState good;
    good.type = "line";
    good.name = "after";
    good.data = {0.0f, 1.0f, 2.0f, 3.0f};
    snap.series.push_back(good);

    auto live = agent::build_figure_from_snapshot(snap);
    ASSERT_EQ(live->axes()[0]->series().size(), 3u);
    EXPECT_FALSE(live->axes()[0]->series()[0]->visible());
    EXPECT_FALSE(live->axes()[0]->series()[1]->visible());
    EXPECT_EQ(live->axes()[0]->series()[2]->label(), "after");

    // ... and the figure-wide index of the line still finds it.
    agent::LiveSeriesIndex where;
    ASSERT_TRUE(agent::locate_live_series(snap, 2, where));
    EXPECT_EQ(where.series_index, 2u);
}

TEST(MultiprocSnapshot, StatsDataUpdateCarriesFullPayload)
{
    Figure             fig;
    std::vector<float> pos = {1.0f, 2.0f};
    std::vector<float> h   = {3.0f, 4.0f};
    auto&              bar = fig.subplot(1, 1, 1).bar(pos, h);

    ipc::SnapshotSeriesState payload;
    ASSERT_TRUE(encode_snapshot_series_data(bar, payload));

    // A SET_SERIES_DATA diff applied to the window cache replaces the data;
    // the rebuilt series must match.
    ipc::SnapshotFigureState snap;
    snap.axes.resize(1);
    ipc::SnapshotSeriesState cached;
    cached.type = "bar";
    cached.data = payload.data;
    snap.series.push_back(cached);
    auto        live = agent::build_figure_from_snapshot(snap);
    const auto* rb   = series_as<BarSeries>(*live, 0, 0);
    ASSERT_NE(rb, nullptr);
    expect_spans_eq(rb->bar_heights(), h, "bar heights");
}

// ─── 3D-type series in 2D axes and the reverse ──────────────────────────────

TEST(MultiprocSnapshot, MismatchedDimensionSeriesDoNotShiftLiveIndices)
{
    ipc::SnapshotFigureState snap;
    snap.grid_rows = 1;
    snap.grid_cols = 2;
    snap.axes.resize(2);
    snap.axes[1].is_3d = true;

    auto add = [&](const char* type, uint32_t axes, const char* name, std::vector<float> data)
    {
        ipc::SnapshotSeriesState ss;
        ss.type       = type;
        ss.axes_index = axes;
        ss.name       = name;
        ss.data       = std::move(data);
        snap.series.push_back(std::move(ss));
    };
    add("line3d", 0, "stray3d", {0, 0, 0, 1, 1, 1});                   // 0: 3D type in a 2D axes
    add("line", 0, "a", {0, 0, 1, 1});                                 // 1
    add("scatter3d", 1, "cloud", {0, 0, 0, 1, 1, 1});                  // 2
    add("scatter", 1, "stray2d", {0, 0, 1, 1});                        // 3: 2D type in a 3D axes
    add("scatter", 0, "b", {0, 0, 1, 1});                              // 4
    add("line", 5, "no_axes", {0, 0, 1, 1});                           // 5: axes does not exist
    add("surface", 1, "surf", {0, 0, 0, 1, 0, 1, 0, 1, 2, 1, 1, 3});   // 6

    auto live = agent::build_figure_from_snapshot(snap);

    // Every series op must land on the series that carries its name, or nowhere.
    for (uint32_t g = 0; g < snap.series.size(); ++g)
    {
        agent::LiveSeriesIndex where;
        const bool             found = agent::locate_live_series(snap, g, where);
        const std::string&     name  = snap.series[g].name;
        if (name.rfind("stray", 0) == 0 || name == "no_axes")
        {
            EXPECT_FALSE(found) << name;
            continue;
        }
        ASSERT_TRUE(found) << name;
        AxesBase* ax = agent::live_axes_at(*live, where.axes_index);
        ASSERT_NE(ax, nullptr) << name;
        ASSERT_LT(where.series_index, ax->series().size()) << name;
        EXPECT_EQ(ax->series()[where.series_index]->label(), name);
    }

    EXPECT_EQ(agent::live_axes_at(*live, 0)->series().size(), 2u);
    EXPECT_EQ(agent::live_axes_at(*live, 1)->series().size(), 2u);
}

TEST(MultiprocSnapshot, SeriesIsBuiltFollowsAxesDimension)
{
    ipc::SnapshotFigureState snap;   // no axes: the window builds one 2D axes
    ipc::SnapshotSeriesState ss;
    ss.type = "line";
    EXPECT_TRUE(agent::series_is_built(snap, ss));
    ss.type = "surface";
    EXPECT_FALSE(agent::series_is_built(snap, ss));
    ss.type       = "line";
    ss.axes_index = 1;
    EXPECT_FALSE(agent::series_is_built(snap, ss));
}

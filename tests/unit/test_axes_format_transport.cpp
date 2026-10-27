// Axis scale, date-time mode, legend position and bar category names travel
// over multiproc IPC (snapshot + diff ops) and through the workspace / figure
// files (optional keys and chunks: older files still load).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "agent/snapshot_figure_builder.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/axes_format_state.hpp"
#include "ipc/codec.hpp"
#include "ui/app/figure_snapshot.hpp"
#include "ui/workspace/figure_serializer.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;

namespace
{

// A figure with every field set away from its default.
std::unique_ptr<Figure> make_figure()
{
    auto  fig = std::make_unique<Figure>(FigureConfig{.width = 640, .height = 480});
    auto& ax  = fig->subplot(1, 2, 1);
    ax.plot(std::vector<float>{1, 10, 100}, std::vector<float>{1, 2, 3});
    ax.xscale(ScaleType::Log10);
    ax.yscale(ScaleType::Sqrt);
    ax.xaxis_datetime(false);
    ax.yaxis_datetime(true);
    auto& bx = fig->subplot(1, 2, 2);
    bx.bar(std::vector<std::string>{"alpha", "beta", "gamma"}, std::vector<float>{1, 2, 3});
    bx.xaxis_datetime(true);
    fig->legend().position = LegendPosition::OutsideRight;
    return fig;
}

ipc::SnapshotFigureState round_trip(const ipc::SnapshotFigureState& s)
{
    ipc::StateSnapshotPayload p;
    p.figures.push_back(s);
    auto back = ipc::decode_state_snapshot(ipc::encode_state_snapshot(p));
    EXPECT_TRUE(back.has_value());
    return back ? back->figures.at(0) : ipc::SnapshotFigureState{};
}

ipc::DiffOp round_trip(const ipc::DiffOp& op)
{
    ipc::StateDiffPayload d;
    d.ops.push_back(op);
    auto back = ipc::decode_state_diff(ipc::encode_state_diff(d));
    EXPECT_TRUE(back.has_value());
    return back ? back->ops.at(0) : ipc::DiffOp{};
}

void expect_restored(const Figure& fig)
{
    ASSERT_EQ(fig.axes().size(), 2u);
    const Axes& a = *fig.axes()[0];
    EXPECT_EQ(a.xscale(), ScaleType::Log10);
    EXPECT_EQ(a.yscale(), ScaleType::Sqrt);
    EXPECT_FALSE(a.xaxis_datetime());
    EXPECT_TRUE(a.yaxis_datetime());
    const Axes& b = *fig.axes()[1];
    EXPECT_TRUE(b.xaxis_datetime());
    ASSERT_EQ(b.series().size(), 1u);
    auto* bar = dynamic_cast<const BarSeries*>(b.series()[0].get());
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->categories(), std::vector<std::string>({"alpha", "beta", "gamma"}));
    EXPECT_EQ(fig.legend().position, LegendPosition::OutsideRight);
}

std::string temp_path(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

}   // namespace

TEST(AxesFormatTransport, SnapshotRoundTripRebuildsTheWindowFigure)
{
    auto       fig  = make_figure();
    const auto snap = round_trip(figure_to_snapshot(*fig, 7));
    ASSERT_EQ(snap.axes.size(), 2u);
    EXPECT_EQ(snap.axes[0].x_scale, static_cast<uint8_t>(ScaleType::Log10));
    EXPECT_EQ(snap.legend_position, static_cast<uint8_t>(LegendPosition::OutsideRight));
    ASSERT_EQ(snap.series.size(), 2u);
    EXPECT_EQ(snap.series[1].categories.size(), 3u);

    auto live = agent::build_figure_from_snapshot(snap);
    ASSERT_TRUE(live);
    expect_restored(*live);
}

TEST(AxesFormatTransport, OlderSnapshotsKeepDefaults)
{
    ipc::SnapshotFigureState s;   // no new fields set
    s.axes.resize(1);
    const auto back = round_trip(s);
    EXPECT_EQ(back.legend_position, static_cast<uint8_t>(LegendPosition::Best));
    EXPECT_EQ(back.axes[0].x_scale, 0);
    EXPECT_FALSE(back.axes[0].x_datetime);
}

TEST(AxesFormatTransport, DiffOpsUpdateTheDaemonModel)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("f");
    model.add_axes(fid);
    uint32_t idx = 0;
    model.add_series_with_diff(fid, "bars", "bar", 0, idx);

    ipc::SnapshotAxisState fmt;
    fmt.x_scale    = static_cast<uint8_t>(ScaleType::Log2);
    fmt.y_datetime = true;
    ASSERT_TRUE(model.apply_diff_op(round_trip(ipc::make_axis_scale_op(fid, 0, fmt))));
    ASSERT_TRUE(model.apply_diff_op(
        round_trip(ipc::make_legend_position_op(fid, LegendPosition::TopLeft))));
    ASSERT_TRUE(model.apply_diff_op(
        round_trip(ipc::make_series_categories_op(fid, 0, idx, {"a", "", "c d"}))));

    const auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    const auto& f = snap.figures[0];
    EXPECT_EQ(f.axes[0].x_scale, static_cast<uint8_t>(ScaleType::Log2));
    EXPECT_TRUE(f.axes[0].y_datetime);
    EXPECT_FALSE(f.axes[0].x_datetime);
    EXPECT_EQ(f.legend_position, static_cast<uint8_t>(LegendPosition::TopLeft));
    EXPECT_EQ(f.series[idx].categories, std::vector<std::string>({"a", "", "c d"}));
}

TEST(AxesFormatTransport, MalformedOpsAreRejected)
{
    std::vector<std::string> names;
    ipc::DiffOp              op = ipc::make_series_categories_op(1, 0, 0, {"x", "y"});
    op.f1                       = 3;   // count does not match the names
    EXPECT_FALSE(ipc::categories_from_op(op, names));
    op.f1 = -1;
    EXPECT_FALSE(ipc::categories_from_op(op, names));
    EXPECT_EQ(ipc::scale_from_wire(42), ScaleType::Linear);
    EXPECT_EQ(ipc::legend_position_from_wire(99), LegendPosition::Best);
}

TEST(AxesFormatTransport, WorkspaceJsonRoundTrip)
{
    auto                 fig = make_figure();
    std::vector<Figure*> figs{fig.get()};
    const auto           data = Workspace::capture(figs, 0, "night", true, 320.0f, false);
    const std::string    path = temp_path("spectra_axes_format_ws.spectra");
    ASSERT_TRUE(Workspace::save(path, data));

    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_EQ(loaded.figures[0].legend_position, static_cast<int>(LegendPosition::OutsideRight));

    // Apply onto a fresh figure with the same layout and a plain bar series.
    Figure fresh({.width = 640, .height = 480});
    fresh.subplot(1, 2, 1).plot(std::vector<float>{1, 2}, std::vector<float>{1, 2});
    fresh.subplot(1, 2, 2).bar(std::vector<float>{0, 1, 2}, std::vector<float>{1, 2, 3});
    std::vector<Figure*> targets{&fresh};
    ASSERT_TRUE(Workspace::apply(loaded, targets));
    expect_restored(fresh);
    std::filesystem::remove(path);
}

TEST(AxesFormatTransport, WorkspaceWithoutNewKeysLeavesFigureAlone)
{
    auto                 fig = make_figure();
    std::vector<Figure*> figs{fig.get()};
    auto                 data = Workspace::capture(figs, 0, "night", true, 320.0f, false);
    const std::string    path = temp_path("spectra_axes_format_old.spectra");
    ASSERT_TRUE(Workspace::save(path, data));

    // Strip the new keys, as an older build would have written the file.
    std::string json;
    {
        std::ifstream in(path);
        json.assign(std::istreambuf_iterator<char>(in), {});
    }
    std::string        out;
    std::istringstream lines(json);
    for (std::string line; std::getline(lines, line);)
    {
        if (line.find("\"legend_position\"") != std::string::npos
            || line.find("\"categories\"") != std::string::npos)
            continue;
        out += line + "\n";
    }
    {
        std::ofstream o(path, std::ios::trunc);
        o << out;
    }
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    EXPECT_EQ(loaded.figures[0].legend_position, -1);

    Figure fresh({.width = 640, .height = 480});
    fresh.legend().position = LegendPosition::BottomLeft;
    fresh.subplot(1, 2, 1);
    fresh.subplot(1, 2, 2)
        .bar(std::vector<float>{0, 1}, std::vector<float>{1, 2})
        .categories({"kept", "names"});
    std::vector<Figure*> targets{&fresh};
    ASSERT_TRUE(Workspace::apply(loaded, targets));
    EXPECT_EQ(fresh.legend().position, LegendPosition::BottomLeft);
    auto* bar = dynamic_cast<const BarSeries*>(fresh.axes()[1]->series()[0].get());
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->categories(), std::vector<std::string>({"kept", "names"}));
    std::filesystem::remove(path);
}

TEST(AxesFormatTransport, BinaryFigureFileRoundTrip)
{
    auto              fig  = make_figure();
    const std::string path = temp_path("spectra_axes_format_fig.spectra");
    ASSERT_TRUE(FigureSerializer::save(path, *fig));
    Figure loaded({.width = 640, .height = 480});
    ASSERT_TRUE(FigureSerializer::load(path, loaded));
    expect_restored(loaded);
    std::filesystem::remove(path);
}

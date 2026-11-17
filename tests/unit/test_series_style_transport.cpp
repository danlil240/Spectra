// Series line style and marker style (e.g. from a Python format string "r--o")
// travel over multiproc IPC: the "line_style" / "marker_style" property tokens,
// the SnapshotSeriesState fields and the SET_SERIES_LINE_STYLE /
// SET_SERIES_MARKER_STYLE diff ops.  Unset fields keep the series defaults.

#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/series3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <vector>

#include "agent/snapshot_figure_builder.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/codec.hpp"
#include "ipc/series_style_state.hpp"
#include "ipc/style3d_state.hpp"
#include "ui/app/figure_snapshot.hpp"

using namespace spectra;

namespace
{

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

ipc::SnapshotSeriesState line_state(const char* type = "line")
{
    ipc::SnapshotSeriesState ss;
    ss.type = type;
    ss.data = {0.0f, 1.0f, 1.0f, 2.0f, 2.0f, 0.5f};
    return ss;
}

}   // namespace

TEST(SeriesStyleTransport, LineStyleTokens)
{
    LineStyle ls{};
    ASSERT_TRUE(ipc::parse_line_style_token("--", ls));
    EXPECT_EQ(ls, LineStyle::Dashed);
    ASSERT_TRUE(ipc::parse_line_style_token(":", ls));
    EXPECT_EQ(ls, LineStyle::Dotted);
    ASSERT_TRUE(ipc::parse_line_style_token("-.", ls));
    EXPECT_EQ(ls, LineStyle::DashDot);
    ASSERT_TRUE(ipc::parse_line_style_token("-..", ls));
    EXPECT_EQ(ls, LineStyle::DashDotDot);
    ASSERT_TRUE(ipc::parse_line_style_token("-", ls));
    EXPECT_EQ(ls, LineStyle::Solid);
    ASSERT_TRUE(ipc::parse_line_style_token("none", ls));
    EXPECT_EQ(ls, LineStyle::None);
    EXPECT_FALSE(ipc::parse_line_style_token("dashed!", ls));
    EXPECT_FALSE(ipc::parse_line_style_token("---", ls));
}

TEST(SeriesStyleTransport, MarkerStyleTokens)
{
    MarkerStyle ms{};
    ASSERT_TRUE(ipc::parse_marker_style_token("o", ms));
    EXPECT_EQ(ms, MarkerStyle::Circle);
    ASSERT_TRUE(ipc::parse_marker_style_token("^", ms));
    EXPECT_EQ(ms, MarkerStyle::TriangleUp);
    ASSERT_TRUE(ipc::parse_marker_style_token("D", ms));
    EXPECT_EQ(ms, MarkerStyle::FilledDiamond);
    ASSERT_TRUE(ipc::parse_marker_style_token("none", ms));
    EXPECT_EQ(ms, MarkerStyle::None);
    EXPECT_FALSE(ipc::parse_marker_style_token("Q", ms));
    EXPECT_FALSE(ipc::parse_marker_style_token("oo", ms));
}

TEST(SeriesStyleTransport, WireValuesAreValidated)
{
    LineStyle   ls{};
    MarkerStyle ms{};
    EXPECT_FALSE(ipc::line_style_from_wire(ipc::SERIES_STYLE_UNSET, ls));
    EXPECT_FALSE(ipc::line_style_from_wire(ipc::LINE_STYLE_WIRE_MAX + 1, ls));
    EXPECT_FALSE(ipc::marker_style_from_wire(ipc::SERIES_STYLE_UNSET, ms));
    EXPECT_FALSE(ipc::marker_style_from_wire(ipc::MARKER_STYLE_WIRE_MAX + 1, ms));
    EXPECT_EQ(ipc::style_wire_from_double(2.0, ipc::LINE_STYLE_WIRE_MAX), 2);
    EXPECT_EQ(ipc::style_wire_from_double(2.5, ipc::LINE_STYLE_WIRE_MAX), ipc::SERIES_STYLE_UNSET);
    EXPECT_EQ(ipc::style_wire_from_double(-1.0, ipc::LINE_STYLE_WIRE_MAX), ipc::SERIES_STYLE_UNSET);
    EXPECT_EQ(ipc::style_wire_from_double(99.0, ipc::LINE_STYLE_WIRE_MAX), ipc::SERIES_STYLE_UNSET);
}

TEST(SeriesStyleTransport, SnapshotFieldsRoundTrip)
{
    ipc::SnapshotFigureState fig;
    fig.axes.emplace_back();
    auto styled         = line_state();
    styled.line_style   = static_cast<uint8_t>(LineStyle::Dashed);
    styled.marker_style = static_cast<uint8_t>(MarkerStyle::Circle);
    fig.series.push_back(styled);
    fig.series.push_back(line_state());   // unset: defaults

    const auto back = round_trip(fig);
    ASSERT_EQ(back.series.size(), 2u);
    EXPECT_EQ(back.series[0].line_style, static_cast<uint8_t>(LineStyle::Dashed));
    EXPECT_EQ(back.series[0].marker_style, static_cast<uint8_t>(MarkerStyle::Circle));
    EXPECT_EQ(back.series[1].line_style, ipc::SERIES_STYLE_UNSET);
    EXPECT_EQ(back.series[1].marker_style, ipc::SERIES_STYLE_UNSET);
}

TEST(SeriesStyleTransport, DiffOpsRoundTripAndUpdateTheDaemonModel)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("f");
    model.add_axes(fid);
    uint32_t idx = 0;
    model.add_series_with_diff(fid, "s", "line", 0, idx);

    // Fresh series: unset.
    auto snap = model.snapshot();
    EXPECT_EQ(snap.figures[0].series[0].line_style, ipc::SERIES_STYLE_UNSET);
    EXPECT_EQ(snap.figures[0].series[0].marker_style, ipc::SERIES_STYLE_UNSET);

    auto op_ls =
        round_trip(model.set_series_line_style(fid, idx, static_cast<uint8_t>(LineStyle::Dotted)));
    EXPECT_EQ(op_ls.type, ipc::DiffOp::Type::SET_SERIES_LINE_STYLE);
    EXPECT_EQ(op_ls.f1, static_cast<double>(LineStyle::Dotted));
    auto op_ms = round_trip(
        model.set_series_marker_style(fid, idx, static_cast<uint8_t>(MarkerStyle::Square)));
    EXPECT_EQ(op_ms.type, ipc::DiffOp::Type::SET_SERIES_MARKER_STYLE);

    snap = model.snapshot();
    EXPECT_EQ(snap.figures[0].series[0].line_style, static_cast<uint8_t>(LineStyle::Dotted));
    EXPECT_EQ(snap.figures[0].series[0].marker_style, static_cast<uint8_t>(MarkerStyle::Square));

    // A peer's diff op is applied (and validated) by the model too.
    daemon::FigureModel peer;
    const auto          pid = peer.create_figure("p");
    peer.add_axes(pid);
    uint32_t pidx = 0;
    peer.add_series_with_diff(pid, "s", "line", 0, pidx);
    op_ls.figure_id = pid;
    op_ms.figure_id = pid;
    ASSERT_TRUE(peer.apply_diff_op(op_ls));
    ASSERT_TRUE(peer.apply_diff_op(op_ms));
    auto bad = op_ls;
    bad.f1   = 42.0;
    EXPECT_FALSE(peer.apply_diff_op(bad));
    const auto psnap = peer.snapshot();
    EXPECT_EQ(psnap.figures[0].series[0].line_style, static_cast<uint8_t>(LineStyle::Dotted));
    EXPECT_EQ(psnap.figures[0].series[0].marker_style, static_cast<uint8_t>(MarkerStyle::Square));
}

TEST(SeriesStyleTransport, WindowBuildsStyledSeriesAndKeepsDefaultsWhenUnset)
{
    ipc::SnapshotFigureState snap;
    snap.axes.emplace_back();
    auto styled         = line_state();
    styled.line_style   = static_cast<uint8_t>(LineStyle::DashDot);
    styled.marker_style = static_cast<uint8_t>(MarkerStyle::TriangleUp);
    snap.series.push_back(styled);
    snap.series.push_back(line_state());
    snap.series.push_back(line_state("scatter"));

    auto fig = agent::build_figure_from_snapshot(round_trip(snap));
    ASSERT_EQ(fig->axes().size(), 1u);
    const auto& series = fig->axes()[0]->series();
    ASSERT_EQ(series.size(), 3u);
    EXPECT_EQ(series[0]->line_style(), LineStyle::DashDot);
    EXPECT_EQ(series[0]->marker_style(), MarkerStyle::TriangleUp);

    // Unset: whatever a freshly created series of that type uses.
    Figure ref;
    auto&  rax = ref.subplot(1, 1, 1);
    auto&  rl  = rax.line(std::vector<float>{0, 1}, std::vector<float>{0, 1});
    auto&  rs  = rax.scatter(std::vector<float>{0, 1}, std::vector<float>{0, 1});
    EXPECT_EQ(series[1]->line_style(), rl.line_style());
    EXPECT_EQ(series[1]->marker_style(), rl.marker_style());
    EXPECT_EQ(series[2]->line_style(), rs.line_style());
    EXPECT_EQ(series[2]->marker_style(), rs.marker_style());
}

TEST(SeriesStyleTransport, AppSnapshotCapturesStyle)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.plot(std::vector<float>{0, 1, 2}, std::vector<float>{1, 0, 1}, "r:s");

    const auto snap = figure_to_snapshot(fig, 1);
    ASSERT_EQ(snap.series.size(), 1u);
    EXPECT_EQ(snap.series[0].line_style, static_cast<uint8_t>(LineStyle::Dotted));
    EXPECT_EQ(snap.series[0].marker_style, static_cast<uint8_t>(MarkerStyle::Square));

    auto        rebuilt = agent::build_figure_from_snapshot(round_trip(snap));
    const auto& s       = *rebuilt->axes()[0]->series()[0];
    EXPECT_EQ(s.line_style(), LineStyle::Dotted);
    EXPECT_EQ(s.marker_style(), MarkerStyle::Square);
}

// ─── 3D styles: depth shade, surface edges / floor contours, light ──────────

TEST(Style3DTransport, SnapshotFieldsRoundTrip)
{
    ipc::SnapshotFigureState snap;
    ipc::SnapshotAxisState   sa;
    sa.is_3d            = true;
    sa.light_mode       = static_cast<uint8_t>(LightMode::Headlight);
    sa.fog              = 0.25f;
    sa.light_angles_set = true;
    sa.light_azimuth    = 60.0f;
    sa.light_elevation  = 20.0f;
    snap.axes.push_back(sa);
    auto ss           = line_state("surface");
    ss.show_edges     = true;
    ss.contour_levels = 8;
    ss.depth_shade    = 0;
    ss.edge_color     = {0.9f, 0.1f, 0.2f, 0.6f};
    snap.series.push_back(ss);

    const auto back = round_trip(snap);
    ASSERT_EQ(back.axes.size(), 1u);
    EXPECT_EQ(back.axes[0].light_mode, static_cast<uint8_t>(LightMode::Headlight));
    EXPECT_FLOAT_EQ(back.axes[0].fog, 0.25f);
    EXPECT_TRUE(back.axes[0].light_angles_set);
    EXPECT_FLOAT_EQ(back.axes[0].light_azimuth, 60.0f);
    EXPECT_FLOAT_EQ(back.axes[0].light_elevation, 20.0f);
    ASSERT_EQ(back.series.size(), 1u);
    EXPECT_TRUE(back.series[0].show_edges);
    EXPECT_EQ(back.series[0].contour_levels, 8);
    EXPECT_EQ(back.series[0].depth_shade, 0);
    EXPECT_FLOAT_EQ(back.series[0].edge_color[0], 0.9f);
    EXPECT_FLOAT_EQ(back.series[0].edge_color[1], 0.1f);
    EXPECT_FLOAT_EQ(back.series[0].edge_color[2], 0.2f);
    EXPECT_FLOAT_EQ(back.series[0].edge_color[3], 0.6f);

    // Defaults survive as "unset" / off.
    ipc::SnapshotFigureState plain;
    plain.axes.emplace_back();
    plain.series.push_back(line_state());
    const auto pb = round_trip(plain);
    EXPECT_EQ(pb.series[0].depth_shade, ipc::SERIES_STYLE_UNSET);
    EXPECT_FALSE(pb.series[0].show_edges);
    EXPECT_EQ(pb.series[0].contour_levels, 0);
    EXPECT_EQ(pb.axes[0].light_mode, 0);
}

TEST(Style3DTransport, DiffOpsUpdateModelAndLiveSeries)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("f");
    model.add_axes(fid, 0.0, 1.0, 0.0, 1.0, true);
    uint32_t sidx = 0;
    model.add_series_with_diff(fid, "s", "scatter3d", 0, sidx);

    auto op = round_trip(model.update_series_style_3d(fid,
                                                      sidx,
                                                      [](ipc::SnapshotSeriesState& ss)
                                                      { ss.depth_shade = 0; }));
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_SERIES_STYLE_3D);
    auto lop = round_trip(model.update_axes_light_3d(fid,
                                                     0,
                                                     [](ipc::SnapshotAxisState& sa)
                                                     {
                                                         sa.light_mode = 1;
                                                         sa.fog        = 0.5f;
                                                     }));
    EXPECT_EQ(lop.type, ipc::DiffOp::Type::SET_AXES_LIGHT_3D);
    auto snap = model.snapshot();
    EXPECT_EQ(snap.figures[0].series[0].depth_shade, 0);
    EXPECT_EQ(snap.figures[0].axes[0].light_mode, 1);
    EXPECT_FLOAT_EQ(snap.figures[0].axes[0].fog, 0.5f);

    // The ops applied to live objects in a window.
    Axes3D                   ax;
    std::vector<float>       v  = {0.0f, 1.0f};
    auto&                    sc = ax.scatter3d(v, v, v);
    ipc::SnapshotSeriesState ss;
    ipc::series_style_3d_from_op(op, ss);
    ipc::apply_series_style_3d(sc, ss);
    EXPECT_FALSE(sc.depth_shade());
    ipc::SnapshotAxisState sa;
    ipc::axes_light_3d_from_op(lop, sa);
    ipc::apply_axes_light_3d(ax, sa);
    EXPECT_EQ(ax.light_mode(), LightMode::Headlight);
    EXPECT_FLOAT_EQ(ax.fog(), 0.5f);

    // Surface: edges + contour levels through the op.
    std::vector<float>       z    = {0.0f, 1.0f, 1.0f, 2.0f};
    auto&                    surf = ax.surface(v, v, z);
    ipc::DiffOp              sop;
    ipc::SnapshotSeriesState want;
    want.show_edges     = true;
    want.contour_levels = 6;
    want.edge_color     = {1.0f, 1.0f, 1.0f, 0.5f};
    ipc::series_style_3d_to_op(want, sop);
    ipc::SnapshotSeriesState got;
    ipc::series_style_3d_from_op(round_trip(sop), got);
    ipc::apply_series_style_3d(surf, got);
    EXPECT_TRUE(surf.show_edges());
    EXPECT_TRUE(surf.contour_projection());
    EXPECT_EQ(surf.contour_levels(), 6);
    EXPECT_FLOAT_EQ(surf.edge_color().r, 1.0f);
    EXPECT_FLOAT_EQ(surf.edge_color().a, 0.5f);

    // The daemon keeps the edge colour and sends it with the op.
    uint32_t sfidx = 0;
    model.add_series_with_diff(fid, "surf", "surface", 0, sfidx);
    auto eop = round_trip(model.update_series_style_3d(fid,
                                                       sfidx,
                                                       [](ipc::SnapshotSeriesState& ss)
                                                       {
                                                           ss.show_edges = true;
                                                           ss.edge_color = {0.2f, 0.4f, 0.6f, 0.8f};
                                                       }));
    ASSERT_EQ(eop.data.size(), 4u);
    EXPECT_FLOAT_EQ(eop.data[2], 0.6f);
    EXPECT_FLOAT_EQ(model.snapshot().figures[0].series[sfidx].edge_color[3], 0.8f);
}

TEST(Style3DTransport, AppSnapshotCapturesAndWindowRebuilds3DStyle)
{
    Figure             fig;
    auto&              ax = fig.subplot3d(1, 1, 1);
    std::vector<float> v  = {0.0f, 1.0f};
    std::vector<float> z  = {0.0f, 1.0f, 1.0f, 2.0f};
    ax.light_mode(LightMode::Headlight);
    ax.fog(0.4f);
    ax.scatter3d(v, v, v).depth_shade(false);
    ax.surface(v, v, z)
        .show_edges(true)
        .edge_color(Color{0.8f, 0.2f, 0.1f, 0.7f})
        .contour_projection(true)
        .contour_levels(5);

    const auto snap = figure_to_snapshot(fig, 1);
    auto       win  = agent::build_figure_from_snapshot(round_trip(snap));
    ASSERT_FALSE(win->all_axes().empty());
    auto* wax = dynamic_cast<Axes3D*>(win->all_axes()[0].get());
    ASSERT_NE(wax, nullptr);
    EXPECT_EQ(wax->light_mode(), LightMode::Headlight);
    EXPECT_FLOAT_EQ(wax->fog(), 0.4f);
    ASSERT_EQ(wax->series().size(), 2u);
    auto* sc = dynamic_cast<ScatterSeries3D*>(wax->series()[0].get());
    auto* sf = dynamic_cast<SurfaceSeries*>(wax->series()[1].get());
    ASSERT_TRUE(sc && sf);
    EXPECT_FALSE(sc->depth_shade());
    EXPECT_TRUE(sf->show_edges());
    EXPECT_TRUE(sf->contour_projection());
    EXPECT_EQ(sf->contour_levels(), 5);
    EXPECT_FLOAT_EQ(sf->edge_color().r, 0.8f);
    EXPECT_FLOAT_EQ(sf->edge_color().g, 0.2f);
    EXPECT_FLOAT_EQ(sf->edge_color().b, 0.1f);
    EXPECT_FLOAT_EQ(sf->edge_color().a, 0.7f);
}

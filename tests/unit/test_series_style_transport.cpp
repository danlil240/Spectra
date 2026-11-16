// Series line style and marker style (e.g. from a Python format string "r--o")
// travel over multiproc IPC: the "line_style" / "marker_style" property tokens,
// the SnapshotSeriesState fields and the SET_SERIES_LINE_STYLE /
// SET_SERIES_MARKER_STYLE diff ops.  Unset fields keep the series defaults.

#include <gtest/gtest.h>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <vector>

#include "agent/snapshot_figure_builder.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/codec.hpp"
#include "ipc/series_style_state.hpp"
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

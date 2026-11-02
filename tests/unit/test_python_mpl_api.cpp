// Backend side of the matplotlib-style Python API: native statistical series
// created over IPC (band / stem payloads), axis scale and date-time properties,
// REQ_EXPORT_FIGURE codec and export request validation.

#include <gtest/gtest.h>

#include <filesystem>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <vector>

#include "agent/live_series_index.hpp"
#include "agent/snapshot_figure_builder.hpp"
#include "daemon/figure_export.hpp"
#include "daemon/figure_model.hpp"
#include "ipc/axes_format_state.hpp"
#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ui/app/figure_snapshot.hpp"

using namespace spectra;
namespace fs = std::filesystem;

namespace
{

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

template <typename T>
const T* series_as(const Figure& fig, size_t axes, size_t index)
{
    const AxesBase* ax = snapshot_axes_at(fig, axes);
    if (!ax || index >= ax->series().size())
        return nullptr;
    return dynamic_cast<const T*>(ax->series()[index].get());
}

void expect_spans_eq(std::span<const float> a, std::span<const float> b, const char* what)
{
    ASSERT_EQ(a.size(), b.size()) << what;
    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_FLOAT_EQ(a[i], b[i]) << what << "[" << i << "]";
}

}   // namespace

// ─── Band / stem payloads ───────────────────────────────────────────────────

TEST(PythonMplApi, BandAndStemRebuildFromSnapshot)
{
    std::vector<float> x  = {0.0f, 1.0f, 2.0f, 3.0f};
    std::vector<float> lo = {-1.0f, -0.5f, 0.0f, 0.5f};
    std::vector<float> hi = {1.0f, 1.5f, 2.0f, 2.5f};
    std::vector<float> y  = {0.5f, 2.0f, -1.0f, 3.0f};

    Figure fig({.width = 800, .height = 600});
    auto&  ax   = fig.subplot(1, 1, 1);
    auto&  band = ax.band(x, lo, hi);
    band.fill_opacity(0.4f).edge_width(2.0f).show_edges(false);
    band.label("CI");
    auto& stem = ax.stem(x, y);
    stem.baseline(0.25f).stem_width(3.0f).show_heads(false);
    stem.label("impulses");

    const auto snap = round_trip(figure_to_snapshot(fig, 7));
    ASSERT_EQ(snap.series.size(), 2u);
    EXPECT_EQ(snap.series[0].type, "band");
    EXPECT_EQ(snap.series[1].type, "stem");
    EXPECT_EQ(snap.series[0].point_count, 4u);
    EXPECT_TRUE(is_stats_series_type("band"));
    EXPECT_TRUE(is_stats_series_type("stem"));

    auto live = agent::build_figure_from_snapshot(snap);

    const auto* rband = series_as<BandSeries>(*live, 0, 0);
    ASSERT_NE(rband, nullptr);
    EXPECT_EQ(rband->label(), "CI");
    EXPECT_FLOAT_EQ(rband->fill_opacity(), 0.4f);
    EXPECT_FLOAT_EQ(rband->edge_width(), 2.0f);
    EXPECT_FALSE(rband->show_edges());
    expect_spans_eq(rband->x_values(), x, "band x");
    expect_spans_eq(rband->lower_values(), lo, "band lower");
    expect_spans_eq(rband->upper_values(), hi, "band upper");

    const auto* rstem = series_as<StemSeries>(*live, 0, 1);
    ASSERT_NE(rstem, nullptr);
    EXPECT_EQ(rstem->label(), "impulses");
    EXPECT_FLOAT_EQ(rstem->baseline(), 0.25f);
    EXPECT_FLOAT_EQ(rstem->stem_width(), 3.0f);
    EXPECT_FALSE(rstem->show_heads());
    expect_spans_eq(rstem->stem_x(), x, "stem x");
    expect_spans_eq(rstem->stem_y(), y, "stem y");
}

TEST(PythonMplApi, MalformedBandPayloadKeepsSlot)
{
    ipc::SnapshotFigureState snap;
    snap.axes.resize(1);
    ipc::SnapshotSeriesState bad;
    bad.type = "band";
    bad.data = {0.25f, 1.0f, 1.0f, 0.0f, 3.0f, 0.0f, 1.0f};   // 3 samples need 9 floats
    snap.series.push_back(bad);
    ipc::SnapshotSeriesState after;
    after.type = "line";
    after.name = "after";
    after.data = {0.0f, 1.0f, 1.0f, 2.0f};
    snap.series.push_back(after);

    auto live = agent::build_figure_from_snapshot(snap);
    ASSERT_EQ(live->axes()[0]->series().size(), 2u);
    EXPECT_FALSE(live->axes()[0]->series()[0]->visible());
    EXPECT_EQ(live->axes()[0]->series()[1]->label(), "after");
}

// A Python client creates a "bar" series and sends its payload with REQ_SET_DATA:
// the model keeps it verbatim and the window builds a native BarSeries.
TEST(PythonMplApi, PythonBarSeriesThroughFigureModel)
{
    daemon::FigureModel model;
    const auto          fid  = model.create_figure("bars");
    const auto          axes = model.add_axes(fid, 0.0, 4.0, 0.0, 10.0);
    uint32_t            sidx = 99;
    model.add_series_with_diff(fid, "sales", "bar", axes, sidx);
    EXPECT_EQ(sidx, 0u);
    EXPECT_EQ(model.series_type(fid, 0), "bar");
    EXPECT_EQ(model.series_type(fid, 5), "");

    // [bar_width, baseline, orientation, gradient, n, positions[n], heights[n]]
    const std::vector<float> payload = {0.5f, 0.0f, 0.0f, 0.0f, 3.0f, 0, 1, 2, 4, 7, 5};
    model.set_series_data(fid, 0, payload);
    auto cats = ipc::make_series_categories_op(fid, axes, 0, {"a", "b", "c"});
    ASSERT_TRUE(model.apply_diff_op(cats));

    const auto  snap = round_trip(model.snapshot({fid}).figures.at(0));
    auto        live = agent::build_figure_from_snapshot(snap);
    const auto* bar  = series_as<BarSeries>(*live, 0, 0);
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->label(), "sales");
    EXPECT_FLOAT_EQ(bar->bar_width(), 0.5f);
    ASSERT_EQ(bar->bar_heights().size(), 3u);
    EXPECT_FLOAT_EQ(bar->bar_heights()[1], 7.0f);
    EXPECT_EQ(bar->categories(), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_GT(bar->fill_vertex_count(), 0u);
}

// ─── Axis scale / date-time ─────────────────────────────────────────────────

TEST(PythonMplApi, SetAxisFormatKeepsOtherFields)
{
    daemon::FigureModel model;
    const auto          fid = model.create_figure("scales");
    const auto          ax  = model.add_axes(fid);
    constexpr int       K   = daemon::FigureModel::KEEP_AXIS_FORMAT;

    ipc::DiffOp op;
    ASSERT_TRUE(model.set_axis_format(fid, ax, K, 1, K, K, &op));   // yscale log10
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_AXIS_SCALE);
    EXPECT_DOUBLE_EQ(op.f1, 0.0);
    EXPECT_DOUBLE_EQ(op.f2, 1.0);
    ASSERT_TRUE(model.set_axis_format(fid, ax, 3, K, 1, K, &op));   // xscale sqrt + datetime
    EXPECT_DOUBLE_EQ(op.f1, 3.0);
    EXPECT_DOUBLE_EQ(op.f2, 1.0);   // y scale kept
    EXPECT_DOUBLE_EQ(op.f3, 1.0);
    EXPECT_DOUBLE_EQ(op.f4, 0.0);

    const auto sa = round_trip(model.snapshot({fid}).figures.at(0)).axes.at(0);
    EXPECT_EQ(sa.x_scale, 3);
    EXPECT_EQ(sa.y_scale, 1);
    EXPECT_TRUE(sa.x_datetime);
    EXPECT_FALSE(sa.y_datetime);

    // The window applies the state to a live axes.
    Figure fig;
    auto&  live = fig.subplot(1, 1, 1);
    ipc::apply_axis_format_state(live, sa);
    EXPECT_EQ(live.xscale(), ScaleType::Sqrt);
    EXPECT_EQ(live.yscale(), ScaleType::Log10);
    EXPECT_TRUE(live.xaxis_datetime());
}

TEST(PythonMplApi, SetAxisFormatRejectsBadTargetsAndValues)
{
    daemon::FigureModel model;
    const auto          fid  = model.create_figure("scales");
    const auto          ax2d = model.add_axes(fid);
    const auto          ax3d = model.add_axes(fid, 0, 1, 0, 1, true);
    constexpr int       K    = daemon::FigureModel::KEEP_AXIS_FORMAT;
    const auto          rev  = model.revision();

    ipc::DiffOp op;
    EXPECT_FALSE(model.set_axis_format(fid + 100, ax2d, 1, K, K, K, &op));   // no figure
    EXPECT_FALSE(model.set_axis_format(fid, 42, 1, K, K, K, &op));           // no axes
    EXPECT_FALSE(model.set_axis_format(fid, ax3d, 1, K, K, K, &op));         // 3D axes
    EXPECT_FALSE(model.set_axis_format(fid, ax2d, 4, K, K, K, &op));         // bad scale
    EXPECT_FALSE(model.set_axis_format(fid, ax2d, K, -2, K, K, &op));
    EXPECT_FALSE(model.set_axis_format(fid, ax2d, K, K, 2, K, &op));   // datetime is 0/1
    EXPECT_EQ(model.revision(), rev);
}

// ─── REQ_EXPORT_FIGURE ──────────────────────────────────────────────────────

TEST(PythonMplApi, ExportRequestCodecRoundTrip)
{
    ipc::ReqExportFigurePayload req;
    req.figure_id    = 12;
    req.path         = "/tmp/out dir/figure ü.png";
    req.format       = "svg";
    req.width        = 1920;
    req.height       = 1080;
    req.theme        = "light";
    const auto bytes = ipc::encode_req_export_figure(req);
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], 0x01);   // FlatBuffers prefix
    auto dec = ipc::decode_req_export_figure(bytes);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(dec->figure_id, 12u);
    EXPECT_EQ(dec->path, req.path);
    EXPECT_EQ(dec->format, "svg");
    EXPECT_EQ(dec->width, 1920u);
    EXPECT_EQ(dec->height, 1080u);
    EXPECT_EQ(dec->theme, "light");

    // Defaults survive, garbage is rejected.
    auto empty = ipc::decode_req_export_figure(ipc::encode_req_export_figure({}));
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->path.empty());
    EXPECT_EQ(empty->width, 0u);
    const std::vector<uint8_t> junk = {0x01, 0xFF, 0x00, 0x13};
    EXPECT_FALSE(ipc::decode_req_export_figure(junk).has_value());
    EXPECT_FALSE(ipc::decode_req_export_figure({}).has_value());
}

TEST(PythonMplApi, ExportFormatFromExtension)
{
    EXPECT_EQ(daemon::resolve_export_format("", "/a/b.png"), "png");
    EXPECT_EQ(daemon::resolve_export_format("", "/a/b.SVG"), "svg");
    EXPECT_EQ(daemon::resolve_export_format("", "/a/b"), "png");
    EXPECT_EQ(daemon::resolve_export_format("SVG", "/a/b.png"), "svg");
    EXPECT_EQ(daemon::resolve_export_format("pdf", "/a/b.pdf"), "");
}

TEST(PythonMplApi, ExportRequestValidation)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_mpl_api_validate";
    fs::create_directories(dir);

    ipc::ReqExportFigurePayload req;
    req.path = (dir / "ok.png").string();
    EXPECT_TRUE(daemon::validate_export_request(req).ok());

    auto expect_400 = [](const ipc::ReqExportFigurePayload& r)
    {
        const auto res = daemon::validate_export_request(r);
        EXPECT_FALSE(res.ok()) << r.path;
        EXPECT_EQ(res.code, 400u) << r.path;
        EXPECT_FALSE(res.message.empty());
    };

    auto r = req;
    r.path = "";
    expect_400(r);
    r.path = "relative.png";   // backend cwd differs from the client's
    expect_400(r);
    r.path = (dir / "missing" / "x.png").string();
    expect_400(r);
    r.path = dir.string();   // a directory
    expect_400(r);
    r        = req;
    r.format = "jpeg";
    expect_400(r);
    r       = req;
    r.width = daemon::MAX_EXPORT_EDGE + 1;
    expect_400(r);

    fs::remove_all(dir);
}

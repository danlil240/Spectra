// Step draw styles, dense-line decimation / density mode and annotation
// box / halo / rotation: geometry, IPC, persistence and vector export.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "core/annotation_layout.hpp"
#include "data/line_decimation.hpp"
#include "ipc/annotation_state.hpp"
#include "ipc/axes_format_state.hpp"
#include "ipc/codec.hpp"
#include "ui/workspace/figure_serializer.hpp"

namespace spectra
{
namespace
{

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

// ─── Step expansion ─────────────────────────────────────────────────────────

TEST(StepGeometry, CountsAndIndices)
{
    EXPECT_EQ(data::step_vertex_count(0, DrawStyle::StepsPost), 0u);
    EXPECT_EQ(data::step_vertex_count(1, DrawStyle::StepsPost), 1u);
    EXPECT_EQ(data::step_vertex_count(4, DrawStyle::Default), 4u);
    EXPECT_EQ(data::step_vertex_count(4, DrawStyle::StepsPre), 7u);
    EXPECT_EQ(data::step_vertex_count(4, DrawStyle::StepsPost), 7u);
    EXPECT_EQ(data::step_vertex_count(4, DrawStyle::StepsMid), 10u);
    EXPECT_EQ(data::step_vertex_index(3, DrawStyle::StepsPost), 6u);
    EXPECT_EQ(data::step_vertex_index(3, DrawStyle::StepsMid), 9u);
    EXPECT_FALSE(data::is_step_style(DrawStyle::Default));
    EXPECT_TRUE(data::is_step_style(DrawStyle::StepsMid));
}

TEST(StepGeometry, PrePostMidVertices)
{
    const std::vector<float> x = {0.0f, 1.0f, 3.0f};
    const std::vector<float> y = {10.0f, 20.0f, 30.0f};
    std::vector<float>       ox, oy;

    data::expand_steps(x, y, DrawStyle::StepsPost, ox, oy);
    EXPECT_EQ(ox, (std::vector<float>{0, 1, 1, 3, 3}));
    EXPECT_EQ(oy, (std::vector<float>{10, 10, 20, 20, 30}));

    ox.clear();
    oy.clear();
    data::expand_steps(x, y, DrawStyle::StepsPre, ox, oy);
    EXPECT_EQ(ox, (std::vector<float>{0, 0, 1, 1, 3}));
    EXPECT_EQ(oy, (std::vector<float>{10, 20, 20, 30, 30}));

    ox.clear();
    oy.clear();
    data::expand_steps(x, y, DrawStyle::StepsMid, ox, oy);
    EXPECT_EQ(ox, (std::vector<float>{0, 0.5f, 0.5f, 1, 2, 2, 3}));
    EXPECT_EQ(oy, (std::vector<float>{10, 10, 20, 20, 20, 30, 30}));

    // Every data point sits at its step_vertex_index.
    for (auto ds : {DrawStyle::StepsPre, DrawStyle::StepsMid, DrawStyle::StepsPost})
    {
        ox.clear();
        oy.clear();
        data::expand_steps(x, y, ds, ox, oy);
        ASSERT_EQ(ox.size(), data::step_vertex_count(x.size(), ds));
        for (size_t i = 0; i < x.size(); ++i)
        {
            EXPECT_EQ(ox[data::step_vertex_index(i, ds)], x[i]);
            EXPECT_EQ(oy[data::step_vertex_index(i, ds)], y[i]);
        }
    }
}

TEST(StepGeometry, NaNKeepsGap)
{
    const std::vector<float> x = {0.0f, 1.0f, 2.0f};
    const std::vector<float> y = {1.0f, NaN, 3.0f};
    std::vector<float>       ox, oy;
    data::expand_steps(x, y, DrawStyle::StepsPost, ox, oy);
    // p0, (1, 1), p1(NaN), (2, NaN), p2: the line breaks around the NaN.
    ASSERT_EQ(oy.size(), 5u);
    EXPECT_EQ(oy[1], 1.0f);
    EXPECT_TRUE(std::isnan(oy[2]));
    EXPECT_TRUE(std::isnan(oy[3]));
}

TEST(StepGeometry, StairsPoints)
{
    const std::vector<float> v = {3.0f, 5.0f, 2.0f};
    const std::vector<float> e = {0.0f, 1.0f, 2.0f, 4.0f};
    std::vector<float>       x, y;
    data::stairs_points(v, e, x, y);
    EXPECT_EQ(x, e);
    EXPECT_EQ(y, (std::vector<float>{3, 5, 2, 2}));
    data::stairs_points(v, std::vector<float>{0.0f, 1.0f}, x, y);
    EXPECT_TRUE(x.empty());
}

TEST(StepGeometry, AxesStepAndStairs)
{
    Axes                     ax;
    const std::vector<float> x = {0, 1, 2};
    const std::vector<float> y = {1, 2, 3};
    auto&                    s = ax.step(x, y, "r--");
    EXPECT_EQ(s.draw_style(), DrawStyle::StepsPre);
    EXPECT_EQ(s.line_style(), LineStyle::Dashed);
    EXPECT_EQ(ax.step(x, y, "-", DrawStyle::StepsPost).draw_style(), DrawStyle::StepsPost);

    const std::vector<float> e  = {0, 1, 2, 3};
    auto&                    st = ax.stairs(y, e);
    EXPECT_EQ(st.draw_style(), DrawStyle::StepsPost);
    EXPECT_EQ(st.point_count(), 4u);
    EXPECT_EQ(st.y_data()[3], 3.0f);
}

// ─── M4 decimation ──────────────────────────────────────────────────────────

struct M4Case
{
    std::vector<float>    x, y;
    std::vector<uint32_t> keep;
};

M4Case run_m4(size_t n, size_t columns, double x0, double x1, unsigned seed)
{
    M4Case                          c;
    std::mt19937                    rng(seed);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i)
    {
        c.x.push_back(static_cast<float>(i) / static_cast<float>(n) * 10.0f);
        c.y.push_back(std::sin(c.x.back()) + noise(rng));
    }
    const double w = (x1 - x0) / static_cast<double>(columns);
    data::m4_decimate(
        c.y,
        0,
        n,
        columns,
        [&](size_t i) { return (static_cast<double>(c.x[i]) - x0) / w; },
        c.keep);
    return c;
}

TEST(M4Decimation, KeepsFirstLastMinMaxOfEveryColumn)
{
    const size_t n = 100000, cols = 400;
    const auto   c = run_m4(n, cols, 0.0, 10.0, 7);
    ASSERT_FALSE(c.keep.empty());
    EXPECT_LE(c.keep.size(), 4 * (cols + 2));
    EXPECT_TRUE(std::is_sorted(c.keep.begin(), c.keep.end()));
    EXPECT_EQ(std::adjacent_find(c.keep.begin(), c.keep.end()), c.keep.end());
    EXPECT_EQ(c.keep.front(), 0u);
    EXPECT_EQ(c.keep.back(), n - 1);

    // Per column: min / max / first / last of the kept points equal those of all points.
    // Same column mapping as run_m4 (x0 = 0, column width 10 / cols).
    const double w      = 10.0 / static_cast<double>(cols);
    auto         col_of = [&](size_t i)
    {
        const double v = static_cast<double>(c.x[i]) / w;
        return v >= static_cast<double>(cols) ? static_cast<long>(cols) : static_cast<long>(v);
    };
    std::vector<float> all_min(cols + 1, INFINITY), all_max(cols + 1, -INFINITY);
    std::vector<float> kept_min(cols + 1, INFINITY), kept_max(cols + 1, -INFINITY);
    std::vector<long>  first(cols + 1, -1), last(cols + 1, -1);
    for (size_t i = 0; i < n; ++i)
    {
        const long k = col_of(i);
        all_min[k]   = std::min(all_min[k], c.y[i]);
        all_max[k]   = std::max(all_max[k], c.y[i]);
        if (first[k] < 0)
            first[k] = static_cast<long>(i);
        last[k] = static_cast<long>(i);
    }
    std::vector<bool> kept(n, false);
    for (auto i : c.keep)
    {
        kept[i]      = true;
        const long k = col_of(i);
        kept_min[k]  = std::min(kept_min[k], c.y[i]);
        kept_max[k]  = std::max(kept_max[k], c.y[i]);
    }
    for (size_t k = 0; k <= cols; ++k)
    {
        if (first[k] < 0)
            continue;
        EXPECT_EQ(kept_min[k], all_min[k]) << "column " << k;
        EXPECT_EQ(kept_max[k], all_max[k]) << "column " << k;
        EXPECT_TRUE(kept[first[k]]) << "column " << k;
        EXPECT_TRUE(kept[last[k]]) << "column " << k;
    }
}

TEST(M4Decimation, OutOfViewPointsMergeIntoEdgeColumns)
{
    // View covers only the middle of the data: everything left of it is one
    // column, everything right of it another.
    const auto c    = run_m4(20000, 100, 4.0, 6.0, 3);
    size_t     left = 0, right = 0;
    for (auto i : c.keep)
    {
        left += c.x[i] < 4.0f ? 1 : 0;
        right += c.x[i] >= 6.0f ? 1 : 0;
    }
    EXPECT_LE(left, 4u);
    EXPECT_LE(right, 4u);
}

TEST(M4Decimation, NaNGapsSurvive)
{
    std::vector<float> y(1000, 1.0f);
    y[500] = NaN;
    std::vector<uint32_t> keep;
    data::m4_decimate(
        y,
        0,
        y.size(),
        10,
        [](size_t i) { return static_cast<double>(i) / 100.0; },
        keep);
    EXPECT_NE(std::find(keep.begin(), keep.end(), 500u), keep.end());
    EXPECT_NE(std::find(keep.begin(), keep.end(), 499u), keep.end());   // last before the gap
    EXPECT_NE(std::find(keep.begin(), keep.end(), 501u), keep.end());   // first after it
}

TEST(M4Decimation, Threshold)
{
    EXPECT_FALSE(data::should_m4_decimate(1000, 0));
    EXPECT_FALSE(data::should_m4_decimate(800 * data::M4_MIN_POINTS_PER_COLUMN, 800));
    EXPECT_TRUE(data::should_m4_decimate(800 * data::M4_MIN_POINTS_PER_COLUMN + 1, 800));
}

TEST(DensityMode, AlphaFallsWithPointsPerColumn)
{
    EXPECT_EQ(data::density_alpha(100, 800), 1.0f);
    EXPECT_EQ(data::density_alpha(100, 0), 1.0f);
    const float a4   = data::density_alpha(3200, 800);
    const float a400 = data::density_alpha(320000, 800);
    EXPECT_NEAR(a4, 0.2f, 1e-5f);
    EXPECT_LT(a400, a4);
    EXPECT_GE(a400, 0.01f);
    EXPECT_EQ(data::density_alpha(100000000, 10), 0.01f);
}

TEST(LineSeriesStyle, SettersMarkDirty)
{
    LineSeries s;
    s.clear_dirty();
    s.draw_style(DrawStyle::StepsMid);
    EXPECT_TRUE(s.is_dirty());
    s.clear_dirty();
    s.dense_mode(DenseLineMode::Density);
    EXPECT_TRUE(s.is_dirty());
    EXPECT_EQ(s.dense_mode(), DenseLineMode::Density);
}

// ─── Annotation layout ──────────────────────────────────────────────────────

TEST(AnnotationLayout, PaddedAndRotatedBox)
{
    const annotation_layout::Box b{10, 20, 50, 30};
    const auto                   p = annotation_layout::padded(b, 4);
    EXPECT_FLOAT_EQ(p.x0, 6);
    EXPECT_FLOAT_EQ(p.y1, 34);

    const auto q0 = annotation_layout::rotate_box(b, 10, 30, 0.0f);
    EXPECT_FLOAT_EQ(q0.x[1], 50);
    EXPECT_FLOAT_EQ(q0.y[2], 30);

    // 90 degrees counter-clockwise about the bottom-left corner: the box now
    // rises from the pivot (screen y up = smaller y), 40 px tall, 10 px wide.
    const auto q = annotation_layout::rotate_box(b, 10, 30, 90.0f);
    const auto r = annotation_layout::bounds(q);
    EXPECT_NEAR(r.x0, 0.0f, 1e-4f);
    EXPECT_NEAR(r.x1, 10.0f, 1e-4f);
    EXPECT_NEAR(r.y0, -10.0f, 1e-4f);
    EXPECT_NEAR(r.y1, 30.0f, 1e-4f);
    // Top-right corner (50, 20) goes to (0, -10).
    EXPECT_NEAR(q.x[1], 0.0f, 1e-4f);
    EXPECT_NEAR(q.y[1], -10.0f, 1e-4f);
}

TEST(AnnotationLayout, ExtentIncludesBoxPaddingAndRotation)
{
    TextAnnotation ann(0, 0, "hello");
    ann.halign(TextAnnotation::HAlign::Left).valign(TextAnnotation::VAlign::Bottom);
    auto e = annotation_layout::annotation_extent(ann, 100, 100, 40, 10);
    EXPECT_FLOAT_EQ(e.x0, 100);
    EXPECT_FLOAT_EQ(e.y0, 90);

    TextAnnotation::Box bb;
    bb.pad = 5;
    ann.bbox(bb);
    e = annotation_layout::annotation_extent(ann, 100, 100, 40, 10);
    EXPECT_FLOAT_EQ(e.x0, 95);
    EXPECT_FLOAT_EQ(e.x1, 145);
    EXPECT_FLOAT_EQ(e.y0, 85);

    ann.rotation(90.0f);
    e = annotation_layout::annotation_extent(ann, 100, 100, 40, 10);
    // Padded box (95..145, 85..105) rotated about (100, 100).
    EXPECT_NEAR(e.x0, 85.0f, 1e-3f);
    EXPECT_NEAR(e.x1, 105.0f, 1e-3f);
    EXPECT_NEAR(e.y0, 55.0f, 1e-3f);
    EXPECT_NEAR(e.y1, 105.0f, 1e-3f);
}

TEST(TextAnnotationStyle, Api)
{
    TextAnnotation a(1, 2, "t");
    EXPECT_FALSE(a.bbox());
    a.bbox(true);
    ASSERT_TRUE(a.bbox());
    EXPECT_FLOAT_EQ(a.bbox()->pad, 4.0f);
    a.bbox(false);
    EXPECT_FALSE(a.bbox());
    a.halo_color(colors::red);
    EXPECT_TRUE(a.halo());
    a.rotation(NaN);
    EXPECT_EQ(a.rotation(), 0.0f);
    a.rotation(45.0f);
    EXPECT_EQ(a.rotation(), 45.0f);
}

// ─── IPC ────────────────────────────────────────────────────────────────────

TextAnnotation styled_annotation()
{
    TextAnnotation      a(1.5f, 2.5f, "peak");
    TextAnnotation::Box bb;
    bb.pad        = 6.0f;
    bb.fill       = Color(1.0f, 1.0f, 0.8f, 0.7f);
    bb.edge_width = 0.0f;
    a.bbox(bb).halo_color(Color(0, 0, 0, 1)).halo_width(3.0f).rotation(30.0f);
    return a;
}

TEST(AnnotationIpc, StyleRoundTrip)
{
    const auto st = ipc::annotation_to_state(styled_annotation());
    EXPECT_TRUE(ipc::has_annotation_style(st));
    const auto packed = ipc::pack_annotation(st);
    EXPECT_EQ(packed.size(), ipc::ANNOTATION_PACKED_FLOATS_WITH_STYLE);
    ipc::SnapshotAnnotationState back;
    ASSERT_TRUE(ipc::unpack_annotation(packed, st.text, back));
    EXPECT_EQ(back, st);

    TextAnnotation t(0, 0, "");
    ipc::apply_annotation_state(t, back);
    ASSERT_TRUE(t.bbox());
    EXPECT_FLOAT_EQ(t.bbox()->pad, 6.0f);
    ASSERT_TRUE(t.bbox()->fill);
    EXPECT_FLOAT_EQ(t.bbox()->fill->a, 0.7f);
    EXPECT_FALSE(t.bbox()->edge);
    EXPECT_FLOAT_EQ(t.bbox()->edge_width, 0.0f);
    EXPECT_TRUE(t.halo());
    EXPECT_FLOAT_EQ(t.halo_width(), 3.0f);
    EXPECT_FLOAT_EQ(t.rotation(), 30.0f);
}

TEST(AnnotationIpc, PlainAnnotationKeepsShortLayout)
{
    TextAnnotation a(1, 2, "plain");
    const auto     st = ipc::annotation_to_state(a);
    EXPECT_EQ(ipc::pack_annotation(st).size(), ipc::ANNOTATION_PACKED_FLOATS);
    ipc::SnapshotAnnotationState back;
    ASSERT_TRUE(ipc::unpack_annotation(ipc::pack_annotation(st), "plain", back));
    EXPECT_FALSE(back.has_bbox);
    EXPECT_EQ(back.rotation, 0.0f);
}

TEST(AnnotationIpc, BadStyleValuesFallBack)
{
    auto st   = ipc::annotation_to_state(styled_annotation());
    auto tail = ipc::pack_annotation_style(st);
    tail[1]   = NaN;        // pad
    tail[20]  = INFINITY;   // rotation
    ipc::unpack_annotation_style(tail, st);
    EXPECT_FLOAT_EQ(st.bbox_pad, 4.0f);
    EXPECT_FLOAT_EQ(st.rotation, 0.0f);
}

TEST(DrawStyleIpc, OpAndWireValues)
{
    auto op = ipc::make_series_draw_style_op(7, 0, 3, DrawStyle::StepsMid, DenseLineMode::Density);
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_SERIES_DRAW_STYLE);
    EXPECT_EQ(static_cast<int>(op.type), 42);
    ipc::SnapshotSeriesState ss;
    ipc::apply_series_draw_style_op(ss, op);
    EXPECT_EQ(ss.draw_style, static_cast<uint8_t>(DrawStyle::StepsMid));
    EXPECT_EQ(ss.dense_mode, static_cast<uint8_t>(DenseLineMode::Density));
    EXPECT_EQ(ipc::draw_style_from_wire(99), DrawStyle::Default);
    EXPECT_EQ(ipc::dense_mode_from_wire(-1), DenseLineMode::Auto);

    LineSeries line;
    ipc::apply_series_draw_style(line, ss.draw_style, ss.dense_mode);
    EXPECT_EQ(line.draw_style(), DrawStyle::StepsMid);
    EXPECT_EQ(line.dense_mode(), DenseLineMode::Density);
}

TEST(DrawStyleIpc, SnapshotRoundTrip)
{
    ipc::StateSnapshotPayload snap;
    ipc::SnapshotFigureState  fig;
    fig.figure_id = 1;
    ipc::SnapshotAxisState ax;
    ax.annotations.push_back(ipc::annotation_to_state(styled_annotation()));
    ax.annotations.push_back(ipc::annotation_to_state(TextAnnotation(0, 0, "plain")));
    fig.axes.push_back(ax);
    ipc::SnapshotSeriesState ss;
    ss.type       = "line";
    ss.draw_style = 3;
    ss.dense_mode = 2;
    fig.series.push_back(ss);
    snap.figures.push_back(fig);

    const auto decoded = ipc::decode_state_snapshot(ipc::encode_state_snapshot(snap));
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded->figures.size(), 1u);
    const auto& f = decoded->figures[0];
    ASSERT_EQ(f.axes[0].annotations.size(), 2u);
    EXPECT_EQ(f.axes[0].annotations[0], ax.annotations[0]);
    EXPECT_EQ(f.axes[0].annotations[1], ax.annotations[1]);
    EXPECT_EQ(f.series[0].draw_style, 3);
    EXPECT_EQ(f.series[0].dense_mode, 2);

    ipc::StateDiffPayload diff;
    diff.ops.push_back(
        ipc::make_series_draw_style_op(1, 0, 0, DrawStyle::StepsPre, DenseLineMode::Exact));
    const auto d = ipc::decode_state_diff(ipc::encode_state_diff(diff));
    ASSERT_TRUE(d);
    ASSERT_EQ(d->ops.size(), 1u);
    EXPECT_EQ(d->ops[0].type, ipc::DiffOp::Type::SET_SERIES_DRAW_STYLE);
    EXPECT_EQ(d->ops[0].f1, 1.0);
    EXPECT_EQ(d->ops[0].f2, 1.0);
}

// ─── Persistence ────────────────────────────────────────────────────────────

TEST(PlotExtrasPersistence, BinaryWorkspaceRoundTrip)
{
    const std::string path =
        (std::filesystem::temp_directory_path() / "spectra_test_plot_extras.spectra").string();
    Figure                   src({640, 480});
    auto&                    ax = src.subplot(1, 1, 1);
    const std::vector<float> x  = {0, 1, 2, 3};
    const std::vector<float> y  = {1, 3, 2, 4};
    ax.line(x, y).draw_style(DrawStyle::StepsPost).dense_mode(DenseLineMode::Density);
    ax.line(x, y);   // plain line: no style chunk
    auto& t = ax.text(1, 2, "boxed");
    t.bbox(true).halo(true).rotation(15.0f);
    ax.text(2, 3, "plain");
    ASSERT_TRUE(FigureSerializer::save(path, src));

    Figure dst({640, 480});
    ASSERT_TRUE(FigureSerializer::load(path, dst));
    std::remove(path.c_str());
    auto* lax = dynamic_cast<Axes*>(dst.axes()[0].get());
    ASSERT_NE(lax, nullptr);
    ASSERT_EQ(lax->series().size(), 2u);
    auto* l0 = dynamic_cast<LineSeries*>(lax->series()[0].get());
    auto* l1 = dynamic_cast<LineSeries*>(lax->series()[1].get());
    ASSERT_TRUE(l0 && l1);
    EXPECT_EQ(l0->draw_style(), DrawStyle::StepsPost);
    EXPECT_EQ(l0->dense_mode(), DenseLineMode::Density);
    EXPECT_EQ(l1->draw_style(), DrawStyle::Default);
    ASSERT_EQ(lax->annotations().size(), 2u);
    const auto& a0 = *lax->annotations()[0];
    EXPECT_TRUE(a0.bbox());
    EXPECT_TRUE(a0.halo());
    EXPECT_FLOAT_EQ(a0.rotation(), 15.0f);
    EXPECT_FALSE(lax->annotations()[1]->bbox());
    EXPECT_EQ(lax->annotations()[1]->rotation(), 0.0f);
}

// ─── Vector export ──────────────────────────────────────────────────────────

TEST(PlotExtrasSvg, StepLineHasCorners)
{
    Figure                   fig({400, 300});
    auto&                    ax = fig.subplot(1, 1, 1);
    const std::vector<float> x  = {0, 1, 2};
    const std::vector<float> y  = {0, 1, 0};
    ax.line(x, y);
    fig.compute_layout();
    const std::string plain = SvgExporter::to_string(fig);
    dynamic_cast<LineSeries*>(ax.series()[0].get())->draw_style(DrawStyle::StepsPost);
    const std::string steps          = SvgExporter::to_string(fig);
    auto              points_in_line = [](const std::string& svg)
    {
        const auto el   = svg.find("<path class=\"line\"");
        const auto d    = svg.find(" d=\"", el);
        const auto end  = svg.find('"', d + 4);
        const auto path = svg.substr(d + 4, end - d - 4);
        return std::count(path.begin(), path.end(), ' ') + 1;   // "Mx y x y x y"
    };
    ASSERT_NE(plain.find("<path class=\"line\""), std::string::npos);
    // Two more vertices (the step corners): each adds an "x y" pair.
    EXPECT_EQ(points_in_line(steps) - points_in_line(plain), 4);

    // Dashes (line style) combine with steps: the stepped path is dashed.
    ax.series()[0]->line_style(LineStyle::Dashed);
    const std::string dashed = SvgExporter::to_string(fig);
    EXPECT_NE(dashed.find("stroke-dasharray"), std::string::npos);
    EXPECT_EQ(points_in_line(dashed), points_in_line(steps));
}

TEST(PlotExtrasSvg, AnnotationBoxHaloRotation)
{
    Figure fig({400, 300});
    auto&  ax = fig.subplot(1, 1, 1);
    ax.xlim(0, 10);
    ax.ylim(0, 10);
    ax.text(5, 5, "boxed").bbox(true).rotation(30.0f);
    ax.text(2, 2, "halo").halo(true);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find("rotate(-30"), std::string::npos);
    EXPECT_NE(svg.find("paint-order=\"stroke\""), std::string::npos);
    // The box is a polygon in the annotations group.
    const auto group = svg.find("class=\"annotations\"");
    ASSERT_NE(group, std::string::npos);
    EXPECT_NE(svg.find("<polygon", group), std::string::npos);
}

TEST(PlotExtrasSvg, DensityModeSplitsTranslucentPieces)
{
    Figure             fig({400, 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x(20000), y(20000);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = static_cast<float>((i * 7919) % 101);
    }
    ax.line(x, y).dense_mode(DenseLineMode::Density);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    size_t            n = 0, pos = 0;
    while ((pos = svg.find("class=\"line\"", pos)) != std::string::npos)
    {
        ++n;
        ++pos;
    }
    EXPECT_GT(n, 100u);
    EXPECT_NE(svg.find("stroke-opacity=\"0.0"), std::string::npos);
}

}   // namespace
}   // namespace spectra

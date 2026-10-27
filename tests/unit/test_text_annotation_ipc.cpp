// Text annotations (Axes::text / Axes::annotate) over IPC: snapshot codec,
// packed property/diff layout, daemon FigureModel and the multiproc app's
// figure_to_snapshot() / incremental diff helper.

#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include <vector>

#include "daemon/figure_model.hpp"
#include "ipc/annotation_state.hpp"
#include "ipc/codec.hpp"
#include "ui/app/figure_snapshot.hpp"

using namespace spectra;
namespace ipc = spectra::ipc;

namespace
{

ipc::SnapshotAnnotationState make_state(float x, const char* text)
{
    ipc::SnapshotAnnotationState a;
    a.x         = x;
    a.y         = 2.5f;
    a.text      = text;
    a.has_color = true;
    a.color_r   = 0.9f;
    a.color_g   = 0.1f;
    a.color_b   = 0.2f;
    a.color_a   = 0.8f;
    a.font_size = 16.0f;
    a.halign    = 2;
    a.valign    = 0;
    a.has_arrow = true;
    a.arrow_x   = -1.0f;
    a.arrow_y   = 4.0f;
    a.visible   = false;
    return a;
}

}   // namespace

TEST(TextAnnotationIpc, PackUnpackRoundTrip)
{
    const auto a      = make_state(1.5f, "peak");
    const auto packed = ipc::pack_annotation(a);
    ASSERT_EQ(packed.size(), ipc::ANNOTATION_PACKED_FLOATS);

    ipc::SnapshotAnnotationState b;
    ASSERT_TRUE(ipc::unpack_annotation(packed, "peak", b));
    EXPECT_EQ(a, b);
}

TEST(TextAnnotationIpc, UnpackRejectsShortPayload)
{
    std::vector<float>           short_data(ipc::ANNOTATION_PACKED_FLOATS - 1, 0.0f);
    ipc::SnapshotAnnotationState out;
    out.text = "unchanged";
    EXPECT_FALSE(ipc::unpack_annotation(short_data, "x", out));
    EXPECT_EQ(out.text, "unchanged");
}

TEST(TextAnnotationIpc, SnapshotCodecCarriesAnnotations)
{
    ipc::StateSnapshotPayload snap;
    ipc::SnapshotFigureState  fig;
    fig.figure_id = 7;
    ipc::SnapshotAxisState ax;
    ax.annotations.push_back(make_state(1.0f, "first"));
    ipc::SnapshotAnnotationState plain;   // all defaults (theme colour, no arrow)
    plain.text = "second {with} [braces] \"quotes\"";
    ax.annotations.push_back(plain);
    fig.axes.push_back(ax);
    fig.axes.push_back(ipc::SnapshotAxisState{});   // axes without annotations
    snap.figures.push_back(fig);

    auto bytes   = ipc::encode_state_snapshot(snap);
    auto decoded = ipc::decode_state_snapshot(bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->figures.size(), 1u);
    ASSERT_EQ(decoded->figures[0].axes.size(), 2u);
    EXPECT_EQ(decoded->figures[0].axes[0].annotations, ax.annotations);
    EXPECT_TRUE(decoded->figures[0].axes[1].annotations.empty());
}

TEST(TextAnnotationIpc, UpdatePropertyCarriesData)
{
    ipc::ReqUpdatePropertyPayload p;
    p.figure_id    = 3;
    p.axes_index   = 1;
    p.series_index = 0xFFFFFFFFu;
    p.property     = "annotation";
    p.str_val      = "label";
    p.data         = ipc::pack_annotation(make_state(2.0f, "label"));

    auto decoded = ipc::decode_req_update_property(ipc::encode_req_update_property(p));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->property, "annotation");
    EXPECT_EQ(decoded->series_index, 0xFFFFFFFFu);
    EXPECT_EQ(decoded->data, p.data);

    ipc::ReqUpdateBatchPayload batch;
    batch.updates.push_back(p);
    auto db = ipc::decode_req_update_batch(ipc::encode_req_update_batch(batch));
    ASSERT_TRUE(db.has_value());
    ASSERT_EQ(db->updates.size(), 1u);
    EXPECT_EQ(db->updates[0].data, p.data);
}

TEST(TextAnnotationIpc, DiffOpCodecCarriesAnnotationOps)
{
    ipc::StateDiffPayload diff;
    const auto            a = make_state(4.0f, "moved");
    diff.ops.push_back(ipc::make_annotation_op(ipc::DiffOp::Type::SET_ANNOTATION, 9, 0, 1, &a));
    diff.ops.push_back(
        ipc::make_annotation_op(ipc::DiffOp::Type::CLEAR_ANNOTATIONS, 9, 0, 0, nullptr));

    auto decoded = ipc::decode_state_diff(ipc::encode_state_diff(diff));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 2u);
    EXPECT_EQ(decoded->ops[0].type, ipc::DiffOp::Type::SET_ANNOTATION);
    EXPECT_EQ(decoded->ops[0].series_index, 1u);
    ipc::SnapshotAnnotationState back;
    ASSERT_TRUE(ipc::unpack_annotation(decoded->ops[0].data, decoded->ops[0].str_val, back));
    EXPECT_EQ(back, a);
    EXPECT_EQ(decoded->ops[1].type, ipc::DiffOp::Type::CLEAR_ANNOTATIONS);
}

TEST(TextAnnotationIpc, FigureToSnapshotIncludesAnnotations)
{
    Figure fig;
    auto&  ax = fig.subplot(1, 1, 1);
    ax.line(std::vector<float>{0.0f, 1.0f}, std::vector<float>{0.0f, 1.0f});
    ax.text(0.5f, 0.25f, "plain");
    ax.annotate("max", 1.0f, 1.0f, 0.2f, 0.9f).color(Color(1.0f, 0.0f, 0.0f)).font_size(18.0f);

    auto snap = figure_to_snapshot(fig, 100);
    ASSERT_EQ(snap.axes.size(), 1u);
    const auto& anns = snap.axes[0].annotations;
    ASSERT_EQ(anns.size(), 2u);
    EXPECT_EQ(anns[0].text, "plain");
    EXPECT_FLOAT_EQ(anns[0].x, 0.5f);
    EXPECT_FALSE(anns[0].has_color);
    EXPECT_FALSE(anns[0].has_arrow);
    EXPECT_EQ(anns[1].text, "max");
    EXPECT_TRUE(anns[1].has_arrow);
    EXPECT_FLOAT_EQ(anns[1].arrow_x, 1.0f);
    EXPECT_TRUE(anns[1].has_color);
    EXPECT_FLOAT_EQ(anns[1].color_r, 1.0f);
    EXPECT_FLOAT_EQ(anns[1].font_size, 18.0f);
    EXPECT_EQ(anns[1].halign, static_cast<uint8_t>(TextAnnotation::HAlign::Center));
}

TEST(TextAnnotationIpc, FigureToSnapshotUsesTabTitle)
{
    Figure fig;
    fig.subplot(1, 1, 1);
    fig.set_tab_title("Telemetry");
    EXPECT_EQ(figure_to_snapshot(fig, 1).title, "Telemetry");
}

TEST(TextAnnotationIpc, StateRoundTripsThroughTextAnnotation)
{
    Axes       ax;
    const auto a = make_state(3.0f, "rt");
    ipc::apply_annotation_state(ax.text(0.0f, 0.0f, ""), a);
    ASSERT_EQ(ax.annotations().size(), 1u);
    EXPECT_EQ(ipc::annotation_to_state(*ax.annotations()[0]), a);
}

TEST(TextAnnotationIpc, DiffAnnotationsReproducesTarget)
{
    const std::vector<ipc::SnapshotAnnotationState> before = {make_state(1.0f, "a"),
                                                              make_state(2.0f, "b")};
    auto                                            edited = before;
    edited[1].text                                         = "b2";
    auto appended                                          = edited;
    appended.push_back(make_state(3.0f, "c"));
    const std::vector<ipc::SnapshotAnnotationState> shrunk = {make_state(5.0f, "z")};

    using List = std::vector<ipc::SnapshotAnnotationState>;
    for (const List* target : std::vector<const List*>{&edited, &appended, &shrunk, &before})
    {
        auto ops  = ipc::diff_annotations(before, *target, 1, 0);
        auto list = before;
        for (const auto& op : ops)
            ASSERT_TRUE(ipc::apply_annotation_op(list, op));
        EXPECT_EQ(list, *target);
    }

    // Pure append/edit never clears (cheap for the window).
    auto ops = ipc::diff_annotations(before, appended, 1, 0);
    ASSERT_EQ(ops.size(), 2u);
    EXPECT_EQ(ops[0].type, ipc::DiffOp::Type::SET_ANNOTATION);
    EXPECT_EQ(ops[1].type, ipc::DiffOp::Type::ADD_ANNOTATION);
    EXPECT_TRUE(ipc::diff_annotations(before, before, 1, 0).empty());
}

TEST(TextAnnotationIpc, FigureModelAddSetClear)
{
    daemon::FigureModel model;
    auto                fid = model.create_figure("f");
    model.add_axes(fid);

    ipc::DiffOp op;
    EXPECT_FALSE(model.add_annotation(fid, 5, make_state(0.0f, "bad axes"), &op));
    EXPECT_FALSE(model.set_annotation(fid, 0, 0, make_state(0.0f, "no such"), &op));

    auto rev = model.revision();
    ASSERT_TRUE(model.add_annotation(fid, 0, make_state(1.0f, "one"), &op));
    EXPECT_GT(model.revision(), rev);
    EXPECT_EQ(op.type, ipc::DiffOp::Type::ADD_ANNOTATION);
    EXPECT_EQ(op.series_index, 0u);
    ASSERT_TRUE(model.add_annotation(fid, 0, make_state(2.0f, "two"), &op));
    EXPECT_EQ(model.annotation_count(fid, 0), 2u);

    ASSERT_TRUE(model.set_annotation(fid, 0, 1, make_state(9.0f, "two'"), &op));
    EXPECT_EQ(op.type, ipc::DiffOp::Type::SET_ANNOTATION);

    auto snap = model.snapshot();
    ASSERT_EQ(snap.figures.size(), 1u);
    ASSERT_EQ(snap.figures[0].axes[0].annotations.size(), 2u);
    EXPECT_EQ(snap.figures[0].axes[0].annotations[1].text, "two'");
    EXPECT_FLOAT_EQ(snap.figures[0].axes[0].annotations[1].x, 9.0f);

    ASSERT_TRUE(model.clear_annotations(fid, 0, &op));
    EXPECT_EQ(op.type, ipc::DiffOp::Type::CLEAR_ANNOTATIONS);
    EXPECT_EQ(model.annotation_count(fid, 0), 0u);
}

TEST(TextAnnotationIpc, FigureModelAppliesAnnotationDiffOps)
{
    daemon::FigureModel model;
    auto                fid = model.create_figure("f");
    model.add_axes(fid);

    const auto a = make_state(1.0f, "from app");
    EXPECT_TRUE(model.apply_diff_op(
        ipc::make_annotation_op(ipc::DiffOp::Type::ADD_ANNOTATION, fid, 0, 0, &a)));
    EXPECT_EQ(model.annotation_count(fid, 0), 1u);
    // Out-of-range SET and unknown axes are rejected.
    EXPECT_FALSE(model.apply_diff_op(
        ipc::make_annotation_op(ipc::DiffOp::Type::SET_ANNOTATION, fid, 0, 3, &a)));
    EXPECT_FALSE(model.apply_diff_op(
        ipc::make_annotation_op(ipc::DiffOp::Type::ADD_ANNOTATION, fid, 4, 0, &a)));

    // A snapshot pushed by the app keeps its annotations.
    ipc::StateSnapshotPayload snap;
    ipc::SnapshotFigureState  fs;
    ipc::SnapshotAxisState    ax;
    ax.annotations.push_back(a);
    fs.axes.push_back(ax);
    snap.figures.push_back(fs);
    auto ids = model.load_snapshot(snap);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(model.annotation_count(ids[0], 0), 1u);
}

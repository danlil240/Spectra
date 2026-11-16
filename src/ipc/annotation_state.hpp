// annotation_state.hpp — Convert 2D text annotations between the core model
// (spectra::TextAnnotation), the IPC snapshot struct and the flat float
// layout carried by DiffOp::data / ReqUpdatePropertyPayload::data.
//
// Header-only so the app, daemon, window agent and unit tests share one copy.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <spectra/annotation.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "message.hpp"

namespace spectra::ipc
{

// Packed float layout (index: field):
//   0 x, 1 y, 2 has_color, 3 r, 4 g, 5 b, 6 a, 7 font_size, 8 halign, 9 valign,
//   10 has_arrow, 11 arrow_x, 12 arrow_y, 13 visible
// Optional style tail (older senders stop after 13; missing = no box, no
// halo, no rotation):
//   14 has_bbox, 15 bbox_pad, 16 has_bbox_fill, 17-20 bbox_fill rgba,
//   21 has_bbox_edge, 22-25 bbox_edge rgba, 26 bbox_edge_width, 27 halo,
//   28 has_halo_color, 29-32 halo_color rgba, 33 halo_width, 34 rotation
// The text travels separately (DiffOp::str_val / ReqUpdatePropertyPayload::str_val).
// Python's _axes.py mirrors this layout.
inline constexpr size_t ANNOTATION_PACKED_FLOATS = 14;
inline constexpr size_t ANNOTATION_STYLE_FLOATS  = 21;
inline constexpr size_t ANNOTATION_PACKED_FLOATS_WITH_STYLE =
    ANNOTATION_PACKED_FLOATS + ANNOTATION_STYLE_FLOATS;

// The style tail (indices 14..34 above, rebased to 0).
inline std::vector<float> pack_annotation_style(const SnapshotAnnotationState& a)
{
    auto b = [](bool v) { return v ? 1.0f : 0.0f; };
    return {b(a.has_bbox),     a.bbox_pad,      b(a.has_bbox_fill),  a.bbox_fill[0],
            a.bbox_fill[1],    a.bbox_fill[2],  a.bbox_fill[3],      b(a.has_bbox_edge),
            a.bbox_edge[0],    a.bbox_edge[1],  a.bbox_edge[2],      a.bbox_edge[3],
            a.bbox_edge_width, b(a.halo),       b(a.has_halo_color), a.halo_color[0],
            a.halo_color[1],   a.halo_color[2], a.halo_color[3],     a.halo_width,
            a.rotation};
}

// True when @p a has any style beyond the defaults (worth sending).
inline bool has_annotation_style(const SnapshotAnnotationState& a)
{
    return a.has_bbox || a.halo || a.rotation != 0.0f;
}

// Read a style tail; a short or empty span leaves the defaults.  Non-finite
// numbers fall back to the defaults too.
inline void unpack_annotation_style(std::span<const float> d, SnapshotAnnotationState& a)
{
    if (d.size() < ANNOTATION_STYLE_FLOATS)
        return;
    auto num = [](float v, float fallback, float lo, float hi)
    { return (std::isfinite(v) && v >= lo && v <= hi) ? v : fallback; };
    auto rgba = [&](size_t i, std::array<float, 4>& out)
    {
        for (size_t k = 0; k < 4; ++k)
            out[k] = num(d[i + k], k == 3 ? 1.0f : 0.0f, 0.0f, 1.0f);
    };
    a.has_bbox      = d[0] != 0.0f;
    a.bbox_pad      = num(d[1], 4.0f, 0.0f, 1000.0f);
    a.has_bbox_fill = d[2] != 0.0f;
    rgba(3, a.bbox_fill);
    a.has_bbox_edge = d[7] != 0.0f;
    rgba(8, a.bbox_edge);
    a.bbox_edge_width = num(d[12], 1.0f, 0.0f, 1000.0f);
    a.halo            = d[13] != 0.0f;
    a.has_halo_color  = d[14] != 0.0f;
    rgba(15, a.halo_color);
    a.halo_width = num(d[19], 2.0f, 0.0f, 1000.0f);
    a.rotation   = num(d[20], 0.0f, -1.0e6f, 1.0e6f);
}

inline std::vector<float> pack_annotation(const SnapshotAnnotationState& a)
{
    std::vector<float> out{a.x,
                           a.y,
                           a.has_color ? 1.0f : 0.0f,
                           a.color_r,
                           a.color_g,
                           a.color_b,
                           a.color_a,
                           a.font_size,
                           static_cast<float>(a.halign),
                           static_cast<float>(a.valign),
                           a.has_arrow ? 1.0f : 0.0f,
                           a.arrow_x,
                           a.arrow_y,
                           a.visible ? 1.0f : 0.0f};
    if (has_annotation_style(a))
    {
        const auto tail = pack_annotation_style(a);
        out.insert(out.end(), tail.begin(), tail.end());
    }
    return out;
}

// Returns false (and leaves @p out untouched) when @p data is too short.
inline bool unpack_annotation(std::span<const float>   data,
                              std::string_view         text,
                              SnapshotAnnotationState& out)
{
    if (data.size() < ANNOTATION_PACKED_FLOATS)
        return false;
    auto align = [](float v, uint8_t fallback) -> uint8_t
    {
        if (!(v >= 0.0f && v <= 2.0f))
            return fallback;
        return static_cast<uint8_t>(v + 0.5f);
    };
    SnapshotAnnotationState a;
    a.x         = data[0];
    a.y         = data[1];
    a.text      = std::string(text);
    a.has_color = data[2] != 0.0f;
    a.color_r   = data[3];
    a.color_g   = data[4];
    a.color_b   = data[5];
    a.color_a   = data[6];
    a.font_size = data[7] > 0.0f ? data[7] : 0.0f;
    a.halign    = align(data[8], 0);
    a.valign    = align(data[9], 2);
    a.has_arrow = data[10] != 0.0f;
    a.arrow_x   = data[11];
    a.arrow_y   = data[12];
    a.visible   = data[13] != 0.0f;
    unpack_annotation_style(data.subspan(ANNOTATION_PACKED_FLOATS), a);
    out = std::move(a);
    return true;
}

inline SnapshotAnnotationState annotation_to_state(const TextAnnotation& t)
{
    SnapshotAnnotationState a;
    a.x    = t.x();
    a.y    = t.y();
    a.text = t.text();
    if (t.color())
    {
        a.has_color = true;
        a.color_r   = t.color()->r;
        a.color_g   = t.color()->g;
        a.color_b   = t.color()->b;
        a.color_a   = t.color()->a;
    }
    a.font_size = t.font_size();
    a.halign    = static_cast<uint8_t>(t.halign());
    a.valign    = static_cast<uint8_t>(t.valign());
    a.has_arrow = t.has_arrow();
    a.arrow_x   = t.arrow_x();
    a.arrow_y   = t.arrow_y();
    a.visible   = t.visible();
    if (const auto& bb = t.bbox())
    {
        a.has_bbox = true;
        a.bbox_pad = bb->pad;
        if (bb->fill)
        {
            a.has_bbox_fill = true;
            a.bbox_fill     = {bb->fill->r, bb->fill->g, bb->fill->b, bb->fill->a};
        }
        if (bb->edge)
        {
            a.has_bbox_edge = true;
            a.bbox_edge     = {bb->edge->r, bb->edge->g, bb->edge->b, bb->edge->a};
        }
        a.bbox_edge_width = bb->edge_width;
    }
    a.halo = t.halo();
    if (const auto& hc = t.halo_color())
    {
        a.has_halo_color = true;
        a.halo_color     = {hc->r, hc->g, hc->b, hc->a};
    }
    a.halo_width = t.halo_width();
    a.rotation   = t.rotation();
    return a;
}

// Apply every field except the colour reset: an unset colour in @p a leaves
// @p t's colour alone, so callers rebuilding from state should start from a
// fresh TextAnnotation (Axes::text()).
inline void apply_annotation_state(TextAnnotation& t, const SnapshotAnnotationState& a)
{
    t.text(a.text).position(a.x, a.y).font_size(a.font_size);
    t.halign(static_cast<TextAnnotation::HAlign>(a.halign <= 2 ? a.halign : 0));
    t.valign(static_cast<TextAnnotation::VAlign>(a.valign <= 2 ? a.valign : 2));
    if (a.has_color)
        t.color(Color(a.color_r, a.color_g, a.color_b, a.color_a));
    if (a.has_arrow)
        t.arrow_to(a.arrow_x, a.arrow_y);
    else
        t.clear_arrow();
    t.visible(a.visible);
    if (a.has_bbox)
    {
        TextAnnotation::Box bb;
        bb.pad = a.bbox_pad;
        if (a.has_bbox_fill)
            bb.fill = Color(a.bbox_fill[0], a.bbox_fill[1], a.bbox_fill[2], a.bbox_fill[3]);
        if (a.has_bbox_edge)
            bb.edge = Color(a.bbox_edge[0], a.bbox_edge[1], a.bbox_edge[2], a.bbox_edge[3]);
        bb.edge_width = a.bbox_edge_width;
        t.bbox(bb);
    }
    else
        t.bbox(false);
    if (a.has_halo_color)
        t.halo_color(Color(a.halo_color[0], a.halo_color[1], a.halo_color[2], a.halo_color[3]));
    t.halo(a.halo).halo_width(a.halo_width).rotation(a.rotation);
}

inline DiffOp make_annotation_op(DiffOp::Type                   type,
                                 uint64_t                       figure_id,
                                 uint32_t                       axes_index,
                                 uint32_t                       annotation_index,
                                 const SnapshotAnnotationState* a)
{
    DiffOp op;
    op.type         = type;
    op.figure_id    = figure_id;
    op.axes_index   = axes_index;
    op.series_index = annotation_index;
    if (a)
    {
        op.str_val = a->text;
        op.data    = pack_annotation(*a);
    }
    return op;
}

// Apply an annotation DiffOp to a list of annotation states (an axis cache).
// Returns false when the op is not an annotation op or is malformed.
inline bool apply_annotation_op(std::vector<SnapshotAnnotationState>& list, const DiffOp& op)
{
    switch (op.type)
    {
        case DiffOp::Type::ADD_ANNOTATION:
        {
            SnapshotAnnotationState a;
            if (!unpack_annotation(op.data, op.str_val, a))
                return false;
            list.push_back(std::move(a));
            return true;
        }
        case DiffOp::Type::SET_ANNOTATION:
        {
            if (op.series_index >= list.size())
                return false;
            return unpack_annotation(op.data, op.str_val, list[op.series_index]);
        }
        case DiffOp::Type::CLEAR_ANNOTATIONS:
            list.clear();
            return true;
        default:
            return false;
    }
}

// Ops that turn annotation list @p before into @p after (appends become
// ADD_ANNOTATION, edits SET_ANNOTATION; anything else clears and re-adds).
inline std::vector<DiffOp> diff_annotations(const std::vector<SnapshotAnnotationState>& before,
                                            const std::vector<SnapshotAnnotationState>& after,
                                            uint64_t                                    figure_id,
                                            uint32_t                                    axes_index)
{
    std::vector<DiffOp> ops;
    size_t              start = 0;
    if (after.size() >= before.size())
    {
        for (size_t i = 0; i < before.size(); ++i)
        {
            if (!(before[i] == after[i]))
                ops.push_back(make_annotation_op(DiffOp::Type::SET_ANNOTATION,
                                                 figure_id,
                                                 axes_index,
                                                 static_cast<uint32_t>(i),
                                                 &after[i]));
        }
        start = before.size();
    }
    else
    {
        ops.push_back(
            make_annotation_op(DiffOp::Type::CLEAR_ANNOTATIONS, figure_id, axes_index, 0, nullptr));
    }
    for (size_t i = start; i < after.size(); ++i)
        ops.push_back(make_annotation_op(DiffOp::Type::ADD_ANNOTATION,
                                         figure_id,
                                         axes_index,
                                         static_cast<uint32_t>(i),
                                         &after[i]));
    return ops;
}

}   // namespace spectra::ipc

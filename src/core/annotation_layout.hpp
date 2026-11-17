#pragma once

// Screen-space layout helpers for Axes text annotations, shared by the GPU
// renderer and the SVG exporter so both place text and arrows identically.

#include <cmath>
#include <spectra/annotation.hpp>

namespace spectra::annotation_layout
{

// Axis-aligned box in screen pixels (y grows downwards).
struct Box
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

// Gap between the text box and the start of its arrow, in pixels.
inline constexpr float ARROW_GAP = 4.0f;

// Box covered by text of size (w, h) whose anchor is at (ax, ay).
inline Box text_box(float                  ax,
                    float                  ay,
                    float                  w,
                    float                  h,
                    TextAnnotation::HAlign ha,
                    TextAnnotation::VAlign va)
{
    float x0 = ax;
    if (ha == TextAnnotation::HAlign::Center)
        x0 = ax - w * 0.5f;
    else if (ha == TextAnnotation::HAlign::Right)
        x0 = ax - w;

    float y0 = ay;
    if (va == TextAnnotation::VAlign::Middle)
        y0 = ay - h * 0.5f;
    else if (va == TextAnnotation::VAlign::Bottom)
        y0 = ay - h;

    return {x0, y0, x0 + w, y0 + h};
}

// Box grown by `pad` on every side.
inline Box padded(const Box& b, float pad)
{
    return {b.x0 - pad, b.y0 - pad, b.x1 + pad, b.y1 + pad};
}

// Corners of a box rotated about a pivot, in order top-left, top-right,
// bottom-right, bottom-left of the unrotated box.
struct Quad
{
    float x[4] = {};
    float y[4] = {};
};

// Box `b` rotated by `degrees` counter-clockwise on screen (y grows
// downwards) about (px, py).  0 degrees gives the box's own corners.
inline Quad rotate_box(const Box& b, float px, float py, float degrees)
{
    const float rad   = degrees * 3.14159265358979f / 180.0f;
    const float c     = std::cos(rad);
    const float s     = std::sin(rad);
    const float cx[4] = {b.x0, b.x1, b.x1, b.x0};
    const float cy[4] = {b.y0, b.y0, b.y1, b.y1};
    Quad        q;
    for (int i = 0; i < 4; ++i)
    {
        const float dx = cx[i] - px;
        const float dy = cy[i] - py;
        q.x[i]         = px + dx * c + dy * s;
        q.y[i]         = py - dx * s + dy * c;
    }
    return q;
}

// Axis-aligned bounds of a quad.
inline Box bounds(const Quad& q)
{
    Box b{q.x[0], q.y[0], q.x[0], q.y[0]};
    for (int i = 1; i < 4; ++i)
    {
        b.x0 = std::fmin(b.x0, q.x[i]);
        b.y0 = std::fmin(b.y0, q.y[i]);
        b.x1 = std::fmax(b.x1, q.x[i]);
        b.y1 = std::fmax(b.y1, q.y[i]);
    }
    return b;
}

// Screen-space extent an annotation's arrow starts from: its text box (with
// the background box padding, when it has one) rotated about the anchor.
inline Box annotation_extent(const TextAnnotation& ann, float ax, float ay, float w, float h)
{
    Box b = text_box(ax, ay, w, h, ann.halign(), ann.valign());
    if (ann.bbox())
        b = padded(b, ann.bbox()->pad);
    if (ann.rotation() == 0.0f)
        return b;
    return bounds(rotate_box(b, ax, ay, ann.rotation()));
}

// Offsets (unit circle, 8 directions) at which a halo copy of the text is
// drawn, scaled by the halo width.
inline constexpr int   HALO_COPIES = 8;
inline constexpr float HALO_DIR_X[8] =
    {1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f};
inline constexpr float HALO_DIR_Y[8] =
    {0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f};

// Start point of an arrow aimed from the centre of `b` at (tx, ty): where that
// ray leaves `b` grown by `gap`.  Returns false when the target lies inside the
// grown box (the arrow would be hidden behind the text).
inline bool arrow_start(const Box& b, float tx, float ty, float gap, float& sx, float& sy)
{
    const float cx = (b.x0 + b.x1) * 0.5f;
    const float cy = (b.y0 + b.y1) * 0.5f;
    const float hx = (b.x1 - b.x0) * 0.5f + gap;
    const float hy = (b.y1 - b.y0) * 0.5f + gap;
    const float dx = tx - cx;
    const float dy = ty - cy;
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return false;

    float s = INFINITY;
    if (std::fabs(dx) > 1e-6f)
        s = std::fmin(s, hx / std::fabs(dx));
    if (std::fabs(dy) > 1e-6f)
        s = std::fmin(s, hy / std::fabs(dy));
    if (!(s < 1.0f))
        return false;

    sx = cx + dx * s;
    sy = cy + dy * s;
    return true;
}

// Arrowhead size in pixels.
inline constexpr float ARROW_HEAD_LEN    = 9.0f;
inline constexpr float ARROW_HEAD_HALF_W = 3.5f;

// Screen-space arrow from a text box to a target point.
struct Arrow
{
    float sx = 0.0f, sy = 0.0f;   // shaft start (just outside the text box)
    float ex = 0.0f, ey = 0.0f;   // shaft end (centre of the head's base)
    float tx = 0.0f, ty = 0.0f;   // tip (the annotated point)
    float lx = 0.0f, ly = 0.0f;   // head base corner, one side
    float rx = 0.0f, ry = 0.0f;   // head base corner, other side
    float nx = 0.0f, ny = 0.0f;   // unit normal of the shaft
};

// Compute the arrow for text box `b` pointing at (tx, ty).  Returns false when
// there is nothing to draw (target hidden by the text, or arrow under 1 px).
inline bool arrow_geometry(const Box& b, float tx, float ty, Arrow& a)
{
    float sx = 0.0f;
    float sy = 0.0f;
    if (!arrow_start(b, tx, ty, ARROW_GAP, sx, sy))
        return false;
    const float dx  = tx - sx;
    const float dy  = ty - sy;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 1.0f))
        return false;
    const float ux = dx / len;
    const float uy = dy / len;
    const float hl = std::fmin(ARROW_HEAD_LEN, len);
    const float bx = tx - ux * hl;
    const float by = ty - uy * hl;

    a.sx = sx;
    a.sy = sy;
    a.ex = bx;
    a.ey = by;
    a.tx = tx;
    a.ty = ty;
    a.nx = -uy;
    a.ny = ux;
    a.lx = bx + a.nx * ARROW_HEAD_HALF_W;
    a.ly = by + a.ny * ARROW_HEAD_HALF_W;
    a.rx = bx - a.nx * ARROW_HEAD_HALF_W;
    a.ry = by - a.ny * ARROW_HEAD_HALF_W;
    return true;
}

}   // namespace spectra::annotation_layout

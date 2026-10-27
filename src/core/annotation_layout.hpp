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

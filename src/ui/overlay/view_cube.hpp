#pragma once

// Geometry of the view cube: a small cube in the top-right corner of every
// 3D axes that turns with the camera.  Clicking one of its faces moves the
// camera to that standard view; the strip under the cube toggles between
// perspective and orthographic projection.
//
// Pure (no ImGui): the input handler hit-tests with it and the ImGui layer
// draws from the same layout, so the two always agree.

#include <algorithm>
#include <array>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>

namespace spectra::view_cube
{

inline constexpr float SIZE          = 76.0f;   // cube widget box (px)
inline constexpr float MARGIN        = 8.0f;    // inset from the viewport corner
inline constexpr float TOGGLE_HEIGHT = 16.0f;   // projection toggle strip under the cube
inline constexpr float TOGGLE_GAP    = 2.0f;
// Below this viewport size the widget is hidden (it would cover the plot).
inline constexpr float MIN_VIEWPORT_W = 220.0f;
inline constexpr float MIN_VIEWPORT_H = 200.0f;
// Half the cube edge as a fraction of SIZE: the cube's widest projection
// (its space diagonal, sqrt(3) * half edge) still fits the widget box.
inline constexpr float HALF_EDGE_FRACTION = 0.27f;

struct Face
{
    Camera::ViewPreset   preset = Camera::ViewPreset::Front;
    const char*          label  = "";
    std::array<float, 4> x{};
    std::array<float, 4> y{};
    float                center_x = 0.0f;
    float                center_y = 0.0f;
    float                facing   = 0.0f;   // > 0: faces the viewer (larger = more head-on)
};

struct Layout
{
    bool                valid = false;
    Rect                box;      // widget box holding the cube
    Rect                toggle;   // projection toggle strip below it
    std::array<Face, 6> faces;
    // Indices into faces of those facing the viewer, farthest first (paint
    // in this order; hit-test in reverse).
    std::array<int, 6> order{};
    int                visible_count = 0;
};

enum class HitKind
{
    None,
    Face,
    ProjectionToggle,
};

struct Hit
{
    HitKind            kind   = HitKind::None;
    Camera::ViewPreset preset = Camera::ViewPreset::Front;
};

inline bool viewport_fits(const Rect& vp)
{
    return vp.w >= MIN_VIEWPORT_W && vp.h >= MIN_VIEWPORT_H;
}

// Layout of the view cube of `axes` (invalid when the viewport is too small).
inline Layout compute_layout(const Axes3D& axes)
{
    Layout      out;
    const Rect& vp = axes.viewport();
    if (!viewport_fits(vp))
        return out;

    out.valid  = true;
    out.box    = Rect{vp.x + vp.w - SIZE - MARGIN, vp.y + MARGIN, SIZE, SIZE};
    out.toggle = Rect{out.box.x, out.box.y + SIZE + TOGGLE_GAP, SIZE, TOGGLE_HEIGHT};

    const Camera& cam = axes.camera();
    const mat4    v   = cam.view_matrix();
    // Rotation part of the view matrix (column-major): world -> view space.
    auto rotate = [&v](double x, double y, double z)
    {
        return vec3{v.m[0] * x + v.m[4] * y + v.m[8] * z,
                    v.m[1] * x + v.m[5] * y + v.m[9] * z,
                    v.m[2] * x + v.m[6] * y + v.m[10] * z};
    };

    const float cx   = out.box.x + SIZE * 0.5f;
    const float cy   = out.box.y + SIZE * 0.5f;
    const float half = SIZE * HALF_EDGE_FRACTION;

    const bool z_up = cam.up_axis == Camera::UpAxis::Z;
    using P         = Camera::ViewPreset;
    // Face of the cube seen by each preset: the face whose outward normal
    // points from the target toward that preset's camera.
    struct FaceDef
    {
        int         axis;   // 0 = X, 1 = Y, 2 = Z
        int         sign;   // +1 / -1
        P           preset;
        const char* label;
    };
    const std::array<FaceDef, 6> defs =
        z_up ? std::array<FaceDef, 6>{FaceDef{0, +1, P::Right, "RIGHT"},
                                      FaceDef{0, -1, P::Left, "LEFT"},
                                      FaceDef{1, -1, P::Front, "FRONT"},
                                      FaceDef{1, +1, P::Back, "BACK"},
                                      FaceDef{2, +1, P::Top, "TOP"},
                                      FaceDef{2, -1, P::Bottom, "BOTTOM"}}
             : std::array<FaceDef, 6>{FaceDef{0, +1, P::Right, "RIGHT"},
                                      FaceDef{0, -1, P::Left, "LEFT"},
                                      FaceDef{2, +1, P::Front, "FRONT"},
                                      FaceDef{2, -1, P::Back, "BACK"},
                                      FaceDef{1, +1, P::Top, "TOP"},
                                      FaceDef{1, -1, P::Bottom, "BOTTOM"}};

    for (size_t i = 0; i < defs.size(); ++i)
    {
        const FaceDef& d = defs[i];
        Face&          f = out.faces[i];
        f.preset         = d.preset;
        f.label          = d.label;

        double n[3] = {0.0, 0.0, 0.0};
        n[d.axis]   = d.sign;
        // Two in-face axes, ordered so the corners run around the face.
        const int a = (d.axis + 1) % 3;
        const int b = (d.axis + 2) % 3;

        const vec3 nv = rotate(n[0], n[1], n[2]);
        f.facing      = static_cast<float>(nv.z);   // view space looks down -z
        f.center_x    = cx + static_cast<float>(nv.x) * half;
        f.center_y    = cy - static_cast<float>(nv.y) * half;

        constexpr int su[4] = {-1, +1, +1, -1};
        constexpr int sv[4] = {-1, -1, +1, +1};
        for (int k = 0; k < 4; ++k)
        {
            double p[3] = {n[0], n[1], n[2]};
            p[a] += su[k];
            p[b] += sv[k];
            const vec3 pv = rotate(p[0], p[1], p[2]);
            f.x[k]        = cx + static_cast<float>(pv.x) * half;
            f.y[k]        = cy - static_cast<float>(pv.y) * half;
        }
    }

    // Faces toward the viewer, farthest (least head-on) first.
    for (int i = 0; i < 6; ++i)
    {
        if (out.faces[i].facing > 1e-4f)
            out.order[out.visible_count++] = i;
    }
    // Insertion sort: at most six entries, and std::sort over this partial
    // range trips GCC 15's -Warray-bounds false positive (introsort's heap
    // fallback indexes past the struct as far as the optimizer can tell).
    for (int i = 1; i < out.visible_count; ++i)
    {
        const int idx = out.order[i];
        int       j   = i - 1;
        while (j >= 0 && out.faces[out.order[j]].facing > out.faces[idx].facing)
        {
            out.order[j + 1] = out.order[j];
            --j;
        }
        out.order[j + 1] = idx;
    }
    return out;
}

// Point-in-convex-quad test (either winding).
inline bool point_in_face(const Face& f, float px, float py)
{
    int sign = 0;
    for (int k = 0; k < 4; ++k)
    {
        const int   n     = (k + 1) % 4;
        const float cross = (f.x[n] - f.x[k]) * (py - f.y[k]) - (f.y[n] - f.y[k]) * (px - f.x[k]);
        if (std::abs(cross) < 1e-6f)
            continue;
        const int s = cross > 0.0f ? 1 : -1;
        if (sign == 0)
            sign = s;
        else if (s != sign)
            return false;
    }
    return true;
}

inline bool point_in_rect(const Rect& r, float px, float py)
{
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

// What a click at (px, py) hits.  Faces are tested nearest first.
inline Hit hit_test(const Layout& layout, float px, float py)
{
    Hit hit;
    if (!layout.valid)
        return hit;
    if (point_in_rect(layout.toggle, px, py))
    {
        hit.kind = HitKind::ProjectionToggle;
        return hit;
    }
    for (int i = layout.visible_count - 1; i >= 0; --i)
    {
        const Face& f = layout.faces[static_cast<size_t>(layout.order[static_cast<size_t>(i)])];
        if (point_in_face(f, px, py))
        {
            hit.kind   = HitKind::Face;
            hit.preset = f.preset;
            return hit;
        }
    }
    return hit;
}

inline Hit hit_test(const Axes3D& axes, float px, float py)
{
    return hit_test(compute_layout(axes), px, py);
}

}   // namespace spectra::view_cube

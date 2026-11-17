#pragma once

// Pure (GPU-free) geometry of the 3D axes box, shared by the raster renderer
// (grid planes, box edges, tick marks, tick labels) and the SVG/PDF export so
// they all agree on which faces sit at the back.
//
// The data is drawn inside a normalized box centred on the origin
// (Axes3D::data_to_normalized_matrix), so the camera position relative to the
// origin tells for each axis which of the two perpendicular faces is far from
// the viewer.  As in matplotlib and MATLAB, the grid panes go on the far faces
// so they always stay behind the data.

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>

namespace spectra::axes3d_box
{

struct BoxFaces
{
    // back_hi[i]: the back pane perpendicular to axis i (0 = x, 1 = y, 2 = z)
    // lies at that axis' maximum (false: at its minimum).
    std::array<bool, 3> back_hi{false, false, false};
    // Camera up axis (2 = Z, 1 = Y).
    int up_axis = 2;
    // Bottom corner (per axis: true = max) whose vertical edge carries the
    // up-axis ticks: the leftmost one on screen.  The up-axis entry is unused.
    std::array<bool, 3> up_corner_hi{false, false, false};

    bool operator==(const BoxFaces&) const = default;
};

// Far face of one axis from the eye position relative to the box centre
// (`eye`) and the view direction component (`view`).  When the eye lies in the
// face's mid plane (e.g. an orthographic top view), the view direction decides.
inline bool back_face_hi(double eye, double view)
{
    if (eye > 0.0)
        return false;   // camera on the + side: the far wall is at the minimum
    if (eye < 0.0)
        return true;
    return view > 0.0;   // looking towards +axis: the far wall is at the maximum
}

// Back faces and tick corner for a camera and its projection * view matrix
// (applied to the normalized box corners at +-Axes3D::box_half_size()).
inline BoxFaces compute_faces(const Camera& cam, const mat4& view_proj)
{
    BoxFaces     f;
    const vec3   view   = cam.target - cam.position;
    const double eye[3] = {cam.position.x, cam.position.y, cam.position.z};
    const double dir[3] = {view.x, view.y, view.z};
    for (int i = 0; i < 3; ++i)
        f.back_hi[static_cast<size_t>(i)] = back_face_hi(eye[i], dir[i]);
    f.up_axis = (cam.up_axis == Camera::UpAxis::Z) ? 2 : 1;

    // Leftmost of the four bottom corners on screen.
    const int    up     = f.up_axis;
    const int    h_a    = 0;   // X is horizontal for both Z-up and Y-up
    const int    h_b    = (up == 2) ? 1 : 2;
    const double hs     = Axes3D::box_half_size();
    double       best_x = 1e30;
    for (int ci = 0; ci < 4; ++ci)
    {
        double c[3];
        c[up]           = -hs;
        c[h_a]          = (ci & 1) ? hs : -hs;
        c[h_b]          = (ci & 2) ? hs : -hs;
        const vec4 clip = mat4_mul_vec4(
            view_proj,
            {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]), 1.0f});
        if (clip.w <= 0.001f)
            continue;
        const double sx = clip.x / clip.w;
        if (sx < best_x)
        {
            best_x                                   = sx;
            f.up_corner_hi[static_cast<size_t>(h_a)] = (ci & 1) != 0;
            f.up_corner_hi[static_cast<size_t>(h_b)] = (ci & 2) != 0;
        }
    }
    return f;
}

// Faces for an axes as the renderer draws it (its camera and viewport).
inline BoxFaces compute_faces(const Axes3D& axes)
{
    Rect vp = axes.viewport();
    if (!(vp.w >= 1.0f) || !(vp.h >= 1.0f))
        vp = Rect{0.0f, 0.0f, 1.0f, 1.0f};   // not laid out yet: any aspect will do
    const mat4 view_proj = mat4_mul(axes.projection_matrix(vp), axes.camera().view_matrix());
    return compute_faces(axes.camera(), view_proj);
}

// ── Box edges ──────────────────────────────────────────────────────────────
// Edge e in [0, 12) runs along axis a = e / 4.  Its other two coordinates are
// b = (a + 1) % 3 at max when (e & 1), and c = (a + 2) % 3 at max when (e & 2).

inline constexpr int EDGE_COUNT = 12;

inline constexpr int edge_index(int axis, bool b_hi, bool c_hi)
{
    return axis * 4 + (b_hi ? 1 : 0) + (c_hi ? 2 : 0);
}

// Endpoints of edge e of the box [lo, hi].
inline void edge_endpoints(int e, const double lo[3], const double hi[3], vec3& p0, vec3& p1)
{
    const int a = e / 4;
    const int b = (a + 1) % 3;
    const int c = (a + 2) % 3;
    double    q0[3];
    double    q1[3];
    q0[a] = lo[a];
    q1[a] = hi[a];
    q0[b] = q1[b] = (e & 1) ? hi[b] : lo[b];
    q0[c] = q1[c] = (e & 2) ? hi[c] : lo[c];
    p0            = {q0[0], q0[1], q0[2]};
    p1            = {q1[0], q1[1], q1[2]};
}

// Where the ticks of one axis go: the box edge along `axis` with the other two
// coordinates at corner_hi, and the outward direction of the tick marks
// (along out_axis, towards + when out_positive).
struct TickEdge
{
    std::array<bool, 3> corner_hi{false, false, false};
    int                 out_axis     = 0;
    bool                out_positive = false;
};

// Horizontal axes are labelled along the bottom edge on the near side of the
// other horizontal axis; the up axis on the vertical edge at up_corner_hi.
inline TickEdge tick_edge(const BoxFaces& f, int axis)
{
    TickEdge  t;
    const int up = f.up_axis;
    if (axis == up)
    {
        t.corner_hi    = f.up_corner_hi;
        t.out_axis     = 0;
        t.out_positive = f.up_corner_hi[0];
        return t;
    }
    const int other                         = 3 - axis - up;
    t.corner_hi[static_cast<size_t>(other)] = !f.back_hi[static_cast<size_t>(other)];
    t.corner_hi[static_cast<size_t>(up)]    = false;
    t.out_axis                              = other;
    t.out_positive                          = t.corner_hi[static_cast<size_t>(other)];
    return t;
}

inline int tick_edge_index(const BoxFaces& f, int axis)
{
    const TickEdge t = tick_edge(f, axis);
    return edge_index(axis,
                      t.corner_hi[static_cast<size_t>((axis + 1) % 3)],
                      t.corner_hi[static_cast<size_t>((axis + 2) % 3)]);
}

// True when edge e borders one of the three back panes.
inline bool edge_on_back_pane(const BoxFaces& f, int e)
{
    const int  a    = e / 4;
    const auto b    = static_cast<size_t>((a + 1) % 3);
    const auto c    = static_cast<size_t>((a + 2) % 3);
    const bool b_hi = (e & 1) != 0;
    const bool c_hi = (e & 2) != 0;
    return b_hi == f.back_hi[b] || c_hi == f.back_hi[c];
}

// Bit mask (bit e = edge e) of the box edges drawn for a box style:
// Full draws all 12, Back the 9 edges of the back panes plus the edges that
// carry ticks, None nothing.
inline uint16_t edge_mask(BoxStyle style, const BoxFaces& f)
{
    if (style == BoxStyle::None)
        return 0;
    if (style == BoxStyle::Full)
        return static_cast<uint16_t>((1u << EDGE_COUNT) - 1u);
    uint16_t mask = 0;
    for (int e = 0; e < EDGE_COUNT; ++e)
    {
        if (edge_on_back_pane(f, e))
            mask = static_cast<uint16_t>(mask | (1u << e));
    }
    for (int axis = 0; axis < 3; ++axis)
        mask = static_cast<uint16_t>(mask | (1u << tick_edge_index(f, axis)));
    return mask;
}

// Position of the grid plane perpendicular to `axis` inside [lo, hi].
inline double back_plane(const BoxFaces& f, int axis, const double lo[3], const double hi[3])
{
    return f.back_hi[static_cast<size_t>(axis)] ? hi[axis] : lo[axis];
}

// View-space distance range [near, far] of the eight corners of the normalized
// 3D box under `view`: what the depth cue (series depth shade, Axes3D::fog)
// fades over (the renderer's FrameUBO::depth_cue_near/far, SVG export).
inline void depth_cue_range(const mat4& view, float& near_out, float& far_out)
{
    const double hs = Axes3D::box_half_size();
    double       lo = std::numeric_limits<double>::max();
    double       hi = std::numeric_limits<double>::lowest();
    for (int c = 0; c < 8; ++c)
    {
        const vec4 p =
            mat4_mul_vec4(view,
                          vec4{(c & 1) ? hs : -hs, (c & 2) ? hs : -hs, (c & 4) ? hs : -hs, 1.0});
        lo = std::min(lo, -p.z);
        hi = std::max(hi, -p.z);
    }
    near_out = static_cast<float>(lo);
    far_out  = static_cast<float>(hi);
}

// Depth cue factor in [0, 1] of box-space point `world` (0 at the nearest box
// corner, 1 at the farthest), as apply_depth_cue() in the 3D shaders.
inline float depth_cue_t(const mat4& view, float near_d, float far_d, const vec3& world)
{
    const float span = far_d - near_d;
    if (!(span > 1e-6f))
        return 0.0f;
    const vec4  p = mat4_mul_vec4(view, vec4{world, 1.0});
    const float d = static_cast<float>(-p.z);
    return std::clamp((d - near_d) / span, 0.0f, 1.0f);
}

}   // namespace spectra::axes3d_box

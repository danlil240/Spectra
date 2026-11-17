#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <spectra/axes3d.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series3d.hpp>

namespace spectra
{

struct Projected3DPoint
{
    float screen_x  = 0.0f;
    float screen_y  = 0.0f;
    float ndc_depth = 0.0f;
    bool  visible   = false;
};

// Project a data-space point through the Axes3D MVP into viewport screen coordinates.
inline Projected3DPoint project_axes3d_data_point(const Axes3D& axes, vec3 data_pos)
{
    Projected3DPoint out;

    const auto& vp  = axes.viewport();
    const auto& cam = axes.camera();
    if (vp.w <= 0.0f || vp.h <= 0.0f)
        return out;

    const mat4 proj  = axes.projection_matrix(vp);
    const mat4 view  = cam.view_matrix();
    const mat4 model = axes.data_to_normalized_matrix();
    const mat4 mvp   = mat4_mul(proj, mat4_mul(view, model));

    const float clip_x =
        mvp.m[0] * data_pos.x + mvp.m[4] * data_pos.y + mvp.m[8] * data_pos.z + mvp.m[12];
    const float clip_y =
        mvp.m[1] * data_pos.x + mvp.m[5] * data_pos.y + mvp.m[9] * data_pos.z + mvp.m[13];
    const float clip_z =
        mvp.m[2] * data_pos.x + mvp.m[6] * data_pos.y + mvp.m[10] * data_pos.z + mvp.m[14];
    const float clip_w =
        mvp.m[3] * data_pos.x + mvp.m[7] * data_pos.y + mvp.m[11] * data_pos.z + mvp.m[15];

    if (clip_w <= 0.001f)
        return out;

    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;
    out.ndc_depth     = clip_z / clip_w;
    out.screen_x      = vp.x + (ndc_x + 1.0f) * 0.5f * vp.w;
    out.screen_y      = vp.y + (ndc_y + 1.0f) * 0.5f * vp.h;
    out.visible       = true;
    return out;
}

struct Nearest3DPickCandidate
{
    bool          found       = false;
    const Series* series      = nullptr;
    size_t        point_index = 0;
    float         data_x      = 0.0f;
    float         data_y      = 0.0f;
    float         data_z      = 0.0f;
    float         screen_x    = 0.0f;
    float         screen_y    = 0.0f;
    float         distance_px = 0.0f;
    float         ndc_depth   = 1.0f;
};

// Ray under a screen point of a 3D axes, in data space: points origin +
// t * dir for t in [0, 1] run from the near to the far clip plane.  False when
// the viewport is empty or the projection cannot be inverted.
inline bool axes3d_cursor_ray(const Axes3D& axes,
                              float         cursor_x,
                              float         cursor_y,
                              vec3&         origin,
                              vec3&         dir)
{
    const auto& vp = axes.viewport();
    if (vp.w <= 0.0f || vp.h <= 0.0f)
        return false;
    // Unproject in box space (well conditioned), then map to data space in
    // double through the axes limits (the model is a per-axis scale + shift).
    const mat4 vp_mat = mat4_mul(axes.projection_matrix(vp), axes.camera().view_matrix());
    if (std::fabs(mat4_determinant(vp_mat)) < 1e-30f)
        return false;
    const mat4   inv   = mat4_inverse(vp_mat);
    const double ndc_x = (static_cast<double>(cursor_x) - vp.x) / vp.w * 2.0 - 1.0;
    const double ndc_y = (static_cast<double>(cursor_y) - vp.y) / vp.h * 2.0 - 1.0;
    const vec4   n4    = mat4_mul_vec4(inv, {ndc_x, ndc_y, 0.0, 1.0});
    const vec4   f4    = mat4_mul_vec4(inv, {ndc_x, ndc_y, 1.0, 1.0});
    if (std::fabs(n4.w) < 1e-12 || std::fabs(f4.w) < 1e-12)
        return false;
    const AxisLimits lim[3]  = {axes.x_limits(), axes.y_limits(), axes.z_limits()};
    const double     hs      = Axes3D::box_half_size();
    auto             to_data = [&](const vec4& w4)
    {
        const double w[3] = {w4.x / w4.w, w4.y / w4.w, w4.z / w4.w};
        double       d[3];
        for (int a = 0; a < 3; ++a)
        {
            const double span = lim[a].max - lim[a].min;
            d[a]              = (lim[a].min + lim[a].max) * 0.5
                   + w[a] * (span > 1e-30 ? span : 2.0 * hs) / (2.0 * hs);
        }
        return vec3{d[0], d[1], d[2]};
    };
    origin = to_data(n4);
    dir    = to_data(f4) - origin;
    return vec3_length_sq(dir) > 0.0;
}

// Ray / triangle intersection (Moller-Trumbore).  On a hit returns true with
// the ray parameter t and the barycentric weights (u, v) of b and c.
inline bool ray_triangle_hit(const vec3& o,
                             const vec3& d,
                             const vec3& a,
                             const vec3& b,
                             const vec3& c,
                             double&     t,
                             double&     u,
                             double&     v)
{
    const vec3   e1  = b - a;
    const vec3   e2  = c - a;
    const vec3   p   = vec3_cross(d, e2);
    const double det = vec3_dot(e1, p);
    if (!(std::fabs(det) > 1e-300))
        return false;
    const double inv = 1.0 / det;
    const vec3   s   = o - a;
    u                = vec3_dot(s, p) * inv;
    if (u < 0.0 || u > 1.0)
        return false;
    const vec3 q = vec3_cross(s, e1);
    v            = vec3_dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0)
        return false;
    t = vec3_dot(e2, q) * inv;
    return std::isfinite(t);
}

// Screen-space distance (px) within which a wireframe surface's grid line is
// picked.  Wireframe cells are see-through, so their interiors never pick.
inline constexpr float WIREFRAME_PICK_PX = 5.0f;

// Nearest surface or mesh under the cursor (ray cast against its triangles,
// clipped to the axes box).  The candidate snaps to the triangle vertex
// nearest the hit, so the data tip shows a real data value; distance_px is 0
// since the cursor is on the surface.  Wireframe surfaces pick only their grid
// lines within WIREFRAME_PICK_PX of the cursor (frontmost line wins), so points
// seen through an open cell stay hoverable.
inline Nearest3DPickCandidate pick_surface_3d_in_axes(const Axes3D& axes,
                                                      float         cursor_x,
                                                      float         cursor_y)
{
    Nearest3DPickCandidate best;
    vec3                   o, d;
    if (!axes3d_cursor_ray(axes, cursor_x, cursor_y, o, d))
        return best;

    const AxisLimits lim[3] = {axes.x_limits(), axes.y_limits(), axes.z_limits()};
    auto             in_box = [&](const vec3& p)
    {
        const double c[3] = {p.x, p.y, p.z};
        for (int a = 0; a < 3; ++a)
        {
            const double lo  = std::min(lim[a].min, lim[a].max);
            const double hi  = std::max(lim[a].min, lim[a].max);
            const double eps = (hi - lo) * 1e-6;
            if (!(c[a] >= lo - eps && c[a] <= hi + eps))
                return false;
        }
        return true;
    };
    // Slab test of the ray against a data-space bounding box.
    auto ray_hits_aabb = [&](const vec3& lo, const vec3& hi)
    {
        double       t0 = 0.0, t1 = 1.0;
        const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
        const double l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
        for (int a = 0; a < 3; ++a)
        {
            if (std::fabs(dd[a]) < 1e-300)
            {
                if (oo[a] < l[a] || oo[a] > h[a])
                    return false;
                continue;
            }
            double ta = (l[a] - oo[a]) / dd[a];
            double tb = (h[a] - oo[a]) / dd[a];
            if (ta > tb)
                std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1)
                return false;
        }
        return true;
    };

    double best_t = std::numeric_limits<double>::max();
    auto   try_tri =
        [&](const Series* series, const vec3& a, const vec3& b, const vec3& c, const size_t idx[3])
    {
        double t = 0.0, u = 0.0, v = 0.0;
        if (!ray_triangle_hit(o, d, a, b, c, t, u, v) || t < 0.0 || t > 1.0 || t >= best_t)
            return;
        const vec3 hit = o + d * t;
        if (!in_box(hit))
            return;
        // Against a wireframe candidate (other series) compare screen depth.
        const Projected3DPoint proj = project_axes3d_data_point(axes, hit);
        const float            hd   = proj.visible ? proj.ndc_depth : 1.0f;
        if (best.found && best.distance_px > 0.0f && !(hd < best.ndc_depth))
            return;
        best_t = t;
        // Snap to the vertex with the largest barycentric weight.
        const double w[3]         = {1.0 - u - v, u, v};
        const int    k            = (w[0] >= w[1] && w[0] >= w[2]) ? 0 : (w[1] >= w[2] ? 1 : 2);
        const vec3&  sv           = k == 0 ? a : (k == 1 ? b : c);
        best.found                = true;
        best.series               = series;
        best.point_index          = idx[k];
        best.data_x               = static_cast<float>(sv.x);
        best.data_y               = static_cast<float>(sv.y);
        best.data_z               = static_cast<float>(sv.z);
        best.distance_px          = 0.0f;
        best.ndc_depth            = hd;
        const Projected3DPoint sp = project_axes3d_data_point(axes, sv);
        best.screen_x             = sp.visible ? sp.screen_x : cursor_x;
        best.screen_y             = sp.visible ? sp.screen_y : cursor_y;
    };
    auto finite3 = [](const vec3& p)
    { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };

    // Data space -> screen, with the MVP computed once (wireframe picking
    // projects many grid vertices).
    const auto& vp = axes.viewport();
    const mat4  mvp =
        mat4_mul(axes.projection_matrix(vp),
                 mat4_mul(axes.camera().view_matrix(), axes.data_to_normalized_matrix()));
    auto project = [&](const vec3& p)
    {
        Projected3DPoint out;
        const double     cx = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8] * p.z + mvp.m[12];
        const double     cy = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9] * p.z + mvp.m[13];
        const double     cz = mvp.m[2] * p.x + mvp.m[6] * p.y + mvp.m[10] * p.z + mvp.m[14];
        const double     cw = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
        if (!(cw > 0.001))
            return out;
        out.screen_x  = static_cast<float>(vp.x + (cx / cw + 1.0) * 0.5 * vp.w);
        out.screen_y  = static_cast<float>(vp.y + (cy / cw + 1.0) * 0.5 * vp.h);
        out.ndc_depth = static_cast<float>(cz / cw);
        out.visible   = true;
        return out;
    };
    // One wireframe grid line p-q: picked when its projection passes within
    // WIREFRAME_PICK_PX of the cursor inside the box; the frontmost such line
    // wins and the candidate snaps to its nearer end.
    auto try_wire_segment = [&](const Series*           series,
                                const vec3&             p,
                                const vec3&             q,
                                const Projected3DPoint& sp,
                                const Projected3DPoint& sq,
                                size_t                  ip,
                                size_t                  iq)
    {
        if (!sp.visible || !sq.visible)
            return;
        const float ex = sq.screen_x - sp.screen_x, ey = sq.screen_y - sp.screen_y;
        const float len2 = ex * ex + ey * ey;
        float       f    = 0.0f;
        if (len2 > 1e-12f)
            f = std::clamp(((cursor_x - sp.screen_x) * ex + (cursor_y - sp.screen_y) * ey) / len2,
                           0.0f,
                           1.0f);
        const float dx   = sp.screen_x + f * ex - cursor_x;
        const float dy   = sp.screen_y + f * ey - cursor_y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (!(dist <= WIREFRAME_PICK_PX))
            return;
        const float depth = sp.ndc_depth + f * (sq.ndc_depth - sp.ndc_depth);
        if ((best.found && !(depth < best.ndc_depth))
            || !in_box(p + (q - p) * static_cast<double>(f)))
            return;
        const bool              nq  = f > 0.5f;
        const vec3&             sv  = nq ? q : p;
        const Projected3DPoint& svp = nq ? sq : sp;
        best.found                  = true;
        best.series                 = series;
        best.point_index            = nq ? iq : ip;
        best.data_x                 = static_cast<float>(sv.x);
        best.data_y                 = static_cast<float>(sv.y);
        best.data_z                 = static_cast<float>(sv.z);
        best.distance_px            = dist;
        best.ndc_depth              = depth;
        best.screen_x               = svp.screen_x;
        best.screen_y               = svp.screen_y;
    };
    // Screen rectangle of a data-space box, grown by the wire pick radius:
    // false when the cursor is outside it (or the box crosses the eye plane,
    // which is then conservatively kept).
    auto cursor_near_box = [&](const vec3& lo, const vec3& hi)
    {
        float x0 = std::numeric_limits<float>::max(), y0 = x0;
        float x1 = -x0, y1 = -x0;
        for (int k = 0; k < 8; ++k)
        {
            const Projected3DPoint c =
                project(vec3{(k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z});
            if (!c.visible)
                return true;
            x0 = std::min(x0, c.screen_x);
            x1 = std::max(x1, c.screen_x);
            y0 = std::min(y0, c.screen_y);
            y1 = std::max(y1, c.screen_y);
        }
        return cursor_x >= x0 - WIREFRAME_PICK_PX && cursor_x <= x1 + WIREFRAME_PICK_PX
               && cursor_y >= y0 - WIREFRAME_PICK_PX && cursor_y <= y1 + WIREFRAME_PICK_PX;
    };

    for (const auto& series_ptr : axes.series())
    {
        if (!series_ptr || !series_ptr->visible())
            continue;
        if (auto* surf = dynamic_cast<const SurfaceSeries*>(series_ptr.get()))
        {
            const auto uc = static_cast<size_t>(std::max(surf->cols(), 0));
            const auto ur = static_cast<size_t>(std::max(surf->rows(), 0));
            const auto xg = surf->x_grid();
            const auto yg = surf->y_grid();
            const auto zv = surf->z_values();
            if (uc < 2 || ur < 2 || xg.size() < uc || yg.size() < ur || zv.size() < uc * ur)
                continue;
            vec3 bmin, bmax;
            surf->get_bounds(bmin, bmax);
            auto vtx = [&](size_t i, size_t j) { return vec3{xg[j], yg[i], zv[i * uc + j]}; };
            constexpr size_t BLOCK = 16;
            if (surf->wireframe())
            {
                // The drawn grid lines: row segments (i, j)-(i, j+1) and
                // column segments (i, j)-(i+1, j).  Blocks whose screen
                // footprint misses the cursor are skipped.
                if (!cursor_near_box(bmin, bmax))
                    continue;
                for (size_t bi = 0; bi < ur; bi += BLOCK)
                    for (size_t bj = 0; bj < uc; bj += BLOCK)
                    {
                        const size_t ie = std::min(bi + BLOCK, ur - 1);
                        const size_t je = std::min(bj + BLOCK, uc - 1);
                        vec3         lo{std::numeric_limits<double>::max(),
                                std::numeric_limits<double>::max(),
                                std::numeric_limits<double>::max()};
                        vec3         hi  = lo * -1.0;
                        bool         any = false;
                        for (size_t i = bi; i <= ie; ++i)
                            for (size_t j = bj; j <= je; ++j)
                            {
                                const vec3 p = vtx(i, j);
                                if (!finite3(p))
                                    continue;
                                lo  = vec3_min(lo, p);
                                hi  = vec3_max(hi, p);
                                any = true;
                            }
                        if (!any || !cursor_near_box(lo, hi))
                            continue;
                        // Each block owns the segments starting in it.
                        const size_t iend = std::min(bi + BLOCK, ur);
                        const size_t jend = std::min(bj + BLOCK, uc);
                        for (size_t i = bi; i < iend; ++i)
                            for (size_t j = bj; j < jend; ++j)
                            {
                                const vec3 p = vtx(i, j);
                                if (!finite3(p))
                                    continue;
                                const Projected3DPoint pp = project(p);
                                const size_t           ip = i * uc + j;
                                if (j + 1 < uc)
                                {
                                    const vec3 q = vtx(i, j + 1);
                                    if (finite3(q))
                                        try_wire_segment(series_ptr.get(),
                                                         p,
                                                         q,
                                                         pp,
                                                         project(q),
                                                         ip,
                                                         ip + 1);
                                }
                                if (i + 1 < ur)
                                {
                                    const vec3 q = vtx(i + 1, j);
                                    if (finite3(q))
                                        try_wire_segment(series_ptr.get(),
                                                         p,
                                                         q,
                                                         pp,
                                                         project(q),
                                                         ip,
                                                         ip + uc);
                                }
                            }
                    }
                continue;
            }
            if (!ray_hits_aabb(bmin, bmax))
                continue;
            // Cells in blocks of BLOCK x BLOCK: only blocks whose bounding box
            // the ray crosses are tested triangle by triangle, so large grids
            // stay cheap to hover.
            for (size_t bi = 0; bi + 1 < ur; bi += BLOCK)
                for (size_t bj = 0; bj + 1 < uc; bj += BLOCK)
                {
                    const size_t ie = std::min(bi + BLOCK, ur - 1);
                    const size_t je = std::min(bj + BLOCK, uc - 1);
                    vec3         lo{std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max()};
                    vec3         hi  = lo * -1.0;
                    bool         any = false;
                    for (size_t i = bi; i <= ie; ++i)
                        for (size_t j = bj; j <= je; ++j)
                        {
                            const vec3 p = vtx(i, j);
                            if (!finite3(p))
                                continue;
                            lo  = vec3_min(lo, p);
                            hi  = vec3_max(hi, p);
                            any = true;
                        }
                    if (!any || !ray_hits_aabb(lo, hi))
                        continue;
                    for (size_t i = bi; i < ie; ++i)
                        for (size_t j = bj; j < je; ++j)
                        {
                            const size_t i00 = i * uc + j, i01 = i00 + 1, i10 = i00 + uc,
                                         i11 = i10 + 1;
                            const vec3 p00 = vtx(i, j), p01 = vtx(i, j + 1);
                            const vec3 p10 = vtx(i + 1, j), p11 = vtx(i + 1, j + 1);
                            if (!finite3(p00) || !finite3(p01) || !finite3(p10) || !finite3(p11))
                                continue;
                            const size_t t0[3] = {i00, i10, i01};
                            const size_t t1[3] = {i01, i10, i11};
                            try_tri(series_ptr.get(), p00, p10, p01, t0);
                            try_tri(series_ptr.get(), p01, p10, p11, t1);
                        }
                }
        }
        else if (auto* mesh = dynamic_cast<const MeshSeries*>(series_ptr.get()))
        {
            const auto   v  = mesh->vertices();
            const auto   ix = mesh->indices();
            const size_t nv = v.size() / 6;
            if (nv == 0 || ix.size() < 3)
                continue;
            vec3 bmin, bmax;
            mesh->get_bounds(bmin, bmax);
            if (!ray_hits_aabb(bmin, bmax))
                continue;
            for (size_t t = 0; t + 2 < ix.size(); t += 3)
            {
                const size_t id[3] = {ix[t], ix[t + 1], ix[t + 2]};
                if (id[0] >= nv || id[1] >= nv || id[2] >= nv)
                    continue;
                vec3 p[3];
                for (int k = 0; k < 3; ++k)
                    p[k] = {v[id[k] * 6], v[id[k] * 6 + 1], v[id[k] * 6 + 2]};
                if (finite3(p[0]) && finite3(p[1]) && finite3(p[2]))
                    try_tri(series_ptr.get(), p[0], p[1], p[2], id);
            }
        }
    }
    return best;
}

// Find the nearest pickable 3D point in screen space within the given axes:
// scatter / line vertices near the cursor, or the surface / mesh under it
// (pick_surface_3d_in_axes) unless a point near the cursor lies in front of it.
inline Nearest3DPickCandidate find_nearest_3d_in_axes(const Axes3D& axes,
                                                      float         cursor_x,
                                                      float         cursor_y)
{
    Nearest3DPickCandidate best;
    float                  best_dist2 = std::numeric_limits<float>::max();

    const auto& vp = axes.viewport();
    if (vp.w <= 0.0f || vp.h <= 0.0f)
        return best;
    if (cursor_x < vp.x || cursor_x > vp.x + vp.w || cursor_y < vp.y || cursor_y > vp.y + vp.h)
        return best;

    auto consider_point = [&](const Series* series, size_t index, float x, float y, float z)
    {
        const Projected3DPoint proj = project_axes3d_data_point(axes, {x, y, z});
        if (!proj.visible)
            return;

        const float dx    = cursor_x - proj.screen_x;
        const float dy    = cursor_y - proj.screen_y;
        const float dist2 = dx * dx + dy * dy;

        const bool replace =
            !best.found || dist2 < best_dist2 - 1e-4f
            || (std::abs(dist2 - best_dist2) <= 4.0f && proj.ndc_depth < best.ndc_depth - 1e-6f);

        if (replace)
        {
            best.found       = true;
            best.series      = series;
            best.point_index = index;
            best.data_x      = x;
            best.data_y      = y;
            best.data_z      = z;
            best.screen_x    = proj.screen_x;
            best.screen_y    = proj.screen_y;
            best.ndc_depth   = proj.ndc_depth;
            best_dist2       = dist2;
            best.distance_px = std::sqrt(dist2);
        }
    };

    for (const auto& series_ptr : axes.series())
    {
        if (!series_ptr || !series_ptr->visible())
            continue;

        if (auto* sc = dynamic_cast<const ScatterSeries3D*>(series_ptr.get()))
        {
            const auto xd = sc->x_data();
            const auto yd = sc->y_data();
            const auto zd = sc->z_data();
            const auto n  = sc->point_count();
            if (xd.empty() || yd.empty() || zd.empty())
                continue;
            for (size_t i = 0; i < n; ++i)
                consider_point(series_ptr.get(), i, xd[i], yd[i], zd[i]);
        }
        else if (auto* ls = dynamic_cast<const LineSeries3D*>(series_ptr.get()))
        {
            const auto xd = ls->x_data();
            const auto yd = ls->y_data();
            const auto zd = ls->z_data();
            const auto n  = ls->point_count();
            if (xd.empty() || yd.empty() || zd.empty())
                continue;
            for (size_t i = 0; i < n; ++i)
                consider_point(series_ptr.get(), i, xd[i], yd[i], zd[i]);
        }
    }

    // A surface under the cursor wins unless a point within the hover radius
    // is in front of it.
    constexpr float              POINT_OVER_SURFACE_PX = 12.0f;
    const Nearest3DPickCandidate surf = pick_surface_3d_in_axes(axes, cursor_x, cursor_y);
    if (surf.found
        && (!best.found || best.distance_px > POINT_OVER_SURFACE_PX
            || best.ndc_depth > surf.ndc_depth))
        return surf;

    return best;
}

}   // namespace spectra

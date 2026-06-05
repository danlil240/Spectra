#pragma once

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

    const float aspect = vp.w / std::max(vp.h, 1.0f);
    const mat4  proj   = cam.projection_matrix(aspect);
    const mat4  view   = cam.view_matrix();
    const mat4  model  = axes.data_to_normalized_matrix();
    const mat4  mvp    = mat4_mul(proj, mat4_mul(view, model));

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

// Find the nearest pickable 3D point in screen space within the given axes.
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

    return best;
}

}   // namespace spectra

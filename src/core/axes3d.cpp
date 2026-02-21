#include "axes3d.hpp"

#include <algorithm>
#include <cmath>
#include <spectra/camera.hpp>
#include <spectra/series.hpp>

namespace spectra
{

namespace
{

TickResult compute_ticks_for_range(float min, float max)
{
    TickResult result;

    if (max <= min)
    {
        result.positions = {min};
        result.labels = {std::to_string(min)};
        return result;
    }

    float range = max - min;
    float rough_step = range / 5.0f;

    float magnitude = std::pow(10.0f, std::floor(std::log10(rough_step)));
    float normalized = rough_step / magnitude;

    float nice_step;
    if (normalized < 1.5f)
        nice_step = 1.0f;
    else if (normalized < 3.0f)
        nice_step = 2.0f;
    else if (normalized < 7.0f)
        nice_step = 5.0f;
    else
        nice_step = 10.0f;

    nice_step *= magnitude;

    float start = std::ceil(min / nice_step) * nice_step;

    for (float val = start; val <= max + 1e-6f; val += nice_step)
    {
        result.positions.push_back(val);

        char buf[32];
        if (std::abs(val) < 1e-10f)
        {
            result.labels.push_back("0");
        }
        else if (std::abs(val) >= 1000.0f || std::abs(val) < 0.01f)
        {
            snprintf(buf, sizeof(buf), "%.2e", val);
            result.labels.push_back(buf);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%.2f", val);
            std::string str(buf);
            while (str.back() == '0')
                str.pop_back();
            if (str.back() == '.')
                str.pop_back();
            result.labels.push_back(str);
        }
    }

    return result;
}

}  // anonymous namespace

Axes3D::Axes3D() : camera_(std::make_unique<Camera>())
{
    camera_->target = {0.0f, 0.0f, 0.0f};
    camera_->up = {0.0f, 1.0f, 0.0f};
    camera_->azimuth = 45.0f;
    camera_->elevation = 30.0f;
    camera_->distance = box_half_size() * 2.0f * 2.2f;
    camera_->update_position_from_orbit();
}

Axes3D::~Axes3D() = default;

void Axes3D::xlim(float min, float max)
{
    xlim_ = AxisLimits{min, max};
}

void Axes3D::ylim(float min, float max)
{
    ylim_ = AxisLimits{min, max};
}

void Axes3D::zlim(float min, float max)
{
    zlim_ = AxisLimits{min, max};
}

void Axes3D::xlabel(const std::string& lbl)
{
    xlabel_ = lbl;
}

void Axes3D::ylabel(const std::string& lbl)
{
    ylabel_ = lbl;
}

void Axes3D::zlabel(const std::string& lbl)
{
    zlabel_ = lbl;
}

AxisLimits Axes3D::x_limits() const
{
    if (xlim_)
        return *xlim_;
    return {0.0f, 1.0f};
}

AxisLimits Axes3D::y_limits() const
{
    if (ylim_)
        return *ylim_;
    return {0.0f, 1.0f};
}

AxisLimits Axes3D::z_limits() const
{
    if (zlim_)
        return *zlim_;
    return {0.0f, 1.0f};
}

TickResult Axes3D::compute_x_ticks() const
{
    auto lim = x_limits();
    return compute_ticks_for_range(lim.min, lim.max);
}

TickResult Axes3D::compute_y_ticks() const
{
    auto lim = y_limits();
    return compute_ticks_for_range(lim.min, lim.max);
}

TickResult Axes3D::compute_z_ticks() const
{
    auto lim = z_limits();
    return compute_ticks_for_range(lim.min, lim.max);
}

void Axes3D::auto_fit()
{
    if (series_.empty())
    {
        xlim(-1.0f, 1.0f);
        ylim(-1.0f, 1.0f);
        zlim(-1.0f, 1.0f);
        return;
    }

    constexpr float INF = 1e30f;
    vec3 global_min{INF, INF, INF};
    vec3 global_max{-INF, -INF, -INF};
    bool has_bounds = false;

    for (auto& s : series_)
    {
        vec3 s_min, s_max;
        if (auto* ls = dynamic_cast<LineSeries3D*>(s.get()))
        {
            if (ls->point_count() == 0)
                continue;
            ls->get_bounds(s_min, s_max);
        }
        else if (auto* ss = dynamic_cast<ScatterSeries3D*>(s.get()))
        {
            if (ss->point_count() == 0)
                continue;
            ss->get_bounds(s_min, s_max);
        }
        else if (auto* sf = dynamic_cast<SurfaceSeries*>(s.get()))
        {
            if (sf->z_values().empty())
                continue;
            sf->get_bounds(s_min, s_max);
        }
        else if (auto* ms = dynamic_cast<MeshSeries*>(s.get()))
        {
            if (ms->vertex_count() == 0)
                continue;
            ms->get_bounds(s_min, s_max);
        }
        else
        {
            continue;
        }
        global_min = vec3_min(global_min, s_min);
        global_max = vec3_max(global_max, s_max);
        has_bounds = true;
    }

    if (!has_bounds)
    {
        xlim(-1.0f, 1.0f);
        ylim(-1.0f, 1.0f);
        zlim(-1.0f, 1.0f);
        return;
    }

    // Add 5% padding
    vec3 extent = global_max - global_min;
    vec3 pad = extent * 0.05f;
    for (int i = 0; i < 3; ++i)
    {
        if (pad[i] < 1e-6f)
            pad[i] = 0.5f;
    }
    global_min -= pad;
    global_max += pad;

    xlim(global_min.x, global_max.x);
    ylim(global_min.y, global_max.y);
    zlim(global_min.z, global_max.z);

    // Camera targets the center of the normalized cube (origin)
    camera_->target = {0.0f, 0.0f, 0.0f};

    // Distance based on fixed cube size, not data extent
    float cube_size = box_half_size() * 2.0f;
    camera_->distance = cube_size * 2.2f;
    camera_->update_position_from_orbit();
}

LineSeries3D& Axes3D::line3d(std::span<const float> x,
                             std::span<const float> y,
                             std::span<const float> z)
{
    auto series = std::make_unique<LineSeries3D>(x, y, z);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

ScatterSeries3D& Axes3D::scatter3d(std::span<const float> x,
                                   std::span<const float> y,
                                   std::span<const float> z)
{
    auto series = std::make_unique<ScatterSeries3D>(x, y, z);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

SurfaceSeries& Axes3D::surface(std::span<const float> x_grid,
                               std::span<const float> y_grid,
                               std::span<const float> z_values)
{
    auto series = std::make_unique<SurfaceSeries>(x_grid, y_grid, z_values);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

MeshSeries& Axes3D::mesh(std::span<const float> vertices, std::span<const uint32_t> indices)
{
    auto series = std::make_unique<MeshSeries>(vertices, indices);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

mat4 Axes3D::data_to_normalized_matrix() const
{
    auto xl = x_limits();
    auto yl = y_limits();
    auto zl = z_limits();

    float hs = box_half_size();

    // Scale: map each axis range to [-hs, +hs]
    float sx = (xl.max - xl.min) > 1e-10f ? (2.0f * hs) / (xl.max - xl.min) : 1.0f;
    float sy = (yl.max - yl.min) > 1e-10f ? (2.0f * hs) / (yl.max - yl.min) : 1.0f;
    float sz = (zl.max - zl.min) > 1e-10f ? (2.0f * hs) / (zl.max - zl.min) : 1.0f;

    // Center of data range
    float cx = (xl.min + xl.max) * 0.5f;
    float cy = (yl.min + yl.max) * 0.5f;
    float cz = (zl.min + zl.max) * 0.5f;

    // Model = Scale * Translate(-center)
    // result = S * (p - c) = S*p - S*c
    // Column-major mat4:
    mat4 m = mat4_identity();
    m.m[0] = sx;
    m.m[5] = sy;
    m.m[10] = sz;
    m.m[12] = -sx * cx;  // translation x
    m.m[13] = -sy * cy;  // translation y
    m.m[14] = -sz * cz;  // translation z
    m.m[15] = 1.0f;
    return m;
}

void Axes3D::zoom_limits(float factor)
{
    auto xl = x_limits();
    auto yl = y_limits();
    auto zl = z_limits();

    auto zoom_range = [&](AxisLimits lim) -> AxisLimits
    {
        float center = (lim.min + lim.max) * 0.5f;
        float half_range = (lim.max - lim.min) * 0.5f * factor;
        if (half_range < 1e-10f)
            half_range = 1e-10f;
        return {center - half_range, center + half_range};
    };

    auto new_xl = zoom_range(xl);
    auto new_yl = zoom_range(yl);
    auto new_zl = zoom_range(zl);

    xlim(new_xl.min, new_xl.max);
    ylim(new_yl.min, new_yl.max);
    zlim(new_zl.min, new_zl.max);
}

void Axes3D::pan_limits(float dx_screen, float dy_screen, float vp_w, float vp_h)
{
    const auto& cam = camera();

    // Camera right and up vectors in world (normalized) space
    vec3 forward = vec3_normalize(cam.target - cam.position);
    vec3 right = vec3_normalize(vec3_cross(forward, cam.up));
    vec3 up = vec3_cross(right, forward);

    // Scale: how much world-space movement per pixel
    // Match Camera::pan() scale so drag feels consistent
    float scale = cam.distance * 0.002f;
    if (cam.projection_mode == Camera::ProjectionMode::Orthographic)
        scale = cam.ortho_size * 0.002f;

    // World-space displacement (in normalized cube space)
    // Negate dx because dragging right should shift data left (show data to the right)
    vec3 world_delta = right * (-dx_screen * scale) + up * (dy_screen * scale);

    // Convert normalized-space displacement to data-space displacement per axis.
    // Model matrix maps data range [min,max] to [-hs, +hs], so:
    //   scale_x = (2*hs) / (xmax - xmin)
    //   data_delta_x = world_delta_x / scale_x = world_delta_x * (xmax-xmin) / (2*hs)
    auto xl = x_limits();
    auto yl = y_limits();
    auto zl = z_limits();
    float hs = box_half_size();

    float x_range = xl.max - xl.min;
    float y_range = yl.max - yl.min;
    float z_range = zl.max - zl.min;

    float ddx = (x_range > 1e-10f) ? world_delta.x * x_range / (2.0f * hs) : 0.0f;
    float ddy = (y_range > 1e-10f) ? world_delta.y * y_range / (2.0f * hs) : 0.0f;
    float ddz = (z_range > 1e-10f) ? world_delta.z * z_range / (2.0f * hs) : 0.0f;

    xlim(xl.min + ddx, xl.max + ddx);
    ylim(yl.min + ddy, yl.max + ddy);
    zlim(zl.min + ddz, zl.max + ddz);
}

}  // namespace spectra

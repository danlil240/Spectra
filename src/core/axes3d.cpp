#include "axes3d.hpp"
#include <plotix/camera.hpp>
#include <plotix/series.hpp>
#include <algorithm>
#include <cmath>

namespace plotix {

namespace {

TickResult compute_ticks_for_range(float min, float max) {
    TickResult result;
    
    if (max <= min) {
        result.positions = {min};
        result.labels = {std::to_string(min)};
        return result;
    }

    float range = max - min;
    float rough_step = range / 5.0f;
    
    float magnitude = std::pow(10.0f, std::floor(std::log10(rough_step)));
    float normalized = rough_step / magnitude;
    
    float nice_step;
    if (normalized < 1.5f) nice_step = 1.0f;
    else if (normalized < 3.0f) nice_step = 2.0f;
    else if (normalized < 7.0f) nice_step = 5.0f;
    else nice_step = 10.0f;
    
    nice_step *= magnitude;
    
    float start = std::ceil(min / nice_step) * nice_step;
    
    for (float val = start; val <= max + 1e-6f; val += nice_step) {
        result.positions.push_back(val);
        
        char buf[32];
        if (std::abs(val) < 1e-10f) {
            result.labels.push_back("0");
        } else if (std::abs(val) >= 1000.0f || std::abs(val) < 0.01f) {
            snprintf(buf, sizeof(buf), "%.2e", val);
            result.labels.push_back(buf);
        } else {
            snprintf(buf, sizeof(buf), "%.2f", val);
            std::string str(buf);
            while (str.back() == '0') str.pop_back();
            if (str.back() == '.') str.pop_back();
            result.labels.push_back(str);
        }
    }
    
    return result;
}

} // anonymous namespace

Axes3D::Axes3D() : camera_(std::make_unique<Camera>()) {
    camera_->position = {5.0f, 5.0f, 5.0f};
    camera_->target = {0.0f, 0.0f, 0.0f};
    camera_->up = {0.0f, 1.0f, 0.0f};
    camera_->azimuth = 45.0f;
    camera_->elevation = 30.0f;
    camera_->distance = 8.66f;
    camera_->update_position_from_orbit();
}

Axes3D::~Axes3D() = default;

void Axes3D::xlim(float min, float max) {
    xlim_ = AxisLimits{min, max};
}

void Axes3D::ylim(float min, float max) {
    ylim_ = AxisLimits{min, max};
}

void Axes3D::zlim(float min, float max) {
    zlim_ = AxisLimits{min, max};
}

void Axes3D::xlabel(const std::string& lbl) {
    xlabel_ = lbl;
}

void Axes3D::ylabel(const std::string& lbl) {
    ylabel_ = lbl;
}

void Axes3D::zlabel(const std::string& lbl) {
    zlabel_ = lbl;
}

AxisLimits Axes3D::x_limits() const {
    if (xlim_) return *xlim_;
    return {0.0f, 1.0f};
}

AxisLimits Axes3D::y_limits() const {
    if (ylim_) return *ylim_;
    return {0.0f, 1.0f};
}

AxisLimits Axes3D::z_limits() const {
    if (zlim_) return *zlim_;
    return {0.0f, 1.0f};
}

TickResult Axes3D::compute_x_ticks() const {
    auto lim = x_limits();
    return compute_ticks_for_range(lim.min, lim.max);
}

TickResult Axes3D::compute_y_ticks() const {
    auto lim = y_limits();
    return compute_ticks_for_range(lim.min, lim.max);
}

TickResult Axes3D::compute_z_ticks() const {
    auto lim = z_limits();
    return compute_ticks_for_range(lim.min, lim.max);
}

void Axes3D::auto_fit() {
    if (series_.empty()) {
        xlim(-1.0f, 1.0f);
        ylim(-1.0f, 1.0f);
        zlim(-1.0f, 1.0f);
        return;
    }

    constexpr float INF = 1e30f;
    vec3 global_min{INF, INF, INF};
    vec3 global_max{-INF, -INF, -INF};
    bool has_bounds = false;

    for (auto& s : series_) {
        vec3 s_min, s_max;
        if (auto* ls = dynamic_cast<LineSeries3D*>(s.get())) {
            if (ls->point_count() == 0) continue;
            ls->get_bounds(s_min, s_max);
        } else if (auto* ss = dynamic_cast<ScatterSeries3D*>(s.get())) {
            if (ss->point_count() == 0) continue;
            ss->get_bounds(s_min, s_max);
        } else if (auto* sf = dynamic_cast<SurfaceSeries*>(s.get())) {
            if (sf->z_values().empty()) continue;
            sf->get_bounds(s_min, s_max);
        } else if (auto* ms = dynamic_cast<MeshSeries*>(s.get())) {
            if (ms->vertex_count() == 0) continue;
            ms->get_bounds(s_min, s_max);
        } else {
            continue;
        }
        global_min = vec3_min(global_min, s_min);
        global_max = vec3_max(global_max, s_max);
        has_bounds = true;
    }

    if (!has_bounds) {
        xlim(-1.0f, 1.0f);
        ylim(-1.0f, 1.0f);
        zlim(-1.0f, 1.0f);
        return;
    }

    // Add 5% padding
    vec3 extent = global_max - global_min;
    vec3 pad = extent * 0.05f;
    for (int i = 0; i < 3; ++i) {
        if (pad[i] < 1e-6f) pad[i] = 0.5f;
    }
    global_min -= pad;
    global_max += pad;

    xlim(global_min.x, global_max.x);
    ylim(global_min.y, global_max.y);
    zlim(global_min.z, global_max.z);

    vec3 center = (global_min + global_max) * 0.5f;
    camera_->target = center;

    float max_extent = std::max({extent.x, extent.y, extent.z});
    if (max_extent < 1e-6f) max_extent = 1.0f;
    camera_->distance = max_extent * 2.0f;
    camera_->update_position_from_orbit();
}

LineSeries3D& Axes3D::line3d(std::span<const float> x, std::span<const float> y, std::span<const float> z) {
    auto series = std::make_unique<LineSeries3D>(x, y, z);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

ScatterSeries3D& Axes3D::scatter3d(std::span<const float> x, std::span<const float> y, std::span<const float> z) {
    auto series = std::make_unique<ScatterSeries3D>(x, y, z);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

SurfaceSeries& Axes3D::surface(std::span<const float> x_grid, std::span<const float> y_grid,
                                std::span<const float> z_values) {
    auto series = std::make_unique<SurfaceSeries>(x_grid, y_grid, z_values);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

MeshSeries& Axes3D::mesh(std::span<const float> vertices, std::span<const uint32_t> indices) {
    auto series = std::make_unique<MeshSeries>(vertices, indices);
    auto* ptr = series.get();
    series_.push_back(std::move(series));
    return *ptr;
}

} // namespace plotix

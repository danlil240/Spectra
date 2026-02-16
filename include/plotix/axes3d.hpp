#pragma once

#include <plotix/axes.hpp>
#include <plotix/camera.hpp>
#include <plotix/fwd.hpp>
#include <plotix/math3d.hpp>
#include <plotix/series3d.hpp>
#include <memory>
#include <string>
#include <vector>

namespace plotix {

class Axes3D : public AxesBase {
public:
    Axes3D();
    ~Axes3D();

    void xlim(float min, float max);
    void ylim(float min, float max);
    void zlim(float min, float max);

    void xlabel(const std::string& lbl);
    void ylabel(const std::string& lbl);
    void zlabel(const std::string& lbl);

    AxisLimits x_limits() const;
    AxisLimits y_limits() const;
    AxisLimits z_limits() const;

    const std::string& get_xlabel() const { return xlabel_; }
    const std::string& get_ylabel() const { return ylabel_; }
    const std::string& get_zlabel() const { return zlabel_; }

    TickResult compute_x_ticks() const;
    TickResult compute_y_ticks() const;
    TickResult compute_z_ticks() const;

    void auto_fit() override;

    Camera& camera() { return *camera_; }
    const Camera& camera() const { return *camera_; }

    enum class GridPlane {
        None = 0,
        XY = 1 << 0,
        XZ = 1 << 1,
        YZ = 1 << 2,
        All = XY | XZ | YZ
    };

    void set_grid_planes(int planes) { grid_planes_ = planes; }
    int grid_planes() const { return grid_planes_; }

    bool show_bounding_box() const { return show_bounding_box_; }
    void show_bounding_box(bool enabled) { show_bounding_box_ = enabled; }

    // Returns a model matrix that maps data coordinates [xlim, ylim, zlim]
    // into a fixed-size normalized cube [-box_half_size, +box_half_size]³.
    // This keeps the bounding box a constant visual size regardless of zoom.
    mat4 data_to_normalized_matrix() const;

    // The half-size of the fixed normalized bounding box in world units.
    static constexpr float box_half_size() { return 3.0f; }

    // Zoom by scaling axis limits (bounding box stays fixed, data range changes)
    void zoom_limits(float factor);

    LineSeries3D& line3d(std::span<const float> x, std::span<const float> y, std::span<const float> z);
    ScatterSeries3D& scatter3d(std::span<const float> x, std::span<const float> y, std::span<const float> z);
    SurfaceSeries& surface(std::span<const float> x_grid, std::span<const float> y_grid,
                           std::span<const float> z_values);
    MeshSeries& mesh(std::span<const float> vertices, std::span<const uint32_t> indices);

private:
    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;
    std::optional<AxisLimits> zlim_;

    std::string xlabel_;
    std::string ylabel_;
    std::string zlabel_;

    std::unique_ptr<Camera> camera_;
    int grid_planes_ = static_cast<int>(GridPlane::XY);
    bool show_bounding_box_ = true;
};

inline Axes3D::GridPlane operator|(Axes3D::GridPlane a, Axes3D::GridPlane b) {
    return static_cast<Axes3D::GridPlane>(static_cast<int>(a) | static_cast<int>(b));
}

} // namespace plotix

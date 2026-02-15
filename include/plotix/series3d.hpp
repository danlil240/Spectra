#pragma once

#include <plotix/color.hpp>
#include <plotix/fwd.hpp>
#include <plotix/math3d.hpp>
#include <plotix/plot_style.hpp>
#include <plotix/series.hpp>

#include <span>
#include <string>
#include <vector>

namespace plotix {

enum class ColormapType {
    None = 0,
    Viridis,
    Plasma,
    Inferno,
    Magma,
    Jet,
    Coolwarm,
    Grayscale,
};

class LineSeries3D : public Series {
public:
    LineSeries3D() = default;
    LineSeries3D(std::span<const float> x, std::span<const float> y, std::span<const float> z);

    LineSeries3D& set_x(std::span<const float> x);
    LineSeries3D& set_y(std::span<const float> y);
    LineSeries3D& set_z(std::span<const float> z);
    void append(float x, float y, float z);

    LineSeries3D& width(float w) { line_width_ = w; dirty_ = true; return *this; }
    float width() const { return line_width_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    std::span<const float> z_data() const { return z_; }
    size_t point_count() const { return x_.size(); }

    void record_commands(Renderer& renderer) override;

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::label;
    using Series::color;
    using Series::line_style;
    using Series::marker_style;
    using Series::marker_size;
    using Series::opacity;

    LineSeries3D& label(const std::string& lbl) { Series::label(lbl); return *this; }
    LineSeries3D& color(const Color& c)         { Series::color(c); return *this; }
    LineSeries3D& line_style(LineStyle s)       { Series::line_style(s); return *this; }
    LineSeries3D& marker_style(MarkerStyle s)   { Series::marker_style(s); return *this; }
    LineSeries3D& marker_size(float s)          { Series::marker_size(s); return *this; }
    LineSeries3D& opacity(float o)              { Series::opacity(o); return *this; }

private:
    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> z_;
    float line_width_ = 2.0f;
};

class ScatterSeries3D : public Series {
public:
    ScatterSeries3D() = default;
    ScatterSeries3D(std::span<const float> x, std::span<const float> y, std::span<const float> z);

    ScatterSeries3D& set_x(std::span<const float> x);
    ScatterSeries3D& set_y(std::span<const float> y);
    ScatterSeries3D& set_z(std::span<const float> z);
    void append(float x, float y, float z);

    ScatterSeries3D& size(float s) { point_size_ = s; dirty_ = true; return *this; }
    float size() const { return point_size_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    std::span<const float> z_data() const { return z_; }
    size_t point_count() const { return x_.size(); }

    void record_commands(Renderer& renderer) override;

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::label;
    using Series::color;
    using Series::line_style;
    using Series::marker_style;
    using Series::marker_size;
    using Series::opacity;

    ScatterSeries3D& label(const std::string& lbl) { Series::label(lbl); return *this; }
    ScatterSeries3D& color(const Color& c)         { Series::color(c); return *this; }
    ScatterSeries3D& line_style(LineStyle s)       { Series::line_style(s); return *this; }
    ScatterSeries3D& marker_style(MarkerStyle s)   { Series::marker_style(s); return *this; }
    ScatterSeries3D& marker_size(float s)          { Series::marker_size(s); return *this; }
    ScatterSeries3D& opacity(float o)              { Series::opacity(o); return *this; }

private:
    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> z_;
    float point_size_ = 4.0f;
};

struct SurfaceMesh {
    std::vector<float> vertices;     // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    std::vector<uint32_t> indices;   // Triangle indices
    size_t vertex_count = 0;
    size_t triangle_count = 0;
};

class SurfaceSeries : public Series {
public:
    SurfaceSeries() = default;
    SurfaceSeries(std::span<const float> x_grid, std::span<const float> y_grid,
                  std::span<const float> z_values);

    void set_data(std::span<const float> x_grid, std::span<const float> y_grid,
                  std::span<const float> z_values);

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    std::span<const float> x_grid() const { return x_grid_; }
    std::span<const float> y_grid() const { return y_grid_; }
    std::span<const float> z_values() const { return z_values_; }

    const SurfaceMesh& mesh() const { return mesh_; }
    bool is_mesh_generated() const { return mesh_generated_; }

    void generate_mesh();

    void record_commands(Renderer& renderer) override;

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    SurfaceSeries& colormap(ColormapType cm) { colormap_ = cm; dirty_ = true; return *this; }
    SurfaceSeries& colormap(const std::string& name);
    ColormapType colormap_type() const { return colormap_; }

    void set_colormap_range(float min_val, float max_val) { cmap_min_ = min_val; cmap_max_ = max_val; }
    float colormap_min() const { return cmap_min_; }
    float colormap_max() const { return cmap_max_; }

    static Color sample_colormap(ColormapType cm, float t);

    using Series::label;
    using Series::color;
    using Series::opacity;

    SurfaceSeries& label(const std::string& lbl) { Series::label(lbl); return *this; }
    SurfaceSeries& color(const Color& c)         { Series::color(c); return *this; }
    SurfaceSeries& opacity(float o)              { Series::opacity(o); return *this; }

private:
    std::vector<float> x_grid_;
    std::vector<float> y_grid_;
    std::vector<float> z_values_;
    int rows_ = 0;
    int cols_ = 0;

    SurfaceMesh mesh_;
    bool mesh_generated_ = false;

    ColormapType colormap_ = ColormapType::None;
    float cmap_min_ = 0.0f;
    float cmap_max_ = 1.0f;
};

class MeshSeries : public Series {
public:
    MeshSeries() = default;
    MeshSeries(std::span<const float> vertices, std::span<const uint32_t> indices);

    void set_vertices(std::span<const float> vertices);
    void set_indices(std::span<const uint32_t> indices);

    std::span<const float> vertices() const { return vertices_; }
    std::span<const uint32_t> indices() const { return indices_; }

    size_t vertex_count() const { return vertices_.size() / 6; }
    size_t triangle_count() const { return indices_.size() / 3; }

    void record_commands(Renderer& renderer) override;

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::label;
    using Series::color;
    using Series::opacity;

    MeshSeries& label(const std::string& lbl) { Series::label(lbl); return *this; }
    MeshSeries& color(const Color& c)         { Series::color(c); return *this; }
    MeshSeries& opacity(float o)              { Series::opacity(o); return *this; }

private:
    std::vector<float> vertices_;        // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    std::vector<uint32_t> indices_;      // Triangle indices
};

} // namespace plotix

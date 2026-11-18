#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <spectra/series3d.hpp>

#include "contour.hpp"

namespace spectra
{

namespace
{

void assign_coord(std::vector<float>& out, std::span<const float> in, SeriesDirtyFlag& dirty)
{
    out.assign(in.begin(), in.end());
    dirty.store(true, std::memory_order_release);
}

vec3 compute_centroid_xyz(const std::vector<float>& x,
                          const std::vector<float>& y,
                          const std::vector<float>& z)
{
    const size_t n = std::min({x.size(), y.size(), z.size()});
    if (n == 0)
        return {0.0f, 0.0f, 0.0f};

    vec3 sum{0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < n; ++i)
    {
        sum.x += x[i];
        sum.y += y[i];
        sum.z += z[i];
    }
    return sum / static_cast<float>(n);
}

void get_bounds_xyz(const std::vector<float>& x,
                    const std::vector<float>& y,
                    const std::vector<float>& z,
                    vec3&                     min_out,
                    vec3&                     max_out)
{
    const size_t n = std::min({x.size(), y.size(), z.size()});
    if (n == 0)
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    min_out = {x[0], y[0], z[0]};
    max_out = min_out;

    for (size_t i = 1; i < n; ++i)
    {
        min_out.x = std::fmin(min_out.x, x[i]);
        min_out.y = std::fmin(min_out.y, y[i]);
        min_out.z = std::fmin(min_out.z, z[i]);
        max_out.x = std::fmax(max_out.x, x[i]);
        max_out.y = std::fmax(max_out.y, y[i]);
        max_out.z = std::fmax(max_out.z, z[i]);
    }
}

}   // anonymous namespace

// ─── LineSeries3D ────────────────────────────────────────────────────────────

LineSeries3D::LineSeries3D(std::span<const float> x,
                           std::span<const float> y,
                           std::span<const float> z)
{
    set_x(x);
    set_y(y);
    set_z(z);
}

LineSeries3D& LineSeries3D::set_x(std::span<const float> x)
{
    assign_coord(x_, x, dirty_);
    return *this;
}

LineSeries3D& LineSeries3D::set_y(std::span<const float> y)
{
    assign_coord(y_, y, dirty_);
    return *this;
}

LineSeries3D& LineSeries3D::set_z(std::span<const float> z)
{
    assign_coord(z_, z, dirty_);
    return *this;
}

void LineSeries3D::append(float x, float y, float z)
{
    x_.push_back(x);
    y_.push_back(y);
    z_.push_back(z);
    dirty_ = true;
}

vec3 LineSeries3D::compute_centroid() const
{
    return compute_centroid_xyz(x_, y_, z_);
}

void LineSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    get_bounds_xyz(x_, y_, z_, min_out, max_out);
}

// ─── ScatterSeries3D ─────────────────────────────────────────────────────────

ScatterSeries3D::ScatterSeries3D(std::span<const float> x,
                                 std::span<const float> y,
                                 std::span<const float> z)
{
    set_x(x);
    set_y(y);
    set_z(z);
}

ScatterSeries3D& ScatterSeries3D::set_x(std::span<const float> x)
{
    assign_coord(x_, x, dirty_);
    return *this;
}

ScatterSeries3D& ScatterSeries3D::set_y(std::span<const float> y)
{
    assign_coord(y_, y, dirty_);
    return *this;
}

ScatterSeries3D& ScatterSeries3D::set_z(std::span<const float> z)
{
    assign_coord(z_, z, dirty_);
    return *this;
}

void ScatterSeries3D::append(float x, float y, float z)
{
    x_.push_back(x);
    y_.push_back(y);
    z_.push_back(z);
    dirty_ = true;
}

ScatterSeries3D& ScatterSeries3D::color_values(std::span<const float> values)
{
    color_values_.assign(values.begin(), values.end());
    if (!colormap_set_)
        recompute_colormap_range();
    dirty_ = true;
    return *this;
}

ScatterSeries3D& ScatterSeries3D::clear_colormap_range()
{
    colormap_set_ = false;
    recompute_colormap_range();
    dirty_ = true;
    return *this;
}

void ScatterSeries3D::recompute_colormap_range()
{
    // Missing samples (NaN / inf) must not poison the range of every point.
    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();
    for (float v : color_values_)
    {
        if (!std::isfinite(v))
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    colormap_min_ = lo <= hi ? lo : 0.0f;
    colormap_max_ = lo <= hi ? hi : 1.0f;
}

ScatterSeries3D& ScatterSeries3D::sizes(std::span<const float> sizes)
{
    sizes_.assign(sizes.begin(), sizes.end());
    dirty_ = true;
    return *this;
}

Color ScatterSeries3D::point_color(size_t i) const
{
    if (!has_colormap() || i >= color_values_.size() || !std::isfinite(color_values_[i]))
        return color_;
    const float range = colormap_max_ - colormap_min_;
    const float t = std::abs(range) > 1e-20f ? (color_values_[i] - colormap_min_) / range : 0.5f;
    Color       c = colormap_.sample(t);
    c.a *= color_.a;
    return c;
}

float ScatterSeries3D::point_size(size_t i) const
{
    if (!has_sizes() || i >= sizes_.size() || !std::isfinite(sizes_[i]))
        return point_size_;
    return std::max(0.0f, sizes_[i]);
}

float ScatterSeries3D::max_point_size() const
{
    if (!has_sizes())
        return point_size_;
    float m = 0.0f;
    for (size_t i = 0; i < sizes_.size(); ++i)
        m = std::max(m, point_size(i));
    return m;
}

vec3 ScatterSeries3D::compute_centroid() const
{
    return compute_centroid_xyz(x_, y_, z_);
}

void ScatterSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    get_bounds_xyz(x_, y_, z_, min_out, max_out);
}

// ─── SurfaceSeries ───────────────────────────────────────────────────────────

SurfaceSeries::SurfaceSeries(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values)
{
    set_data(x_grid, y_grid, z_values);
}

void SurfaceSeries::set_data(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values)
{
    x_grid_.assign(x_grid.begin(), x_grid.end());
    y_grid_.assign(y_grid.begin(), y_grid.end());
    z_values_.assign(z_values.begin(), z_values.end());

    cols_ = static_cast<int>(x_grid_.size());
    rows_ = static_cast<int>(y_grid_.size());

    // Finite data z range: the default colormap range.
    bool any = false;
    for (float z : z_values_)
    {
        if (!std::isfinite(z))
            continue;
        if (!any)
        {
            z_min_ = z;
            z_max_ = z;
            any    = true;
        }
        z_min_ = std::min(z_min_, z);
        z_max_ = std::max(z_max_, z);
    }
    if (!any)
    {
        z_min_ = 0.0f;
        z_max_ = 1.0f;
    }

    mesh_generated_           = false;
    wireframe_mesh_generated_ = false;
    dirty_                    = true;
}

void SurfaceSeries::generate_mesh()
{
    if (cols_ < 2 || rows_ < 2)
    {
        mesh_generated_ = false;
        return;
    }

    mesh_.vertices.clear();
    mesh_.indices.clear();

    size_t expected_z_count = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    if (z_values_.size() != expected_z_count)
    {
        mesh_generated_ = false;
        return;
    }

    mesh_.vertex_count = rows_ * cols_;
    mesh_.vertices.reserve(mesh_.vertex_count * 6);

    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_; ++j)
        {
            float x = x_grid_[j];
            float y = y_grid_[i];
            float z = z_values_[i * cols_ + j];

            // Central differences in the interior, one-sided differences on the
            // grid border (neighbour indices clamped), so every vertex gets a
            // normal that follows the surface slope.
            const int j0 = (j > 0) ? j - 1 : j;
            const int j1 = (j < cols_ - 1) ? j + 1 : j;
            const int i0 = (i > 0) ? i - 1 : i;
            const int i1 = (i < rows_ - 1) ? i + 1 : i;

            const float span_x = x_grid_[j1] - x_grid_[j0];
            const float span_y = y_grid_[i1] - y_grid_[i0];

            float dz_dx = 0.0f;
            float dz_dy = 0.0f;
            if (span_x != 0.0f)
                dz_dx = (z_values_[i * cols_ + j1] - z_values_[i * cols_ + j0]) / span_x;
            if (span_y != 0.0f)
                dz_dy = (z_values_[i1 * cols_ + j] - z_values_[i0 * cols_ + j]) / span_y;
            if (!std::isfinite(dz_dx))
                dz_dx = 0.0f;
            if (!std::isfinite(dz_dy))
                dz_dy = 0.0f;

            vec3 tangent_x{1.0f, 0.0f, dz_dx};
            vec3 tangent_y{0.0f, 1.0f, dz_dy};
            vec3 normal = vec3_normalize(vec3_cross(tangent_x, tangent_y));

            mesh_.vertices.push_back(x);
            mesh_.vertices.push_back(y);
            mesh_.vertices.push_back(z);
            mesh_.vertices.push_back(normal.x);
            mesh_.vertices.push_back(normal.y);
            mesh_.vertices.push_back(normal.z);
        }
    }

    mesh_.triangle_count = (rows_ - 1) * (cols_ - 1) * 2;
    mesh_.indices.reserve(mesh_.triangle_count * 3);

    for (int i = 0; i < rows_ - 1; ++i)
    {
        for (int j = 0; j < cols_ - 1; ++j)
        {
            uint32_t idx0 = i * cols_ + j;
            uint32_t idx1 = i * cols_ + (j + 1);
            uint32_t idx2 = (i + 1) * cols_ + j;
            uint32_t idx3 = (i + 1) * cols_ + (j + 1);

            mesh_.indices.push_back(idx0);
            mesh_.indices.push_back(idx2);
            mesh_.indices.push_back(idx1);

            mesh_.indices.push_back(idx1);
            mesh_.indices.push_back(idx2);
            mesh_.indices.push_back(idx3);
        }
    }

    mesh_generated_ = true;
}

std::vector<float> SurfaceSeries::contour_segments() const
{
    std::vector<float> out;
    if (rows_ < 2 || cols_ < 2)
        return out;
    const auto uc = static_cast<size_t>(cols_);
    const auto ur = static_cast<size_t>(rows_);
    if (z_values_.size() != uc * ur || x_grid_.size() < uc || y_grid_.size() < ur)
        return out;

    // Fractional grid index -> grid coordinate (linear between nodes).
    auto grid_at = [](const std::vector<float>& g, float f)
    {
        const float  cl = std::clamp(f, 0.0f, static_cast<float>(g.size() - 1));
        const auto   i0 = static_cast<size_t>(cl);
        const size_t i1 = std::min(i0 + 1, g.size() - 1);
        const float  t  = cl - static_cast<float>(i0);
        return g[i0] + (g[i1] - g[i0]) * t;
    };
    auto push = [&](float x, float y, float level)
    { out.insert(out.end(), {x, y, level, 0.0f, 0.0f, 0.0f}); };

    for (float level : contour::auto_levels(z_min_, z_max_, contour_levels_, false))
    {
        for (const auto& line : contour::trace(z_values_, uc, ur, level))
        {
            for (size_t k = 0; k + 1 < line.x.size() && k + 1 < line.y.size(); ++k)
            {
                push(grid_at(x_grid_, line.x[k]), grid_at(y_grid_, line.y[k]), level);
                push(grid_at(x_grid_, line.x[k + 1]), grid_at(y_grid_, line.y[k + 1]), level);
            }
        }
    }
    return out;
}

void SurfaceSeries::generate_wireframe_mesh()
{
    if (cols_ < 2 || rows_ < 2)
    {
        wireframe_mesh_generated_ = false;
        return;
    }

    size_t expected_z_count = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    if (z_values_.size() != expected_z_count)
    {
        wireframe_mesh_generated_ = false;
        return;
    }

    wireframe_mesh_.vertices.clear();
    wireframe_mesh_.indices.clear();

    // Reuse the same vertex layout as the solid mesh (position + normal)
    wireframe_mesh_.vertex_count = rows_ * cols_;
    wireframe_mesh_.vertices.reserve(wireframe_mesh_.vertex_count * 6);

    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_; ++j)
        {
            float x = x_grid_[j];
            float y = y_grid_[i];
            float z = z_values_[i * cols_ + j];

            // Normal pointing up for wireframe (not used for lighting but needed for vertex layout)
            wireframe_mesh_.vertices.push_back(x);
            wireframe_mesh_.vertices.push_back(y);
            wireframe_mesh_.vertices.push_back(z);
            wireframe_mesh_.vertices.push_back(0.0f);
            wireframe_mesh_.vertices.push_back(0.0f);
            wireframe_mesh_.vertices.push_back(1.0f);
        }
    }

    // Generate line indices: horizontal lines + vertical lines
    // Each line segment = 2 indices
    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_ - 1; ++j)
        {
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j));
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j + 1));
        }
    }
    for (int j = 0; j < cols_; ++j)
    {
        for (int i = 0; i < rows_ - 1; ++i)
        {
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j));
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>((i + 1) * cols_ + j));
        }
    }

    wireframe_mesh_.triangle_count = 0;   // Not triangles — line segments
    wireframe_mesh_generated_      = true;
}

vec3 SurfaceSeries::compute_centroid() const
{
    if (x_grid_.empty() || y_grid_.empty() || z_values_.empty())
    {
        return {0.0f, 0.0f, 0.0f};
    }

    float x_sum = 0.0f;
    float y_sum = 0.0f;
    float z_sum = 0.0f;
    for (float x : x_grid_)
        x_sum += x;
    for (float y : y_grid_)
        y_sum += y;
    for (float z : z_values_)
        z_sum += z;

    return {x_sum / static_cast<float>(x_grid_.size()),
            y_sum / static_cast<float>(y_grid_.size()),
            z_sum / static_cast<float>(z_values_.size())};
}

void SurfaceSeries::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (x_grid_.empty() || y_grid_.empty() || z_values_.empty())
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    auto [x_min, x_max] = std::minmax_element(x_grid_.begin(), x_grid_.end());
    auto [y_min, y_max] = std::minmax_element(y_grid_.begin(), y_grid_.end());
    auto [z_min, z_max] = std::minmax_element(z_values_.begin(), z_values_.end());

    min_out = {*x_min, *y_min, *z_min};
    max_out = {*x_max, *y_max, *z_max};
}

// ─── MeshSeries ──────────────────────────────────────────────────────────────

MeshSeries::MeshSeries(std::span<const float> vertices, std::span<const uint32_t> indices)
{
    set_vertices(vertices);
    set_indices(indices);
}

void MeshSeries::set_vertices(std::span<const float> vertices)
{
    // Vertices are interleaved {x, y, z, nx, ny, nz}; drop a trailing partial
    // vertex so every consumer can read vertices_[i * 6 + k] for
    // i < vertex_count() without running past the end.  Indices that referenced
    // only complete vertices stay valid because no complete vertex is removed.
    const size_t usable = vertices.size() - vertices.size() % 6;
    vertices_.assign(vertices.begin(), vertices.begin() + static_cast<ptrdiff_t>(usable));
    rebuild_edges();
    dirty_ = true;
}

void MeshSeries::set_indices(std::span<const uint32_t> indices)
{
    indices_.assign(indices.begin(), indices.end());
    rebuild_edges();
    dirty_ = true;
}

MeshSeries& MeshSeries::wireframe(bool w)
{
    wireframe_ = w;
    rebuild_edges();
    dirty_ = true;
    return *this;
}

void MeshSeries::rebuild_edges()
{
    if (wireframe_)
        edge_indices_ = unique_edges(indices_, vertex_count());
    else
        edge_indices_.clear();
}

std::vector<uint32_t> MeshSeries::unique_edges(std::span<const uint32_t> tri_indices,
                                               size_t                    vertex_count)
{
    // Each edge as one 64-bit key (lower index in the high half), then sort +
    // unique: shared edges between neighbouring triangles collapse to one.
    std::vector<uint64_t> keys;
    keys.reserve(tri_indices.size());
    for (size_t t = 0; t + 2 < tri_indices.size(); t += 3)
    {
        const uint32_t v[3] = {tri_indices[t], tri_indices[t + 1], tri_indices[t + 2]};
        if (v[0] >= vertex_count || v[1] >= vertex_count || v[2] >= vertex_count)
            continue;
        for (int k = 0; k < 3; ++k)
        {
            const uint32_t a = v[k];
            const uint32_t b = v[(k + 1) % 3];
            if (a == b)
                continue;
            const uint64_t lo = std::min(a, b);
            const uint64_t hi = std::max(a, b);
            keys.push_back((lo << 32) | hi);
        }
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    std::vector<uint32_t> out;
    out.reserve(keys.size() * 2);
    for (const uint64_t k : keys)
    {
        out.push_back(static_cast<uint32_t>(k >> 32));
        out.push_back(static_cast<uint32_t>(k & 0xFFFFFFFFu));
    }
    return out;
}

vec3 MeshSeries::compute_centroid() const
{
    if (vertices_.size() < 6)
        return {0.0f, 0.0f, 0.0f};

    vec3   sum{0.0f, 0.0f, 0.0f};
    size_t vertex_count = vertices_.size() / 6;

    for (size_t i = 0; i < vertex_count; ++i)
    {
        sum.x += vertices_[i * 6 + 0];
        sum.y += vertices_[i * 6 + 1];
        sum.z += vertices_[i * 6 + 2];
    }

    return sum / static_cast<float>(vertex_count);
}

void MeshSeries::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (vertices_.size() < 6)
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    min_out = {vertices_[0], vertices_[1], vertices_[2]};
    max_out = min_out;

    size_t vertex_count = vertices_.size() / 6;
    for (size_t i = 1; i < vertex_count; ++i)
    {
        float x = vertices_[i * 6 + 0];
        float y = vertices_[i * 6 + 1];
        float z = vertices_[i * 6 + 2];

        min_out.x = std::fmin(min_out.x, x);
        min_out.y = std::fmin(min_out.y, y);
        min_out.z = std::fmin(min_out.z, z);
        max_out.x = std::fmax(max_out.x, x);
        max_out.y = std::fmax(max_out.y, y);
        max_out.z = std::fmax(max_out.z, z);
    }
}

// ─── Colormap Support ─────────────────────────────────────────────────────────

SurfaceSeries& SurfaceSeries::colormap(const std::string& name)
{
    colormap_ = Colormap::from_name(name).value_or(Colormap{});
    dirty_    = true;
    return *this;
}

Color SurfaceSeries::sample_colormap(ColormapType cm, float t)
{
    if (cm == ColormapType::None)
        return {0.5f, 0.5f, 0.5f, 1.0f};
    return Colormap(cm).sample(t);
}

}   // namespace spectra

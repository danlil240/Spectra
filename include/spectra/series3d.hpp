#pragma once

#include <algorithm>
#include <span>
#include <spectra/color.hpp>
#include <spectra/colormap.hpp>
#include <spectra/fwd.hpp>
#include <spectra/math3d.hpp>
#include <spectra/plot_style.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

// ColormapType is defined in color.hpp — included transitively.

enum class BlendMode
{
    Alpha = 0,       // Standard alpha blending (src_alpha, 1-src_alpha)
    Additive,        // Additive blending (src_alpha, one)
    Premultiplied,   // Premultiplied alpha (one, 1-src_alpha)
};

class LineSeries3D : public Series
{
   public:
    LineSeries3D() = default;
    LineSeries3D(std::span<const float> x, std::span<const float> y, std::span<const float> z);

    LineSeries3D& set_x(std::span<const float> x);
    LineSeries3D& set_y(std::span<const float> y);
    LineSeries3D& set_z(std::span<const float> z);
    using Series::append;   // Bring base 2-arg append into scope (avoid -Woverloaded-virtual)
    void append(float x, float y, float z);

    LineSeries3D& width(float w)
    {
        line_width_ = w;
        dirty_      = true;
        return *this;
    }
    float width() const { return line_width_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    std::span<const float> z_data() const { return z_; }
    size_t point_count() const { return std::min({x_.size(), y_.size(), z_.size()}); }

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    LineSeries3D& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    LineSeries3D& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    LineSeries3D& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    LineSeries3D& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    LineSeries3D& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    LineSeries3D& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    LineSeries3D& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

    // Depth shading (a depth cue like matplotlib's depthshade): the farther a
    // part of the line is from the camera, the more it fades towards the
    // plot background, so front and back read apart.  Off by default for
    // lines; Axes3D::fog() applies a cue to every series.
    LineSeries3D& depth_shade(bool enabled)
    {
        depth_shade_ = enabled;
        return *this;
    }
    bool depth_shade() const { return depth_shade_; }

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> z_;
    float              line_width_  = 2.0f;
    BlendMode          blend_mode_  = BlendMode::Alpha;
    bool               depth_shade_ = false;
};

class ScatterSeries3D : public Series
{
   public:
    ScatterSeries3D() = default;
    ScatterSeries3D(std::span<const float> x, std::span<const float> y, std::span<const float> z);

    ScatterSeries3D& set_x(std::span<const float> x);
    ScatterSeries3D& set_y(std::span<const float> y);
    ScatterSeries3D& set_z(std::span<const float> z);
    using Series::append;   // Bring base 2-arg append into scope (avoid -Woverloaded-virtual)
    void append(float x, float y, float z);

    ScatterSeries3D& size(float s)
    {
        point_size_ = s;
        dirty_      = true;
        return *this;
    }
    float size() const { return point_size_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    std::span<const float> z_data() const { return z_; }
    size_t point_count() const { return std::min({x_.size(), y_.size(), z_.size()}); }

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    ScatterSeries3D& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ScatterSeries3D& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ScatterSeries3D& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    ScatterSeries3D& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    ScatterSeries3D& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    ScatterSeries3D& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    ScatterSeries3D& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

    // ── Per-point colour (colormap) ─────────────────────────────────────
    // One value per point, mapped through colormap() to the point colours,
    // as ScatterSeries::color_values() does in 2D.  Both a colormap and one
    // value per point are needed; a NaN / infinite value keeps the series
    // colour.  An empty span drops the per-point colours.
    ScatterSeries3D& color_values(std::span<const float> values);
    // Colormap type (keeps colormap_reversed()).
    ScatterSeries3D& colormap(ColormapType cm)
    {
        colormap_ = Colormap(cm, colormap_.is_reversed());
        dirty_    = true;
        return *this;
    }
    // Built-in, reversed or user colormap (Colormap::from_colors).
    ScatterSeries3D& colormap(const Colormap& cm)
    {
        colormap_ = cm;
        dirty_    = true;
        return *this;
    }
    // Colormap by matplotlib name ("viridis", "RdBu_r"); unknown names give None.
    ScatterSeries3D& colormap(const std::string& name)
    {
        return colormap(Colormap::from_name(name).value_or(Colormap{}));
    }
    ColormapType    colormap() const { return colormap_.type(); }
    const Colormap& cmap() const { return colormap_; }
    // Reverse the colormap (high values take the low end).
    ScatterSeries3D& colormap_reversed(bool reversed)
    {
        colormap_ = colormap_.reversed(reversed);
        dirty_    = true;
        return *this;
    }
    bool colormap_reversed() const { return colormap_.is_reversed(); }

    // Values mapped onto the ends of the colormap (clamped outside).  Without
    // an explicit range the finite min / max of color_values() is used.
    ScatterSeries3D& colormap_range(float min_val, float max_val)
    {
        colormap_min_ = min_val;
        colormap_max_ = max_val;
        colormap_set_ = true;
        dirty_        = true;
        return *this;
    }
    ScatterSeries3D& clear_colormap_range();
    bool             colormap_range_set() const { return colormap_set_; }
    float            colormap_min() const { return colormap_min_; }
    float            colormap_max() const { return colormap_max_; }

    std::span<const float> color_values_data() const { return color_values_; }
    bool                   has_colormap() const
    {
        return !colormap_.is_none() && !color_values_.empty()
               && color_values_.size() == point_count();
    }

    // ── Per-point size ──────────────────────────────────────────────────
    // Marker diameter in pixels for each point (one per point); empty, or a
    // count that does not match the points, falls back to size().
    ScatterSeries3D&       sizes(std::span<const float> sizes);
    std::span<const float> sizes_data() const { return sizes_; }
    bool has_sizes() const { return !sizes_.empty() && sizes_.size() == point_count(); }

    // Colour of point i: its colormapped value when has_colormap() (and the
    // value is finite), else the series colour.  Alpha is the series colour's
    // times the colormap entry's (only user tables are translucent).
    Color point_color(size_t i) const;
    // Marker diameter of point i in pixels (sizes() when has_sizes()).
    float point_size(size_t i) const;
    // Largest marker diameter of any point.
    float max_point_size() const;

    // Depth shading (matplotlib's depthshade): points farther from the camera
    // fade towards the plot background, so the front and back of a cloud read
    // apart.  On by default (a subtle fade); false draws every point in its
    // full colour.
    ScatterSeries3D& depth_shade(bool enabled)
    {
        depth_shade_ = enabled;
        return *this;
    }
    bool depth_shade() const { return depth_shade_; }

   private:
    void recompute_colormap_range();

    std::vector<float> x_;
    std::vector<float> y_;
    std::vector<float> z_;
    float              point_size_ = 6.0f;   // default marker diameter in pixels
    BlendMode          blend_mode_ = BlendMode::Alpha;
    // Per-point attributes
    std::vector<float> color_values_;
    std::vector<float> sizes_;
    Colormap           colormap_;
    float              colormap_min_ = 0.0f;
    float              colormap_max_ = 1.0f;
    bool               colormap_set_ = false;
    bool               depth_shade_  = true;
};

struct SurfaceMesh
{
    std::vector<float>    vertices;   // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    std::vector<uint32_t> indices;    // Triangle indices
    size_t                vertex_count   = 0;
    size_t                triangle_count = 0;
};

class SurfaceSeries : public Series
{
   public:
    SurfaceSeries() = default;
    SurfaceSeries(std::span<const float> x_grid,
                  std::span<const float> y_grid,
                  std::span<const float> z_values);

    void set_data(std::span<const float> x_grid,
                  std::span<const float> y_grid,
                  std::span<const float> z_values);

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    std::span<const float> x_grid() const { return x_grid_; }
    std::span<const float> y_grid() const { return y_grid_; }
    std::span<const float> z_values() const { return z_values_; }

    const SurfaceMesh& mesh() const { return mesh_; }
    const SurfaceMesh& wireframe_mesh() const { return wireframe_mesh_; }
    bool               is_mesh_generated() const { return mesh_generated_; }
    bool               is_wireframe_mesh_generated() const { return wireframe_mesh_generated_; }

    void generate_mesh();
    void generate_wireframe_mesh();

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    // Colormap type (keeps colormap_reversed()).
    SurfaceSeries& colormap(ColormapType cm)
    {
        colormap_ = Colormap(cm, colormap_.is_reversed());
        dirty_    = true;
        return *this;
    }
    // Built-in, reversed or user colormap (Colormap::from_colors).
    SurfaceSeries& colormap(const Colormap& cm)
    {
        colormap_ = cm;
        dirty_    = true;
        return *this;
    }
    // matplotlib name ("viridis", "RdBu_r"); unknown names give None.
    SurfaceSeries&  colormap(const std::string& name);
    ColormapType    colormap_type() const { return colormap_.type(); }
    const Colormap& cmap() const { return colormap_; }

    // Reverse the colormap (high values take the low end), like matplotlib's "_r" maps.
    SurfaceSeries& colormap_reversed(bool reversed)
    {
        colormap_ = colormap_.reversed(reversed);
        dirty_    = true;
        return *this;
    }
    bool colormap_reversed() const { return colormap_.is_reversed(); }

    // Data values mapped onto the ends of the colormap.  Without an explicit
    // range the surface's finite z min / max is used, so the colours stay tied
    // to the data (zooming or panning the axes does not recolour the surface).
    SurfaceSeries& colormap_range(float min_val, float max_val)
    {
        cmap_min_       = min_val;
        cmap_max_       = max_val;
        cmap_range_set_ = true;
        return *this;
    }
    // Back to the automatic (data z min / max) range.
    SurfaceSeries& clear_colormap_range()
    {
        cmap_range_set_ = false;
        return *this;
    }
    bool colormap_range_set() const { return cmap_range_set_; }
    // Effective range: the explicit one when set, else the data z range
    // (0..1 for a surface without finite z values).
    float colormap_min() const { return cmap_range_set_ ? cmap_min_ : z_min_; }
    float colormap_max() const { return cmap_range_set_ ? cmap_max_ : z_max_; }

    // Deprecated alias
    void set_colormap_range(float min_val, float max_val) { colormap_range(min_val, max_val); }

    // Colour of built-in map cm at t (Colormap::sample); mid gray for None.
    static Color sample_colormap(ColormapType cm, float t);

    using Series::color;
    using Series::label;
    using Series::opacity;

    SurfaceSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    SurfaceSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    SurfaceSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Material properties for Phong lighting
    SurfaceSeries& ambient(float a)
    {
        ambient_ = a;
        return *this;
    }
    SurfaceSeries& specular(float s)
    {
        specular_ = s;
        return *this;
    }
    SurfaceSeries& shininess(float s)
    {
        shininess_ = s;
        return *this;
    }
    float ambient() const { return ambient_; }
    float specular() const { return specular_; }
    float shininess() const { return shininess_; }

    SurfaceSeries& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    SurfaceSeries& double_sided(bool d)
    {
        double_sided_ = d;
        return *this;
    }
    bool double_sided() const { return double_sided_; }

    SurfaceSeries& wireframe(bool w)
    {
        wireframe_ = w;
        dirty_     = true;
        return *this;
    }
    bool wireframe() const { return wireframe_; }

    // Per-vertex alpha from colormap: when enabled, the colormap position also
    // drives the alpha channel (colormap_min = colormap_alpha_min, colormap_max
    // = colormap_alpha_max; by default low Z = transparent, high Z = opaque).
    SurfaceSeries& colormap_alpha(bool enabled)
    {
        colormap_alpha_ = enabled;
        dirty_          = true;
        return *this;
    }
    bool colormap_alpha() const { return colormap_alpha_; }

    // Custom alpha range for colormap alpha mapping
    SurfaceSeries& colormap_alpha_range(float min_alpha, float max_alpha)
    {
        cmap_alpha_min_ = min_alpha;
        cmap_alpha_max_ = max_alpha;
        return *this;
    }
    float colormap_alpha_min() const { return cmap_alpha_min_; }
    float colormap_alpha_max() const { return cmap_alpha_max_; }

    // Deprecated alias
    void set_colormap_alpha_range(float min_alpha, float max_alpha)
    {
        cmap_alpha_min_ = min_alpha;
        cmap_alpha_max_ = max_alpha;
    }

    bool is_transparent() const
    {
        return (color_.a * style_.opacity) < 0.99f || colormap_alpha_
               || colormap_.has_transparency();
    }

    // Edge lines over the faces (MATLAB's surf EdgeColor, matplotlib's
    // plot_surface edgecolor): the grid lines drawn on top of the shaded
    // surface in edge_color().  Off by default; ignored in wireframe() mode.
    SurfaceSeries& show_edges(bool enabled)
    {
        show_edges_ = enabled;
        dirty_      = true;
        return *this;
    }
    bool           show_edges() const { return show_edges_; }
    SurfaceSeries& edge_color(const Color& c)
    {
        edge_color_ = c;
        return *this;
    }
    const Color& edge_color() const { return edge_color_; }

    // Contour projection (MATLAB's surfc): iso-lines of the surface drawn on
    // the floor of the 3D box (the bottom pane, or the top pane when the
    // camera looks up from below), coloured through the colormap.  Off by
    // default.  contour_levels() is the approximate number of "nice" levels.
    SurfaceSeries& contour_projection(bool enabled)
    {
        contour_projection_ = enabled;
        dirty_              = true;
        return *this;
    }
    bool           contour_projection() const { return contour_projection_; }
    SurfaceSeries& contour_levels(int n)
    {
        contour_levels_ = std::max(n, 1);
        dirty_          = true;
        return *this;
    }
    int contour_levels() const { return contour_levels_; }

    // Iso-line segments of the contour projection as a line list: two
    // vertices per segment, each {x, y, level, 0, 0, 0} (the surface vertex
    // layout; the level is the data value the colormap maps).  Empty when
    // the grid is too small or flat.
    std::vector<float> contour_segments() const;

   private:
    std::vector<float> x_grid_;
    std::vector<float> y_grid_;
    std::vector<float> z_values_;
    int                rows_ = 0;
    int                cols_ = 0;

    SurfaceMesh mesh_;
    SurfaceMesh wireframe_mesh_;
    bool        mesh_generated_           = false;
    bool        wireframe_mesh_generated_ = false;

    Colormap colormap_;
    float    cmap_min_       = 0.0f;
    float    cmap_max_       = 1.0f;
    bool     cmap_range_set_ = false;
    float    z_min_          = 0.0f;   // finite data z range (set_data)
    float    z_max_          = 1.0f;

    float ambient_   = 0.0f;   // 0 = shader default (0.15)
    float specular_  = 0.0f;   // 0 = shader default (0.3)
    float shininess_ = 0.0f;   // 0 = shader default (32)

    BlendMode blend_mode_     = BlendMode::Alpha;
    bool      double_sided_   = true;   // Default: render both sides (shader flips normal)
    bool      wireframe_      = false;
    bool      colormap_alpha_ = false;
    float     cmap_alpha_min_ = 0.1f;
    float     cmap_alpha_max_ = 1.0f;

    bool  show_edges_         = false;
    Color edge_color_         = {0.0f, 0.0f, 0.0f, 0.35f};
    bool  contour_projection_ = false;
    int   contour_levels_     = 10;
};

class MeshSeries : public Series
{
   public:
    MeshSeries() = default;
    MeshSeries(std::span<const float> vertices, std::span<const uint32_t> indices);

    void set_vertices(std::span<const float> vertices);
    void set_indices(std::span<const uint32_t> indices);

    std::span<const float>    vertices() const { return vertices_; }
    std::span<const uint32_t> indices() const { return indices_; }

    size_t vertex_count() const { return vertices_.size() / 6; }
    size_t triangle_count() const { return indices_.size() / 3; }

    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    using Series::color;
    using Series::label;
    using Series::opacity;

    MeshSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    MeshSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    MeshSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Material properties for Phong lighting
    MeshSeries& ambient(float a)
    {
        ambient_ = a;
        return *this;
    }
    MeshSeries& specular(float s)
    {
        specular_ = s;
        return *this;
    }
    MeshSeries& shininess(float s)
    {
        shininess_ = s;
        return *this;
    }
    float ambient() const { return ambient_; }
    float specular() const { return specular_; }
    float shininess() const { return shininess_; }

    MeshSeries& blend_mode(BlendMode m)
    {
        blend_mode_ = m;
        return *this;
    }
    BlendMode blend_mode() const { return blend_mode_; }

    MeshSeries& double_sided(bool d)
    {
        double_sided_ = d;
        return *this;
    }
    bool double_sided() const { return double_sided_; }

    // Wireframe: draw the mesh as its unique triangle edges (lines in the
    // series colour, depth tested) instead of filled faces.  Hover picking
    // then follows the edges, so points seen through a face stay hoverable.
    MeshSeries& wireframe(bool w);
    bool        wireframe() const { return wireframe_; }

    // Line-list indices (two per edge) of the unique triangle edges, extracted
    // while wireframe() is on; empty otherwise.
    std::span<const uint32_t> edge_indices() const { return edge_indices_; }

    // Unique undirected edges of a triangle list as line-list index pairs
    // (lower index first, sorted).  Triangles referencing a vertex >=
    // vertex_count and a trailing partial triangle are skipped, as are
    // zero-length edges (a == b).
    static std::vector<uint32_t> unique_edges(std::span<const uint32_t> tri_indices,
                                              size_t                    vertex_count);

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

   private:
    std::vector<float>    vertices_;       // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    std::vector<uint32_t> indices_;        // Triangle indices
    std::vector<uint32_t> edge_indices_;   // Wireframe edges (line list)

    void rebuild_edges();

    float ambient_   = 0.0f;   // 0 = shader default (0.15)
    float specular_  = 0.0f;   // 0 = shader default (0.3)
    float shininess_ = 0.0f;   // 0 = shader default (32)

    BlendMode blend_mode_   = BlendMode::Alpha;
    bool      double_sided_ = true;
    bool      wireframe_    = false;
};

}   // namespace spectra

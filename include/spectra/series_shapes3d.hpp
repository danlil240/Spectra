#pragma once

// ─── 3D Shape Series ────────────────────────────────────────────────────────
//
// Renders 3D geometric primitives (boxes, spheres, cylinders, cones, arrows,
// planes) in a 3D axes.  Each shape generates MeshSeries-compatible vertex
// data ({x,y,z, nx,ny,nz} per vertex + triangle indices) and is rendered via
// the existing mesh3d pipeline with Phong lighting.
//
// Easy API:
//   auto& sh = spectra::shapes3d();
//   sh.box(0,0,0, 1,1,1).sphere(3,0,0, 0.5f).arrow3d(0,0,0, 2,2,2);
//
// Object API:
//   auto& sh = ax3d.shapes3d();
//   sh.box(x,y,z, sx,sy,sz).sphere(cx,cy,cz,r);
//
// Animation:
//   spectra::on_update([&](float dt, float t) {
//       sh.clear_shapes();
//       sh.sphere(cos(t)*2, sin(t)*2, 0, 0.5f);
//   });
//
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

struct ShapeDef3D
{
    enum class Type
    {
        Box,
        Sphere,
        Cylinder,
        Cone,
        Arrow3D,
        Plane,
    };

    Type  type = Type::Box;
    Color shape_color{0.0f, 0.0f, 0.0f, 0.0f};   // zero-alpha = use series color

    // Geometry params (meaning depends on type):
    //   Box:      cx, cy, cz, sx, sy, sz  (center + half-extents)
    //   Sphere:   cx, cy, cz, radius
    //   Cylinder: x1, y1, z1, x2, y2, z2, radius
    //   Cone:     x1, y1, z1, x2, y2, z2, radius (base)
    //   Arrow3D:  x1, y1, z1, x2, y2, z2, shaft_radius
    //   Plane:    cx, cy, cz, nx, ny, nz, half_size
    float params[7] = {};

    // Tessellation detail
    int segments = 32;

    // Arrow head sizing
    float arrow_head_length_frac = 0.2f;
    float arrow_head_radius_mult = 2.5f;
};

class ShapeSeries3D : public Series
{
   public:
    ShapeSeries3D() = default;

    // ── Shape creation (fluent, returns *this for chaining) ──

    // Add a box centered at (cx,cy,cz) with half-extents (sx,sy,sz).
    ShapeSeries3D& box(float cx, float cy, float cz, float sx, float sy, float sz);

    // Add a sphere at (cx,cy,cz) with radius r.
    ShapeSeries3D& sphere(float cx, float cy, float cz, float r);

    // Add a cylinder from (x1,y1,z1) to (x2,y2,z2) with given radius.
    ShapeSeries3D& cylinder(float x1, float y1, float z1,
                            float x2, float y2, float z2, float radius);

    // Add a cone from base (x1,y1,z1) to apex (x2,y2,z2) with base radius.
    ShapeSeries3D& cone(float x1, float y1, float z1,
                        float x2, float y2, float z2, float radius);

    // Add a 3D arrow from (x1,y1,z1) to (x2,y2,z2) with shaft radius.
    ShapeSeries3D& arrow3d(float x1, float y1, float z1,
                           float x2, float y2, float z2, float shaft_radius = 0.02f);

    // Add a plane at center (cx,cy,cz) with normal (nx,ny,nz) and half_size.
    ShapeSeries3D& plane(float cx, float cy, float cz,
                         float nx, float ny, float nz, float half_size = 1.0f);

    // ── Per-shape style modifiers (apply to the last added shape) ──

    ShapeSeries3D& shape_color(const Color& c);
    ShapeSeries3D& segments(int n);
    ShapeSeries3D& arrow_head(float length_frac, float radius_mult);

    // ── Bulk operations ──

    ShapeSeries3D& clear_shapes();
    size_t         shape_count() const { return shapes_.size(); }

    // ── Geometry access (for renderer — MeshSeries-compatible format) ──

    // Flat vertex data: {x,y,z, nx,ny,nz} per vertex
    std::span<const float> vertices() const { return vertices_; }
    size_t                 vertex_count() const { return vertices_.size() / 6; }

    // Triangle indices
    std::span<const uint32_t> indices() const { return indices_; }
    size_t                    triangle_count() const { return indices_.size() / 3; }

    // Rebuild mesh geometry from shape definitions.
    void rebuild_geometry();

    void record_commands(Renderer& renderer) override;

    // 3D bounds for auto_fit
    vec3 compute_centroid() const;
    void get_bounds(vec3& min_out, vec3& max_out) const;

    // Material properties (forwarded to pipeline push constants)
    ShapeSeries3D& ambient(float a)
    {
        ambient_ = a;
        return *this;
    }
    ShapeSeries3D& specular(float s)
    {
        specular_ = s;
        return *this;
    }
    ShapeSeries3D& shininess(float s)
    {
        shininess_ = s;
        return *this;
    }
    float ambient() const { return ambient_; }
    float specular() const { return specular_; }
    float shininess() const { return shininess_; }

    bool is_transparent() const { return (color_.a * style_.opacity) < 0.99f; }

    // Fluent setters (covariant return)
    using Series::color;
    using Series::label;
    using Series::opacity;
    ShapeSeries3D& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ShapeSeries3D& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ShapeSeries3D& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

   private:
    std::vector<ShapeDef3D> shapes_;

    // Generated mesh data (MeshSeries-compatible)
    std::vector<float>    vertices_;   // {x,y,z, nx,ny,nz} per vertex
    std::vector<uint32_t> indices_;    // triangle indices

    float ambient_   = 0.0f;
    float specular_  = 0.0f;
    float shininess_ = 0.0f;

    // Geometry generators
    void generate_box(const ShapeDef3D& def);
    void generate_sphere(const ShapeDef3D& def);
    void generate_cylinder(const ShapeDef3D& def);
    void generate_cone(const ShapeDef3D& def);
    void generate_arrow3d(const ShapeDef3D& def);
    void generate_plane(const ShapeDef3D& def);

    // Helper: push a vertex {x,y,z, nx,ny,nz} and return its index
    uint32_t push_vertex(float x, float y, float z, float nx, float ny, float nz);

    // Helper: push a triangle from three vertex indices
    void push_tri(uint32_t a, uint32_t b, uint32_t c);

    // Helper: generate a capped cylinder/cone between two points
    void generate_tube(float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float r_bottom, float r_top,
                       int segments, bool cap_bottom, bool cap_top);
};

}   // namespace spectra

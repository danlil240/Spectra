#include <cmath>
#include <spectra/series_shapes3d.hpp>

namespace spectra
{

static constexpr float PI = 3.14159265358979f;

// ── Shape creation ──────────────────────────────────────────────────────────

ShapeSeries3D& ShapeSeries3D::box(float cx, float cy, float cz, float sx, float sy, float sz)
{
    ShapeDef3D def;
    def.type      = ShapeDef3D::Type::Box;
    def.params[0] = cx;
    def.params[1] = cy;
    def.params[2] = cz;
    def.params[3] = sx;
    def.params[4] = sy;
    def.params[5] = sz;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries3D& ShapeSeries3D::sphere(float cx, float cy, float cz, float r)
{
    ShapeDef3D def;
    def.type      = ShapeDef3D::Type::Sphere;
    def.params[0] = cx;
    def.params[1] = cy;
    def.params[2] = cz;
    def.params[3] = r;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries3D&
ShapeSeries3D::cylinder(float x1, float y1, float z1, float x2, float y2, float z2, float radius)
{
    ShapeDef3D def;
    def.type      = ShapeDef3D::Type::Cylinder;
    def.params[0] = x1;
    def.params[1] = y1;
    def.params[2] = z1;
    def.params[3] = x2;
    def.params[4] = y2;
    def.params[5] = z2;
    def.params[6] = radius;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries3D&
ShapeSeries3D::cone(float x1, float y1, float z1, float x2, float y2, float z2, float radius)
{
    ShapeDef3D def;
    def.type      = ShapeDef3D::Type::Cone;
    def.params[0] = x1;
    def.params[1] = y1;
    def.params[2] = z1;
    def.params[3] = x2;
    def.params[4] = y2;
    def.params[5] = z2;
    def.params[6] = radius;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries3D& ShapeSeries3D::arrow3d(float x1,
                                      float y1,
                                      float z1,
                                      float x2,
                                      float y2,
                                      float z2,
                                      float shaft_radius)
{
    ShapeDef3D def;
    def.type      = ShapeDef3D::Type::Arrow3D;
    def.params[0] = x1;
    def.params[1] = y1;
    def.params[2] = z1;
    def.params[3] = x2;
    def.params[4] = y2;
    def.params[5] = z2;
    def.params[6] = shaft_radius;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

ShapeSeries3D&
ShapeSeries3D::plane(float cx, float cy, float cz, float nx, float ny, float nz, float half_size)
{
    ShapeDef3D def;
    def.type      = ShapeDef3D::Type::Plane;
    def.params[0] = cx;
    def.params[1] = cy;
    def.params[2] = cz;
    def.params[3] = nx;
    def.params[4] = ny;
    def.params[5] = nz;
    def.params[6] = half_size;
    shapes_.push_back(std::move(def));
    dirty_ = true;
    return *this;
}

// ── Per-shape style modifiers ───────────────────────────────────────────────

ShapeSeries3D& ShapeSeries3D::shape_color(const Color& c)
{
    if (!shapes_.empty())
    {
        shapes_.back().shape_color = c;
        dirty_                     = true;
    }
    return *this;
}

ShapeSeries3D& ShapeSeries3D::segments(int n)
{
    if (!shapes_.empty())
    {
        shapes_.back().segments = n;
        dirty_                  = true;
    }
    return *this;
}

ShapeSeries3D& ShapeSeries3D::arrow_head(float length_frac, float radius_mult)
{
    if (!shapes_.empty())
    {
        shapes_.back().arrow_head_length_frac = length_frac;
        shapes_.back().arrow_head_radius_mult = radius_mult;
        dirty_                                = true;
    }
    return *this;
}

// ── Bulk operations ─────────────────────────────────────────────────────────

ShapeSeries3D& ShapeSeries3D::clear_shapes()
{
    shapes_.clear();
    vertices_.clear();
    indices_.clear();
    dirty_ = true;
    return *this;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

uint32_t ShapeSeries3D::push_vertex(float x, float y, float z, float nx, float ny, float nz)
{
    uint32_t idx = static_cast<uint32_t>(vertices_.size() / 6);
    vertices_.push_back(x);
    vertices_.push_back(y);
    vertices_.push_back(z);
    vertices_.push_back(nx);
    vertices_.push_back(ny);
    vertices_.push_back(nz);
    return idx;
}

void ShapeSeries3D::push_tri(uint32_t a, uint32_t b, uint32_t c)
{
    indices_.push_back(a);
    indices_.push_back(b);
    indices_.push_back(c);
}

// ── Geometry generators ─────────────────────────────────────────────────────

void ShapeSeries3D::generate_box(const ShapeDef3D& def)
{
    float cx = def.params[0];
    float cy = def.params[1];
    float cz = def.params[2];
    float sx = def.params[3];
    float sy = def.params[4];
    float sz = def.params[5];

    // 8 corners
    float x0 = cx - sx, x1 = cx + sx;
    float y0 = cy - sy, y1 = cy + sy;
    float z0 = cz - sz, z1 = cz + sz;

    // Each face has 4 vertices with face normal (24 vertices total for sharp edges)
    struct FaceVert
    {
        float x, y, z, nx, ny, nz;
    };

    // clang-format off
    FaceVert faces[6][4] = {
        // +X face
        {{x1,y0,z0, 1,0,0}, {x1,y1,z0, 1,0,0}, {x1,y1,z1, 1,0,0}, {x1,y0,z1, 1,0,0}},
        // -X face
        {{x0,y0,z1, -1,0,0}, {x0,y1,z1, -1,0,0}, {x0,y1,z0, -1,0,0}, {x0,y0,z0, -1,0,0}},
        // +Y face
        {{x0,y1,z0, 0,1,0}, {x1,y1,z0, 0,1,0}, {x1,y1,z1, 0,1,0}, {x0,y1,z1, 0,1,0}},
        // -Y face
        {{x0,y0,z1, 0,-1,0}, {x1,y0,z1, 0,-1,0}, {x1,y0,z0, 0,-1,0}, {x0,y0,z0, 0,-1,0}},
        // +Z face
        {{x0,y0,z1, 0,0,1}, {x1,y0,z1, 0,0,1}, {x1,y1,z1, 0,0,1}, {x0,y1,z1, 0,0,1}},
        // -Z face
        {{x0,y1,z0, 0,0,-1}, {x1,y1,z0, 0,0,-1}, {x1,y0,z0, 0,0,-1}, {x0,y0,z0, 0,0,-1}},
    };
    // clang-format on

    for (int f = 0; f < 6; ++f)
    {
        uint32_t v0 = push_vertex(faces[f][0].x,
                                  faces[f][0].y,
                                  faces[f][0].z,
                                  faces[f][0].nx,
                                  faces[f][0].ny,
                                  faces[f][0].nz);
        uint32_t v1 = push_vertex(faces[f][1].x,
                                  faces[f][1].y,
                                  faces[f][1].z,
                                  faces[f][1].nx,
                                  faces[f][1].ny,
                                  faces[f][1].nz);
        uint32_t v2 = push_vertex(faces[f][2].x,
                                  faces[f][2].y,
                                  faces[f][2].z,
                                  faces[f][2].nx,
                                  faces[f][2].ny,
                                  faces[f][2].nz);
        uint32_t v3 = push_vertex(faces[f][3].x,
                                  faces[f][3].y,
                                  faces[f][3].z,
                                  faces[f][3].nx,
                                  faces[f][3].ny,
                                  faces[f][3].nz);
        push_tri(v0, v1, v2);
        push_tri(v0, v2, v3);
    }
}

void ShapeSeries3D::generate_sphere(const ShapeDef3D& def)
{
    float cx    = def.params[0];
    float cy    = def.params[1];
    float cz    = def.params[2];
    float r     = def.params[3];
    int   segs  = def.segments;
    int   rings = segs / 2;

    // UV-sphere tessellation
    for (int lat = 0; lat <= rings; ++lat)
    {
        float theta = PI * static_cast<float>(lat) / static_cast<float>(rings);
        float sin_t = std::sin(theta);
        float cos_t = std::cos(theta);

        for (int lon = 0; lon <= segs; ++lon)
        {
            float phi   = 2.0f * PI * static_cast<float>(lon) / static_cast<float>(segs);
            float sin_p = std::sin(phi);
            float cos_p = std::cos(phi);

            float nx = sin_t * cos_p;
            float ny = cos_t;
            float nz = sin_t * sin_p;

            push_vertex(cx + r * nx, cy + r * ny, cz + r * nz, nx, ny, nz);
        }
    }

    // Indices
    for (int lat = 0; lat < rings; ++lat)
    {
        for (int lon = 0; lon < segs; ++lon)
        {
            uint32_t base = static_cast<uint32_t>(vertices_.size() / 6)
                            - static_cast<uint32_t>((rings + 1) * (segs + 1));
            uint32_t curr = base + static_cast<uint32_t>(lat * (segs + 1) + lon);
            uint32_t next = curr + static_cast<uint32_t>(segs + 1);

            push_tri(curr, next, curr + 1);
            push_tri(curr + 1, next, next + 1);
        }
    }
}

void ShapeSeries3D::generate_tube(float x1,
                                  float y1,
                                  float z1,
                                  float x2,
                                  float y2,
                                  float z2,
                                  float r_bottom,
                                  float r_top,
                                  int   segs,
                                  bool  cap_bottom,
                                  bool  cap_top)
{
    // Axis direction
    float dx  = x2 - x1;
    float dy  = y2 - y1;
    float dz  = z2 - z1;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-8f)
        return;

    float ax = dx / len;
    float ay = dy / len;
    float az = dz / len;

    // Build orthonormal basis (u, v) perpendicular to axis
    float ux, uy, uz;
    if (std::abs(ay) < 0.9f)
    {
        // Cross with Y-up
        ux = az;
        uy = 0.0f;
        uz = -ax;
    }
    else
    {
        // Cross with X-right
        ux = 0.0f;
        uy = -az;
        uz = ay;
    }
    float u_len = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (u_len < 1e-8f)
        return;
    ux /= u_len;
    uy /= u_len;
    uz /= u_len;

    // v = axis x u
    float vx = ay * uz - az * uy;
    float vy = az * ux - ax * uz;
    float vz = ax * uy - ay * ux;

    // Generate body vertices
    uint32_t base_idx = static_cast<uint32_t>(vertices_.size() / 6);

    for (int i = 0; i <= segs; ++i)
    {
        float angle = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segs);
        float cos_a = std::cos(angle);
        float sin_a = std::sin(angle);

        // Radial direction (for normals)
        float rx = ux * cos_a + vx * sin_a;
        float ry = uy * cos_a + vy * sin_a;
        float rz = uz * cos_a + vz * sin_a;

        // For a cone/cylinder, the normal needs to account for the slope
        float slope_factor = (r_bottom - r_top) / len;
        float nx           = rx + ax * slope_factor;
        float ny           = ry + ay * slope_factor;
        float nz           = rz + az * slope_factor;
        float n_len        = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (n_len > 1e-8f)
        {
            nx /= n_len;
            ny /= n_len;
            nz /= n_len;
        }

        // Bottom vertex
        push_vertex(x1 + rx * r_bottom, y1 + ry * r_bottom, z1 + rz * r_bottom, nx, ny, nz);
        // Top vertex
        push_vertex(x2 + rx * r_top, y2 + ry * r_top, z2 + rz * r_top, nx, ny, nz);
    }

    // Body triangles
    for (int i = 0; i < segs; ++i)
    {
        uint32_t b0 = base_idx + static_cast<uint32_t>(i * 2);
        uint32_t t0 = b0 + 1;
        uint32_t b1 = b0 + 2;
        uint32_t t1 = b0 + 3;
        push_tri(b0, t0, b1);
        push_tri(b1, t0, t1);
    }

    // Bottom cap
    if (cap_bottom && r_bottom > 1e-8f)
    {
        uint32_t center = push_vertex(x1, y1, z1, -ax, -ay, -az);
        for (int i = 0; i < segs; ++i)
        {
            float a0   = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segs);
            float a1   = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(segs);
            float cos0 = std::cos(a0);
            float sin0 = std::sin(a0);
            float cos1 = std::cos(a1);
            float sin1 = std::sin(a1);

            uint32_t p0 = push_vertex(x1 + (ux * cos0 + vx * sin0) * r_bottom,
                                      y1 + (uy * cos0 + vy * sin0) * r_bottom,
                                      z1 + (uz * cos0 + vz * sin0) * r_bottom,
                                      -ax,
                                      -ay,
                                      -az);
            uint32_t p1 = push_vertex(x1 + (ux * cos1 + vx * sin1) * r_bottom,
                                      y1 + (uy * cos1 + vy * sin1) * r_bottom,
                                      z1 + (uz * cos1 + vz * sin1) * r_bottom,
                                      -ax,
                                      -ay,
                                      -az);
            push_tri(center, p1, p0);
        }
    }

    // Top cap
    if (cap_top && r_top > 1e-8f)
    {
        uint32_t center = push_vertex(x2, y2, z2, ax, ay, az);
        for (int i = 0; i < segs; ++i)
        {
            float a0   = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segs);
            float a1   = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(segs);
            float cos0 = std::cos(a0);
            float sin0 = std::sin(a0);
            float cos1 = std::cos(a1);
            float sin1 = std::sin(a1);

            uint32_t p0 = push_vertex(x2 + (ux * cos0 + vx * sin0) * r_top,
                                      y2 + (uy * cos0 + vy * sin0) * r_top,
                                      z2 + (uz * cos0 + vz * sin0) * r_top,
                                      ax,
                                      ay,
                                      az);
            uint32_t p1 = push_vertex(x2 + (ux * cos1 + vx * sin1) * r_top,
                                      y2 + (uy * cos1 + vy * sin1) * r_top,
                                      z2 + (uz * cos1 + vz * sin1) * r_top,
                                      ax,
                                      ay,
                                      az);
            push_tri(center, p0, p1);
        }
    }
}

void ShapeSeries3D::generate_cylinder(const ShapeDef3D& def)
{
    generate_tube(def.params[0],
                  def.params[1],
                  def.params[2],
                  def.params[3],
                  def.params[4],
                  def.params[5],
                  def.params[6],
                  def.params[6],
                  def.segments,
                  true,
                  true);
}

void ShapeSeries3D::generate_cone(const ShapeDef3D& def)
{
    generate_tube(def.params[0],
                  def.params[1],
                  def.params[2],
                  def.params[3],
                  def.params[4],
                  def.params[5],
                  def.params[6],
                  0.0f,
                  def.segments,
                  true,
                  false);
}

void ShapeSeries3D::generate_arrow3d(const ShapeDef3D& def)
{
    float x1      = def.params[0];
    float y1      = def.params[1];
    float z1      = def.params[2];
    float x2      = def.params[3];
    float y2      = def.params[4];
    float z2      = def.params[5];
    float shaft_r = def.params[6];

    float dx  = x2 - x1;
    float dy  = y2 - y1;
    float dz  = z2 - z1;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-8f)
        return;

    float head_len = len * def.arrow_head_length_frac;
    float head_r   = shaft_r * def.arrow_head_radius_mult;

    // Shaft end point (where head begins)
    float frac = (len - head_len) / len;
    float sx   = x1 + dx * frac;
    float sy   = y1 + dy * frac;
    float sz   = z1 + dz * frac;

    // Shaft (cylinder)
    generate_tube(x1, y1, z1, sx, sy, sz, shaft_r, shaft_r, def.segments, true, false);

    // Head (cone)
    generate_tube(sx, sy, sz, x2, y2, z2, head_r, 0.0f, def.segments, true, false);
}

void ShapeSeries3D::generate_plane(const ShapeDef3D& def)
{
    float cx = def.params[0];
    float cy = def.params[1];
    float cz = def.params[2];
    float nx = def.params[3];
    float ny = def.params[4];
    float nz = def.params[5];
    float hs = def.params[6];

    // Normalize normal
    float n_len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (n_len < 1e-8f)
        return;
    nx /= n_len;
    ny /= n_len;
    nz /= n_len;

    // Build tangent basis
    float ux, uy, uz;
    if (std::abs(ny) < 0.9f)
    {
        ux = nz;
        uy = 0.0f;
        uz = -nx;
    }
    else
    {
        ux = 0.0f;
        uy = -nz;
        uz = ny;
    }
    float u_len = std::sqrt(ux * ux + uy * uy + uz * uz);
    ux /= u_len;
    uy /= u_len;
    uz /= u_len;

    float vx = ny * uz - nz * uy;
    float vy = nz * ux - nx * uz;
    float vz = nx * uy - ny * ux;

    // Four corners
    uint32_t v0 = push_vertex(cx - ux * hs - vx * hs,
                              cy - uy * hs - vy * hs,
                              cz - uz * hs - vz * hs,
                              nx,
                              ny,
                              nz);
    uint32_t v1 = push_vertex(cx + ux * hs - vx * hs,
                              cy + uy * hs - vy * hs,
                              cz + uz * hs - vz * hs,
                              nx,
                              ny,
                              nz);
    uint32_t v2 = push_vertex(cx + ux * hs + vx * hs,
                              cy + uy * hs + vy * hs,
                              cz + uz * hs + vz * hs,
                              nx,
                              ny,
                              nz);
    uint32_t v3 = push_vertex(cx - ux * hs + vx * hs,
                              cy - uy * hs + vy * hs,
                              cz - uz * hs + vz * hs,
                              nx,
                              ny,
                              nz);

    push_tri(v0, v1, v2);
    push_tri(v0, v2, v3);
    // Back face
    uint32_t v4 = push_vertex(cx - ux * hs - vx * hs,
                              cy - uy * hs - vy * hs,
                              cz - uz * hs - vz * hs,
                              -nx,
                              -ny,
                              -nz);
    uint32_t v5 = push_vertex(cx + ux * hs - vx * hs,
                              cy + uy * hs - vy * hs,
                              cz + uz * hs - vz * hs,
                              -nx,
                              -ny,
                              -nz);
    uint32_t v6 = push_vertex(cx + ux * hs + vx * hs,
                              cy + uy * hs + vy * hs,
                              cz + uz * hs + vz * hs,
                              -nx,
                              -ny,
                              -nz);
    uint32_t v7 = push_vertex(cx - ux * hs + vx * hs,
                              cy - uy * hs + vy * hs,
                              cz - uz * hs + vz * hs,
                              -nx,
                              -ny,
                              -nz);
    push_tri(v4, v6, v5);
    push_tri(v4, v7, v6);
}

// ── Rebuild geometry ────────────────────────────────────────────────────────

void ShapeSeries3D::rebuild_geometry()
{
    vertices_.clear();
    indices_.clear();

    for (const auto& def : shapes_)
    {
        switch (def.type)
        {
            case ShapeDef3D::Type::Box:
                generate_box(def);
                break;
            case ShapeDef3D::Type::Sphere:
                generate_sphere(def);
                break;
            case ShapeDef3D::Type::Cylinder:
                generate_cylinder(def);
                break;
            case ShapeDef3D::Type::Cone:
                generate_cone(def);
                break;
            case ShapeDef3D::Type::Arrow3D:
                generate_arrow3d(def);
                break;
            case ShapeDef3D::Type::Plane:
                generate_plane(def);
                break;
        }
    }
}

void ShapeSeries3D::record_commands(Renderer& /*renderer*/)
{
    // GPU command recording handled by the Renderer.
}

vec3 ShapeSeries3D::compute_centroid() const
{
    if (vertices_.empty())
        return {0, 0, 0};

    float  sx = 0.0f;
    float  sy = 0.0f;
    float  sz = 0.0f;
    size_t n  = vertices_.size() / 6;
    for (size_t i = 0; i < n; ++i)
    {
        sx += vertices_[i * 6 + 0];
        sy += vertices_[i * 6 + 1];
        sz += vertices_[i * 6 + 2];
    }
    float fn = static_cast<float>(n);
    return {sx / fn, sy / fn, sz / fn};
}

void ShapeSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (vertices_.empty())
    {
        min_out = {0, 0, 0};
        max_out = {0, 0, 0};
        return;
    }
    min_out  = {1e30f, 1e30f, 1e30f};
    max_out  = {-1e30f, -1e30f, -1e30f};
    size_t n = vertices_.size() / 6;
    for (size_t i = 0; i < n; ++i)
    {
        float x = vertices_[i * 6 + 0];
        float y = vertices_[i * 6 + 1];
        float z = vertices_[i * 6 + 2];
        if (x < min_out.x)
            min_out.x = x;
        if (y < min_out.y)
            min_out.y = y;
        if (z < min_out.z)
            min_out.z = z;
        if (x > max_out.x)
            max_out.x = x;
        if (y > max_out.y)
            max_out.y = y;
        if (z > max_out.z)
            max_out.z = z;
    }
}

}   // namespace spectra

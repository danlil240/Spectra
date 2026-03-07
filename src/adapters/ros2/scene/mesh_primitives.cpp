#include "scene/mesh_primitives.hpp"

#include <cmath>

namespace spectra::adapters::ros2
{

namespace
{
constexpr float PI = 3.14159265358979323846f;

void push_vertex(std::vector<float>& v,
                 float px, float py, float pz,
                 float nx, float ny, float nz)
{
    v.push_back(px);
    v.push_back(py);
    v.push_back(pz);
    v.push_back(nx);
    v.push_back(ny);
    v.push_back(nz);
}

void push_quad(std::vector<uint32_t>& idx,
               uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    idx.push_back(a);
    idx.push_back(b);
    idx.push_back(c);
    idx.push_back(a);
    idx.push_back(c);
    idx.push_back(d);
}
}   // namespace

PrimitiveMesh generate_cube()
{
    PrimitiveMesh mesh;
    mesh.vertices.reserve(24 * 6);
    mesh.indices.reserve(36);

    // clang-format off
    struct Face { float nx, ny, nz; float verts[4][3]; };
    const Face faces[] = {
        { 0,  0,  1, {{ 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}}},
        { 0,  0, -1, {{-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}}},
        { 1,  0,  0, {{ 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f, -0.5f}}},
        {-1,  0,  0, {{-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f}}},
        { 0,  1,  0, {{ 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}}},
        { 0, -1,  0, {{ 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}}},
    };
    // clang-format on

    for (const auto& face : faces)
    {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size() / 6);
        for (int i = 0; i < 4; ++i)
            push_vertex(mesh.vertices,
                        face.verts[i][0], face.verts[i][1], face.verts[i][2],
                        face.nx, face.ny, face.nz);
        push_quad(mesh.indices, base, base + 1, base + 2, base + 3);
    }

    return mesh;
}

PrimitiveMesh generate_sphere(int stacks, int slices)
{
    if (stacks < 3)
        stacks = 3;
    if (slices < 4)
        slices = 4;

    PrimitiveMesh mesh;
    const float radius = 0.5f;

    // Vertices
    for (int i = 0; i <= stacks; ++i)
    {
        float phi   = PI * static_cast<float>(i) / static_cast<float>(stacks);
        float sin_p = std::sin(phi);
        float cos_p = std::cos(phi);

        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * PI * static_cast<float>(j) / static_cast<float>(slices);
            float sin_t = std::sin(theta);
            float cos_t = std::cos(theta);

            float nx = sin_p * cos_t;
            float ny = cos_p;
            float nz = sin_p * sin_t;
            push_vertex(mesh.vertices,
                        radius * nx, radius * ny, radius * nz,
                        nx, ny, nz);
        }
    }

    // Indices
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            uint32_t row0 = static_cast<uint32_t>(i * (slices + 1) + j);
            uint32_t row1 = row0 + static_cast<uint32_t>(slices + 1);

            mesh.indices.push_back(row0);
            mesh.indices.push_back(row1);
            mesh.indices.push_back(row0 + 1);

            mesh.indices.push_back(row0 + 1);
            mesh.indices.push_back(row1);
            mesh.indices.push_back(row1 + 1);
        }
    }

    return mesh;
}

PrimitiveMesh generate_cylinder(int slices)
{
    if (slices < 4)
        slices = 4;

    PrimitiveMesh mesh;
    const float radius = 0.5f;
    const float half_h = 0.5f;

    // Side vertices
    for (int i = 0; i <= slices; ++i)
    {
        float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        float cos_t = std::cos(theta);
        float sin_t = std::sin(theta);
        float nx    = cos_t;
        float nz    = sin_t;

        push_vertex(mesh.vertices,
                    radius * cos_t, half_h, radius * sin_t,
                    nx, 0.0f, nz);
        push_vertex(mesh.vertices,
                    radius * cos_t, -half_h, radius * sin_t,
                    nx, 0.0f, nz);
    }

    // Side indices
    for (int i = 0; i < slices; ++i)
    {
        uint32_t base = static_cast<uint32_t>(i * 2);
        push_quad(mesh.indices, base, base + 1, base + 3, base + 2);
    }

    // Top cap
    uint32_t top_center = static_cast<uint32_t>(mesh.vertices.size() / 6);
    push_vertex(mesh.vertices, 0.0f, half_h, 0.0f, 0.0f, 1.0f, 0.0f);
    for (int i = 0; i <= slices; ++i)
    {
        float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        push_vertex(mesh.vertices,
                    radius * std::cos(theta), half_h, radius * std::sin(theta),
                    0.0f, 1.0f, 0.0f);
    }
    for (int i = 0; i < slices; ++i)
    {
        mesh.indices.push_back(top_center);
        mesh.indices.push_back(top_center + 1 + static_cast<uint32_t>(i));
        mesh.indices.push_back(top_center + 2 + static_cast<uint32_t>(i));
    }

    // Bottom cap
    uint32_t bot_center = static_cast<uint32_t>(mesh.vertices.size() / 6);
    push_vertex(mesh.vertices, 0.0f, -half_h, 0.0f, 0.0f, -1.0f, 0.0f);
    for (int i = 0; i <= slices; ++i)
    {
        float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        push_vertex(mesh.vertices,
                    radius * std::cos(theta), -half_h, radius * std::sin(theta),
                    0.0f, -1.0f, 0.0f);
    }
    for (int i = 0; i < slices; ++i)
    {
        mesh.indices.push_back(bot_center);
        mesh.indices.push_back(bot_center + 2 + static_cast<uint32_t>(i));
        mesh.indices.push_back(bot_center + 1 + static_cast<uint32_t>(i));
    }

    return mesh;
}

PrimitiveMesh generate_cone(int slices)
{
    if (slices < 4)
        slices = 4;

    PrimitiveMesh mesh;
    const float radius = 0.5f;
    const float half_h = 0.5f;
    const float slope  = radius / (2.0f * half_h);   // for normal calc

    // Apex vertex at top for each slice (unique normal per slice for smooth shading)
    for (int i = 0; i < slices; ++i)
    {
        float theta0 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        float theta1 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(slices);
        float mid    = (theta0 + theta1) * 0.5f;
        float nx     = std::cos(mid);
        float nz     = std::sin(mid);
        float ny     = slope;
        float len    = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len;
        ny /= len;
        nz /= len;

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size() / 6);
        push_vertex(mesh.vertices, 0.0f, half_h, 0.0f, nx, ny, nz);
        push_vertex(mesh.vertices,
                    radius * std::cos(theta0), -half_h, radius * std::sin(theta0),
                    nx, ny, nz);
        push_vertex(mesh.vertices,
                    radius * std::cos(theta1), -half_h, radius * std::sin(theta1),
                    nx, ny, nz);

        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
    }

    // Bottom cap
    uint32_t bot_center = static_cast<uint32_t>(mesh.vertices.size() / 6);
    push_vertex(mesh.vertices, 0.0f, -half_h, 0.0f, 0.0f, -1.0f, 0.0f);
    for (int i = 0; i <= slices; ++i)
    {
        float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        push_vertex(mesh.vertices,
                    radius * std::cos(theta), -half_h, radius * std::sin(theta),
                    0.0f, -1.0f, 0.0f);
    }
    for (int i = 0; i < slices; ++i)
    {
        mesh.indices.push_back(bot_center);
        mesh.indices.push_back(bot_center + 2 + static_cast<uint32_t>(i));
        mesh.indices.push_back(bot_center + 1 + static_cast<uint32_t>(i));
    }

    return mesh;
}

PrimitiveMesh generate_arrow(int slices)
{
    if (slices < 4)
        slices = 4;

    PrimitiveMesh mesh;
    const float shaft_radius = 0.02f;
    const float head_radius  = 0.06f;
    const float shaft_length = 0.8f;
    const float head_length  = 0.2f;

    // Shaft: cylinder along +X, from x=0 to x=shaft_length
    for (int i = 0; i <= slices; ++i)
    {
        float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        float cy    = std::cos(theta);
        float cz    = std::sin(theta);

        push_vertex(mesh.vertices,
                    0.0f, shaft_radius * cy, shaft_radius * cz,
                    0.0f, cy, cz);
        push_vertex(mesh.vertices,
                    shaft_length, shaft_radius * cy, shaft_radius * cz,
                    0.0f, cy, cz);
    }
    for (int i = 0; i < slices; ++i)
    {
        uint32_t base = static_cast<uint32_t>(i * 2);
        push_quad(mesh.indices, base, base + 1, base + 3, base + 2);
    }

    // Head: cone from x=shaft_length to x=1.0
    const float tip_x  = shaft_length + head_length;
    const float slope  = head_radius / head_length;

    for (int i = 0; i < slices; ++i)
    {
        float theta0 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        float theta1 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(slices);
        float mid    = (theta0 + theta1) * 0.5f;

        // Normal for cone side: tangent to surface
        float ny  = std::cos(mid);
        float nz  = std::sin(mid);
        float nx  = slope;
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len;
        ny /= len;
        nz /= len;

        uint32_t base = static_cast<uint32_t>(mesh.vertices.size() / 6);
        push_vertex(mesh.vertices, tip_x, 0.0f, 0.0f, nx, ny, nz);
        push_vertex(mesh.vertices,
                    shaft_length, head_radius * std::cos(theta0), head_radius * std::sin(theta0),
                    nx, ny, nz);
        push_vertex(mesh.vertices,
                    shaft_length, head_radius * std::cos(theta1), head_radius * std::sin(theta1),
                    nx, ny, nz);

        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
    }

    // Head base cap (disc at x=shaft_length)
    uint32_t cap_center = static_cast<uint32_t>(mesh.vertices.size() / 6);
    push_vertex(mesh.vertices, shaft_length, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    for (int i = 0; i <= slices; ++i)
    {
        float theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        push_vertex(mesh.vertices,
                    shaft_length, head_radius * std::cos(theta), head_radius * std::sin(theta),
                    -1.0f, 0.0f, 0.0f);
    }
    for (int i = 0; i < slices; ++i)
    {
        mesh.indices.push_back(cap_center);
        mesh.indices.push_back(cap_center + 2 + static_cast<uint32_t>(i));
        mesh.indices.push_back(cap_center + 1 + static_cast<uint32_t>(i));
    }

    return mesh;
}

}   // namespace spectra::adapters::ros2

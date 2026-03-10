#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spectra::adapters::ros2
{

/// Mesh data with interleaved position + normal: {x, y, z, nx, ny, nz} per vertex.
/// Compatible with PipelineType::Mesh3D / Marker3D vertex format.
struct PrimitiveMesh
{
    std::vector<float>    vertices;   // interleaved {pos.x, pos.y, pos.z, norm.x, norm.y, norm.z}
    std::vector<uint32_t> indices;

    size_t vertex_count() const { return vertices.size() / 6; }
    size_t index_count() const { return indices.size(); }
    size_t vertex_bytes() const { return vertices.size() * sizeof(float); }
    size_t index_bytes() const { return indices.size() * sizeof(uint32_t); }
};

/// Unit cube centered at origin, side length 1.
PrimitiveMesh generate_cube();

/// Unit sphere centered at origin, radius 0.5.
/// @param stacks  Number of latitude divisions (>= 3).
/// @param slices  Number of longitude divisions (>= 4).
PrimitiveMesh generate_sphere(int stacks = 16, int slices = 24);

/// Unit cylinder along Y-axis, radius 0.5, height 1, centered at origin.
/// @param slices  Number of radial divisions (>= 4).
PrimitiveMesh generate_cylinder(int slices = 24);

/// Unit cone along Y-axis, radius 0.5 at base, height 1, base at y=-0.5.
/// @param slices  Number of radial divisions (>= 4).
PrimitiveMesh generate_cone(int slices = 24);

/// Arrow along +X axis: shaft cylinder + cone head.  Total length 1.
/// Shaft radius 0.02, head radius 0.06, head length 0.2.
PrimitiveMesh generate_arrow(int slices = 16);

}   // namespace spectra::adapters::ros2

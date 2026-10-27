#pragma once

// shape3d_sort.hpp — CPU back-to-front triangle sort for translucent
// ShapeSeries3D draw ranges.
//
// The 3D pass already orders translucent ranges against each other by their
// centroid (renderer.cpp).  Inside one range the triangles were drawn in
// generation order, so a translucent sphere or box blended its far faces over
// its near ones whenever the camera looked from the "wrong" side.  This sorts
// the triangles of each translucent range back to front for the current
// camera; the index layout (range offsets and sizes) is unchanged, so the
// sorted array is a drop-in replacement for ShapeSeries3D::indices().

#include <spectra/math3d.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

namespace spectra
{

// Sorts the triangles of indices[first, first + count) (count rounded down to
// whole triangles) back to front.  `vertices` is the ShapeSeries3D layout
// (6 floats per vertex: position then normal); `model` maps data to world
// space, where `eye` is the camera position and `forward` its unit view
// direction.  Perspective ranks triangles by distance from the eye;
// orthographic by depth along `forward`.  Out-of-range vertex references
// leave the range untouched.
inline void sort_triangles_back_to_front(std::span<uint32_t>    indices,
                                         size_t                 first,
                                         size_t                 count,
                                         std::span<const float> vertices,
                                         const mat4&            model,
                                         vec3                   eye,
                                         vec3                   forward,
                                         bool                   orthographic)
{
    if (first >= indices.size())
        return;
    count               = std::min(count, indices.size() - first) / 3 * 3;
    const size_t ntri   = count / 3;
    const size_t nverts = vertices.size() / 6;
    if (ntri < 2)
        return;

    std::vector<std::pair<float, uint32_t>> keyed(ntri);
    for (size_t t = 0; t < ntri; ++t)
    {
        double cx = 0.0, cy = 0.0, cz = 0.0;
        for (size_t k = 0; k < 3; ++k)
        {
            const uint32_t v = indices[first + t * 3 + k];
            if (v >= nverts)
                return;
            cx += vertices[v * 6 + 0];
            cy += vertices[v * 6 + 1];
            cz += vertices[v * 6 + 2];
        }
        const vec4   w   = mat4_mul_vec4(model,
                                         {static_cast<float>(cx / 3.0),
                                          static_cast<float>(cy / 3.0),
                                          static_cast<float>(cz / 3.0),
                                          1.0f});
        const vec3   d   = vec3{w.x, w.y, w.z} - eye;
        const double key = orthographic ? vec3_dot(d, forward) : vec3_dot(d, d);
        keyed[t]         = {static_cast<float>(key), static_cast<uint32_t>(t)};
    }

    // Farthest first; stable so equal-depth triangles keep generation order.
    std::stable_sort(keyed.begin(),
                     keyed.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<uint32_t> src(indices.begin() + static_cast<std::ptrdiff_t>(first),
                              indices.begin() + static_cast<std::ptrdiff_t>(first + count));
    for (size_t t = 0; t < ntri; ++t)
    {
        const size_t s             = static_cast<size_t>(keyed[t].second) * 3;
        indices[first + t * 3 + 0] = src[s + 0];
        indices[first + t * 3 + 1] = src[s + 1];
        indices[first + t * 3 + 2] = src[s + 2];
    }
}

}   // namespace spectra

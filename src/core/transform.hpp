#pragma once

#include <array>
#include <plotix/series.hpp>

namespace plotix
{

// 4x4 column-major matrix (glm-compatible layout)
using Mat4 = std::array<float, 16>;

// Coordinate transform utilities for mapping data space to screen space.
// The pipeline is: data → NDC → screen pixels.

// Build an orthographic projection matrix for 2D plotting.
// Maps [left, right] × [bottom, top] to NDC [-1, 1] × [-1, 1].
Mat4 ortho_projection(float left, float right, float bottom, float top);

// Map a data-space point to NDC given axis limits.
// Returns (ndc_x, ndc_y) in [-1, 1].
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

Vec2 data_to_ndc(float data_x, float data_y, float x_min, float x_max, float y_min, float y_max);

// Map an NDC point to screen pixel coordinates given a viewport rect.
Vec2 ndc_to_screen(float ndc_x, float ndc_y, const Rect& viewport);

// Convenience: data → screen in one step.
Vec2 data_to_screen(float data_x,
                    float data_y,
                    float x_min,
                    float x_max,
                    float y_min,
                    float y_max,
                    const Rect& viewport);

}  // namespace plotix

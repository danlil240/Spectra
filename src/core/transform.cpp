#include "transform.hpp"

namespace spectra
{

Mat4 ortho_projection(float left, float right, float bottom, float top)
{
    // Standard orthographic projection matrix (column-major).
    // Maps [left,right] × [bottom,top] → [-1,1] × [-1,1].
    // Near/far fixed to -1/1 for 2D.
    Mat4 m{};

    float rl = right - left;
    float tb = top - bottom;

    // Avoid division by zero
    if (rl == 0.0f)
        rl = 1.0f;
    if (tb == 0.0f)
        tb = 1.0f;

    // Column 0
    m[0] = 2.0f / rl;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;

    // Column 1
    m[4] = 0.0f;
    m[5] = 2.0f / tb;
    m[6] = 0.0f;
    m[7] = 0.0f;

    // Column 2
    m[8]  = 0.0f;
    m[9]  = 0.0f;
    m[10] = -1.0f;   // maps z to [-1,1], but irrelevant for 2D
    m[11] = 0.0f;

    // Column 3 (translation)
    m[12] = -(right + left) / rl;
    m[13] = -(top + bottom) / tb;
    m[14] = 0.0f;
    m[15] = 1.0f;

    return m;
}

Vec2 data_to_ndc(float data_x, float data_y, float x_min, float x_max, float y_min, float y_max)
{
    float range_x = x_max - x_min;
    float range_y = y_max - y_min;

    if (range_x == 0.0f)
        range_x = 1.0f;
    if (range_y == 0.0f)
        range_y = 1.0f;

    return {2.0f * (data_x - x_min) / range_x - 1.0f, 2.0f * (data_y - y_min) / range_y - 1.0f};
}

Vec2 ndc_to_screen(float ndc_x, float ndc_y, const Rect& viewport)
{
    return {viewport.x + (ndc_x + 1.0f) * 0.5f * viewport.w,
            viewport.y + (ndc_y + 1.0f) * 0.5f * viewport.h};
}

Vec2 data_to_screen(float       data_x,
                    float       data_y,
                    float       x_min,
                    float       x_max,
                    float       y_min,
                    float       y_max,
                    const Rect& viewport)
{
    auto ndc = data_to_ndc(data_x, data_y, x_min, x_max, y_min, y_max);
    return ndc_to_screen(ndc.x, ndc.y, viewport);
}

}   // namespace spectra

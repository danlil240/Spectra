#pragma once

// Helpers for drawing a figure laid out in logical pixels into a framebuffer
// with more physical pixels (HiDPI windows, browser canvases with
// devicePixelRatio > 1, scaled-up exports).
//
// Header-only and free of any GPU API so the math can be unit-tested in the
// default build.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spectra
{

struct PixelRect
{
    int32_t  x = 0;
    int32_t  y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
};

// Scale a logical scissor rect to framebuffer pixels, covering every pixel the
// scaled rect touches, then clip it to the target.  WebGPU rejects a scissor
// that extends past the render target, so the result always fits inside
// [0, target_w) x [0, target_h).
inline PixelRect scale_scissor_rect(int32_t  x,
                                    int32_t  y,
                                    uint32_t w,
                                    uint32_t h,
                                    float    scale,
                                    uint32_t target_w,
                                    uint32_t target_h)
{
    if (!(scale > 0.0f))
        scale = 1.0f;
    const double s  = static_cast<double>(scale);
    double       x0 = std::floor(static_cast<double>(x) * s);
    double       y0 = std::floor(static_cast<double>(y) * s);
    double       x1 = std::ceil((static_cast<double>(x) + static_cast<double>(w)) * s);
    double       y1 = std::ceil((static_cast<double>(y) + static_cast<double>(h)) * s);

    x0 = std::clamp(x0, 0.0, static_cast<double>(target_w));
    y0 = std::clamp(y0, 0.0, static_cast<double>(target_h));
    x1 = std::clamp(x1, x0, static_cast<double>(target_w));
    y1 = std::clamp(y1, y0, static_cast<double>(target_h));

    return PixelRect{static_cast<int32_t>(x0),
                     static_cast<int32_t>(y0),
                     static_cast<uint32_t>(x1 - x0),
                     static_cast<uint32_t>(y1 - y0)};
}

// Logical size of `px` framebuffer pixels at `scale` framebuffer pixels per
// logical pixel (the inverse of Backend::pixel_scale()).
inline uint32_t logical_extent(uint32_t px, float scale)
{
    if (!(scale > 1.0f))
        return px;
    return std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(px) / scale)));
}

// How a window maps onto its framebuffer: the figure is laid out in window
// coordinates (the space mouse events arrive in) and drawn at framebuffer
// resolution, `pixel_scale` framebuffer pixels per window unit.
struct SurfaceScale
{
    uint32_t layout_width  = 0;
    uint32_t layout_height = 0;
    float    pixel_scale   = 1.0f;
};

inline SurfaceScale compute_surface_scale(int window_w, int window_h, uint32_t fb_w, uint32_t fb_h)
{
    SurfaceScale s{fb_w, fb_h, 1.0f};
    if (window_w <= 0 || window_h <= 0 || fb_w == 0 || fb_h == 0)
        return s;

    // Use the width ratio; browsers and desktop compositors scale both axes
    // by the same factor, and rounding can make the height ratio differ by a
    // fraction of a pixel.
    const float scale = static_cast<float>(fb_w) / static_cast<float>(window_w);
    if (scale <= 1.0f)
        return s;   // no HiDPI: lay out directly in framebuffer pixels

    s.pixel_scale   = scale;
    s.layout_width  = logical_extent(fb_w, scale);
    s.layout_height = logical_extent(fb_h, scale);
    return s;
}

// Framebuffer size of a window `logical` device-independent pixels wide at
// `device_pixel_ratio` framebuffer pixels per logical pixel (Qt's
// QWindow::devicePixelRatio(), a browser's window.devicePixelRatio).
inline uint32_t framebuffer_extent(int logical, double device_pixel_ratio)
{
    if (logical <= 0)
        return 0;
    if (!(device_pixel_ratio > 0.0))
        device_pixel_ratio = 1.0;
    const long px = std::lround(static_cast<double>(logical) * device_pixel_ratio);
    return px > 0 ? static_cast<uint32_t>(px) : 0u;
}

// Map a point in window coordinates (device-independent pixels, the space
// mouse events arrive in) to the layout coordinates of a surface described by
// `s`.  The identity whenever the layout is the window size; it only scales
// when the layout fell back to framebuffer pixels (framebuffer not larger than
// the window, or the backend refused a pixel scale).
struct LayoutPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

inline LayoutPoint window_to_layout(const SurfaceScale& s,
                                    int                 window_w,
                                    int                 window_h,
                                    float               x,
                                    float               y)
{
    if (window_w <= 0 || window_h <= 0 || s.layout_width == 0 || s.layout_height == 0)
        return LayoutPoint{x, y};
    return LayoutPoint{x * static_cast<float>(s.layout_width) / static_cast<float>(window_w),
                       y * static_cast<float>(s.layout_height) / static_cast<float>(window_h)};
}

}   // namespace spectra

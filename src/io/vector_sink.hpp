#pragma once

#include <cstddef>

namespace spectra
{

/// Receives a plugin (custom) series' geometry for vector (SVG) export, in data
/// coordinates; colours are straight RGBA in [0, 1].
class CustomVectorSink
{
   public:
    virtual ~CustomVectorSink()                                                       = default;
    virtual void fill_polygon(const float* xy, size_t point_count, const float* rgba) = 0;
    virtual void stroke_polyline(const float* xy,
                                 size_t       point_count,
                                 const float* rgba,
                                 float        width_px)                                      = 0;
    // Axis-aligned rectangle between data corners (x0, y0) and (x1, y1).
    virtual void fill_rect(float x0, float y0, float x1, float y1, const float* rgba)
    {
        const float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
        fill_polygon(xy, 4, rgba);
    }
};

}   // namespace spectra

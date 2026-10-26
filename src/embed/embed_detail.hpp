#pragma once

// Internal helpers shared by EmbedSurface and the C API (spectra_embed_c.cpp).
// Pure logic only — no GPU access — so they can be unit-tested directly.

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace spectra::embed_detail
{

// Locate the (axes_index, series_index) of a series within a figure.
// Uses the same convention as DataInteraction's selection callbacks:
// 2D axes (Figure::axes()) are searched first, then 3D/all axes
// (Figure::all_axes()); each list is indexed from 0.  Writes -1/-1 when
// the series is not found.
inline void find_series_indices(const Figure* fig, const Series* s, int& out_ai, int& out_si)
{
    out_ai = -1;
    out_si = -1;
    if (!fig || !s)
        return;

    auto search = [&](const auto& axes_list) -> bool
    {
        for (std::size_t ai = 0; ai < axes_list.size(); ++ai)
        {
            if (!axes_list[ai])
                continue;
            const auto& ser = axes_list[ai]->series();
            for (std::size_t si = 0; si < ser.size(); ++si)
            {
                if (ser[si].get() == s)
                {
                    out_ai = static_cast<int>(ai);
                    out_si = static_cast<int>(si);
                    return true;
                }
            }
        }
        return false;
    };

    if (search(fig->axes()))
        return;
    search(fig->all_axes());
}

// Validate Figure::subplot / subplot3d arguments without risking int
// overflow in rows * cols.  Figure::subplot throws on invalid input, so
// C entry points must check first.
inline bool valid_subplot_args(int rows, int cols, int index)
{
    if (rows <= 0 || cols <= 0 || index < 1)
        return false;
    const int64_t cells = static_cast<int64_t>(rows) * static_cast<int64_t>(cols);
    // Figure::subplot computes rows * cols in int; reject grids that would overflow it.
    if (cells > static_cast<int64_t>(std::numeric_limits<int>::max()))
        return false;
    return static_cast<int64_t>(index) <= cells;
}

// Compute the byte size of a tightly packed RGBA8 buffer without 32-bit
// wraparound.  Returns false when width/height are zero or the size does
// not fit in size_t.
inline bool rgba_buffer_size(uint32_t width, uint32_t height, std::size_t& out_bytes)
{
    out_bytes = 0;
    if (width == 0 || height == 0)
        return false;
    const std::size_t w   = width;
    const std::size_t h   = height;
    const std::size_t max = std::numeric_limits<std::size_t>::max();
    if (w > max / h || (w * h) > max / 4)
        return false;
    out_bytes = w * h * 4;
    return true;
}

// Animation stepping parameters for spectra_embed_animation_play().
inline constexpr float kDefaultAnimationFps = 60.0f;
inline constexpr float kMaxAnimationFps     = 10000.0f;
inline constexpr int   kMaxAnimationFrames  = 100'000'000;

// Sanitize fps: non-finite, non-positive or absurdly high values fall back
// to 60 fps.  Returns the per-frame step in seconds.
inline float animation_step(float fps)
{
    if (!std::isfinite(fps) || fps <= 0.0f || fps > kMaxAnimationFps)
        fps = kDefaultAnimationFps;
    return 1.0f / fps;
}

// Number of frames to step for a duration.  duration <= 0 means a single
// step (returns 1).  NaN or +inf durations return 0 so the caller
// never loops forever.  The count is capped at kMaxAnimationFrames.
inline int animation_frame_count(float fps, float duration_sec)
{
    if (duration_sec <= 0.0f)
        return 1;
    if (!std::isfinite(duration_sec))
        return 0;
    const double step   = static_cast<double>(animation_step(fps));
    const double frames = std::ceil(static_cast<double>(duration_sec) / step);
    if (frames >= static_cast<double>(kMaxAnimationFrames))
        return kMaxAnimationFrames;
    return frames < 1.0 ? 1 : static_cast<int>(frames);
}

}   // namespace spectra::embed_detail

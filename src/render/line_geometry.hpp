#pragma once

// Helpers for the 2D line pipeline (src/gpu/shaders/line.vert / line.frag).

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "render/backend.hpp"

namespace spectra
{

// Bit 31 of the polyline's last index: the bound buffer holds (x, y, phase).
inline constexpr uint32_t LINE_PHASE_FLAG = 0x80000000u;

// Tell the line pipeline that buffer points [first_pt, last_pt] form one
// polyline: segments inside it are joined (round joins, no double blending
// where they overlap) and only its ends get caps.  NaN points still split it.
// With has_phase the buffer holds three floats per point, the third being the
// dash phase written by build_line_dash_phase().  Stored bit-for-bit in
// SeriesPushConstants::_pad2.
inline void set_line_polyline(SeriesPushConstants& pc,
                              uint32_t             first_pt,
                              uint32_t             last_pt,
                              bool                 has_phase = false)
{
    const uint32_t v[2] = {first_pt,
                           (last_pt & ~LINE_PHASE_FLAG) | (has_phase ? LINE_PHASE_FLAG : 0u)};
    static_assert(sizeof(v) == sizeof(pc._pad2));
    std::memcpy(pc._pad2, v, sizeof(v));
}

// Draw each segment on its own (both ends capped, dash pattern restarting at
// every segment), e.g. for buffers packing unrelated segments.
inline void set_line_isolated(SeriesPushConstants& pc)
{
    set_line_polyline(pc, 0, 0, false);
}

// Fill `out` with (x, y, phase) triples for the vec2 points in `xy`.  phase is
// the screen-space arc length from the start of each run of finite points
// (a NaN point starts a new run at 0), reduced modulo `period` (in px) and
// accumulated in double so long lines keep an exact dash phase.  The scale
// factors are logical px per axis-space unit; screen length is invariant
// under pan, so the result only changes with data, zoom or the pattern.
void build_line_dash_phase(std::span<const float> xy,
                           double                 px_per_unit_x,
                           double                 px_per_unit_y,
                           double                 period,
                           std::vector<float>&    out);

}   // namespace spectra

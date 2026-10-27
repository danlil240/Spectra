#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace spectra::detail
{

// Pick the double-precision origin that the double data setters subtract
// before storing float offsets (logical value = origin + stored float).
//
// Returns 0 when float already represents the data well, so ordinary data
// keeps its plain values in x_data()/y_data().  When the values are large
// compared to their spread (Unix timestamps, 1e7 + small variations) it
// returns the midpoint of the finite range, so the stored offsets are small
// and keep ~24 bits of precision relative to the spread.
inline double choose_data_origin(std::span<const double> values)
{
    double lo    = 0.0;
    double hi    = 0.0;
    bool   found = false;
    for (double v : values)
    {
        if (!std::isfinite(v))
            continue;
        lo    = found ? std::min(lo, v) : v;
        hi    = found ? std::max(hi, v) : v;
        found = true;
    }
    if (!found)
        return 0.0;

    const double range   = hi - lo;
    const double max_abs = std::max(std::abs(lo), std::abs(hi));
    // While the magnitude is within 64x the spread, float rounding stays below
    // 2^-18 of the spread (far below a pixel), so no origin is needed.
    if (max_abs <= 64.0 * range)
        return 0.0;
    // Constant data that float holds exactly needs no origin either.
    if (range == 0.0 && static_cast<double>(static_cast<float>(lo)) == lo)
        return 0.0;
    return lo + 0.5 * range;
}

// Pick the origin for the first point streamed into an empty series through
// the double-precision append(), when the series has no origin yet.
//
// One value says nothing about the spread of the data to come, so this rule
// only looks at magnitude: values of 1000 or more (where float spacing reaches
// ~6e-5 and grows with the value) become the origin, so later points are
// stored as small float offsets from it.  Smaller values keep origin 0, so
// ordinary data keeps its plain values in x_data()/y_data().
inline double choose_stream_origin(double first_value)
{
    constexpr double STREAM_ORIGIN_MIN_ABS = 1000.0;
    if (!std::isfinite(first_value) || std::abs(first_value) < STREAM_ORIGIN_MIN_ABS)
        return 0.0;
    return first_value;
}

// Convert doubles to float offsets from `origin` (computed in double).
inline std::vector<float> to_float_offsets(std::span<const double> values, double origin)
{
    std::vector<float> out(values.size());
    for (size_t i = 0; i < values.size(); ++i)
        out[i] = static_cast<float>(values[i] - origin);
    return out;
}

}   // namespace spectra::detail

#pragma once

// Line style and marker style of a series over IPC (the "line_style" /
// "marker_style" Python properties, SnapshotSeriesState::line_style /
// marker_style and the SET_SERIES_LINE_STYLE / SET_SERIES_MARKER_STYLE diff
// ops).  Header-only so the daemon, the window agent and the unit tests share
// one validation.

#include <spectra/plot_style.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>

#include "message.hpp"

namespace spectra::ipc
{

inline constexpr uint8_t LINE_STYLE_WIRE_MAX = static_cast<uint8_t>(LineStyle::DashDotDot);
inline constexpr uint8_t MARKER_STYLE_WIRE_MAX =
    static_cast<uint8_t>(MarkerStyle::FilledTriangleUp);

// Wire value -> LineStyle; false for SERIES_STYLE_UNSET or an unknown value.
inline bool line_style_from_wire(uint8_t v, LineStyle& out)
{
    if (v > LINE_STYLE_WIRE_MAX)
        return false;
    out = static_cast<LineStyle>(v);
    return true;
}

// Wire value -> MarkerStyle; false for SERIES_STYLE_UNSET or an unknown value.
inline bool marker_style_from_wire(uint8_t v, MarkerStyle& out)
{
    if (v > MARKER_STYLE_WIRE_MAX)
        return false;
    out = static_cast<MarkerStyle>(v);
    return true;
}

// DiffOp f1 (a double) -> wire value; SERIES_STYLE_UNSET when out of range.
inline uint8_t style_wire_from_double(double v, uint8_t max)
{
    if (!std::isfinite(v) || v < 0.0 || v > static_cast<double>(max) || v != std::floor(v))
        return SERIES_STYLE_UNSET;
    return static_cast<uint8_t>(v);
}

// Python "line_style" token: a format-string symbol ("-", "--", ":", "-.",
// "-..") or "none" / "" for no line.
inline bool parse_line_style_token(std::string_view t, LineStyle& out)
{
    if (t.empty() || t == "none" || t == "None")
        out = LineStyle::None;
    else if (t == "-")
        out = LineStyle::Solid;
    else if (t == "--")
        out = LineStyle::Dashed;
    else if (t == ":")
        out = LineStyle::Dotted;
    else if (t == "-.")
        out = LineStyle::DashDot;
    else if (t == "-..")
        out = LineStyle::DashDotDot;
    else
        return false;
    return true;
}

// Python "marker_style" token: a Spectra marker symbol (marker_style_symbol(),
// e.g. "o", "s", "^", "D") or "none" / "" for no marker.
inline bool parse_marker_style_token(std::string_view t, MarkerStyle& out)
{
    if (t.empty() || t == "none" || t == "None")
    {
        out = MarkerStyle::None;
        return true;
    }
    if (t.size() != 1)
        return false;
    for (uint8_t v = 1; v <= MARKER_STYLE_WIRE_MAX; ++v)
    {
        const auto m = static_cast<MarkerStyle>(v);
        if (marker_style_symbol(m) == t[0])
        {
            out = m;
            return true;
        }
    }
    return false;
}

}   // namespace spectra::ipc

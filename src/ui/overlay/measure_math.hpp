#pragma once

// Measure tool and data-tip maths, toolkit-free so it can be unit tested:
//   - snapping a screen position to the nearest drawn data sample,
//   - the measurement between two data points (data-space deltas, slope,
//     ratios on log axes, a distance only when both axes share a unit),
//   - formatting those numbers to the precision the view resolves,
//   - stepping / dragging a data tip along its series.
//
// Positions are always kept in data space; screen mapping goes through
// AxisMapping (axis space), so log axes measure and draw correctly.

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "axis_mapping.hpp"

namespace spectra::measure
{

// ─── Series sample access (line / scatter) ──────────────────────────────────

// Samples of a line or scatter series; 0 for other series types.
size_t point_count(const Series& series);

// Absolute (offset-included) data position of sample @p index.
bool point_at(const Series& series, size_t index, double& x, double& y);

// Central-difference dy/dx at sample @p index (one-sided at the ends).
// Line series only, and only where x is monotonic through the sample;
// false for scatter series, x reversals and non-finite results.
bool point_derivative(const Series& series, size_t index, float& dy_dx);

// ─── Snapping ───────────────────────────────────────────────────────────────

struct SnapPoint
{
    bool          found       = false;
    double        x           = 0.0;   // absolute data coordinates
    double        y           = 0.0;
    const Series* series      = nullptr;
    size_t        index       = 0;
    double        distance_px = 0.0;
};

// Nearest sample of a visible line / scatter series of @p axes within
// @p radius_px screen pixels of (sx, sy).  Distances are measured on screen
// through @p map, so a log axis snaps to the point drawn under the cursor.
SnapPoint snap_to_data(const Axes&        axes,
                       const AxisMapping& map,
                       double             sx,
                       double             sy,
                       double             radius_px);

// ─── Measurement ────────────────────────────────────────────────────────────

// Unit written in an axis label: "Time (s)" -> "s", "Voltage [mV]" -> "mV",
// "Speed / m/s" -> "" (only a trailing (...) or [...] counts).
std::string axis_unit(std::string_view label);

struct Measurement
{
    double dx = 0.0;   // data-space deltas (end - start)
    double dy = 0.0;

    bool   slope_valid = false;   // dy/dx (dx != 0)
    double slope       = 0.0;

    // Euclidean length, only meaningful when both axes are linear and carry
    // the same, known unit; otherwise X and Y units must not be mixed.
    bool   distance_valid = false;
    double distance       = 0.0;

    // Log axes: the ratio end/start (both > 0) and the decades it spans.
    bool   x_ratio_valid = false;
    double x_ratio       = 1.0;
    bool   y_ratio_valid = false;
    double y_ratio       = 1.0;
};

Measurement compute_measurement(double           x0,
                                double           y0,
                                double           x1,
                                double           y1,
                                ScaleType        xscale,
                                ScaleType        yscale,
                                std::string_view x_unit,
                                std::string_view y_unit);

// Data distance one screen pixel spans at data value @p v of an axis
// (`limits` in data space, `pixels` the viewport extent).  On a log axis it
// grows with v; 0 when unknown.
double pixel_resolution(ScaleType scale, AxisLimits limits, double pixels, double v);

// A data value / delta with as many digits as @p resolution resolves (0: six
// significant digits), never fixed "{:.4f}": 0.000123 -> "0.000123",
// 1234567 -> "1234567", 3.3677 at 0.01/px -> "3.37".
std::string format_value(double v, double resolution);

struct MeasureFormat
{
    std::string x_unit;
    std::string y_unit;
    ScaleType   xscale       = ScaleType::Linear;
    ScaleType   yscale       = ScaleType::Linear;
    double      x_resolution = 0.0;   // data per pixel (0 = full precision)
    double      y_resolution = 0.0;
};

// Units (from the axis labels; seconds on a date-time axis), scales and the
// per-pixel resolution of @p axes at data point (x_at, y_at).
MeasureFormat measure_format(const Axes& axes, double x_at, double y_at);

// Label lines for a measurement, e.g.
//   "Δx  3.37 s", "Δy  -203 V", "slope  -60.4 V/s", "dist  5.2 m"
// with "(×12.5)" appended to a log axis delta.
std::vector<std::string> measurement_lines(const Measurement& m, const MeasureFormat& fmt);

// ─── Screen geometry ────────────────────────────────────────────────────────

// Clip segment (x0, y0)-(x1, y1) to @p rect (Liang-Barsky), in place.  Returns
// false when no part of it is inside (or a coordinate is not finite).  Overlay
// lines between far-apart data points are clipped before drawing, so a deep
// zoom never feeds off-screen coordinates (or millions of dashes) to ImGui.
bool clip_segment(const Rect& rect, double& x0, double& y0, double& x1, double& y1);

// Dashes of @p dash_px for a line @p length_px long, at least 1 and at most
// MAX_DASHES.
constexpr int MAX_DASHES = 1024;
int           dash_count(double length_px, double dash_px);

// ─── Data tip navigation ────────────────────────────────────────────────────

// Index @p delta samples from @p index, clamped to the series; nullopt when
// the tip cannot move (already at that end, or no samples).
std::optional<size_t> step_index(const Series& series, size_t index, int delta);

// Sample a data tip dragged to screen position (sx, sy) snaps to: on x-sorted
// data the sample nearest in x (a tip slides along the curve as the mouse
// moves sideways), otherwise the sample nearest on screen.
std::optional<size_t> drag_index(const Series&      series,
                                 const AxisMapping& map,
                                 double             sx,
                                 double             sy);

}   // namespace spectra::measure

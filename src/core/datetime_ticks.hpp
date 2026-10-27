#pragma once

// Date-time axis ticks: values are Unix epoch seconds (UTC), ticks land on
// calendar-aligned instants (whole seconds, minutes, hours, days, months,
// years) and labels show only what changes between ticks ("14:30", "Mar 05",
// "2024").  The coarser context (date of an intraday axis, year of a day or
// month axis) goes into TickResult::offset_label, drawn in the axis corner.
//
// Used by Axes::compute_x_ticks()/compute_y_ticks() when the axis is in
// date-time mode (Axes::xaxis_datetime(true)), so the renderer and the SVG
// exporter label date-time axes the same way.

#include <cstdint>
#include <spectra/axes.hpp>

#include "core/axis_ticks.hpp"

namespace spectra
{

/// A UTC calendar date-time.
struct CivilTime
{
    int64_t year   = 1970;
    int     month  = 1;   // 1..12
    int     day    = 1;   // 1..31
    int     hour   = 0;
    int     minute = 0;
    double  second = 0.0;   // [0, 60), with fraction
};

/// Days since 1970-01-01 of a proleptic Gregorian date.
int64_t days_from_civil(int64_t year, int month, int day);

/// UTC calendar fields of epoch seconds @p t.
CivilTime civil_from_epoch(double t);

/// Date-time ticks for epoch-second limits [@p lo, @p hi].  Uses axis_px,
/// target_px and labels_side_by_side from @p opt.
TickResult generate_datetime_ticks(double lo, double hi, const LinearTickOptions& opt = {});

}   // namespace spectra

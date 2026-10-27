#pragma once

// Linear tick placement and label formatting for 2D axes.
//
// Ticks are spaced by the axis length on screen (about one tick per
// X_TICK_TARGET_PX / Y_TICK_TARGET_PX pixels) with 1-2-2.5-5 steps.  Labels on
// one axis share a single precision; large offsets and very large/small
// magnitudes move into a shared corner label (TickResult::offset_label), e.g.
// "×10⁻⁶" or "+1.7×10⁹".
//
// Axes::compute_x_ticks()/compute_y_ticks() are the only callers; the Vulkan
// renderer (window and headless) and the SVG exporter all read ticks from
// there, so every output path places and formats ticks the same way.

#include <spectra/axes.hpp>
#include <string>
#include <vector>

namespace spectra
{

/// Axis length assumed when the axes has no viewport yet (e.g. before layout).
inline constexpr double DEFAULT_TICK_AXIS_PX = 540.0;
/// Preferred distance between ticks on the x axis, in pixels.
inline constexpr double X_TICK_TARGET_PX = 90.0;
/// Preferred distance between ticks on the y axis, in pixels.
inline constexpr double Y_TICK_TARGET_PX = 80.0;
/// Approximate advance of one tick-label character (FontSize::Tick, Inter).
inline constexpr double TICK_LABEL_CHAR_PX = 8.0;
/// Minimum free space between two neighbouring x tick labels, in pixels.
inline constexpr double TICK_LABEL_GAP_PX = 16.0;

struct LinearTickOptions
{
    /// Axis length on screen in pixels; <= 0 uses DEFAULT_TICK_AXIS_PX.
    double axis_px = 0.0;
    /// Preferred pixel distance between ticks.
    double target_px = Y_TICK_TARGET_PX;
    /// Labels sit side by side (x axis): widen the step until they fit.
    bool labels_side_by_side = false;
    /// Category axis (box, violin, bar): only integer steps (1, 2, 5, 10, ...).
    bool integer_steps = false;
    /// Integer category positions (sorted, unique).  When they all fit, the
    /// ticks are placed exactly at these positions.
    std::vector<double> categories;
};

/// Shared formatting of one axis' tick labels:
/// label = (value - offset) / 10^exponent, printed with `decimals` decimals.
struct TickFormat
{
    double offset   = 0.0;
    int    exponent = 0;
    int    decimals = 0;
};

/// Pick offset, exponent and precision for ticks at multiples of @p step
/// inside the limits [@p lo, @p hi].
TickFormat choose_tick_format(const std::vector<double>& positions,
                              double                     step,
                              double                     lo,
                              double                     hi);

/// Format one tick value with an axis-wide format.
std::string format_tick_label(double value, const TickFormat& fmt);

/// Corner label for a format ("×10⁻⁶", "+1.7×10⁹", "×10⁻⁵ +7.9"); empty when
/// the labels are plain values.
std::string format_tick_offset(const TickFormat& fmt);

/// Linear ticks for the limits [@p lo, @p hi].
TickResult generate_linear_ticks(double lo, double hi, const LinearTickOptions& opt = {});

}   // namespace spectra

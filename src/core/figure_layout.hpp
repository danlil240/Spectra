#pragma once

// Assigns the subplot grid cells (compute_subplot_layout) to a figure's axes
// and adapts each plot area to what is drawn around it:
//
//  * Left margin: grows when the y tick labels (+ y axis label) of a 2D axes
//    need more room than Margins::left, measured with the real metrics of the
//    plot font (Inter, io/text_metrics), so the raster renderer and the SVG
//    exporter — which both read the resulting viewport — agree.  All axes of
//    a grid column grow by the same amount so their left edges stay aligned.
//  * LegendPosition::OutsideRight: a strip on the right of each cell is
//    reserved for the legend (AxesBase::outside_legend_area()).  A
//    LegendPosition::Best legend that would cover too much of a small plot
//    gets the same strip (legend_layout::auto_outside); the legend drawers
//    then place it there (legend_layout::effective_position).
//
// Every layout path (Figure::compute_layout for headless export, the window,
// the Qt and embed surfaces) goes through apply_subplot_layout().

#include <spectra/axes.hpp>
#include <spectra/fwd.hpp>
#include <vector>

#include "core/layout.hpp"

namespace spectra
{

/// Gap kept between the outer edge of the y axis label (or tick labels) and
/// the left edge of the layout cell, in pixels.
inline constexpr float LEFT_LABEL_OUTER_PAD = 6.0f;
/// Largest share of a cell's plot width the left margin may grow by.
inline constexpr float LEFT_GROWTH_MAX_FRACTION = 0.4f;

/// Pixels a 2D axes needs left of its plot area for the tick marks, y tick
/// labels and the rotated y axis label (as drawn by the renderer and the SVG
/// exporter), plus LEFT_LABEL_OUTER_PAD.  Uses the current viewport height
/// for the tick count.
float left_label_extent(const Axes& axes);

/// Set the viewports of @p fig's axes from @p rects (row-major, one per grid
/// slot, as returned by compute_subplot_layout with @p margins).
void apply_subplot_layout(Figure& fig, const std::vector<Rect>& rects, const Margins& margins);

}   // namespace spectra

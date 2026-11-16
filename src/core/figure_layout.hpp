#pragma once

// Assigns the subplot grid cells (compute_subplot_layout) to a figure's axes
// and adapts each plot area to what is drawn around it:
//
//  * Left margin: grows when the y tick labels (+ y axis label) of a 2D axes
//    need more room than Margins::left, measured with the real metrics of the
//    plot font (Inter, io/text_metrics), so the raster renderer and the SVG
//    exporter — which both read the resulting viewport — agree.  All axes of
//    a grid column grow by the same amount so their left edges stay aligned.
//  * Top / bottom margins: grow per grid row when the title, x tick labels or
//    x axis label are larger than the default margins are sized for (custom
//    AxisStyle font sizes, UI scale); theme-sized text keeps the plain cell.
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
/// Largest share of a cell's plot height the top / bottom margin may each grow by.
inline constexpr float VERTICAL_GROWTH_MAX_FRACTION = 0.3f;

struct AxisTextMetrics;

/// Pixels a 2D axes needs left of its plot area for the tick marks, y tick
/// labels and the rotated y axis label (as drawn by the renderer and the SVG
/// exporter), plus LEFT_LABEL_OUTER_PAD.  Uses the current viewport height
/// for the tick count.
/// Sizes come from the axes' AxisStyle and text scale (core/axis_text_style).
float left_label_extent(const Axes& axes);

/// Pixels a 2D axes needs below its plot area (tick marks, x tick labels, and
/// the row holding the x axis label and/or the x tick offset annotation) with
/// the text sizes @p m.
float bottom_label_extent(const Axes& axes, const AxisTextMetrics& m);

/// Pixels a 2D axes needs above its plot area (the row holding the title
/// and/or the y tick offset label) with the text sizes @p m; 0 when nothing is
/// drawn there.
float top_label_extent(const Axes& axes, const AxisTextMetrics& m);

/// Set the viewports of @p fig's axes from @p rects (row-major, one per grid
/// slot, as returned by compute_subplot_layout with @p margins).
/// @p text_scale (the UI scale on screen, 1 for exports) scales the axes text,
/// tick marks and their padding; it is stored on every axes
/// (AxesBase::text_scale()) for the renderer and exporters.  The top, bottom
/// and left margins grow (per row / column) when the text needs more room.
void apply_subplot_layout(Figure&                  fig,
                          const std::vector<Rect>& rects,
                          const Margins&           margins,
                          float                    text_scale = 1.0f);

}   // namespace spectra

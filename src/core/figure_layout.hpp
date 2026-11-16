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
// the Qt and embed surfaces) goes through layout_figure(), which also sizes
// the grid spacing (FigureStyle margins / gaps, constrained layout) and the
// suptitle band.

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

/// Same, with the room left of each grid column's plot areas given per column
/// (@p left_room[c], pixels): the left growth only takes what is missing.
void apply_subplot_layout(Figure&                   fig,
                          const std::vector<Rect>&  rects,
                          const std::vector<float>& left_room);

/// Room a 2D axes needs around its plot area for its decorations, in pixels:
/// tick marks and labels, axis labels, offset labels and title, as the
/// renderer and the SVG exporter draw them (plus a small clearance).
struct AxesDecorations
{
    float left   = 0.0f;
    float right  = 0.0f;
    float top    = 0.0f;
    float bottom = 0.0f;
};

/// Decorations of @p axes at its current viewport (tick count and label
/// overhang follow the viewport size).
AxesDecorations measure_decorations(const Axes& axes);

/// Height of the band the suptitle takes above the grid (0 without one).
float suptitle_band_height(const Figure& fig);
/// Font size and scale the suptitle is drawn with (FontSize::Title tier).
inline constexpr float SUPTITLE_FONT_PX = 18.0f;
inline constexpr float SUPTITLE_SCALE   = 1.15f;

/// Spacing of @p fig's grid: style gaps (manual layout) or, with
/// FigureStyle::constrained_layout, margins and gaps grown to fit the measured
/// decorations of @p decor (one per grid slot, row-major; default-constructed
/// for empty or 3D slots).  @p outer is the minimum outer margin; the result
/// never takes more than MAX_SPACING_FRACTION of @p width / @p height.
GridSpacing            compute_figure_spacing(const Figure&                       fig,
                                              const std::vector<AxesDecorations>& decor,
                                              float                               width,
                                              float                               height,
                                              const Margins&                      outer);
inline constexpr float MAX_SPACING_FRACTION = 0.6f;

/// Lay out @p fig's whole subplot grid in @p content (window / image pixels;
/// content.y already includes any scroll offset) with minimum outer margins
/// @p outer: suptitle band, spacing (manual or constrained), axes viewports,
/// suptitle_rect() and the tick labels hidden on inner shared axes.  Returns
/// the total content height (more than content.h when min_subplot_height
/// makes the grid scroll).  Every layout path goes through here.
float layout_figure(Figure&        fig,
                    const Rect&    content,
                    const Margins& outer,
                    float          min_subplot_height = 0.0f);

}   // namespace spectra

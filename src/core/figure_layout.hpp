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

/// Room already free around the plot areas of each grid column (left) and
/// row (top, bottom), in pixels: the outer margin or the gap to the
/// neighbour minus what the neighbour needs.  The margins only grow by what
/// is missing.
struct SubplotRoom
{
    std::vector<float> left;     // per column
    std::vector<float> top;      // per row
    std::vector<float> bottom;   // per row
};

/// Same, with the room given per column / row (@p room).  Leaves the axes'
/// text scale as it is.
void apply_subplot_layout(Figure& fig, const std::vector<Rect>& rects, const SubplotRoom& room);

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
/// overhang follow the viewport size) and its AxisStyle sizes / text scale.
AxesDecorations measure_decorations(const Axes& axes);

/// Height of the band the suptitle takes above the grid (0 without one), at
/// text scale @p text_scale (the UI scale on screen, 1 for exports).
float suptitle_band_height(const Figure& fig, float text_scale = 1.0f);
/// Text scale the figure was last laid out with (its axes' text_scale()).
float figure_text_scale(const Figure& fig);
/// Font size and scale the suptitle is drawn with (FontSize::Title tier).
inline constexpr float SUPTITLE_FONT_PX = 18.0f;
inline constexpr float SUPTITLE_SCALE   = 1.15f;

/// Spacing of @p fig's grid: style gaps (manual layout) or, with
/// FigureStyle::constrained_layout, margins and gaps grown to fit the measured
/// decorations of @p decor (one per grid slot, row-major; default-constructed
/// for empty or 3D slots).  @p outer is the minimum outer margin; the result
/// never takes more than MAX_SPACING_FRACTION of @p width / @p height.  The
/// style gaps are multiplied by @p gap_scale (the text scale).
GridSpacing            compute_figure_spacing(const Figure&                       fig,
                                              const std::vector<AxesDecorations>& decor,
                                              float                               width,
                                              float                               height,
                                              const Margins&                      outer,
                                              float                               gap_scale = 1.0f);
inline constexpr float MAX_SPACING_FRACTION = 0.6f;

/// Lay out @p fig's whole subplot grid in @p content (window / image pixels;
/// content.y already includes any scroll offset) with minimum outer margins
/// @p outer: suptitle band, spacing (manual or constrained), axes viewports,
/// suptitle_rect() and the tick labels hidden on inner shared axes.  Returns
/// the total content height (more than content.h when min_subplot_height
/// makes the grid scroll).  Every layout path goes through here.
/// @p text_scale (the UI scale on screen, 1 for exports) is stored on every
/// axes (AxesBase::text_scale()) before anything is measured: the decorations,
/// the style gaps and the suptitle band scale with it.  @p outer is expected
/// to be scaled by the caller.
float layout_figure(Figure&        fig,
                    const Rect&    content,
                    const Margins& outer,
                    float          min_subplot_height = 0.0f,
                    float          text_scale         = 1.0f);

}   // namespace spectra

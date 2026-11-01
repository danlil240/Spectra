#pragma once

// The format-neutral vector export walk shared by the SVG and PDF exporters.

#include <spectra/export.hpp>

#include "io/svg_export_internal.hpp"
#include "io/vector_writer.hpp"

namespace spectra
{

class Figure;

// Draws `figure` (layout already computed) through `writer`: every 2D axes, then
// every 3D axes, with the chrome colours of `style`.  The document background is
// the writer's job (style.background).
void draw_vector_scene(const Figure&        figure,
                       const SvgStyle&      style,
                       const SvgExportHost& host,
                       VectorWriter&        writer);

// Chrome colours for an export without an explicit SvgStyle: the light theme, or
// the figure's FigureStyle::background_override with matching ink.
SvgStyle default_svg_style_for(const Figure& figure);

}   // namespace spectra

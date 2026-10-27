#pragma once

#include <string>

#include "core/label_layout.hpp"

namespace spectra
{

// Text metrics the SVG exporter uses to fit plot labels (title, axis labels, 3D title)
// with the shared rules in core/label_layout.hpp.  Exposed so tests can compare the SVG
// fitting with the GPU/raster path.

// Drawn width (pixels) of `text` at plot label font tier `font`, measured with Inter.
float svg_plot_label_width(const std::string& text, LabelFont font);

// Font size (pixels) the exporter writes for plot label font tier `font`.
float svg_plot_label_font_px(LabelFont font);

}   // namespace spectra

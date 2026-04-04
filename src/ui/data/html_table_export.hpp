#pragma once

#include <spectra/figure.hpp>
#include <string>

namespace spectra
{

// Generate an accessible HTML table representation of a figure.
//
// The output is a self-contained HTML fragment containing one <table> element
// per axes in the figure. Each table has proper <caption>, <thead> and <tbody>
// elements so that screen readers can navigate the data.
//
// For 2D series the columns are: Index, X, Y.
// For 3D series (LineSeries3D / ScatterSeries3D) the columns are: Index, X, Y, Z.
//
// The figure title, axes titles, and axis labels are embedded in the HTML as
// headings so that the structural outline is meaningful to assistive technology.
std::string figure_to_html_table(const Figure& fig);

// Write the HTML table to a file.  Returns true on success.
bool figure_to_html_table_file(const Figure& fig, const std::string& path);

}   // namespace spectra

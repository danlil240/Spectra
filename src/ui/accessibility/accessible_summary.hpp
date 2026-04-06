#pragma once

#include <string>
#include <vector>

namespace spectra
{

class Figure;
class Axes;
class Series;

/// Options for accessible summary generation.
struct SummaryOptions
{
    bool include_series_ranges = true;   // Include min/max data range per series
    bool include_lod_info      = true;   // Include LoD level info for chunked series
    bool include_point_count   = true;   // Include point count info
    bool include_axis_ranges   = true;   // Include axis view ranges
    int  max_series_in_summary = 5;      // Max series to detail before summarizing
};

/// Generate a natural-language accessible summary for a Figure.
///
/// Example output:
/// "Figure with 2 axes (2×1 grid). Axes 1 ('Temperature'): 3 series."
///
/// @param figure  The figure to summarize.
/// @param options Options controlling summary verbosity.
/// @return        Natural-language string suitable for screen readers / HTML alt text.
std::string accessible_figure_summary(const Figure& figure, const SummaryOptions& options = {});

/// Generate an accessible summary for a single Axes object.
std::string accessible_axes_summary(const Axes&           axes,
                                    size_t                axes_index,
                                    const SummaryOptions& options = {});

/// Generate an accessible summary for a single Series.
std::string accessible_series_summary(const Series&         series,
                                      size_t                series_index,
                                      const SummaryOptions& options = {});

}   // namespace spectra

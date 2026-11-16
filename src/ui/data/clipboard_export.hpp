#pragma once

#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

// Convert series data to tab-separated values (TSV) suitable for pasting
// into spreadsheets, text editors, etc.
//
// Output format:
//   <label1>_x\t<label1>_y\t<label2>_x\t<label2>_y\t...
//   1.0\t2.0\t1.0\t3.0\t...
//   ...
//
// Missing values (when series have different lengths) are left empty.
std::string series_to_tsv(const std::vector<const Series*>& series);

// Same table with @p sep between cells; with ',' (File > Export > CSV Data)
// header cells holding a comma, quote or newline are quoted (RFC 4180).
std::string series_to_delimited(const std::vector<const Series*>& series, char sep);

// Writes every 2D line / scatter series of @p figure as CSV (File > Export >
// CSV Data).  False with a reason in @p error when there is nothing to export
// or the file cannot be written.
bool export_figure_csv(const Figure& figure, const std::string& path, std::string* error = nullptr);

}   // namespace spectra

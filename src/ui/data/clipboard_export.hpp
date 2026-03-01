#pragma once

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

}   // namespace spectra

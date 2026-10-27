#pragma once

// Public header re-export — kept for backward compatibility with internal includes.
#include <spectra/axes3d.hpp>

#include <string>

namespace spectra
{

// Text drawn at a 3D axis arrow tip: the axis label (or `fallback` when the
// label is empty), followed by the axis' tick offset / multiplier ("×10⁻⁶",
// "+1.7×10⁹") when its tick labels have one (TickResult::offset_label).
inline std::string axes3d_arrow_label(const std::string& label,
                                      const char*        fallback,
                                      const std::string& tick_offset)
{
    std::string text = label.empty() ? std::string(fallback) : label;
    if (!tick_offset.empty())
        text += "  " + tick_offset;
    return text;
}

}   // namespace spectra

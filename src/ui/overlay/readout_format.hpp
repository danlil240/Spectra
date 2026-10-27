#pragma once

#include <cmath>
#include <format>
#include <string>

namespace spectra::ui
{

// Format a data coordinate for tooltips, data labels and crosshair labels.
// Large values (Unix timestamps, 1e7 + small variations) use fixed notation
// with three decimals, because the short %g form would print the same
// "1e+07" for every point; other values use `sig_digits` significant digits.
inline std::string format_readout_value(double v, int sig_digits)
{
    if (std::abs(v) >= 1e6)
        return std::format("{:.3f}", v);
    return std::format("{:.{}g}", v, sig_digits);
}

}   // namespace spectra::ui

#pragma once

#include <cmath>

#include <algorithm>
#include <cstddef>
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

// Compact data coordinate for crosshair axis labels, which sit in a small box
// on the plot edge.  `resolution` is the data distance one screen pixel spans
// at the value (0 = unknown); the label carries just enough digits to resolve
// it, %.6g-style otherwise.  The result never exceeds `max_chars` characters:
// fixed notation when it fits, else scientific with the mantissa shortened, so
// epoch-sized values (1.7e9) cannot overflow the label.
inline std::string format_compact_value(double v, double resolution = 0.0, size_t max_chars = 12)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0.0 ? "inf" : "-inf";
    if (v == 0.0)
        return "0";
    max_chars = std::max<size_t>(max_chars, 7);   // room for "-1e+300"

    int sig = 6;
    if (resolution > 0.0 && std::isfinite(resolution))
    {
        // Fixed notation with as many decimals as one pixel needs.
        const int decimals =
            std::max(0, static_cast<int>(std::ceil(-std::log10(resolution) - 1e-9)));
        // Closer to zero than a pixel resolves (e.g. 1e-17 at a zero crossing): zero.
        if (std::abs(v) < resolution * 0.5)
            return decimals <= 6 ? std::format("{:.{}f}", 0.0, decimals) : "0";
        if (decimals <= 6 && std::abs(v) >= 1e-4)
        {
            const std::string fixed = std::format("{:.{}f}", v, decimals);
            if (fixed.size() <= max_chars)
                return fixed;
        }
        const double digits = std::log10(std::abs(v) / resolution);
        sig                 = std::clamp(static_cast<int>(std::ceil(digits)) + 1, 3, 15);
    }
    for (int s = sig; s >= 1; --s)
    {
        std::string out = std::format("{:.{}g}", v, s);
        if (out.size() <= max_chars)
            return out;
    }
    return std::format("{:.0e}", v);
}

}   // namespace spectra::ui

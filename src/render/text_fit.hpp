#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <spectra/series.hpp>

#include "text_renderer.hpp"

namespace spectra
{

// Result of fitting a single line of text into a horizontal budget.
struct FittedText
{
    std::string text;                          // text to draw (possibly ellipsized)
    FontSize    size      = FontSize::Title;   // font size to draw it at
    bool        truncated = false;             // true when characters were dropped
};

// Screen-space rectangle of a plot label (title, axis label) that was shrunk
// and ellipsized to fit its axes.  The UI uses it to show the full text on hover.
struct TruncatedTextRegion
{
    Rect        rect;
    std::string full_text;
};

// Measures the rendered width (pixels) of text at a given font size.
using TextWidthFn = std::function<float(const std::string&, FontSize)>;

// Fit text into max_width pixels.
//   1. Try each font size from `preferred` down to `min_size` (inclusive); the
//      first size at which the whole text fits is used.
//   2. Otherwise, at `min_size`, keep the longest UTF-8 prefix (on codepoint
//      boundaries, trailing spaces trimmed) that fits together with `ellipsis`.
//   3. If not even the ellipsis fits (e.g. a zero budget), the text is empty.
FittedText fit_text_to_width(const std::string& text,
                             float              max_width,
                             FontSize           preferred,
                             FontSize           min_size,
                             const TextWidthFn& measure,
                             std::string_view   ellipsis = "...");

}   // namespace spectra

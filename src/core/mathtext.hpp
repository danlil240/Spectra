#pragma once

// Matplotlib-style "mathtext" for plot text (titles, axis labels, legend entries,
// annotations): "$\alpha_1 = 10^{-3}$ s" renders with a Greek alpha, a subscript 1 and
// a superscript -3 instead of the literal source.
//
// Pure, GPU-free parsing and layout shared by the GPU text renderer
// (render/text_renderer.cpp) and the SVG / PDF exporters (io/), so every path draws
// and measures the same runs.
//
// Supported subset:
//  - `$...$` math segments; text outside them is plain.  `\$` is a literal dollar.
//    A string with an odd number of unescaped `$` is plain text (only `\$` is
//    unescaped), as in matplotlib.
//  - Greek letters (\alpha..\omega, \Alpha..\Omega, \varepsilon, \vartheta, ...) and
//    common symbols (\pm, \times, \cdot, \degree, \infty, \partial, \nabla, \sum,
//    \int, \approx, \neq, \leq, \geq, \rightarrow, ...), mapped to Unicode.
//  - `_x` / `_{...}` subscripts and `^x` / `^{...}` superscripts, nestable (scripts of
//    scripts shrink once more and then stay at that size).  `x_i^2` stacks the two.
//  - `\mathrm{...}` (upright: same as a plain group, there is no italic face) and
//    `\text{...}` (literal text).
//  - `{...}` groups, escapes `\{ \} \_ \^ \# \% \& \\`, spacing `\, \; \: \  \quad`.
//  - A hyphen in math becomes a minus sign (U+2212).
// Unknown commands are kept literally ("\foo"); unbalanced braces are tolerated.
// Not supported: \frac, \sqrt{} radicals, matrices, accents, font changes other than
// \mathrm / \text, italic math letters, automatic operator spacing.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::mathtext
{

// Size of a sub/superscript relative to its base.
inline constexpr float SCRIPT_SCALE = 0.7f;
// Smallest script size (scripts of scripts); deeper levels keep it.
inline constexpr float MIN_SCRIPT_SCALE = SCRIPT_SCALE * SCRIPT_SCALE;
// Baseline raise of a superscript / drop of a subscript, as a fraction of the base's
// font size.
inline constexpr float SUPERSCRIPT_RISE = 0.38f;
inline constexpr float SUBSCRIPT_DROP   = 0.17f;

struct Run
{
    enum class Kind : uint8_t
    {
        Text,          // draw `text`
        StackBegin,    // a stacked sub+superscript pair starts here (remember x)
        StackRewind,   // the second script of the pair starts back at the remembered x
        StackEnd,      // the pair ends: continue after the wider of the two scripts
    };

    Kind        kind = Kind::Text;
    std::string text;           // UTF-8, commands already replaced by their glyphs
    float       scale = 1.0f;   // font size relative to the base size
    float       shift = 0.0f;   // baseline shift in base font sizes (positive = up)
    float       x     = 0.0f;   // pen x (pixels) from the text origin, set by layout()
};

// True when `text` may need mathtext handling (it contains a '$').  Strings without a
// '$' are always plain and drawn unchanged; callers use this as the fast path.
inline bool has_markup(std::string_view text)
{
    return text.find('$') != std::string_view::npos;
}

// True when `text` has math segments: an even, non-zero number of unescaped '$'.
bool is_math_text(std::string_view text);

// Splits `text` into styled runs.  Plain text (no math segments) yields at most one
// run, with `\$` replaced by '$'.  Empty runs are dropped.
std::vector<Run> parse(std::string_view text);

// The concatenated text of all runs (what a reader would see, without sizes).
std::string to_plain_text(std::string_view text);

// Advance width in pixels of `text` drawn at `scale` times the base font size.
using AdvanceFn = std::function<float(std::string_view text, float scale)>;

// Sets each run's x and returns the total advance width in pixels.
float layout(std::vector<Run>& runs, const AdvanceFn& advance);

// How far scripts reach beyond a plain line of the base font (pixels, >= 0).
struct Overhang
{
    float above = 0.0f;   // above the base font's ascent
    float below = 0.0f;   // below the base font's descent
};

// `font_px` is the base font size the shifts are relative to; `ascent_px` and
// `descent_px` are the base font's ascent and descent (both positive).
Overhang overhang(const std::vector<Run>& runs, float font_px, float ascent_px, float descent_px);

// After math text was cut to a prefix (ellipsizing a long label): closes a math segment
// left open by the cut and drops a dangling backslash / script operator, so the prefix
// still renders as math rather than with a literal '$'.  No-op for plain text.
void close_truncated(std::string& prefix);

}   // namespace spectra::mathtext

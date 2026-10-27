#pragma once

// Pure (ImGui-free) helpers that keep hover overlays (data tooltips, crosshair
// readouts) inside the window: UTF-8 safe ellipsizing and wrapping of labels
// against a pixel budget, a tooltip content layout that never hides the value
// column, and edge/corner clamping of the final box.  Text is measured through
// a caller-supplied function so the helpers can be unit tested without fonts.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::ui::overlay_fit
{

// Minimum distance (pixels) kept between an overlay box and the window edge.
inline constexpr float WINDOW_MARGIN = 4.0f;

// Maximum number of lines a long series name may wrap onto in a tooltip.
inline constexpr int MAX_NAME_LINES = 3;

// Measures the rendered width (pixels) of a run of text.
using MeasureFn = std::function<float(std::string_view)>;

// Byte offset of the codepoint boundary after position i (never splits a
// UTF-8 sequence; stray continuation bytes are skipped as a unit).
inline size_t utf8_next(std::string_view s, size_t i)
{
    if (i >= s.size())
        return s.size();
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0u) == 0x80u)
        ++i;
    return i;
}

// Byte offsets of every codepoint boundary in s, starting with 0 and ending with s.size().
inline std::vector<size_t> utf8_boundaries(std::string_view s)
{
    std::vector<size_t> ends;
    ends.push_back(0);
    for (size_t i = 0; i < s.size();)
    {
        i = utf8_next(s, i);
        ends.push_back(i);
    }
    return ends;
}

inline std::string_view trim_trailing_spaces(std::string_view s)
{
    while (!s.empty() && s.back() == ' ')
        s.remove_suffix(1);
    return s;
}

// Longest codepoint prefix of text (byte length) whose measured width, with
// `suffix` appended, fits max_w.  Returns 0 when not even one codepoint fits.
inline size_t longest_fitting_prefix(std::string_view text,
                                     float            max_w,
                                     const MeasureFn& measure,
                                     std::string_view suffix = {})
{
    const std::vector<size_t> ends = utf8_boundaries(text);
    auto                      fits = [&](size_t k)
    {
        std::string s(trim_trailing_spaces(text.substr(0, ends[k])));
        s.append(suffix);
        return measure(s) <= max_w;
    };
    // Width grows with prefix length: binary search the largest fitting k.
    size_t lo = 0;
    size_t hi = ends.size() - 1;
    while (lo < hi)
    {
        const size_t mid = (lo + hi + 1) / 2;
        if (fits(mid))
            lo = mid;
        else
            hi = mid - 1;
    }
    return ends[lo];
}

// Shorten text to fit max_w pixels: returned unchanged when it fits, otherwise
// the longest UTF-8 prefix followed by `ellipsis`; empty when not even the
// ellipsis fits.
inline std::string ellipsize(std::string_view text,
                             float            max_w,
                             const MeasureFn& measure,
                             std::string_view ellipsis = "...")
{
    if (!measure || measure(text) <= max_w)
        return std::string(text);
    if (measure(ellipsis) > max_w)
        return {};
    const size_t len = longest_fitting_prefix(text, max_w, measure, ellipsis);
    std::string  out(trim_trailing_spaces(text.substr(0, len)));
    out.append(ellipsis);
    return out;
}

// Characters after which a long identifier may be broken (spaces, path and
// field separators), so ROS topic paths and dotted names wrap at a joint.
inline bool is_break_after(char c)
{
    switch (c)
    {
        case ' ':
        case '/':
        case '_':
        case '.':
        case '-':
        case ':':
        case ',':
        case '\\':
            return true;
        default:
            return false;
    }
}

// Wrap text into at most max_lines lines no wider than max_w.  Lines break
// after a space/separator when one is available, otherwise at any codepoint
// boundary (long unbroken identifiers).  If text remains after the last line,
// that line is ellipsized.  Every line holds at least one codepoint, so the
// result is never empty for non-empty input.
inline std::vector<std::string> wrap_lines(std::string_view text,
                                           float            max_w,
                                           int              max_lines,
                                           const MeasureFn& measure)
{
    std::vector<std::string> lines;
    if (text.empty())
        return lines;
    if (!measure || max_lines <= 1)
    {
        lines.push_back(measure ? ellipsize(text, max_w, measure) : std::string(text));
        if (lines.back().empty())
            lines.back() = std::string(text.substr(0, utf8_next(text, 0)));
        return lines;
    }

    std::string_view rest = text;
    while (!rest.empty())
    {
        if (measure(rest) <= max_w)
        {
            lines.emplace_back(rest);
            break;
        }
        if (static_cast<int>(lines.size()) == max_lines - 1)
        {
            std::string last = ellipsize(rest, max_w, measure);
            if (last.empty())
                last = std::string(rest.substr(0, utf8_next(rest, 0)));
            lines.push_back(std::move(last));
            break;
        }

        size_t cut = longest_fitting_prefix(rest, max_w, measure);
        if (cut == 0)
            cut = utf8_next(rest, 0);   // always make progress
        // Prefer the last separator inside the fitting prefix (but not one so
        // early that the line would be mostly empty).
        for (size_t i = cut; i > cut / 3; --i)
        {
            if (is_break_after(rest[i - 1]))
            {
                cut = i;
                break;
            }
        }
        lines.emplace_back(trim_trailing_spaces(rest.substr(0, cut)));
        rest.remove_prefix(cut);
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
    }
    return lines;
}

// Fitted contents of a tooltip card: a (possibly wrapped) header and a
// label/value grid.
struct TooltipContent
{
    std::vector<std::string> name_lines;      // header lines (series name)
    std::vector<std::string> labels;          // row labels, ellipsized if needed
    float                    name_w    = 0;   // widest header line incl. name_prefix_w
    float                    label_w   = 0;   // label column width
    float                    value_w   = 0;   // value column width (values are never shortened)
    float                    content_w = 0;   // inner width of the card
};

// Lay out tooltip content within max_content_w pixels.  Values keep their full
// width; labels share what is left of the row after col_gap and the value
// column; the name wraps onto at most max_name_lines lines after a fixed
// name_prefix_w (colour swatch).  content_w is at least min_content_w when
// that fits, and only exceeds max_content_w when the value column alone is
// wider (a window too small for the numbers).
inline TooltipContent fit_tooltip_content(std::string_view                  name,
                                          float                             name_prefix_w,
                                          std::span<const std::string_view> labels,
                                          std::span<const std::string_view> values,
                                          float                             col_gap,
                                          float                             min_content_w,
                                          float                             max_content_w,
                                          int                               max_name_lines,
                                          const MeasureFn&                  measure)
{
    TooltipContent out;
    max_content_w = std::max(0.0f, max_content_w);

    for (std::string_view v : values)
        out.value_w = std::max(out.value_w, measure(v));

    const float label_budget = std::max(0.0f, max_content_w - col_gap - out.value_w);
    for (std::string_view l : labels)
    {
        out.labels.push_back(ellipsize(l, label_budget, measure));
        out.label_w = std::max(out.label_w, measure(out.labels.back()));
    }

    const float name_budget = std::max(0.0f, max_content_w - name_prefix_w);
    out.name_lines          = wrap_lines(name, name_budget, max_name_lines, measure);
    for (const auto& line : out.name_lines)
        out.name_w = std::max(out.name_w, measure(line));
    out.name_w += name_prefix_w;

    const float data_w = out.label_w + col_gap + out.value_w;
    out.content_w      = std::min(std::max({out.name_w, data_w, min_content_w}), max_content_w);
    out.content_w      = std::max(out.content_w, data_w);   // never hide the value column
    return out;
}

// Keep the span [pos, pos + size] inside [lo, hi]; a span larger than the
// range is pinned to lo.
inline float clamp_span(float pos, float size, float lo, float hi)
{
    if (pos + size > hi)
        pos = hi - size;
    if (pos < lo)
        pos = lo;
    return pos;
}

struct Box
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Place a w x h box next to an anchor point: preferably to the right
// (offset_x) and above (gap_above), flipping to the left / below
// (offset_below) when that side would leave the window, and finally clamping
// all four edges to the window minus margin.
inline Box place_near_anchor(float anchor_x,
                             float anchor_y,
                             float w,
                             float h,
                             float offset_x,
                             float gap_above,
                             float offset_below,
                             float window_w,
                             float window_h,
                             float margin = WINDOW_MARGIN)
{
    Box b{anchor_x + offset_x, anchor_y - h - gap_above, w, h};
    if (b.x + w > window_w - margin)
        b.x = anchor_x - w - offset_x;
    if (b.y < margin)
        b.y = anchor_y + offset_below;
    b.x = clamp_span(b.x, w, margin, window_w - margin);
    b.y = clamp_span(b.y, h, margin, window_h - margin);
    return b;
}

}   // namespace spectra::ui::overlay_fit

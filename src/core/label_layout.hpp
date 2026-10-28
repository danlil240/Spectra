#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace spectra
{

// Horizontal placement of a centred plot label (X axis label, axes title) that shares its
// row with a tick offset / multiplier annotation ("+1.72e9", "x10^-6") pinned to one end of
// the axes.  Used by both the GPU text path and the SVG exporter so the label and the
// annotation never overlap.
struct CenteredLabelSlot
{
    float max_width = 0.0f;   // width budget the label must be fitted (ellipsized) to
    float left      = 0.0f;   // leftmost x the label may start at
    float right     = 0.0f;   // rightmost x the label may end at
};

// Gap kept between a label and the offset annotation on its row (pixels).
inline constexpr float OFFSET_LABEL_GAP = 8.0f;

// Space to reserve at one end of a row for an offset annotation of `annotation_width`
// pixels: the annotation plus `gap`, or 0 when there is no annotation.
float offset_annotation_reserve(float annotation_width, float gap = OFFSET_LABEL_GAP);

// Slot for a label centred over [span_x, span_x + span_w] with `reserve_left` /
// `reserve_right` pixels at either end kept free.  The label may use the whole remaining
// width (it is shifted off-centre rather than shrunk when the reservation is one-sided).
CenteredLabelSlot centered_label_slot(float span_x,
                                      float span_w,
                                      float reserve_left,
                                      float reserve_right);

// Centre x for a label of `label_width` pixels: the span centre when the label fits there,
// otherwise shifted just enough to stay inside the slot.  A label wider than the slot is
// centred in the slot.
float centered_label_x(float                    span_x,
                       float                    span_w,
                       const CenteredLabelSlot& slot,
                       float                    label_width);

// ─── Plot label fitting (title, X/Y axis labels, 3D title) ──────────────────
//
// One rule shared by the GPU/raster text path (render_geometry.cpp) and the SVG exporter
// (svg_export.cpp) so both fit long labels the same way: same width budget, same font
// step-down, same ellipsis.  Each path supplies its own text metrics.

// Font tiers a plot label may use.  Same order and values as the renderer's FontSize
// (Tick < Label < Title), which converts to and from it with a static_cast.
enum class LabelFont : uint8_t
{
    Tick  = 0,
    Label = 1,
    Title = 2,
};

// Ellipsis appended to shortened plot labels ("…", U+2026).
inline constexpr std::string_view LABEL_ELLIPSIS = "\xE2\x80\xA6";

struct FittedLabel
{
    std::string text;                           // text to draw (possibly ellipsized)
    LabelFont   font      = LabelFont::Title;   // font tier to draw it at
    float       scale     = 1.0f;               // size multiplier on `font` (<= 1: shrunk to fit)
    bool        truncated = false;              // true when characters were dropped
};

// Measures the drawn width (pixels) of `text` at font tier `font`.
using LabelWidthFn = std::function<float(const std::string& text, LabelFont font)>;

// Fit text into max_width pixels.  `measure` must scale linearly with the font size.
//   1. Try each font tier from `preferred` down to `min_font` (inclusive); the first tier
//      at which the whole text fits is used at scale 1.
//   2. Shrinking is smooth: when a tier is too wide but the next smaller tier fits, the
//      larger tier is used with `scale` < 1 so the text exactly fills max_width (its size
//      lies between the two tiers) instead of jumping down to the smaller tier.
//   3. Otherwise, at `min_font`, keep the longest UTF-8 prefix (on codepoint boundaries,
//      trailing spaces trimmed) that fits together with `ellipsis`.
//   4. If not even the ellipsis fits (e.g. a zero budget), the text is empty.
FittedLabel fit_label_to_width(const std::string&  text,
                               float               max_width,
                               LabelFont           preferred,
                               LabelFont           min_font,
                               const LabelWidthFn& measure,
                               std::string_view    ellipsis = "...");

// Axes title over a span of `span_w` pixels, with `reserve_left` kept free for the Y
// offset annotation: Title size, shrinking smoothly to Label size, then ellipsized.
FittedLabel fit_axes_title(const std::string&  title,
                           float               span_w,
                           float               reserve_left,
                           const LabelWidthFn& measure,
                           std::string_view    ellipsis = LABEL_ELLIPSIS);

// X axis label under a span of `span_w` pixels, with `reserve_right` kept free for the
// X offset annotation: Label size, ellipsized.
FittedLabel fit_axes_xlabel(const std::string&  label,
                            float               span_w,
                            float               reserve_right,
                            const LabelWidthFn& measure,
                            std::string_view    ellipsis = LABEL_ELLIPSIS);

// Y axis label, drawn rotated along the axes height `span_h`: Label size, ellipsized.
FittedLabel fit_axes_ylabel(const std::string&  label,
                            float               span_h,
                            const LabelWidthFn& measure,
                            std::string_view    ellipsis = LABEL_ELLIPSIS);

// 3D axes title over the viewport width `span_w`: Title size, shrinking smoothly to
// Label size, then ellipsized.
FittedLabel fit_axes3d_title(const std::string&  title,
                             float               span_w,
                             const LabelWidthFn& measure,
                             std::string_view    ellipsis = LABEL_ELLIPSIS);

}   // namespace spectra

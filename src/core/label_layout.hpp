#pragma once

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

}   // namespace spectra

#include "label_layout.hpp"

#include <algorithm>

namespace spectra
{

float offset_annotation_reserve(float annotation_width, float gap)
{
    return annotation_width > 0.0f ? annotation_width + std::max(gap, 0.0f) : 0.0f;
}

CenteredLabelSlot centered_label_slot(float span_x,
                                      float span_w,
                                      float reserve_left,
                                      float reserve_right)
{
    CenteredLabelSlot slot;
    slot.left      = span_x + std::max(reserve_left, 0.0f);
    slot.right     = std::max(slot.left, span_x + span_w - std::max(reserve_right, 0.0f));
    slot.max_width = slot.right - slot.left;
    return slot;
}

float centered_label_x(float span_x, float span_w, const CenteredLabelSlot& slot, float label_width)
{
    const float half = std::max(label_width, 0.0f) * 0.5f;
    const float lo   = slot.left + half;
    const float hi   = slot.right - half;
    if (lo > hi)
        return (slot.left + slot.right) * 0.5f;
    return std::clamp(span_x + span_w * 0.5f, lo, hi);
}

}   // namespace spectra

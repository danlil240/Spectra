#include "label_layout.hpp"

#include <algorithm>
#include <vector>

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

namespace
{

// Byte length of the UTF-8 sequence starting at text[i]: the full sequence when it is
// well formed, otherwise 1 (same stepping as TextRenderer::decode_utf8).
size_t utf8_step(const std::string& text, size_t i)
{
    const auto b0  = static_cast<uint8_t>(text[i]);
    size_t     len = 1;
    if ((b0 & 0xE0) == 0xC0)
        len = 2;
    else if ((b0 & 0xF0) == 0xE0)
        len = 3;
    else if ((b0 & 0xF8) == 0xF0)
        len = 4;
    else
        return 1;
    if (i + len > text.size())
        return 1;
    for (size_t k = 1; k < len; ++k)
        if ((static_cast<uint8_t>(text[i + k]) & 0xC0) != 0x80)
            return 1;
    return len;
}

}   // namespace

FittedLabel fit_label_to_width(const std::string&  text,
                               float               max_width,
                               LabelFont           preferred,
                               LabelFont           min_font,
                               const LabelWidthFn& measure,
                               std::string_view    ellipsis)
{
    FittedLabel result;
    result.text = text;
    result.font = preferred;
    if (text.empty() || !measure)
        return result;

    auto lo = static_cast<int>(min_font);
    auto hi = static_cast<int>(preferred);
    if (lo > hi)
        lo = hi;

    for (int s = hi; s >= lo; --s)
    {
        const auto  f = static_cast<LabelFont>(s);
        const float w = measure(text, f);
        if (w <= max_width)
        {
            result.font = f;
            return result;
        }
        // Too wide at this tier but fits at the next smaller one: shrink this tier
        // continuously (to exactly max_width) rather than snapping down a whole tier.
        if (s > lo && w > 0.0f && measure(text, static_cast<LabelFont>(s - 1)) < max_width)
        {
            result.font  = f;
            result.scale = max_width / w;
            return result;
        }
    }

    // Ellipsize at the minimum size.
    const auto f     = static_cast<LabelFont>(lo);
    result.font      = f;
    result.truncated = true;

    // Byte offsets of codepoint boundaries: ends[k] = byte length of the first k codepoints.
    std::vector<size_t> ends;
    ends.push_back(0);
    for (size_t i = 0; i < text.size();)
    {
        i += utf8_step(text, i);
        ends.push_back(i);
    }

    auto candidate = [&](size_t k)
    {
        size_t len = ends[k];
        while (len > 0 && text[len - 1] == ' ')
            --len;
        std::string s = text.substr(0, len);
        s.append(ellipsis);
        return s;
    };

    // Width grows with the prefix length: binary search for the longest fitting prefix.
    // ends.size() - 1 codepoints would be the whole text, which is known not to fit.
    size_t lo_k = 0;
    size_t hi_k = ends.size() - 2;
    if (measure(candidate(0), f) > max_width)
    {
        result.text.clear();
        return result;
    }
    while (lo_k < hi_k)
    {
        const size_t mid = (lo_k + hi_k + 1) / 2;
        if (measure(candidate(mid), f) <= max_width)
            lo_k = mid;
        else
            hi_k = mid - 1;
    }
    result.text = candidate(lo_k);
    return result;
}

FittedLabel fit_axes_title(const std::string&  title,
                           float               span_w,
                           float               reserve_left,
                           const LabelWidthFn& measure,
                           std::string_view    ellipsis)
{
    const CenteredLabelSlot slot = centered_label_slot(0.0f, span_w, reserve_left, 0.0f);
    return fit_label_to_width(title,
                              slot.max_width,
                              LabelFont::Title,
                              LabelFont::Label,
                              measure,
                              ellipsis);
}

FittedLabel fit_axes_xlabel(const std::string&  label,
                            float               span_w,
                            float               reserve_right,
                            const LabelWidthFn& measure,
                            std::string_view    ellipsis)
{
    const CenteredLabelSlot slot = centered_label_slot(0.0f, span_w, 0.0f, reserve_right);
    return fit_label_to_width(label,
                              slot.max_width,
                              LabelFont::Label,
                              LabelFont::Label,
                              measure,
                              ellipsis);
}

FittedLabel fit_axes_ylabel(const std::string&  label,
                            float               span_h,
                            const LabelWidthFn& measure,
                            std::string_view    ellipsis)
{
    return fit_label_to_width(label,
                              std::max(span_h, 0.0f),
                              LabelFont::Label,
                              LabelFont::Label,
                              measure,
                              ellipsis);
}

FittedLabel fit_axes3d_title(const std::string&  title,
                             float               span_w,
                             const LabelWidthFn& measure,
                             std::string_view    ellipsis)
{
    return fit_label_to_width(title,
                              std::max(span_w, 0.0f),
                              LabelFont::Title,
                              LabelFont::Label,
                              measure,
                              ellipsis);
}

}   // namespace spectra

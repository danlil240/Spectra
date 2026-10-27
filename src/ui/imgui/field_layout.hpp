#pragma once

// Pure (ImGui-free) layout rules for the inspector's "label | value" rows
// (widgets::slider_field, combo_field, info_row, ...), so narrow panels get
// compact stacked rows instead of values drawn over their labels.  Unit tested
// in tests/unit/test_inspector.cpp.

#include <algorithm>

#include "ui/theme/design_tokens.hpp"

namespace spectra::ui::widgets
{

struct RowLayout
{
    bool  stacked = false;   // value goes on its own line below the label
    float value_x = 0.0f;    // value offset from the row start (side-by-side only)
};

/// Input field row: the value column starts INSPECTOR_LABEL_WIDTH (or the
/// label's width, if longer) after the row start, and needs at least
/// INSPECTOR_MIN_VALUE_WIDTH; otherwise the row stacks and the input takes
/// the full width.
inline RowLayout field_row_layout(float avail_w, float label_w)
{
    const float col = std::max(tokens::INSPECTOR_LABEL_WIDTH, label_w);
    if (avail_w - col < tokens::INSPECTOR_MIN_VALUE_WIDTH)
        return {true, 0.0f};
    return {false, col};
}

/// Read-only text row (info_row, stat_row): the value starts at `fraction` of
/// the row width, or after the label if that is longer.  A value that would
/// then run past the row end goes on its own line instead.
inline RowLayout text_row_layout(float avail_w, float label_w, float value_w, float fraction)
{
    const float x = std::max(avail_w * fraction, label_w);
    if (x + value_w > avail_w)
        return {true, 0.0f};
    return {false, x};
}

/// Toggle row: the label left, a `toggle_w` switch right-aligned.  Stacks
/// when the label would run under the switch.
inline RowLayout toggle_row_layout(float avail_w, float label_w, float toggle_w)
{
    const float x = avail_w - toggle_w;
    if (label_w > x)
        return {true, std::max(0.0f, x)};
    return {false, x};
}

}   // namespace spectra::ui::widgets

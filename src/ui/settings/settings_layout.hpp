#pragma once

// Pure sizing math for the Settings modal (no ImGui dependency, unit-tested).
//
// The modal auto-sizes to its content on large screens but must never grow
// past the viewport work area: its tab body scrolls inside a child region and
// the footer actions stay pinned below it (audit Q04).

#include <algorithm>
#include <cmath>

namespace spectra::ui::settings
{

struct SettingsVec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct SettingsWindowLimits
{
    SettingsVec2 min_size;   // lower bound for the auto-sized window
    SettingsVec2 max_size;   // upper bound: work area minus margin, and the design cap
    float        margin = 0.0f;
};

// Design values in unscaled UI units; multiplied by the UI/DPI scale.
inline constexpr float k_settings_min_width  = 420.0f;
inline constexpr float k_settings_max_height = 900.0f;
inline constexpr float k_settings_margin     = 24.0f;

inline float sanitize_settings_scale(float scale)
{
    return (std::isfinite(scale) && scale > 0.0f) ? scale : 1.0f;
}

// Size limits for the Settings window inside a work area of `work` size.
// The margin shrinks on very small work areas (never more than 5% per side)
// so the window keeps as much room as possible.
inline SettingsWindowLimits compute_settings_window_limits(SettingsVec2 work, float scale)
{
    scale        = sanitize_settings_scale(scale);
    const auto w = std::max(work.x, 0.0f);
    const auto h = std::max(work.y, 0.0f);

    SettingsWindowLimits lim;
    lim.margin = std::min(k_settings_margin * scale, std::min(w, h) * 0.05f);

    lim.max_size.x = std::max(w - 2.0f * lim.margin, 1.0f);
    lim.max_size.y = std::max(std::min(k_settings_max_height * scale, h - 2.0f * lim.margin), 1.0f);

    lim.min_size.x = std::min(k_settings_min_width * scale, lim.max_size.x);
    lim.min_size.y = 0.0f;
    return lim;
}

// Final window size for content of natural size `content`.
inline SettingsVec2 compute_settings_window_size(SettingsVec2 work,
                                                 SettingsVec2 content,
                                                 float        scale)
{
    const SettingsWindowLimits lim = compute_settings_window_limits(work, scale);
    return {std::clamp(content.x, lim.min_size.x, lim.max_size.x),
            std::clamp(content.y, lim.min_size.y, lim.max_size.y)};
}

// Maximum height of the scrolling tab body: what remains of the window's
// maximum height after the title bar, tab bar, footer and padding (`chrome_h`).
// Keeps at least `min_body_h` (bounded by the window height itself) so a tiny
// window still shows some content; the window then scrolls as a last resort.
inline float compute_settings_body_max_height(float window_max_h, float chrome_h, float min_body_h)
{
    window_max_h        = std::max(window_max_h, 1.0f);
    const float floor_h = std::clamp(min_body_h, 1.0f, window_max_h);
    return std::max(window_max_h - std::max(chrome_h, 0.0f), floor_h);
}

// Keeps a window of `size` fully inside the work rect (with `margin`) when it
// fits; otherwise pins its top-left corner to the work rect's top-left.
inline SettingsVec2 clamp_settings_window_pos(SettingsVec2 pos,
                                              SettingsVec2 size,
                                              SettingsVec2 work_pos,
                                              SettingsVec2 work_size,
                                              float        margin)
{
    auto clamp_axis = [margin](float p, float s, float wp, float ws)
    {
        const float lo = wp + margin;
        const float hi = wp + ws - margin - s;
        if (hi < lo)
            return std::max(wp, std::min(lo, wp + ws - s));
        return std::clamp(p, lo, hi);
    };
    return {clamp_axis(pos.x, size.x, work_pos.x, work_size.x),
            clamp_axis(pos.y, size.y, work_pos.y, work_size.y)};
}

}   // namespace spectra::ui::settings

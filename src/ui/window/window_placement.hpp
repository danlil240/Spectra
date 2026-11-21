// window_placement.hpp — OS window title and cascade placement rules.
//
// Header-only and windowing-library free so unit tests can check the rules;
// WindowManager (in-process windows) and the spectra-window agent (one
// process per window) apply them.

#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace spectra
{

// OS title of a window showing a figure: "<figure title> — Spectra".
// FigureManager::get_title() already falls back to "Figure N"; an empty
// title (no figure) gives plain "Spectra".
inline std::string window_title_for_figure(std::string_view figure_title)
{
    if (figure_title.empty())
        return "Spectra";
    std::string t(figure_title);
    t += " — Spectra";   // em dash
    return t;
}

// OS title with the window's file: "• <figure> — <file> — Spectra".  The
// leading dot marks unsaved changes; @p document_name is empty for a window
// that was never saved (the title is then window_title_for_figure()'s).
inline std::string window_title_for_document(std::string_view figure_title,
                                             std::string_view document_name,
                                             bool             modified)
{
    // UTF-8 bytes of U+2022 BULLET: a "\u2022" literal follows the compiler's
    // execution charset (MSVC without /utf-8 emits Windows-1252 0x95).
    std::string t = modified ? "\xE2\x80\xA2 " : "";
    if (!figure_title.empty())
    {
        t += figure_title;
        t += " — ";
    }
    if (!document_name.empty())
    {
        t += document_name;
        t += " — ";
    }
    t += "Spectra";
    return t;
}

// Usable desktop area (monitor minus panels/docks), in screen coordinates.
struct ScreenWorkArea
{
    int x = 0;
    int y = 0;
    int w = 0;   // <= 0 = unknown
    int h = 0;
};

inline constexpr int WINDOW_CASCADE_STEP = 30;

// Largest share of the work area a new window may take: leaves a margin
// around it so the title bar and borders stay reachable.
inline constexpr double WINDOW_MAX_WORK_AREA_FRACTION = 0.9;

// Size of a new @p win_w x @p win_h window on @p work: unchanged when it fits
// in WINDOW_MAX_WORK_AREA_FRACTION of the work area, else scaled down to fit,
// keeping its aspect ratio (a 1500x900 default on a 1366x768 laptop).  An
// unknown work area leaves the size unchanged.
inline std::pair<int, int> fit_window_to_work_area(int            win_w,
                                                   int            win_h,
                                                   ScreenWorkArea work,
                                                   double fraction = WINDOW_MAX_WORK_AREA_FRACTION)
{
    if (win_w <= 0 || win_h <= 0 || work.w <= 0 || work.h <= 0 || !(fraction > 0.0))
        return {win_w, win_h};
    const double max_w = static_cast<double>(work.w) * fraction;
    const double max_h = static_cast<double>(work.h) * fraction;
    const double scale = std::min({1.0, max_w / win_w, max_h / win_h});
    if (scale >= 1.0)
        return {win_w, win_h};
    return {std::max(1, static_cast<int>(win_w * scale)),
            std::max(1, static_cast<int>(win_h * scale))};
}

// Client-area position that keeps a window (client @p win_w x @p win_h plus
// its decorations: @p frame_left/top/right/bottom px) inside @p work.  A window
// larger than the work area keeps its title bar on screen (top-left pinned).
// An unknown work area leaves the position unchanged.
inline std::pair<int, int> keep_window_in_work_area(int            x,
                                                    int            y,
                                                    int            win_w,
                                                    int            win_h,
                                                    int            frame_left,
                                                    int            frame_top,
                                                    int            frame_right,
                                                    int            frame_bottom,
                                                    ScreenWorkArea work)
{
    if (work.w <= 0 || work.h <= 0)
        return {x, y};
    const int outer_w = win_w + frame_left + frame_right;
    const int outer_h = win_h + frame_top + frame_bottom;
    const int min_x   = work.x + frame_left;
    const int min_y   = work.y + frame_top;
    const int max_x   = std::max(min_x, work.x + work.w - outer_w + frame_left);
    const int max_y   = std::max(min_y, work.y + work.h - outer_h + frame_top);
    return {std::clamp(x, min_x, max_x), std::clamp(y, min_y, max_y)};
}

// Top-left position of the @p index-th cascaded window (index 0 = @p base).
// Each window is offset by @p step px right and down from the previous one.
// When the next offset would push a @p win_w x @p win_h window past the work
// area, the cascade wraps back to the base, so windows always stay on screen.
// With an unknown work area the offset is applied without wrapping.
inline std::pair<int, int> cascade_window_position(int            base_x,
                                                   int            base_y,
                                                   int            index,
                                                   int            win_w,
                                                   int            win_h,
                                                   ScreenWorkArea work,
                                                   int            step = WINDOW_CASCADE_STEP)
{
    index = std::max(index, 0);
    step  = std::max(step, 1);
    if (work.w <= 0 || work.h <= 0)
        return {base_x + index * step, base_y + index * step};

    // Keep the base itself inside the work area (a window larger than the
    // work area is pinned to its top-left corner).
    const int max_x = std::max(work.x, work.x + work.w - win_w);
    const int max_y = std::max(work.y, work.y + work.h - win_h);
    base_x          = std::clamp(base_x, work.x, max_x);
    base_y          = std::clamp(base_y, work.y, max_y);

    const int room      = std::min(max_x - base_x, max_y - base_y);
    const int positions = room / step + 1;   // >= 1
    const int k         = index % positions;
    return {base_x + k * step, base_y + k * step};
}

}   // namespace spectra

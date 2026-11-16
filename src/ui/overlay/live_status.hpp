#pragma once

// Live (presented-buffer) state of a 2D axes for the per-axes live pill and
// the status bar: following new data, or paused and how far behind the
// newest sample the view's right edge is.  Header-only and toolkit-free.

#include <spectra/axes.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace spectra::ui
{

struct LiveAxesStatus
{
    bool   live           = false;   // the axes has a presented buffer
    bool   following      = false;   // the view tracks new data
    double behind_seconds = 0.0;     // paused: newest sample minus the view's right edge (>= 0)
};

inline LiveAxesStatus live_status(const Axes& axes)
{
    LiveAxesStatus st;
    st.live = axes.has_presented_buffer();
    if (!st.live)
        return st;
    st.following = axes.is_presented_buffer_following();
    if (st.following)
        return st;
    if (auto head = axes.presented_buffer_head())
    {
        const double behind = *head - axes.x_limits().max;
        if (std::isfinite(behind))
            st.behind_seconds = std::max(0.0, behind);
    }
    return st;
}

// "0.4 s", "12 s", "3 min", "1.5 h" — how far a paused view lags.
inline std::string format_behind(double seconds)
{
    seconds = std::max(0.0, seconds);
    if (seconds < 10.0)
        return std::format("{:.1f} s", seconds);
    if (seconds < 120.0)
        return std::format("{:.0f} s", seconds);
    if (seconds < 7200.0)
        return std::format("{:.0f} min", seconds / 60.0);
    return std::format("{:.1f} h", seconds / 3600.0);
}

// Pill text: "Following" or "Paused" / "Paused · 12 s behind".  Lags under
// 0.05 s (one or two frames of new data) read as plain "Paused".
inline std::string live_pill_label(const LiveAxesStatus& st)
{
    if (!st.live)
        return {};
    if (st.following)
        return "Following";
    if (st.behind_seconds < 0.05)
        return "Paused";
    return "Paused \xC2\xB7 " + format_behind(st.behind_seconds) + " behind";
}

// Resume live follow on @p axes (no-op without a presented buffer).
inline void resume_live_follow(Axes& axes)
{
    axes.resume_follow();
}

// Pause live follow on @p axes, freezing the window currently shown.
inline void pause_live_follow(Axes& axes)
{
    if (!axes.is_presented_buffer_following())
        return;
    const AxisLimits x = axes.x_limits();
    axes.xlim(x.min, x.max);
}

}   // namespace spectra::ui

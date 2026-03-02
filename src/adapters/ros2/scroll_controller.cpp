// ScrollController — implementation.
//
// See scroll_controller.hpp for design notes.

#include "scroll_controller.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScrollController::ScrollController()
    : window_s_(DEFAULT_WINDOW_S), now_(0.0), paused_(false),
      view_min_(0.0), view_max_(0.0), last_pruned_count_(0)
{
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void ScrollController::set_window_s(double seconds)
{
    if (seconds < MIN_WINDOW_S)
        seconds = MIN_WINDOW_S;
    if (seconds > MAX_WINDOW_S)
        seconds = MAX_WINDOW_S;
    window_s_ = seconds;
}

// ---------------------------------------------------------------------------
// Time source
// ---------------------------------------------------------------------------

void ScrollController::set_now(double wall_time_s)
{
    now_ = wall_time_s;
}

// ---------------------------------------------------------------------------
// Pause / resume
// ---------------------------------------------------------------------------

void ScrollController::pause()
{
    paused_ = true;
}

void ScrollController::resume()
{
    paused_ = false;
}

void ScrollController::toggle_paused()
{
    paused_ = !paused_;
}

// ---------------------------------------------------------------------------
// Per-frame tick
// ---------------------------------------------------------------------------

void ScrollController::tick(spectra::LineSeries* series, spectra::Axes* axes)
{
    last_pruned_count_ = 0;

    // Compute desired view window even when paused (needed for view_min_/max_).
    const double win_end   = now_;
    const double win_start = now_ - window_s_;

    if (!paused_ && axes != nullptr)
    {
        axes->xlim(win_start, win_end);
        view_min_ = win_start;
        view_max_ = win_end;
    }

    // Prune series data outside 2 × window regardless of pause state.
    if (series != nullptr)
        last_pruned_count_ = prune(series);
}

// ---------------------------------------------------------------------------
// Memory indicator
// ---------------------------------------------------------------------------

size_t ScrollController::memory_bytes(const spectra::LineSeries* series)
{
    if (series == nullptr)
        return 0;
    // x_ and y_ are both vector<float> — each element is 4 bytes.
    const size_t n = series->point_count();
    return n * 2 * sizeof(float);
}

// ---------------------------------------------------------------------------
// Status text helpers
// ---------------------------------------------------------------------------

std::string ScrollController::window_label() const
{
    const double s = window_s_;
    if (s < 60.0)
    {
        // show as "Xs" with no decimal if whole number
        if (std::fmod(s, 1.0) == 0.0)
        {
            std::ostringstream oss;
            oss << static_cast<int>(s) << " s";
            return oss.str();
        }
        std::ostringstream oss;
        oss << s << " s";
        return oss.str();
    }
    else if (s < 3600.0)
    {
        const double minutes = s / 60.0;
        std::ostringstream oss;
        if (std::fmod(minutes, 1.0) == 0.0)
            oss << static_cast<int>(minutes) << " min";
        else
            oss << minutes << " min";
        return oss.str();
    }
    else
    {
        const double hours = s / 3600.0;
        std::ostringstream oss;
        if (std::fmod(hours, 1.0) == 0.0)
            oss << static_cast<int>(hours) << " h";
        else
            oss << hours << " h";
        return oss.str();
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

size_t ScrollController::prune(spectra::LineSeries* series) const
{
    if (series == nullptr)
        return 0;

    const double prune_before = now_ - PRUNE_FACTOR * window_s_;

    // x_data() is sorted ascending (timestamps always increase).
    // Find the first index where x >= prune_before.
    auto x = series->x_data();
    if (x.empty())
        return 0;

    // Binary search for the first element >= prune_before.
    size_t lo = 0, hi = x.size();
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (static_cast<double>(x[mid]) < prune_before)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return 0;   // nothing to prune

    // Build new x/y vectors from index `lo` onward.
    auto y = series->y_data();
    std::vector<float> new_x(x.begin() + static_cast<ptrdiff_t>(lo), x.end());
    std::vector<float> new_y(y.begin() + static_cast<ptrdiff_t>(lo), y.end());

    series->set_x(new_x);
    series->set_y(new_y);

    return lo;   // number of samples removed
}

}   // namespace spectra::adapters::ros2

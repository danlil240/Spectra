#pragma once

// ScrollController — auto-scrolling time window for ROS2 live plots.
//
// Manages the X-axis view window for a single Axes object:
//   - Keeps X range = [now - window_s, now] while in "following" mode
//   - Pauses following when the user pans or zooms (detected via set_paused())
//   - Resumes following via resume() or when Home is triggered
//   - Prunes series data older than 2 × window_s each tick to bound memory
//   - Provides memory_bytes() for the status-bar indicator
//
// Thread-safety:
//   All methods must be called from the render thread (same thread as poll()).
//   No internal locking — same contract as RosPlotManager::poll().
//
// Typical usage inside the render loop:
//
//   scroll_ctrl.set_now(wall_time_seconds());
//   scroll_ctrl.tick(series, axes);   // updates xlim + prunes data
//
// "Home" key:
//   scroll_ctrl.resume();             // re-enables following
//
// Pan / zoom detection:
//   scroll_ctrl.set_paused(true);     // call from input handler
//
// Memory indicator:
//   size_t bytes = scroll_ctrl.memory_bytes(series);

#include <cstddef>
#include <string>

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// ScrollController
// ---------------------------------------------------------------------------

class ScrollController
{
public:
    // Clamp constants for the time window.
    static constexpr double MIN_WINDOW_S =    1.0;
    static constexpr double MAX_WINDOW_S = 3600.0;
    static constexpr double DEFAULT_WINDOW_S = 30.0;

    // Prune factor: remove data older than PRUNE_FACTOR × window_s.
    static constexpr double PRUNE_FACTOR = 2.0;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    ScrollController();
    ~ScrollController() = default;

    // Non-copyable, but movable (plain-data members; move == memberwise copy).
    ScrollController(const ScrollController&)            = delete;
    ScrollController& operator=(const ScrollController&) = delete;
    ScrollController(ScrollController&&)                 = default;
    ScrollController& operator=(ScrollController&&)      = default;

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    // Set the sliding window width in seconds.
    // Clamped to [MIN_WINDOW_S, MAX_WINDOW_S].
    void set_window_s(double seconds);
    double window_s() const { return window_s_; }

    // -----------------------------------------------------------------------
    // Time source
    // -----------------------------------------------------------------------

    // Advance the controller's notion of "now" (wall-clock seconds).
    // Call once per frame before tick().
    void set_now(double wall_time_s);
    double now() const { return now_; }

    // -----------------------------------------------------------------------
    // Pause / resume following
    // -----------------------------------------------------------------------

    // Pause auto-scroll (user panned/zoomed).
    // When paused, tick() skips xlim updates but still prunes data.
    void pause();

    // Resume auto-scroll following.
    // Equivalent to pressing "Home".
    void resume();

    // Toggle pause/resume.
    void toggle_paused();

    bool is_paused() const { return paused_; }

    // -----------------------------------------------------------------------
    // Per-frame tick
    // -----------------------------------------------------------------------

    // Apply the time window to the given axes + series.
    //   - If following: calls axes.xlim(now - window, now)
    //   - Always prunes x_data / y_data older than 2 × window_s from series
    //
    // `series` — pointer to the LineSeries tracked by this controller.
    //            May be nullptr (no-op).
    // `axes`   — pointer to the Axes whose xlim is managed.
    //            May be nullptr (no-op for xlim; pruning still happens if series valid).
    void tick(spectra::LineSeries* series, spectra::Axes* axes);

    // -----------------------------------------------------------------------
    // Memory indicator
    // -----------------------------------------------------------------------

    // Estimated memory consumption of a LineSeries in bytes.
    // Returns 0 if series is nullptr.
    static size_t memory_bytes(const spectra::LineSeries* series);

    // -----------------------------------------------------------------------
    // Status text helpers
    // -----------------------------------------------------------------------

    // Returns "following" or "paused" for display.
    const char* status_text() const { return paused_ ? "paused" : "following"; }

    // How many seconds behind live the view is when paused.
    // Returns 0.0 when following.
    double seconds_behind() const
    {
        if (!paused_ || now_rel_ <= 0.0) return 0.0;
        return now_rel_ - view_max_;
    }

    // Human-readable status: "following", or "paused — 4.2 s behind".
    std::string status_text_detailed() const;

    // Human-readable window description: "30 s", "2 min", "1 h".
    std::string window_label() const;

    // -----------------------------------------------------------------------
    // Scroll bounds (last applied)
    // -----------------------------------------------------------------------

    double view_min() const { return view_min_; }
    double view_max() const { return view_max_; }

    // Samples pruned since last tick() call.
    size_t last_pruned_count() const { return last_pruned_count_; }

    // -----------------------------------------------------------------------
    // Time origin — converts absolute epoch seconds to relative seconds
    // -----------------------------------------------------------------------

    // Returns the time origin (epoch seconds).  All series x-values and
    // xlim calls use (absolute_time - time_origin) so that float precision
    // is sufficient for sub-second accuracy.
    double time_origin() const { return time_origin_; }
    bool   has_time_origin() const { return has_origin_; }

    // Explicitly set the time origin.  Normally auto-set on first set_now().
    void set_time_origin(double origin);

private:
    // Prune series data older than (now_rel - PRUNE_FACTOR * window_s_).
    // Returns number of samples removed.
    size_t prune(spectra::LineSeries* series) const;

    double window_s_           = DEFAULT_WINDOW_S;
    double now_                = 0.0;      // absolute epoch seconds
    double now_rel_            = 0.0;      // relative seconds (now_ - time_origin_)
    double time_origin_        = 0.0;      // epoch seconds of first set_now()
    bool   has_origin_         = false;
    bool   paused_             = false;
    double view_min_           = 0.0;
    double view_max_           = 0.0;
    size_t last_pruned_count_  = 0;
};

}   // namespace spectra::adapters::ros2

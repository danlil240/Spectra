#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <cstddef>
    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <string>
    #include <vector>

    #include "ui/overlay/axis_mapping.hpp"

struct ImFont;

namespace spectra::ui
{
class ThemeManager;
}   // namespace spectra::ui

namespace spectra
{

class Axes;
class Figure;
class TransitionEngine;

// Statistics computed for a selected region of data points.  Values are
// absolute data coordinates (series x_offset/y_offset included), in double so
// epoch timestamps and 1e7 + small data keep their precision.
struct RegionStatistics
{
    size_t point_count = 0;
    double x_min = 0.0, x_max = 0.0;
    double y_min = 0.0, y_max = 0.0;
    double y_mean = 0.0;
    double y_std  = 0.0;
};

// A point captured inside a region selection (absolute data coordinates).
struct SelectedPoint
{
    const Series* series = nullptr;
    size_t        index  = 0;
    double        data_x = 0.0;
    double        data_y = 0.0;
};

// Format a statistic for the region toolbar: 4 significant digits of the
// spread, plus enough leading digits that a value such as 1e7 + 0.5 or an
// epoch timestamp is not rounded to "1e+07".
std::string format_region_value(double value, double spread);

// Region selection: shift-drag to select a rectangular region on the plot.
// Shows a floating mini-toolbar with point count and basic statistics.
// The selection rectangle is defined in data coordinates so it survives
// zoom/pan until explicitly dismissed.
class RegionSelect
{
   public:
    RegionSelect() = default;

    // Set fonts for the mini-toolbar rendering
    void set_fonts(ImFont* body, ImFont* heading);

    // Set the transition engine for animated opacity (optional)
    void set_transition_engine(TransitionEngine* te) { transition_engine_ = te; }

    // ─── Selection lifecycle ────────────────────────────────────────────

    // Begin a new selection at the given screen position.  @p map is the
    // screen <-> data mapping of the axes (viewport, limits, axis scales).
    void begin(double screen_x, double screen_y, const AxisMapping& map);

    // Update the selection end point (while dragging).
    void update_drag(double screen_x, double screen_y, const AxisMapping& map);

    // Finish the selection (mouse release). Computes statistics.
    void finish(const Axes* axes);

    // Dismiss / clear the current selection.
    void dismiss();

    // ─── State queries ──────────────────────────────────────────────────

    bool is_dragging() const { return dragging_; }
    bool has_selection() const { return has_selection_; }

    // Data-coordinate bounds of the selection rectangle.
    double data_x_min() const { return data_x0_ < data_x1_ ? data_x0_ : data_x1_; }
    double data_x_max() const { return data_x0_ > data_x1_ ? data_x0_ : data_x1_; }
    double data_y_min() const { return data_y0_ < data_y1_ ? data_y0_ : data_y1_; }
    double data_y_max() const { return data_y0_ > data_y1_ ? data_y0_ : data_y1_; }

    const RegionStatistics&           statistics() const { return stats_; }
    const std::vector<SelectedPoint>& selected_points() const { return selected_points_; }

    // ─── Drawing ────────────────────────────────────────────────────────

    // Draw the selection rectangle and floating mini-toolbar.
    // Call inside ImGui frame, after build_ui.
    void draw(const AxisMapping& map, float window_width, float window_height);

    // ─── Configuration ──────────────────────────────────────────────────

    void set_fill_alpha(float a) { fill_alpha_ = a; }
    void set_border_width(float w) { border_width_ = w; }
    void set_theme_manager(ui::ThemeManager* tm) { theme_mgr_ = tm; }

   private:
    // Draw the floating mini-toolbar with statistics
    void draw_mini_toolbar(float rx0,
                           float ry0,
                           float rx1,
                           float ry1,
                           float window_width,
                           float window_height);

    // Collect all data points inside the selection rectangle
    void collect_points(const Axes* axes);

    // Compute statistics from selected_points_
    void compute_statistics();

    bool dragging_      = false;
    bool has_selection_ = false;

    // Selection rectangle in absolute data coordinates
    double data_x0_ = 0.0, data_y0_ = 0.0;
    double data_x1_ = 0.0, data_y1_ = 0.0;

    // Cached screen coordinates of drag start (for drawing during drag)
    double screen_start_x_ = 0.0;
    double screen_start_y_ = 0.0;
    double screen_end_x_   = 0.0;
    double screen_end_y_   = 0.0;

    RegionStatistics           stats_;
    std::vector<SelectedPoint> selected_points_;

    // Animation
    float             opacity_           = 0.0f;
    TransitionEngine* transition_engine_ = nullptr;

    // Fonts
    ImFont* font_body_    = nullptr;
    ImFont* font_heading_ = nullptr;

    ui::ThemeManager* theme_mgr_ = nullptr;

    // Visual config
    float fill_alpha_   = 0.15f;
    float border_width_ = 1.5f;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <string>
    #include <vector>

namespace spectra
{

class Axes;

// A persistent data marker pinned to a specific data point.
struct DataMarker
{
    float         data_x      = 0.0f;
    float         data_y      = 0.0f;
    const Series* series      = nullptr;
    size_t        point_index = 0;
    Color         color       = colors::white;
    std::string   series_label;            // series name shown in datatip
    const Axes*   axes        = nullptr;   // owning axes (for multi-subplot)
    float         dy_dx       = 0.0f;      // finite-difference derivative
    bool          dy_dx_valid = false;     // true when derivative was computed
};

// Manages a collection of persistent data markers.
// Markers survive zoom/pan and are drawn as pinned indicators on the canvas.
class DataMarkerManager
{
   public:
    DataMarkerManager() = default;

    void add(float         data_x,
             float         data_y,
             const Series* series,
             size_t        index,
             const Axes*   axes        = nullptr,
             float         dy_dx       = 0.0f,
             bool          dy_dx_valid = false);
    void remove(size_t marker_index);
    void clear();

    // Remove all markers that reference the given series (call before series is destroyed)
    void remove_for_series(const Series* series);

    // Toggle a data label: if a marker already exists at this (series, index),
    // remove it; otherwise add a new one. Returns true if a label was added.
    bool toggle_or_add(float         data_x,
                       float         data_y,
                       const Series* series,
                       size_t        index,
                       const Axes*   axes        = nullptr,
                       float         dy_dx       = 0.0f,
                       bool          dy_dx_valid = false);

    const std::vector<DataMarker>& markers() const { return markers_; }
    size_t                         count() const { return markers_.size(); }

    // Draw all markers. Converts data coords to screen coords using the viewport and limits.
    // When filter_axes is non-null, only markers belonging to that axes are drawn.
    void draw(const Rect& viewport,
              float       xlim_min,
              float       xlim_max,
              float       ylim_min,
              float       ylim_max,
              float       opacity     = 1.0f,
              const Axes* filter_axes = nullptr);

    // Hit-test: returns index of marker near screen position, or -1.
    // When filter_axes is non-null, only markers belonging to that axes are tested.
    int hit_test(float       screen_x,
                 float       screen_y,
                 const Rect& viewport,
                 float       xlim_min,
                 float       xlim_max,
                 float       ylim_min,
                 float       ylim_max,
                 float       radius_px   = 10.0f,
                 const Axes* filter_axes = nullptr) const;

   private:
    std::vector<DataMarker> markers_;

    // Find existing marker for the same series + point_index, or -1
    int find_duplicate(const Series* series, size_t point_index) const;

    // Convert data coordinates to screen coordinates
    static void data_to_screen(float       data_x,
                               float       data_y,
                               const Rect& viewport,
                               float       xlim_min,
                               float       xlim_max,
                               float       ylim_min,
                               float       ylim_max,
                               float&      screen_x,
                               float&      screen_y);
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

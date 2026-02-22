#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <spectra/color.hpp>
    #include <spectra/series.hpp>
    #include <vector>

namespace spectra
{

// A persistent data marker pinned to a specific data point.
struct DataMarker
{
    float         data_x      = 0.0f;
    float         data_y      = 0.0f;
    const Series* series      = nullptr;
    size_t        point_index = 0;
    Color         color       = colors::white;
};

// Manages a collection of persistent data markers.
// Markers survive zoom/pan and are drawn as pinned indicators on the canvas.
class DataMarkerManager
{
   public:
    DataMarkerManager() = default;

    void add(float data_x, float data_y, const Series* series, size_t index);
    void remove(size_t marker_index);
    void clear();

    const std::vector<DataMarker>& markers() const { return markers_; }
    size_t                         count() const { return markers_.size(); }

    // Draw all markers. Converts data coords to screen coords using the viewport and limits.
    void draw(const Rect& viewport,
              float       xlim_min,
              float       xlim_max,
              float       ylim_min,
              float       ylim_max,
              float       opacity = 1.0f);

    // Hit-test: returns index of marker near screen position, or -1
    int hit_test(float       screen_x,
                 float       screen_y,
                 const Rect& viewport,
                 float       xlim_min,
                 float       xlim_max,
                 float       ylim_min,
                 float       ylim_max,
                 float       radius_px = 10.0f) const;

   private:
    std::vector<DataMarker> markers_;

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

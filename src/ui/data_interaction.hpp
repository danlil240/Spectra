#pragma once

#ifdef PLOTIX_USE_IMGUI

#include "tooltip.hpp"
#include "crosshair.hpp"
#include "data_marker.hpp"
#include "input.hpp"

#include <plotix/figure.hpp>

struct ImFont;

namespace plotix {

// Orchestrates all data-interaction features:
//   - Nearest-point spatial query
//   - Rich hover tooltip
//   - Crosshair overlay
//   - Persistent data markers (click to pin, right-click to remove)
class DataInteraction {
public:
    DataInteraction() = default;

    // Set fonts for tooltip rendering
    void set_fonts(ImFont* body, ImFont* heading);

    // Main update: run nearest-point query and update internal state.
    // Call once per frame after input handling.
    void update(const CursorReadout& cursor, Figure& figure);

    // Draw all overlays (tooltip, crosshair, markers).
    // Call inside ImGui frame, after build_ui.
    void draw_overlays(float window_width, float window_height);

    // Nearest-point result from the last update
    const NearestPointResult& nearest_point() const { return nearest_; }

    // Crosshair control
    bool crosshair_active() const { return crosshair_.enabled(); }
    void toggle_crosshair() { crosshair_.toggle(); }
    void set_crosshair(bool e) { crosshair_.set_enabled(e); }

    // Tooltip control
    bool tooltip_active() const { return tooltip_.enabled(); }
    void set_tooltip(bool e) { tooltip_.set_enabled(e); }

    // Marker control
    void add_marker(float data_x, float data_y, const Series* series, size_t index);
    void remove_marker(size_t idx);
    void clear_markers();
    const std::vector<DataMarker>& markers() const { return markers_.markers(); }

    // Handle mouse click for marker placement/removal.
    // Returns true if the click was consumed by the data interaction layer.
    bool on_mouse_click(int button, double screen_x, double screen_y);

    // Set snap radius for nearest-point detection (in pixels)
    void set_snap_radius(float px);
    float snap_radius() const { return tooltip_.snap_radius(); }

private:
    // Perform nearest-point spatial query across all visible series in the active axes.
    NearestPointResult find_nearest(const CursorReadout& cursor, Figure& figure) const;

    NearestPointResult nearest_;
    Tooltip tooltip_;
    Crosshair crosshair_;
    DataMarkerManager markers_;

    // Cached state for drawing
    CursorReadout last_cursor_;
    Axes* active_axes_ = nullptr;
    Rect active_viewport_;
    float xlim_min_ = 0.0f, xlim_max_ = 1.0f;
    float ylim_min_ = 0.0f, ylim_max_ = 1.0f;
};

} // namespace plotix

#endif // PLOTIX_USE_IMGUI

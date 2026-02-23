#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <functional>
    #include <spectra/figure.hpp>
    #include <spectra/fwd.hpp>

    #include "crosshair.hpp"
    #include "data_marker.hpp"
    #include "ui/input/input.hpp"
    #include "legend_interaction.hpp"
    #include "ui/input/region_select.hpp"
    #include "tooltip.hpp"

struct ImFont;

namespace spectra
{

// Orchestrates all data-interaction features:
//   - Nearest-point spatial query
//   - Rich hover tooltip
//   - Crosshair overlay
//   - Persistent data markers (click to pin, right-click to remove)
class DataInteraction
{
   public:
    DataInteraction() = default;

    // Set fonts for tooltip/legend/region rendering
    void set_fonts(ImFont* body, ImFont* heading, ImFont* icon = nullptr);

    // Main update: run nearest-point query and update internal state.
    // Call once per frame after input handling.
    void update(const CursorReadout& cursor, Figure& figure);

    // Draw all overlays (tooltip, crosshair, markers).
    // Call inside ImGui frame, after build_ui.
    void draw_overlays(float window_width, float window_height);

    // Draw legend overlay for a specific figure (for split-mode panes).
    // Respects figure.legend().visible.
    void draw_legend_for_figure(Figure& figure);

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

    // Region selection (shift-drag)
    void                    begin_region_select(double screen_x, double screen_y);
    void                    update_region_drag(double screen_x, double screen_y);
    void                    finish_region_select();
    void                    dismiss_region_select();
    bool                    is_region_dragging() const { return region_.is_dragging(); }
    bool                    has_region_selection() const { return region_.has_selection(); }
    const RegionStatistics& region_statistics() const { return region_.statistics(); }

    // Legend interaction
    LegendInteraction&       legend() { return legend_; }
    const LegendInteraction& legend() const { return legend_; }

    // Set the transition engine for animated markers/regions
    void set_transition_engine(class TransitionEngine* te);

    // Set the axis link manager for shared cursor across subplots
    void             set_axis_link_manager(class AxisLinkManager* alm) { axis_link_mgr_ = alm; }
    AxisLinkManager* axis_link_manager() const { return axis_link_mgr_; }

    // Set snap radius for nearest-point detection (in pixels)
    void  set_snap_radius(float px);
    float snap_radius() const { return tooltip_.snap_radius(); }

    // Invalidate cached figure pointer (call when a figure is destroyed)
    void clear_figure_cache(Figure* fig = nullptr)
    {
        if (!fig || last_figure_ == fig)
        {
            last_figure_ = nullptr;
            active_axes_ = nullptr;
        }
    }

    // Series selection callback: fired when user clicks near a series.
    // Args: (Figure*, Axes*, int axes_index, Series*, int series_index)
    using SeriesSelectedCallback = std::function<void(Figure*, Axes*, int, Series*, int)>;
    void set_on_series_selected(SeriesSelectedCallback cb) { on_series_selected_ = std::move(cb); }

   private:
    // Perform nearest-point spatial query across all visible series in the active axes.
    NearestPointResult find_nearest(const CursorReadout& cursor, Figure& figure) const;

    NearestPointResult nearest_;
    Tooltip            tooltip_;
    Crosshair          crosshair_;
    DataMarkerManager  markers_;
    RegionSelect       region_;
    LegendInteraction  legend_;

    // Axis link manager for shared cursor
    AxisLinkManager* axis_link_mgr_ = nullptr;

    // Series selection callback
    SeriesSelectedCallback on_series_selected_;

    // Cached state for drawing
    CursorReadout last_cursor_;
    Figure*       last_figure_ = nullptr;
    Axes*         active_axes_ = nullptr;
    Rect          active_viewport_;
    float         xlim_min_ = 0.0f, xlim_max_ = 1.0f;
    float         ylim_min_ = 0.0f, ylim_max_ = 1.0f;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

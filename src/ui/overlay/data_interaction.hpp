#pragma once

#ifdef SPECTRA_USE_IMGUI

    #include <functional>
    #include <string>
    #include <vector>
    #include <spectra/figure.hpp>
    #include <spectra/fwd.hpp>

    #include "annotation.hpp"
    #include "axis_mapping.hpp"
    #include "compare_readout.hpp"
    #include "crosshair.hpp"
    #include "data_marker.hpp"
    #include "ui/input/input.hpp"
    #include "legend_interaction.hpp"
    #include "ui/input/region_select.hpp"
    #include "tooltip.hpp"
    #include "ui/workspace/overlay_snapshot.hpp"

struct ImFont;
struct ImDrawList;

namespace spectra::ui
{
struct ThemeColors;
}   // namespace spectra::ui

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

    // Inject ThemeManager into all overlay sub-objects.
    void set_theme_manager(ui::ThemeManager* tm);

    // Main update: run nearest-point query and update internal state.
    // Call once per frame after input handling.
    void update(const CursorReadout& cursor, Figure& figure);

    // Draw all overlays (tooltip, crosshair, markers).
    // Pass current_figure when available so overlay rendering does not depend
    // on stale cached figure pointers.
    // When dl is non-null, all overlay primitives are drawn into that draw list
    // instead of GetForegroundDrawList(), enabling proper z-ordering behind menus.
    void draw_overlays(float       window_width,
                       float       window_height,
                       Figure*     current_figure = nullptr,
                       ImDrawList* dl             = nullptr);

    // Draw legend overlay for a specific figure (for split-mode panes).
    // Respects figure.legend().visible.
    void draw_legend_for_figure(Figure& figure);

    // Nearest-point result from the last update
    const NearestPointResult& nearest_point() const { return nearest_; }

    // Status-bar X/Y/Z of the 3D pick of the last update(): the scatter / line
    // point or surface / mesh vertex the 3D tooltip shows.  False when the
    // cursor is over no 3D data.
    bool readout_3d(double& x, double& y, double& z) const
    {
        if (!nearest_.found || !nearest_.is_3d || nearest_.distance_px > tooltip_.snap_radius_3d())
            return false;
        x = nearest_.data_x;
        y = nearest_.data_y;
        z = nearest_.data_z;
        return true;
    }
    size_t last_query_points_examined() const { return last_query_points_examined_; }

    // Crosshair control
    bool  crosshair_active() const { return crosshair_.enabled(); }
    float crosshair_opacity() const { return crosshair_.opacity(); }
    // Cursor readout of the last update() (what the overlays were drawn for).
    const CursorReadout& last_cursor() const { return last_cursor_; }
    // Screen-space viewport of the axes under the cursor at the last update().
    const Rect& active_viewport() const { return active_viewport_; }
    void        toggle_crosshair() { crosshair_.toggle(); }
    void        set_crosshair(bool e) { crosshair_.set_enabled(e); }
    // 2D axes under the cursor at the last update() (nullptr when none).
    const Axes* active_axes() const { return active_axes_; }

    // 3D spike lines: while a point of a 3D series is hovered, dashed guides
    // run from it to the three back panes of the box (Plotly-style).  On by
    // default.
    bool spikes3d_active() const { return spikes3d_enabled_; }
    void set_spikes3d(bool e) { spikes3d_enabled_ = e; }
    void toggle_spikes3d() { spikes3d_enabled_ = !spikes3d_enabled_; }
    // True when the last draw_overlays() drew 3D spike lines.
    bool spikes3d_drawn() const { return spikes3d_drawn_; }

    // Compare-at-X readout (shown with the crosshair): every visible series'
    // value at the cursor X in one tooltip, replacing the single-point tooltip.
    bool               compare_enabled() const { return compare_.enabled(); }
    void               set_compare_enabled(bool e) { compare_.set_enabled(e); }
    probe::ProbeMode   compare_mode() const { return compare_.mode(); }
    void               set_compare_mode(probe::ProbeMode m) { compare_.set_mode(m); }
    probe::CompareSort compare_sort() const { return compare_.sort(); }
    void               set_compare_sort(probe::CompareSort s) { compare_.set_sort(s); }
    // Hide the compare readout while a tool uses the crosshair for its own
    // purpose (Measure turns the crosshair on and draws its own labels).
    void set_compare_suppressed(bool s) { compare_suppressed_ = s; }
    // True when the last draw_overlays() showed the compare readout.
    bool compare_readout_drawn() const { return compare_drawn_; }

    // Tooltip control
    bool tooltip_active() const { return tooltip_.enabled(); }
    void set_tooltip(bool e) { tooltip_.set_enabled(e); }

    // Marker / data label control
    // data_x/data_y are absolute data coordinates (series offsets included).
    void add_marker(double        data_x,
                    double        data_y,
                    const Series* series,
                    size_t        index,
                    const Axes*   axes = nullptr);
    void remove_marker(size_t idx);
    void clear_markers();
    void insert_marker(size_t idx, DataMarker m) { markers_.insert(idx, std::move(m)); }
    const std::vector<DataMarker>& markers() const { return markers_.markers(); }

    // Toggle a data label (datatip) on a point: adds if absent, removes if present.
    // Returns true if a label was added.
    bool toggle_data_label(float         data_x,
                           float         data_y,
                           const Series* series,
                           size_t        index,
                           const Axes*   axes        = nullptr,
                           float         dy_dx       = 0.0f,
                           bool          dy_dx_valid = false)
    {
        return markers_.toggle_or_add(data_x, data_y, series, index, axes, dy_dx, dy_dx_valid);
    }

    // Handle mouse click for marker placement/removal.
    // Returns true if the click was consumed by the data interaction layer.
    bool on_mouse_click(int button, double screen_x, double screen_y);

    // Pan-mode click behavior: datatip marker operations only (no series selection callbacks).
    // Left click on a tip selects it (Shift: add to / remove from the
    // selection; clicking the only selected tip removes it), on a sample pins
    // a new, selected tip, on empty canvas clears the selection.
    bool on_mouse_click_datatip_only(int button, double screen_x, double screen_y, int mods = 0);

    // Data tip drag along its series (Pan tool).  begin returns false when
    // no 2D tip is under the pointer.  end with moved == false is a click.
    bool begin_marker_drag(double screen_x, double screen_y);
    void update_marker_drag(double screen_x, double screen_y);
    void end_marker_drag(double screen_x, double screen_y, bool moved, int mods);
    void cancel_marker_drag();   // put the tip back where the drag started
    bool is_marker_dragging() const { return drag_marker_ >= 0; }

    // Step every selected 2D tip of the current figure @p delta samples along
    // its series.  Returns true when any tip is selected (the key is consumed
    // even at the end of the series).
    bool step_selected_markers(int delta);
    // Deselect all tips; true when something was selected.
    bool   clear_marker_selection() { return markers_.clear_selection(); }
    size_t selected_marker_count() const { return markers_.selected_count(); }

    // Boxed multi-line readout (measure tool, tip deltas) centred above @p x, @p y.
    static void draw_measurement_label(ImDrawList*                     dl,
                                       float                           x,
                                       float                           y,
                                       const std::vector<std::string>& lines,
                                       const ui::ThemeColors&          colors);

    // Select-mode click behavior: series selection callbacks only (no datatip marker mutations).
    bool on_mouse_click_series_only(double screen_x, double screen_y);

    // Region selection (ROI tool — analysis rectangle)
    void                    begin_region_select(double screen_x, double screen_y);
    void                    update_region_drag(double screen_x, double screen_y);
    void                    finish_region_select();
    void                    dismiss_region_select();
    bool                    is_region_dragging() const { return region_.is_dragging(); }
    bool                    has_region_selection() const { return region_.has_selection(); }
    const RegionStatistics& region_statistics() const { return region_.statistics(); }

    // Rectangle multi-select: select all series whose data intersects a screen-space rectangle.
    // Fires the on_rect_series_selected_ callback with all matching series.
    void select_series_in_rect(const BoxZoomRect& rect, Figure& figure);

    // Legend interaction
    LegendInteraction&       legend() { return legend_; }
    const LegendInteraction& legend() const { return legend_; }

    // Annotation management (Annotate tool)
    AnnotationManager&       annotations() { return annotations_; }
    const AnnotationManager& annotations() const { return annotations_; }

    // Handle mouse click in Annotate mode.
    // Returns true if the click was consumed (annotation placed/removed).
    bool on_mouse_click_annotate(int button, double screen_x, double screen_y);

    // Handle mouse drag for annotation repositioning.
    void begin_annotation_drag(double screen_x, double screen_y);
    void update_annotation_drag(double screen_x, double screen_y);
    void end_annotation_drag();
    bool is_annotation_dragging() const { return annotations_.is_dragging(); }

    // Set the transition engine for animated markers/regions
    void set_transition_engine(class TransitionEngine* te);

    // Set the axis link manager for shared cursor across subplots
    void             set_axis_link_manager(class AxisLinkManager* alm) { axis_link_mgr_ = alm; }
    AxisLinkManager* axis_link_manager() const { return axis_link_mgr_; }

    // Set snap radius for nearest-point detection (in pixels)
    void  set_snap_radius(float px);
    float snap_radius() const { return tooltip_.snap_radius(); }

    // Clean up all references to a series that is about to be destroyed.
    // Call this BEFORE the series is freed.
    void notify_series_removed(const Series* s)
    {
        markers_.remove_for_series(s);
        drag_marker_ = -1;   // indices shifted
        if (nearest_.series == s)
            nearest_ = {};
    }

    // Invalidate cached figure pointer (call when a figure is destroyed)
    void clear_figure_cache(Figure* fig = nullptr)
    {
        // The legend keys its state and pooled ImGui window by figure address.
        if (fig)
            legend_.forget_figure(reinterpret_cast<uintptr_t>(fig));
        if (!fig || last_figure_ == fig)
        {
            last_figure_   = nullptr;
            active_axes_   = nullptr;
            active_axes3d_ = nullptr;
            region_axes_   = nullptr;
            // Reset nearest-point result — it holds a raw Series* pointer
            // that becomes dangling when the figure's series are destroyed.
            nearest_ = {};
            // Clear markers — they hold raw Series* pointers that become
            // dangling when the figure (and its series) are destroyed.
            markers_.clear();
            drag_marker_ = -1;
        }
    }

    // Series selection callback: fired when user left-clicks near a series (toggles selection).
    // Args: (Figure*, Axes*, int axes_index, Series*, int series_index)
    using SeriesSelectedCallback = std::function<void(Figure*, Axes*, int, Series*, int)>;
    void set_on_series_selected(SeriesSelectedCallback cb) { on_series_selected_ = std::move(cb); }

    // Right-click series selection: fired when user right-clicks near a series (no toggle, always
    // selects).
    void set_on_series_right_click_selected(SeriesSelectedCallback cb)
    {
        on_series_rc_selected_ = std::move(cb);
    }

    // Series deselection callback: fired when user left-clicks on canvas but NOT near any series.
    using SeriesDeselectedCallback = std::function<void()>;
    void set_on_series_deselected(SeriesDeselectedCallback cb)
    {
        on_series_deselected_ = std::move(cb);
    }

    // Point selection callback: fired when a concrete point is selected.
    // Args: (Figure*, Axes*, int axes_index, Series*, int series_index, size_t point_index)
    using PointSelectedCallback = std::function<void(Figure*, Axes*, int, Series*, int, size_t)>;
    void set_on_point_selected(PointSelectedCallback cb) { on_point_selected_ = std::move(cb); }

    // Rectangle multi-select callback: fired when a rectangle drag selects multiple series.
    // Args: vector of (Figure*, Axes*, axes_index, Series*, series_index) tuples
    struct RectSelectedEntry
    {
        Figure* figure       = nullptr;
        Axes*   axes         = nullptr;
        int     axes_index   = -1;
        Series* series       = nullptr;
        int     series_index = -1;
    };
    using RectSeriesSelectedCallback = std::function<void(const std::vector<RectSelectedEntry>&)>;
    void set_on_rect_series_selected(RectSeriesSelectedCallback cb)
    {
        on_rect_series_selected_ = std::move(cb);
    }

    // Programmatically select/highlight a point (used by Data Editor row selection).
    // Returns true when the point was valid for the provided series and a marker was placed.
    bool select_point(const Series* series, size_t point_index);

    // Capture all overlay state into a plain-data snapshot for serialization.
    // Resolves raw Axes* pointers to index within the given figure.
    OverlaySnapshot capture_overlay_snapshot(const Figure& figure) const;

    // Restore overlay state from a snapshot. Resolves axes indices back to
    // pointers using the given figure. Clears existing markers/annotations first.
    void restore_overlay_snapshot(const OverlaySnapshot& snapshot, Figure& figure);
    // Adds the snapshot's markers and annotations for @p figure, keeping those
    // of other figures (opening a multi-figure workspace).
    void append_overlay_snapshot(const OverlaySnapshot& snapshot, Figure& figure);

    // Hash of @p figure's markers and annotations as a save would write them:
    // part of the document fingerprint, so editing them marks it modified.
    uint64_t overlay_fingerprint(const Figure& figure) const;

    static bool marker_belongs_to(const DataMarker& m, const Figure& figure);
    static bool annotation_belongs_to(const Annotation& a, const Figure& figure);

   private:
    // Dispatch series/point callbacks from current nearest_ selection.
    // Returns true when dispatch succeeded.
    bool dispatch_series_selection_from_nearest();

    // Refresh cached cursor/axes/nearest-point state for an explicit screen position.
    bool refresh_pointer_state(double screen_x, double screen_y);

    // Perform nearest-point spatial query across all visible series in the active axes.
    NearestPointResult find_nearest(const CursorReadout& cursor, Figure& figure) const;

    // Pin / unpin a datatip at the current nearest point (2D or 3D). Returns true if handled.
    bool try_toggle_datatip_at_nearest();

    // Click on 2D tip @p idx (select / Shift-toggle / remove the only selected).
    void click_marker(size_t idx, int mods);

    // Delta readout between the two selected tips of each axes.
    void draw_marker_deltas(Figure& figure, ImDrawList* dl);
    // Spike lines of the hovered 3D point (spikes3d_active()); true when drawn.
    bool draw_spikes_3d(ImDrawList* dl);

    // Tip being dragged (-1: none) and the sample it started on.
    int    drag_marker_      = -1;
    size_t drag_start_point_ = 0;

    // Screen <-> data mapping of the hovered 2D axes (through its axis scales).
    AxisMapping active_mapping() const
    {
        if (active_axes_)
            return AxisMapping(*active_axes_);
        return AxisMapping(active_viewport_, {xlim_min_, xlim_max_}, {ylim_min_, ylim_max_});
    }

    NearestPointResult nearest_;
    Tooltip            tooltip_;
    Crosshair          crosshair_;
    CompareReadout     compare_;
    bool               compare_drawn_      = false;
    bool               compare_suppressed_ = false;
    bool               spikes3d_enabled_   = true;
    bool               spikes3d_drawn_     = false;
    DataMarkerManager  markers_;
    AnnotationManager  annotations_;
    RegionSelect       region_;
    LegendInteraction  legend_;

    // Axis link manager for shared cursor
    AxisLinkManager* axis_link_mgr_ = nullptr;

    ui::ThemeManager* theme_mgr_ = nullptr;

    // Series selection / deselection callbacks
    SeriesSelectedCallback     on_series_selected_;
    SeriesSelectedCallback     on_series_rc_selected_;
    SeriesDeselectedCallback   on_series_deselected_;
    PointSelectedCallback      on_point_selected_;
    RectSeriesSelectedCallback on_rect_series_selected_;

    // Cached state for drawing
    CursorReadout last_cursor_;
    Figure*       last_figure_   = nullptr;
    Axes*         active_axes_   = nullptr;
    const Axes3D* active_axes3d_ = nullptr;
    Rect          active_viewport_;
    // Double precision for x: limits can sit at epoch scale (~1.7e9).
    double xlim_min_ = 0.0, xlim_max_ = 1.0;
    double ylim_min_ = 0.0, ylim_max_ = 1.0;

    // Region selection: remember which axes the ROI was started in
    // so it stays in the correct subplot when the cursor moves.
    Axes*          region_axes_                = nullptr;
    mutable size_t last_query_points_examined_ = 0;
};

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI

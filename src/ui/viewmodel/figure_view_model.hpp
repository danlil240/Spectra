#pragma once

#include <functional>
#include <spectra/axes.hpp>
#include <spectra/camera.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra
{

class UndoManager;

/**
 * FigureViewModel — View-layer state for a single Figure.
 *
 * Part of the Model/ViewModel separation (LT-1 in ARCHITECTURE_REVIEW.md).
 *
 * The core Figure class is the *model*: it owns pure data (axes, series,
 * style, legend, config).  FigureViewModel is the *view-model*: it owns
 * all per-figure UI state that the renderer, inspector, tab bar, and
 * interaction subsystems need but that is NOT part of the data model.
 *
 * Phase 1: Absorbed the fields previously in FigureState (figure_manager.hpp).
 *          FigureState remains a type alias for backward compatibility.
 * Phase 2: Migrated per-figure state from WindowUIContext into ViewModel.
 * Phase 3: All fields behind accessor methods.  Setters fire ChangeCallback
 *          with a ChangeField tag.  Optional UndoManager integration pushes
 *          undoable actions for key property mutations (title, modified,
 *          3D mode, selection).
 */
class FigureViewModel
{
   public:
    // ── Enumeration of mutable properties (Phase 3) ──────────────────
    enum class ChangeField
    {
        SelectedSeriesIndex,
        SelectedAxesIndex,
        InspectorScrollY,
        IsModified,
        CustomTitle,
        IsIn3DMode,
        Saved3DCamera,
        HomeLimits,
        ZoomCache,
        AxesSnapshots,
    };

    FigureViewModel() = default;
    explicit FigureViewModel(FigureId id, Figure* model = nullptr);

    // ── Model reference ──────────────────────────────────────────────
    Figure* model() const { return model_; }
    void    set_model(Figure* m) { model_ = m; }

    FigureId figure_id() const { return figure_id_; }
    void     set_figure_id(FigureId id) { figure_id_ = id; }

    // ── Undo integration (Phase 3) ───────────────────────────────────
    // When non-null, setters for key properties push UndoActions.
    void         set_undo_manager(UndoManager* mgr) { undo_mgr_ = mgr; }
    UndoManager* undo_manager() const { return undo_mgr_; }

    // ── Axis limit snapshots (restored on tab switch) ────────────────
    struct AxesSnapshot
    {
        AxisLimits x_limits;
        AxisLimits y_limits;
    };

    void save_axes_state();
    void restore_axes_state();

    // ── Change notification (Phase 3: fully wired) ───────────────────
    using ChangeCallback = std::function<void(FigureViewModel&, ChangeField)>;
    void set_on_changed(ChangeCallback cb) { on_changed_ = std::move(cb); }

    // ── Accessor methods (Phase 3: encapsulated, notify on change) ───

    // Axes snapshots
    const std::vector<AxesSnapshot>& axes_snapshots() const { return axes_snapshots_; }
    std::vector<AxesSnapshot>&       axes_snapshots_mut() { return axes_snapshots_; }

    // Inspector selection state
    int  selected_series_index() const { return selected_series_index_; }
    void set_selected_series_index(int idx);

    int  selected_axes_index() const { return selected_axes_index_; }
    void set_selected_axes_index(int idx);

    // Scroll positions
    float inspector_scroll_y() const { return inspector_scroll_y_; }
    void  set_inspector_scroll_y(float y);

    // Modified flag
    bool is_modified() const { return is_modified_; }
    void set_is_modified(bool v);

    // Custom title
    const std::string& custom_title() const { return custom_title_; }
    void               set_custom_title(const std::string& title);

    // 2D/3D mode toggle
    bool is_in_3d_mode() const { return is_in_3d_mode_; }
    void set_is_in_3d_mode(bool v);

    // Saved camera when toggling out of 3D
    const Camera& saved_3d_camera() const { return saved_3d_camera_; }
    Camera&       saved_3d_camera_mut() { return saved_3d_camera_; }
    void          set_saved_3d_camera(const Camera& cam);

    // Initial axes limits for Home button
    struct InitialLimits
    {
        AxisLimits x, y;
    };
    const std::unordered_map<Axes*, InitialLimits>& home_limits() const { return home_limits_; }
    std::unordered_map<Axes*, InitialLimits>&       home_limits_mut() { return home_limits_; }
    void set_home_limit(Axes* ax, const InitialLimits& lim);

    // Zoom cache
    float  cached_data_min() const { return cached_data_min_; }
    float  cached_data_max() const { return cached_data_max_; }
    size_t cached_zoom_series_count() const { return cached_zoom_series_count_; }
    bool   zoom_cache_valid() const { return zoom_cache_valid_; }
    void   set_zoom_cache(float data_min, float data_max, size_t series_count);
    void   invalidate_zoom_cache();

   private:
    FigureId figure_id_ = INVALID_FIGURE_ID;
    Figure*  model_     = nullptr;

    // Inspector / selection
    int   selected_series_index_ = -1;
    int   selected_axes_index_   = -1;
    float inspector_scroll_y_    = 0.0f;

    // Figure metadata
    bool        is_modified_ = false;
    std::string custom_title_;

    // Axes snapshots (restored on tab switch)
    std::vector<AxesSnapshot> axes_snapshots_;

    // 3D mode
    bool   is_in_3d_mode_ = false;
    Camera saved_3d_camera_;

    // Home limits
    std::unordered_map<Axes*, InitialLimits> home_limits_;

    // Zoom cache
    float  cached_data_min_          = 0.0f;
    float  cached_data_max_          = 0.0f;
    size_t cached_zoom_series_count_ = 0;
    bool   zoom_cache_valid_         = false;

    // Change notification
    ChangeCallback on_changed_;
    void           notify_changed(ChangeField field);

    // Undo
    UndoManager* undo_mgr_         = nullptr;
    bool         suppressing_undo_ = false;   // guard against re-entrant undo pushes
};

}   // namespace spectra

#pragma once

#include <functional>
#include <spectra/axes.hpp>
#include <spectra/fwd.hpp>

namespace spectra
{

class UndoManager;

/**
 * AxesViewModel — View-layer state for a single Axes.
 *
 * Part of the Model/ViewModel separation (LT-5 in ARCHITECTURE_REVIEW_V2.md).
 *
 * The core Axes class is the *model*: it owns data (series collection, axis
 * labels, autoscale config, presented-buffer settings).  AxesViewModel is the
 * *view-model*: it owns per-view UI state that the renderer, input handler,
 * and interaction subsystems need but that is NOT part of the data model.
 *
 * Phase 1: Create ViewModel with forwarding accessors for view-relevant state
 *          that currently lives on Axes (visual limits).  Add new per-axes
 *          view fields (hover, scroll).
 * Phase 2: Migrate callers to use AxesViewModel instead of direct Axes access.
 * Phase 3: Move visual-limit storage from Axes into AxesViewModel; enforce
 *          accessor-only access with change callbacks.
 */
class AxesViewModel
{
   public:
    // ── Enumeration of mutable properties ────────────────────────────
    enum class ChangeField
    {
        VisualXLim,
        VisualYLim,
        IsHovered,
        ScrollY,
    };

    AxesViewModel() = default;
    explicit AxesViewModel(Axes* model);

    // ── Model reference ──────────────────────────────────────────────
    Axes* model() const { return model_; }
    void  set_model(Axes* m) { model_ = m; }

    // ── Undo integration ─────────────────────────────────────────────
    void         set_undo_manager(UndoManager* mgr) { undo_mgr_ = mgr; }
    UndoManager* undo_manager() const { return undo_mgr_; }

    // ── Change notification ──────────────────────────────────────────
    using ChangeCallback = std::function<void(AxesViewModel&, ChangeField)>;
    void set_on_changed(ChangeCallback cb) { on_changed_ = std::move(cb); }

    // ── Forwarding accessors (Phase 1) ───────────────────────────────
    // In Phase 1 these delegate to the Axes model.  In Phase 3 the
    // storage will migrate here for per-view zoom independence.

    AxisLimits visual_xlim() const;
    AxisLimits visual_ylim() const;
    void       set_visual_xlim(double min, double max);
    void       set_visual_ylim(double min, double max);

    // ── New per-view state ───────────────────────────────────────────

    bool is_hovered() const { return is_hovered_; }
    void set_is_hovered(bool v);

    float scroll_y() const { return scroll_y_; }
    void  set_scroll_y(float y);

   private:
    Axes* model_ = nullptr;

    // Per-view state (owned by ViewModel)
    bool  is_hovered_ = false;
    float scroll_y_   = 0.0f;

    // Change notification
    ChangeCallback on_changed_;
    void           notify_changed(ChangeField field);

    // Undo
    UndoManager* undo_mgr_         = nullptr;
    bool         suppressing_undo_ = false;
};

}   // namespace spectra

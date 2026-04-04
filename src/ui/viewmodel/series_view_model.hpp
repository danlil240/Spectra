#pragma once

#include <functional>
#include <optional>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <string>

namespace spectra
{

class UndoManager;

/**
 * SeriesViewModel — View-layer state for a single Series.
 *
 * Part of the Model/ViewModel separation (LT-5 in ARCHITECTURE_REVIEW_V2.md).
 *
 * The core Series class is the *model*: it owns data arrays (x, y, z),
 * dirty tracking, and thread-safe pending buffers.  SeriesViewModel is the
 * *view-model*: it owns per-view UI state such as visibility overrides,
 * selection highlight, and label/opacity overrides.
 *
 * Phase 1: Create ViewModel with forwarding accessors for visible(), color(),
 *          label(), and opacity() that currently live on Series.  Add new
 *          per-view override fields.
 * Phase 2: Migrate callers (Renderer, Inspector, etc.) to use SeriesViewModel.
 * Phase 3: Move visibility/color/label storage from Series into
 *          SeriesViewModel; enforce accessor-only access with change callbacks.
 *
 * Override semantics: If an override is set (e.g. visible_override_), the
 * effective value uses the override.  Otherwise, the model's value is returned.
 * This enables per-window visibility (show series A in window 1, hide in
 * window 2) while keeping the shared model unchanged.
 */
class SeriesViewModel
{
   public:
    // ── Enumeration of mutable properties ────────────────────────────
    enum class ChangeField
    {
        Visible,
        Color,
        Label,
        Opacity,
        IsSelected,
        IsHighlighted,
    };

    SeriesViewModel() = default;
    explicit SeriesViewModel(Series* model);

    // ── Model reference ──────────────────────────────────────────────
    Series* model() const { return model_; }
    void    set_model(Series* m) { model_ = m; }

    // ── Undo integration ─────────────────────────────────────────────
    void         set_undo_manager(UndoManager* mgr) { undo_mgr_ = mgr; }
    UndoManager* undo_manager() const { return undo_mgr_; }

    // ── Change notification ──────────────────────────────────────────
    using ChangeCallback = std::function<void(SeriesViewModel&, ChangeField)>;
    void set_on_changed(ChangeCallback cb) { on_changed_ = std::move(cb); }

    // ── Effective accessors (override → model fallback) ──────────────
    // These return the per-view override if set, otherwise the model value.

    bool               effective_visible() const;
    Color              effective_color() const;
    const std::string& effective_label() const;
    float              effective_opacity() const;

    // ── Override management ──────────────────────────────────────────

    // Visibility override (per-window show/hide).
    bool has_visible_override() const { return visible_override_.has_value(); }
    void set_visible_override(bool v);
    void clear_visible_override();

    // Color override (per-window color change).
    bool has_color_override() const { return color_override_.has_value(); }
    void set_color_override(const Color& c);
    void clear_color_override();

    // Label override (user-edited display name).
    bool has_label_override() const { return label_override_.has_value(); }
    void set_label_override(const std::string& lbl);
    void clear_label_override();

    // Opacity override (per-window opacity tweak).
    bool has_opacity_override() const { return opacity_override_.has_value(); }
    void set_opacity_override(float o);
    void clear_opacity_override();

    // ── Direct forwarding to model (Phase 1 convenience) ─────────────
    // These modify the underlying Series model directly, matching the
    // existing Series API.  Callers can use these during the migration
    // period before Phase 3 moves storage to the ViewModel.

    void set_visible(bool v);
    void set_color(const Color& c);
    void set_label(const std::string& lbl);
    void set_opacity(float o);

    // ── Per-view selection / highlight ────────────────────────────────

    bool is_selected() const { return is_selected_; }
    void set_is_selected(bool v);

    bool is_highlighted() const { return is_highlighted_; }
    void set_is_highlighted(bool v);

   private:
    Series* model_ = nullptr;

    // Per-view overrides (std::nullopt = use model value)
    std::optional<bool>        visible_override_;
    std::optional<Color>       color_override_;
    std::optional<std::string> label_override_;
    std::optional<float>       opacity_override_;

    // Per-view interaction state
    bool is_selected_    = false;
    bool is_highlighted_ = false;

    // Change notification
    ChangeCallback on_changed_;
    void           notify_changed(ChangeField field);

    // Undo
    UndoManager* undo_mgr_         = nullptr;
    bool         suppressing_undo_ = false;
};

}   // namespace spectra

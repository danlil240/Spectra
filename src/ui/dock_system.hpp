#pragma once

#include <cstddef>
#include <functional>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>  // For Rect
#include <string>
#include <vector>

#include "split_view.hpp"

namespace spectra
{

// ─── Drop zone indicators for drag-to-dock ──────────────────────────────────

enum class DropZone
{
    None,
    Left,
    Right,
    Top,
    Bottom,
    Center  // Tab into existing pane (no split)
};

struct DropTarget
{
    DropZone zone = DropZone::None;
    SplitPane* target_pane = nullptr;
    Rect highlight_rect{};  // Visual indicator rect
};

// ─── DockSystem ──────────────────────────────────────────────────────────────
// High-level docking system that combines the existing DockManager (for panel
// layout: nav rail, inspector, status bar) with SplitViewManager (for canvas
// split views). Provides drag-to-dock, drop zone visualization, and unified
// state serialization.
//
// Architecture:
//   DockManager  → owns the outer chrome layout (nav rail, inspector, etc.)
//   SplitViewManager → owns the canvas split tree (figure panes)
//   DockSystem   → orchestrates both + drag-to-dock + serialization

class DockSystem
{
   public:
    using DockCallback = std::function<void()>;

    DockSystem();
    ~DockSystem() = default;

    // Non-copyable
    DockSystem(const DockSystem&) = delete;
    DockSystem& operator=(const DockSystem&) = delete;

    // ── Split view access ───────────────────────────────────────────────

    SplitViewManager& split_view() { return split_view_; }
    const SplitViewManager& split_view() const { return split_view_; }

    // ── Convenience split operations ────────────────────────────────────

    // Split the active pane right (horizontal split)
    SplitPane* split_right(size_t new_figure_index, float ratio = 0.5f);

    // Split the active pane down (vertical split)
    SplitPane* split_down(size_t new_figure_index, float ratio = 0.5f);

    // Split a specific figure's pane
    SplitPane* split_figure_right(size_t figure_index, size_t new_figure_index, float ratio = 0.5f);
    SplitPane* split_figure_down(size_t figure_index, size_t new_figure_index, float ratio = 0.5f);

    // Close a split pane (unsplit, keeping sibling)
    bool close_split(size_t figure_index);

    // Reset to single pane
    void reset_splits();

    // ── Drag-to-dock ────────────────────────────────────────────────────

    // Begin dragging a tab/figure for docking
    void begin_drag(size_t figure_index, float mouse_x, float mouse_y);

    // Update drag position — computes drop zone highlights
    DropTarget update_drag(float mouse_x, float mouse_y);

    // Complete the drag — perform the dock operation
    bool end_drag(float mouse_x, float mouse_y);

    // Cancel drag without docking
    void cancel_drag();

    bool is_dragging() const { return is_dragging_; }
    size_t dragging_figure() const { return dragging_figure_index_; }
    DropTarget current_drop_target() const { return current_drop_target_; }

    // ── Layout ──────────────────────────────────────────────────────────

    // Update layout — call once per frame with the canvas rect from LayoutManager
    void update_layout(const Rect& canvas_bounds);

    // Get all leaf pane rects for rendering
    struct PaneInfo
    {
        size_t figure_index;
        Rect bounds;
        bool is_active;
        SplitPane::PaneId pane_id;
    };
    std::vector<PaneInfo> get_pane_infos() const;

    // ── Splitter interaction ────────────────────────────────────────────

    // Hit-test: is the mouse over a splitter?
    bool is_over_splitter(float x, float y) const;

    // Get the split direction of the splitter at point (for cursor icon)
    SplitDirection splitter_direction_at(float x, float y) const;

    // Delegate to SplitViewManager
    void begin_splitter_drag(float x, float y);
    void update_splitter_drag(float mouse_pos);
    void end_splitter_drag();
    bool is_dragging_splitter() const { return split_view_.is_dragging_splitter(); }

    // ── Active pane ─────────────────────────────────────────────────────

    size_t active_figure_index() const { return split_view_.active_figure_index(); }
    void set_active_figure_index(size_t idx) { split_view_.set_active_figure_index(idx); }

    // Click in a pane to activate it
    void activate_pane_at(float x, float y);

    // Move a figure from one pane to another (cross-pane tab drag)
    bool move_figure_to_pane(size_t figure_index, SplitPane::PaneId target_pane_id);

    // Activate a specific figure within a pane's local tab bar
    void activate_local_tab(SplitPane::PaneId pane_id, size_t local_index);

    // ── State queries ───────────────────────────────────────────────────

    bool is_split() const { return split_view_.is_split(); }
    size_t pane_count() const { return split_view_.pane_count(); }

    // ── Serialization ───────────────────────────────────────────────────

    std::string serialize() const;
    bool deserialize(const std::string& data);

    // ── Callbacks ───────────────────────────────────────────────────────

    void set_on_layout_changed(DockCallback cb) { on_layout_changed_ = std::move(cb); }

   private:
    SplitViewManager split_view_;

    // Drag-to-dock state
    bool is_dragging_ = false;
    size_t dragging_figure_index_ = 0;
    float drag_mouse_x_ = 0.0f;
    float drag_mouse_y_ = 0.0f;
    DropTarget current_drop_target_;

    // Callbacks
    DockCallback on_layout_changed_;

    // Internal helpers
    DropTarget compute_drop_target(float x, float y) const;
    Rect compute_drop_highlight(const SplitPane* pane, DropZone zone) const;

    // Drop zone detection constants
    static constexpr float DROP_ZONE_FRACTION = 0.25f;  // Edge fraction for drop zones
    static constexpr float DROP_ZONE_MIN_SIZE = 40.0f;  // Minimum drop zone size in pixels
};

}  // namespace spectra

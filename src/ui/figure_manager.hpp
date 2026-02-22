#pragma once

#include <cstddef>
#include <functional>
#include <spectra/figure.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "figure_registry.hpp"

namespace spectra
{

class TabBar;

// Per-figure state that persists across tab switches
struct FigureState
{
    // Axis limits snapshot (restored when switching back to this figure)
    struct AxesSnapshot
    {
        AxisLimits x_limits;
        AxisLimits y_limits;
    };
    std::vector<AxesSnapshot> axes_snapshots;

    // Inspector selection state
    int selected_series_index = -1;   // -1 = none
    int selected_axes_index   = -1;   // -1 = none

    // Scroll positions
    float inspector_scroll_y = 0.0f;

    // Modified flag (unsaved changes)
    bool is_modified = false;

    // Custom title (empty = auto-generated)
    std::string custom_title;
};

/**
 * FigureManager — Manages multi-figure lifecycle for Spectra.
 *
 * Encapsulates figure creation, closing, switching, reordering, and
 * duplication. Maintains per-figure state that persists across tab switches.
 * Designed to work with TabBar for UI representation.
 */
class FigureManager
{
   public:
    using FigureChangeCallback       = std::function<void(FigureId new_id, Figure* fig)>;
    using FigureCloseCallback        = std::function<void(FigureId id)>;
    using WindowCloseRequestCallback = std::function<void()>;

    explicit FigureManager(FigureRegistry& registry);
    ~FigureManager() = default;

    // Disable copying
    FigureManager(const FigureManager&)            = delete;
    FigureManager& operator=(const FigureManager&) = delete;

    // Wire to TabBar for synchronized UI
    void    set_tab_bar(TabBar* tab_bar);
    TabBar* tab_bar() const { return tab_bar_; }

    // Figure lifecycle
    FigureId create_figure(const FigureConfig& config = {});
    bool     close_figure(FigureId index);
    bool     close_all_except(FigureId index);
    bool     close_to_right(FigureId index);
    FigureId duplicate_figure(FigureId index);

    // Cross-window figure transfer (does NOT unregister from FigureRegistry)
    // remove_figure: removes from this manager's ordered list and tab bar,
    //   returns the FigureState (axis snapshots, inspector state, title).
    //   Returns default FigureState if not found.
    FigureState remove_figure(FigureId id);

    // add_figure: adds an existing registry figure to this manager with
    //   the given state.  Appends to tab bar and switches to it.
    void add_figure(FigureId id, FigureState state);

    // Navigation
    void switch_to(FigureId index);
    void switch_to_next();
    void switch_to_previous();
    void move_tab(FigureId from_index, FigureId to_index);

    // State queries
    FigureId active_index() const { return active_index_; }
    Figure*  active_figure() const;
    size_t   count() const { return ordered_ids_.size(); }
    bool     can_close(FigureId index) const;

    // Figure access by positional index (for backward compatibility)
    Figure*                      get_figure(FigureId id) const;
    const std::vector<FigureId>& figure_ids() const { return ordered_ids_; }
    FigureRegistry&              registry() { return registry_; }

    // Per-figure state
    FigureState&       state(FigureId index);
    const FigureState& state(FigureId index) const;
    FigureState&       active_state();

    // Title management
    std::string get_title(FigureId index) const;
    void        set_title(FigureId index, const std::string& title);
    void        mark_modified(FigureId index, bool modified = true);
    bool        is_modified(FigureId index) const;

    // Callbacks
    void set_on_figure_changed(FigureChangeCallback cb) { on_figure_changed_ = std::move(cb); }
    void set_on_figure_closed(FigureCloseCallback cb) { on_figure_closed_ = std::move(cb); }
    void set_on_window_close_request(WindowCloseRequestCallback cb)
    {
        on_window_close_request_ = std::move(cb);
    }

    // Process pending operations (call once per frame from main loop)
    // Returns true if active figure changed this frame
    bool process_pending();

    // Queue operations (safe to call from callbacks)
    void queue_create();
    void queue_close(FigureId index);
    void queue_switch(FigureId index);

    // Save/restore axis state for the current figure
    void save_active_state();
    void restore_state(FigureId index);

    // Generate default title for a figure
    static std::string default_title(FigureId index);

   private:
    FigureRegistry&       registry_;
    std::vector<FigureId> ordered_ids_;   // Ordered list of registry IDs (tab order)
    std::unordered_map<FigureId, FigureState> states_;
    FigureId                                  active_index_ = INVALID_FIGURE_ID;
    TabBar*                                   tab_bar_      = nullptr;

    // Convert positional index to FigureId and vice versa
    size_t   id_to_pos(FigureId id) const;
    FigureId pos_to_id(size_t pos) const;

    // Pending operations (processed in process_pending())
    FigureId pending_switch_ = INVALID_FIGURE_ID;
    FigureId pending_close_  = INVALID_FIGURE_ID;
    bool     pending_create_ = false;

    // Callbacks
    FigureChangeCallback       on_figure_changed_;
    FigureCloseCallback        on_figure_closed_;
    WindowCloseRequestCallback on_window_close_request_;

    // Internal helpers
    void   sync_tab_bar();
    void   ensure_states();
    size_t next_figure_number() const;
};

}   // namespace spectra

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <plotix/figure.hpp>
#include <plotix/fwd.hpp>
#include <plotix/series.hpp>
#include <string>
#include <vector>

namespace plotix
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
    int selected_series_index = -1;  // -1 = none
    int selected_axes_index = -1;    // -1 = none

    // Scroll positions
    float inspector_scroll_y = 0.0f;

    // Modified flag (unsaved changes)
    bool is_modified = false;

    // Custom title (empty = auto-generated)
    std::string custom_title;
};

/**
 * FigureManager — Manages multi-figure lifecycle for Plotix.
 *
 * Encapsulates figure creation, closing, switching, reordering, and
 * duplication. Maintains per-figure state that persists across tab switches.
 * Designed to work with TabBar for UI representation.
 */
class FigureManager
{
   public:
    using FigureChangeCallback = std::function<void(size_t new_index, Figure* fig)>;
    using FigureCloseCallback = std::function<void(size_t index)>;

    explicit FigureManager(std::vector<std::unique_ptr<Figure>>& figures);
    ~FigureManager() = default;

    // Disable copying
    FigureManager(const FigureManager&) = delete;
    FigureManager& operator=(const FigureManager&) = delete;

    // Wire to TabBar for synchronized UI
    void set_tab_bar(TabBar* tab_bar);
    TabBar* tab_bar() const { return tab_bar_; }

    // Figure lifecycle
    size_t create_figure(const FigureConfig& config = {});
    bool close_figure(size_t index);
    bool close_all_except(size_t index);
    bool close_to_right(size_t index);
    size_t duplicate_figure(size_t index);

    // Navigation
    void switch_to(size_t index);
    void switch_to_next();
    void switch_to_previous();
    void move_tab(size_t from_index, size_t to_index);

    // State queries
    size_t active_index() const { return active_index_; }
    Figure* active_figure() const;
    size_t count() const { return figures_.size(); }
    bool can_close(size_t index) const;

    // Per-figure state
    FigureState& state(size_t index);
    const FigureState& state(size_t index) const;
    FigureState& active_state();

    // Title management
    std::string get_title(size_t index) const;
    void set_title(size_t index, const std::string& title);
    void mark_modified(size_t index, bool modified = true);
    bool is_modified(size_t index) const;

    // Callbacks
    void set_on_figure_changed(FigureChangeCallback cb) { on_figure_changed_ = std::move(cb); }
    void set_on_figure_closed(FigureCloseCallback cb) { on_figure_closed_ = std::move(cb); }

    // Process pending operations (call once per frame from main loop)
    // Returns true if active figure changed this frame
    bool process_pending();

    // Queue operations (safe to call from callbacks)
    void queue_create();
    void queue_close(size_t index);
    void queue_switch(size_t index);

    // Save/restore axis state for the current figure
    void save_active_state();
    void restore_state(size_t index);

    // Generate default title for a figure
    static std::string default_title(size_t index);

   private:
    std::vector<std::unique_ptr<Figure>>& figures_;
    std::vector<FigureState> states_;
    size_t active_index_ = 0;
    TabBar* tab_bar_ = nullptr;

    // Pending operations (processed in process_pending())
    size_t pending_switch_ = SIZE_MAX;
    size_t pending_close_ = SIZE_MAX;
    bool pending_create_ = false;

    // Callbacks
    FigureChangeCallback on_figure_changed_;
    FigureCloseCallback on_figure_closed_;

    // Internal helpers
    void sync_tab_bar();
    void ensure_states();
    size_t next_figure_number() const;
};

}  // namespace plotix

#pragma once

#include <plotix/series.hpp>  // For Rect definition

namespace plotix {

/**
 * LayoutManager - Zone-based layout engine for Plotix UI
 * 
 * Replaces hardcoded pixel positions with a responsive zone system.
 * All UI components query their layout rectangles from this manager.
 */
class LayoutManager {
public:
    LayoutManager();
    ~LayoutManager() = default;

    // Disable copying
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    /**
     * Update all zone rectangles based on current window size.
     * Call this once per frame when window size changes.
     */
    void update(float window_width, float window_height);

    // Zone rectangle queries
    Rect command_bar_rect() const;
    Rect nav_rail_rect() const;
    Rect canvas_rect() const;
    Rect inspector_rect() const;
    Rect status_bar_rect() const;
    Rect floating_toolbar_rect() const;

    // Configuration
    void set_inspector_visible(bool visible);
    void set_inspector_width(float width);
    void set_nav_rail_width(float width);
    void set_nav_rail_expanded(bool expanded);

    // State queries
    bool is_inspector_visible() const { return inspector_visible_; }
    float inspector_width() const { return inspector_width_; }
    bool is_nav_rail_expanded() const { return nav_rail_expanded_; }
    float nav_rail_width() const { return nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_; }

    // Layout constants (matching the design spec)
    static constexpr float COMMAND_BAR_HEIGHT = 40.0f;
    static constexpr float STATUS_BAR_HEIGHT = 28.0f;
    static constexpr float NAV_RAIL_COLLAPSED_WIDTH = 48.0f;
    static constexpr float NAV_RAIL_EXPANDED_WIDTH = 200.0f;
    static constexpr float INSPECTOR_DEFAULT_WIDTH = 320.0f;
    static constexpr float INSPECTOR_MIN_WIDTH = 240.0f;
    static constexpr float INSPECTOR_MAX_WIDTH = 480.0f;
    static constexpr float FLOATING_TOOLBAR_HEIGHT = 40.0f;
    static constexpr float FLOATING_TOOLBAR_WIDTH = 200.0f;

private:
    // Window dimensions
    float window_width_ = 1280.0f;
    float window_height_ = 720.0f;

    // Zone rectangles (computed in update())
    Rect command_bar_rect_;
    Rect nav_rail_rect_;
    Rect canvas_rect_;
    Rect inspector_rect_;
    Rect status_bar_rect_;
    Rect floating_toolbar_rect_;

    // Configuration state
    bool inspector_visible_ = true;
    float inspector_width_ = INSPECTOR_DEFAULT_WIDTH;
    bool nav_rail_expanded_ = false;
    float nav_rail_collapsed_width_ = NAV_RAIL_COLLAPSED_WIDTH;
    float nav_rail_expanded_width_ = NAV_RAIL_EXPANDED_WIDTH;

    // Helper methods
    void compute_zones();
    Rect compute_command_bar() const;
    Rect compute_nav_rail() const;
    Rect compute_canvas() const;
    Rect compute_inspector() const;
    Rect compute_status_bar() const;
    Rect compute_floating_toolbar() const;
};

} // namespace plotix

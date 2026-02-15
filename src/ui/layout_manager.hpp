#pragma once

#include <plotix/series.hpp>  // For Rect definition

namespace plotix {

/**
 * LayoutManager - Zone-based layout engine for Plotix UI
 * 
 * Replaces hardcoded pixel positions with a responsive zone system.
 * All UI components query their layout rectangles from this manager.
 * Supports smooth animated transitions for panel open/close/resize.
 */
class LayoutManager {
public:
    LayoutManager();
    ~LayoutManager() = default;

    // Disable copying
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    /**
     * Update all zone rectangles based on current window size and delta time.
     * Call this once per frame. Drives animated transitions.
     */
    void update(float window_width, float window_height, float dt = 0.0f);

    // Zone rectangle queries
    Rect command_bar_rect() const;
    Rect nav_rail_rect() const;
    Rect canvas_rect() const;
    Rect inspector_rect() const;
    Rect status_bar_rect() const;
#if PLOTIX_FLOATING_TOOLBAR
    Rect floating_toolbar_rect() const;
#endif
    Rect tab_bar_rect() const;

    // Configuration
    void set_inspector_visible(bool visible);
    void set_inspector_width(float width);
    void set_nav_rail_width(float width);
    void set_nav_rail_expanded(bool expanded);
    void set_tab_bar_visible(bool visible);
    void reset_inspector_width();

    // Bottom panel (timeline)
    void set_bottom_panel_height(float h) { bottom_panel_height_ = h; }
    float bottom_panel_height() const { return bottom_panel_height_; }

#if PLOTIX_FLOATING_TOOLBAR
    // Floating toolbar
    void set_floating_toolbar_visible(bool visible);
    void toggle_floating_toolbar();
    bool is_floating_toolbar_visible() const { return floating_toolbar_visible_; }
    float floating_toolbar_opacity() const { return floating_toolbar_opacity_; }
    void set_floating_toolbar_drag_offset(float dx, float dy);
    void reset_floating_toolbar_position();
    void notify_toolbar_activity();
#endif

    // State queries
    bool is_inspector_visible() const { return inspector_visible_; }
    float inspector_width() const { return inspector_width_; }
    float inspector_animated_width() const { return inspector_anim_width_; }
    bool is_nav_rail_expanded() const { return nav_rail_expanded_; }
    float nav_rail_width() const;
    float nav_rail_animated_width() const { return nav_rail_anim_width_; }
    bool is_tab_bar_visible() const { return tab_bar_visible_; }
    bool is_animating() const;

    // Inspector resize interaction helpers
    bool is_inspector_resize_hovered() const { return inspector_resize_hovered_; }
    void set_inspector_resize_hovered(bool hovered) { inspector_resize_hovered_ = hovered; }
    bool is_inspector_resize_active() const { return inspector_resize_active_; }
    void set_inspector_resize_active(bool active) { inspector_resize_active_ = active; }

    // Layout constants (matching the design spec)
    static constexpr float COMMAND_BAR_HEIGHT = 48.0f;
    static constexpr float STATUS_BAR_HEIGHT = 28.0f;
    static constexpr float NAV_RAIL_COLLAPSED_WIDTH = 48.0f;
    static constexpr float NAV_RAIL_EXPANDED_WIDTH = 200.0f;
    static constexpr float NAV_TOOLBAR_INSET = 68.0f;  // Space reserved for floating nav toolbar (margin + toolbar + gap)
    static constexpr float PLOT_LEFT_MARGIN = 100.0f;  // Default plot left margin (matches Margins::left) for tab alignment
    static constexpr float INSPECTOR_DEFAULT_WIDTH = 320.0f;
    static constexpr float INSPECTOR_MIN_WIDTH = 240.0f;
    static constexpr float INSPECTOR_MAX_WIDTH = 480.0f;
#if PLOTIX_FLOATING_TOOLBAR
    static constexpr float FLOATING_TOOLBAR_HEIGHT = 40.0f;
    static constexpr float FLOATING_TOOLBAR_WIDTH = 220.0f;
#endif
    static constexpr float TAB_BAR_HEIGHT = 36.0f;
    static constexpr float RESIZE_HANDLE_WIDTH = 6.0f;
    static constexpr float ANIM_SPEED = 12.0f;

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
#if PLOTIX_FLOATING_TOOLBAR
    Rect floating_toolbar_rect_;
#endif
    Rect tab_bar_rect_;

    // Configuration state
    bool inspector_visible_ = false;
    float inspector_width_ = INSPECTOR_DEFAULT_WIDTH;
    bool nav_rail_expanded_ = false;
    float nav_rail_collapsed_width_ = NAV_RAIL_COLLAPSED_WIDTH;
    float nav_rail_expanded_width_ = NAV_RAIL_EXPANDED_WIDTH;
    bool tab_bar_visible_ = false;
    float bottom_panel_height_ = 0.0f;  // Timeline panel height (0 when hidden)

    // Animated state (smoothly interpolated toward targets)
    float inspector_anim_width_ = 0.0f;   // 0 when hidden
    float nav_rail_anim_width_ = NAV_RAIL_COLLAPSED_WIDTH;

    // Inspector resize interaction state
    bool inspector_resize_hovered_ = false;
    bool inspector_resize_active_ = false;

#if PLOTIX_FLOATING_TOOLBAR
    // Floating toolbar state
    bool floating_toolbar_visible_ = true;
    float floating_toolbar_opacity_ = 1.0f;
    float floating_toolbar_idle_timer_ = 0.0f;
    bool floating_toolbar_has_custom_pos_ = false;
    float floating_toolbar_offset_x_ = 0.0f;
    float floating_toolbar_offset_y_ = 0.0f;
    static constexpr float TOOLBAR_FADE_SPEED = 6.0f;
    static constexpr float TOOLBAR_AUTO_HIDE_DELAY = 3.0f;
#endif

    // Helper: exponential smoothing toward target
    static float smooth_toward(float current, float target, float speed, float dt);

    // Helper methods
    void compute_zones();
    Rect compute_command_bar() const;
    Rect compute_nav_rail() const;
    Rect compute_canvas() const;
    Rect compute_inspector() const;
    Rect compute_status_bar() const;
#if PLOTIX_FLOATING_TOOLBAR
    Rect compute_floating_toolbar() const;
#endif
    Rect compute_tab_bar() const;
};

} // namespace plotix

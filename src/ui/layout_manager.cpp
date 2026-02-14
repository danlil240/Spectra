#include "layout_manager.hpp"
#include <algorithm>
#include <cmath>

namespace plotix {

LayoutManager::LayoutManager() {
    compute_zones();
}

float LayoutManager::smooth_toward(float current, float target, float speed, float dt) {
    if (dt <= 0.0f) return target;  // No dt → snap instantly
    float diff = target - current;
    if (std::abs(diff) < 0.5f) return target;  // Close enough → snap
    return current + diff * std::min(1.0f, speed * dt);
}

void LayoutManager::update(float window_width, float window_height, float dt) {
    window_width_ = window_width;
    window_height_ = window_height;

    // Compute animation targets
    float inspector_target = inspector_visible_ ? inspector_width_ : 0.0f;
    float nav_rail_target = nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;

    // Animate toward targets
    inspector_anim_width_ = smooth_toward(inspector_anim_width_, inspector_target, ANIM_SPEED, dt);
    nav_rail_anim_width_  = smooth_toward(nav_rail_anim_width_, nav_rail_target, ANIM_SPEED, dt);

#if PLOTIX_FLOATING_TOOLBAR
    // Floating toolbar auto-hide: fade out after inactivity
    if (floating_toolbar_visible_ && dt > 0.0f) {
        floating_toolbar_idle_timer_ += dt;
        float opacity_target = (floating_toolbar_idle_timer_ < TOOLBAR_AUTO_HIDE_DELAY) ? 1.0f : 0.15f;
        float diff = opacity_target - floating_toolbar_opacity_;
        if (std::abs(diff) < 0.01f) {
            floating_toolbar_opacity_ = opacity_target;
        } else {
            floating_toolbar_opacity_ += diff * std::min(1.0f, TOOLBAR_FADE_SPEED * dt);
        }
    } else if (!floating_toolbar_visible_) {
        float diff = 0.0f - floating_toolbar_opacity_;
        if (std::abs(diff) < 0.01f) {
            floating_toolbar_opacity_ = 0.0f;
        } else {
            floating_toolbar_opacity_ += diff * std::min(1.0f, TOOLBAR_FADE_SPEED * dt);
        }
    }
#endif

    compute_zones();
}

void LayoutManager::compute_zones() {
    command_bar_rect_      = compute_command_bar();
    nav_rail_rect_         = compute_nav_rail();
    inspector_rect_        = compute_inspector();
    status_bar_rect_       = compute_status_bar();
    tab_bar_rect_          = compute_tab_bar();
    canvas_rect_           = compute_canvas();
#if PLOTIX_FLOATING_TOOLBAR
    floating_toolbar_rect_ = compute_floating_toolbar();
#endif
}

Rect LayoutManager::compute_command_bar() const {
    return Rect{0.0f, 0.0f, window_width_, COMMAND_BAR_HEIGHT};
}

Rect LayoutManager::compute_nav_rail() const {
    float content_h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;
    return Rect{0.0f, COMMAND_BAR_HEIGHT, nav_rail_anim_width_, std::max(0.0f, content_h)};
}

Rect LayoutManager::compute_inspector() const {
    if (inspector_anim_width_ < 1.0f) {
        return Rect{window_width_, COMMAND_BAR_HEIGHT, 0.0f, 0.0f};
    }
    float content_h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;
    return Rect{
        window_width_ - inspector_anim_width_,
        COMMAND_BAR_HEIGHT,
        inspector_anim_width_,
        std::max(0.0f, content_h)
    };
}

Rect LayoutManager::compute_status_bar() const {
    return Rect{0.0f, window_height_ - STATUS_BAR_HEIGHT, window_width_, STATUS_BAR_HEIGHT};
}

Rect LayoutManager::compute_tab_bar() const {
    if (!tab_bar_visible_) {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    float x = NAV_TOOLBAR_INSET + PLOT_LEFT_MARGIN;
    float w = window_width_ - x - inspector_anim_width_;
    return Rect{x, COMMAND_BAR_HEIGHT, std::max(0.0f, w), TAB_BAR_HEIGHT};
}

Rect LayoutManager::compute_canvas() const {
    float x = NAV_TOOLBAR_INSET;
    float w = window_width_ - NAV_TOOLBAR_INSET - inspector_anim_width_;
    float y = COMMAND_BAR_HEIGHT;
    
    float h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;

    // Offset canvas below tab bar when visible
    if (tab_bar_visible_) {
        y += TAB_BAR_HEIGHT;
        h -= TAB_BAR_HEIGHT;
    }

    return Rect{x, y, std::max(0.0f, w), std::max(0.0f, h)};
}

#if PLOTIX_FLOATING_TOOLBAR
Rect LayoutManager::compute_floating_toolbar() const {
    // Default position: centered horizontally, near bottom of canvas (floating on top)
    float default_x = canvas_rect_.x + (canvas_rect_.w - FLOATING_TOOLBAR_WIDTH) * 0.5f;
    float default_y = canvas_rect_.y + canvas_rect_.h - FLOATING_TOOLBAR_HEIGHT - 60.0f;

    float toolbar_x = floating_toolbar_has_custom_pos_ ? floating_toolbar_offset_x_ : default_x;
    float toolbar_y = floating_toolbar_has_custom_pos_ ? floating_toolbar_offset_y_ : default_y;

    // Clamp to stay within canvas bounds
    toolbar_x = std::clamp(toolbar_x, canvas_rect_.x, canvas_rect_.x + canvas_rect_.w - FLOATING_TOOLBAR_WIDTH);
    toolbar_y = std::clamp(toolbar_y, canvas_rect_.y, canvas_rect_.y + canvas_rect_.h - FLOATING_TOOLBAR_HEIGHT);

    return Rect{toolbar_x, toolbar_y, FLOATING_TOOLBAR_WIDTH, FLOATING_TOOLBAR_HEIGHT};
}
#endif

// Zone rectangle getters
Rect LayoutManager::command_bar_rect() const { return command_bar_rect_; }
Rect LayoutManager::nav_rail_rect() const { return nav_rail_rect_; }
Rect LayoutManager::canvas_rect() const { return canvas_rect_; }
Rect LayoutManager::inspector_rect() const { return inspector_rect_; }
Rect LayoutManager::status_bar_rect() const { return status_bar_rect_; }
#if PLOTIX_FLOATING_TOOLBAR
Rect LayoutManager::floating_toolbar_rect() const { return floating_toolbar_rect_; }
#endif
Rect LayoutManager::tab_bar_rect() const { return tab_bar_rect_; }

float LayoutManager::nav_rail_width() const {
    return nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;
}

bool LayoutManager::is_animating() const {
    float inspector_target = inspector_visible_ ? inspector_width_ : 0.0f;
    float nav_target = nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;
    return std::abs(inspector_anim_width_ - inspector_target) > 0.5f ||
           std::abs(nav_rail_anim_width_ - nav_target) > 0.5f;
}

// Configuration methods
void LayoutManager::set_inspector_visible(bool visible) {
    inspector_visible_ = visible;
}

void LayoutManager::set_inspector_width(float width) {
    inspector_width_ = std::clamp(width, INSPECTOR_MIN_WIDTH, INSPECTOR_MAX_WIDTH);
    // During active drag, snap the animated width to avoid lag
    if (inspector_resize_active_ && inspector_visible_) {
        inspector_anim_width_ = inspector_width_;
        compute_zones();
    }
}

void LayoutManager::reset_inspector_width() {
    inspector_width_ = INSPECTOR_DEFAULT_WIDTH;
}

void LayoutManager::set_nav_rail_width(float width) {
    nav_rail_expanded_width_ = std::max(width, NAV_RAIL_COLLAPSED_WIDTH);
}

void LayoutManager::set_nav_rail_expanded(bool expanded) {
    nav_rail_expanded_ = expanded;
}

void LayoutManager::set_tab_bar_visible(bool visible) {
    tab_bar_visible_ = visible;
}

#if PLOTIX_FLOATING_TOOLBAR
void LayoutManager::set_floating_toolbar_visible(bool visible) {
    floating_toolbar_visible_ = visible;
    if (visible) {
        floating_toolbar_idle_timer_ = 0.0f;
    }
}

void LayoutManager::toggle_floating_toolbar() {
    set_floating_toolbar_visible(!floating_toolbar_visible_);
}

void LayoutManager::set_floating_toolbar_drag_offset(float dx, float dy) {
    floating_toolbar_offset_x_ = dx;
    floating_toolbar_offset_y_ = dy;
    floating_toolbar_has_custom_pos_ = true;
    compute_zones();
}

void LayoutManager::reset_floating_toolbar_position() {
    floating_toolbar_has_custom_pos_ = false;
    floating_toolbar_offset_x_ = 0.0f;
    floating_toolbar_offset_y_ = 0.0f;
    compute_zones();
}

void LayoutManager::notify_toolbar_activity() {
    floating_toolbar_idle_timer_ = 0.0f;
    if (floating_toolbar_visible_) {
        floating_toolbar_opacity_ = 1.0f;
    }
}
#endif

} // namespace plotix

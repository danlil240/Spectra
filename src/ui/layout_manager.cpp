#include "layout_manager.hpp"
#include <algorithm>
#include <cmath>

namespace plotix {

LayoutManager::LayoutManager() {
    // Initialize with default window size
    compute_zones();
}

void LayoutManager::update(float window_width, float window_height) {
    // Early exit if size hasn't changed
    if (std::abs(window_width_ - window_width) < 1.0f && 
        std::abs(window_height_ - window_height) < 1.0f) {
        return;
    }

    window_width_ = window_width;
    window_height_ = window_height;
    compute_zones();
}

void LayoutManager::compute_zones() {
    command_bar_rect_ = compute_command_bar();
    nav_rail_rect_ = compute_nav_rail();
    inspector_rect_ = compute_inspector();
    status_bar_rect_ = compute_status_bar();
    canvas_rect_ = compute_canvas();
    floating_toolbar_rect_ = compute_floating_toolbar();
}

Rect LayoutManager::compute_command_bar() const {
    return Rect{
        0.0f,                                    // x
        0.0f,                                    // y
        window_width_,                           // width
        COMMAND_BAR_HEIGHT                       // height
    };
}

Rect LayoutManager::compute_nav_rail() const {
    return Rect{
        0.0f,                                    // x
        COMMAND_BAR_HEIGHT,                      // y (below command bar)
        nav_rail_width(),                        // width
        window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT  // height
    };
}

Rect LayoutManager::compute_inspector() const {
    if (!inspector_visible_) {
        return Rect{window_width_, 0.0f, 0.0f, 0.0f};  // Hidden
    }

    return Rect{
        window_width_ - inspector_width_,        // x (right side)
        COMMAND_BAR_HEIGHT,                      // y (below command bar)
        inspector_width_,                        // width
        window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT  // height
    };
}

Rect LayoutManager::compute_status_bar() const {
    return Rect{
        0.0f,                                    // x
        window_height_ - STATUS_BAR_HEIGHT,      // y (bottom)
        window_width_,                           // width
        STATUS_BAR_HEIGHT                        // height
    };
}

Rect LayoutManager::compute_canvas() const {
    // Canvas occupies the remaining space between nav rail and inspector
    float canvas_x = nav_rail_width();
    float canvas_width = window_width_ - nav_rail_width();
    
    if (inspector_visible_) {
        canvas_width -= inspector_width_;
    }

    return Rect{
        canvas_x,                                // x
        COMMAND_BAR_HEIGHT,                      // y (below command bar)
        std::max(0.0f, canvas_width),           // width
        window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT  // height
    };
}

Rect LayoutManager::compute_floating_toolbar() const {
    // Centered at bottom of canvas area
    const Rect& canvas = canvas_rect();
    float toolbar_x = canvas.x + (canvas.w - FLOATING_TOOLBAR_WIDTH) * 0.5f;
    float toolbar_y = canvas.y + canvas.h - FLOATING_TOOLBAR_HEIGHT - 20.0f;  // 20px padding from bottom

    return Rect{
        toolbar_x,                               // x
        toolbar_y,                               // y
        FLOATING_TOOLBAR_WIDTH,                  // width
        FLOATING_TOOLBAR_HEIGHT                  // height
    };
}

// Zone rectangle getters
Rect LayoutManager::command_bar_rect() const {
    return command_bar_rect_;
}

Rect LayoutManager::nav_rail_rect() const {
    return nav_rail_rect_;
}

Rect LayoutManager::canvas_rect() const {
    return canvas_rect_;
}

Rect LayoutManager::inspector_rect() const {
    return inspector_rect_;
}

Rect LayoutManager::status_bar_rect() const {
    return status_bar_rect_;
}

Rect LayoutManager::floating_toolbar_rect() const {
    return floating_toolbar_rect_;
}

// Configuration methods
void LayoutManager::set_inspector_visible(bool visible) {
    if (inspector_visible_ != visible) {
        inspector_visible_ = visible;
        compute_zones();
    }
}

void LayoutManager::set_inspector_width(float width) {
    // Clamp to valid range
    inspector_width_ = std::clamp(width, INSPECTOR_MIN_WIDTH, INSPECTOR_MAX_WIDTH);
    compute_zones();
}

void LayoutManager::set_nav_rail_width(float width) {
    nav_rail_expanded_width_ = std::max(width, NAV_RAIL_COLLAPSED_WIDTH);
    compute_zones();
}

void LayoutManager::set_nav_rail_expanded(bool expanded) {
    if (nav_rail_expanded_ != expanded) {
        nav_rail_expanded_ = expanded;
        compute_zones();
    }
}

} // namespace plotix

#include "layout_manager.hpp"

#include <algorithm>
#include <cmath>

namespace spectra
{

LayoutManager::LayoutManager()
{
    compute_zones();
}

float LayoutManager::smooth_toward(float current, float target, float speed, float dt)
{
    if (dt <= 0.0f)
        return target;   // No dt → snap instantly
    float diff = target - current;
    if (std::abs(diff) < 0.5f)
        return target;   // Close enough → snap
    return current + diff * std::min(1.0f, speed * dt);
}

void LayoutManager::update(float window_width, float window_height, float dt)
{
    window_width_  = window_width;
    window_height_ = window_height;

    // Compute animation targets
    float inspector_target = inspector_visible_ ? inspector_width_ : 0.0f;
    float nav_rail_target =
        nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;

    // Animate toward targets
    inspector_anim_width_ = smooth_toward(inspector_anim_width_, inspector_target, ANIM_SPEED, dt);
    nav_rail_anim_width_  = smooth_toward(nav_rail_anim_width_, nav_rail_target, ANIM_SPEED, dt);

    compute_zones();
}

void LayoutManager::compute_zones()
{
    command_bar_rect_ = compute_command_bar();
    nav_rail_rect_    = compute_nav_rail();
    inspector_rect_   = compute_inspector();
    status_bar_rect_  = compute_status_bar();
    tab_bar_rect_     = compute_tab_bar();
    canvas_rect_      = compute_canvas();
}

Rect LayoutManager::compute_command_bar() const
{
    return Rect{0.0f, 0.0f, window_width_, COMMAND_BAR_HEIGHT};
}

Rect LayoutManager::compute_nav_rail() const
{
    float content_h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;
    return Rect{0.0f, COMMAND_BAR_HEIGHT, nav_rail_anim_width_, std::max(0.0f, content_h)};
}

Rect LayoutManager::compute_inspector() const
{
    if (inspector_anim_width_ < 1.0f)
    {
        return Rect{window_width_, COMMAND_BAR_HEIGHT, 0.0f, 0.0f};
    }
    float content_h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT;
    return Rect{window_width_ - inspector_anim_width_,
                COMMAND_BAR_HEIGHT,
                inspector_anim_width_,
                std::max(0.0f, content_h)};
}

Rect LayoutManager::compute_status_bar() const
{
    return Rect{0.0f, window_height_ - STATUS_BAR_HEIGHT, window_width_, STATUS_BAR_HEIGHT};
}

Rect LayoutManager::compute_tab_bar() const
{
    if (!tab_bar_visible_)
    {
        return Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
    float x = NAV_TOOLBAR_INSET + PLOT_LEFT_MARGIN;
    float w = window_width_ - x - inspector_anim_width_;
    return Rect{x, COMMAND_BAR_HEIGHT, std::max(0.0f, w), TAB_BAR_HEIGHT};
}

Rect LayoutManager::compute_canvas() const
{
    float x = NAV_TOOLBAR_INSET;
    float w = window_width_ - NAV_TOOLBAR_INSET - inspector_anim_width_;
    float y = COMMAND_BAR_HEIGHT;

    float h = window_height_ - COMMAND_BAR_HEIGHT - STATUS_BAR_HEIGHT - bottom_panel_height_;

    // Offset canvas below tab bar when visible
    if (tab_bar_visible_)
    {
        y += TAB_BAR_HEIGHT;
        h -= TAB_BAR_HEIGHT;
    }

    return Rect{x, y, std::max(0.0f, w), std::max(0.0f, h)};
}

// Zone rectangle getters
Rect LayoutManager::command_bar_rect() const
{
    return command_bar_rect_;
}
Rect LayoutManager::nav_rail_rect() const
{
    return nav_rail_rect_;
}
Rect LayoutManager::canvas_rect() const
{
    return canvas_rect_;
}
Rect LayoutManager::inspector_rect() const
{
    return inspector_rect_;
}
Rect LayoutManager::status_bar_rect() const
{
    return status_bar_rect_;
}

Rect LayoutManager::tab_bar_rect() const
{
    return tab_bar_rect_;
}

float LayoutManager::nav_rail_width() const
{
    return nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;
}

bool LayoutManager::is_animating() const
{
    float inspector_target = inspector_visible_ ? inspector_width_ : 0.0f;
    float nav_target = nav_rail_expanded_ ? nav_rail_expanded_width_ : nav_rail_collapsed_width_;
    return std::abs(inspector_anim_width_ - inspector_target) > 0.5f
           || std::abs(nav_rail_anim_width_ - nav_target) > 0.5f;
}

// Configuration methods
void LayoutManager::set_inspector_visible(bool visible)
{
    inspector_visible_ = visible;
}

void LayoutManager::set_inspector_width(float width)
{
    inspector_width_ = std::clamp(width, INSPECTOR_MIN_WIDTH, INSPECTOR_MAX_WIDTH);
    // During active drag, snap the animated width to avoid lag
    if (inspector_resize_active_ && inspector_visible_)
    {
        inspector_anim_width_ = inspector_width_;
        compute_zones();
    }
}

void LayoutManager::reset_inspector_width()
{
    inspector_width_ = INSPECTOR_DEFAULT_WIDTH;
}

void LayoutManager::set_nav_rail_width(float width)
{
    nav_rail_expanded_width_ = std::max(width, NAV_RAIL_COLLAPSED_WIDTH);
}

void LayoutManager::set_nav_rail_expanded(bool expanded)
{
    nav_rail_expanded_ = expanded;
}

void LayoutManager::set_tab_bar_visible(bool visible)
{
    tab_bar_visible_ = visible;
}

}   // namespace spectra

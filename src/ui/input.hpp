#pragma once

#include <plotix/axes.hpp>

namespace plotix {

// Input handler: maps mouse events to axis limit mutations (pan/zoom).
class InputHandler {
public:
    InputHandler() = default;

    // Set the active axes that input events will affect
    void set_active_axes(Axes* axes) { active_axes_ = axes; }
    Axes* active_axes() const { return active_axes_; }

    // Mouse button event: begin/end drag
    void on_mouse_button(int button, int action, double x, double y);

    // Mouse move event: pan if dragging
    void on_mouse_move(double x, double y);

    // Scroll event: zoom around cursor position
    void on_scroll(double x_offset, double y_offset, double cursor_x, double cursor_y);

    // Set the viewport rect for coordinate mapping
    void set_viewport(float vp_x, float vp_y, float vp_w, float vp_h);

private:
    // Convert screen coordinates to data coordinates
    void screen_to_data(double screen_x, double screen_y,
                        float& data_x, float& data_y) const;

    Axes* active_axes_ = nullptr;

    // Drag state
    bool  dragging_    = false;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    float  drag_start_xlim_min_ = 0.0f;
    float  drag_start_xlim_max_ = 0.0f;
    float  drag_start_ylim_min_ = 0.0f;
    float  drag_start_ylim_max_ = 0.0f;

    // Viewport for coordinate mapping
    float vp_x_ = 0.0f;
    float vp_y_ = 0.0f;
    float vp_w_ = 1.0f;
    float vp_h_ = 1.0f;

    // Zoom factor per scroll tick
    static constexpr float ZOOM_FACTOR = 0.1f;
};

} // namespace plotix

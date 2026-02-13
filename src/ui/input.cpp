#include "input.hpp"

#include <algorithm>
#include <cmath>

namespace plotix {

// Mouse button constants (matching GLFW)
namespace {
    constexpr int MOUSE_BUTTON_LEFT = 0;
    constexpr int ACTION_PRESS   = 1;
    constexpr int ACTION_RELEASE = 0;
} // anonymous namespace

void InputHandler::on_mouse_button(int button, int action, double x, double y) {
    if (!active_axes_) return;

    if (button == MOUSE_BUTTON_LEFT) {
        if (action == ACTION_PRESS) {
            dragging_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;

            auto xlim = active_axes_->x_limits();
            auto ylim = active_axes_->y_limits();
            drag_start_xlim_min_ = xlim.min;
            drag_start_xlim_max_ = xlim.max;
            drag_start_ylim_min_ = ylim.min;
            drag_start_ylim_max_ = ylim.max;
        } else if (action == ACTION_RELEASE) {
            dragging_ = false;
        }
    }
}

void InputHandler::on_mouse_move(double x, double y) {
    if (!active_axes_ || !dragging_) return;

    // Compute drag delta in screen pixels
    double dx_screen = x - drag_start_x_;
    double dy_screen = y - drag_start_y_;

    // Convert pixel delta to data-space delta
    float x_range = drag_start_xlim_max_ - drag_start_xlim_min_;
    float y_range = drag_start_ylim_max_ - drag_start_ylim_min_;

    float dx_data = -static_cast<float>(dx_screen) * x_range / vp_w_;
    float dy_data =  static_cast<float>(dy_screen) * y_range / vp_h_; // Y is inverted (screen Y goes down)

    active_axes_->xlim(drag_start_xlim_min_ + dx_data,
                       drag_start_xlim_max_ + dx_data);
    active_axes_->ylim(drag_start_ylim_min_ + dy_data,
                       drag_start_ylim_max_ + dy_data);
}

void InputHandler::on_scroll(double /*x_offset*/, double y_offset,
                              double cursor_x, double cursor_y) {
    if (!active_axes_) return;

    auto xlim = active_axes_->x_limits();
    auto ylim = active_axes_->y_limits();

    // Compute cursor position in data space
    float data_x, data_y;
    screen_to_data(cursor_x, cursor_y, data_x, data_y);

    // Zoom factor: scroll up = zoom in (shrink range), scroll down = zoom out
    float factor = 1.0f - static_cast<float>(y_offset) * ZOOM_FACTOR;
    factor = std::clamp(factor, 0.1f, 10.0f);

    // Scale limits around the cursor position
    float new_xmin = data_x + (xlim.min - data_x) * factor;
    float new_xmax = data_x + (xlim.max - data_x) * factor;
    float new_ymin = data_y + (ylim.min - data_y) * factor;
    float new_ymax = data_y + (ylim.max - data_y) * factor;

    active_axes_->xlim(new_xmin, new_xmax);
    active_axes_->ylim(new_ymin, new_ymax);
}

void InputHandler::set_viewport(float vp_x, float vp_y, float vp_w, float vp_h) {
    vp_x_ = vp_x;
    vp_y_ = vp_y;
    vp_w_ = vp_w;
    vp_h_ = vp_h;
}

void InputHandler::screen_to_data(double screen_x, double screen_y,
                                   float& data_x, float& data_y) const {
    if (!active_axes_) {
        data_x = 0.0f;
        data_y = 0.0f;
        return;
    }

    auto xlim = active_axes_->x_limits();
    auto ylim = active_axes_->y_limits();

    // Normalize screen position within viewport [0, 1]
    float norm_x = (static_cast<float>(screen_x) - vp_x_) / vp_w_;
    float norm_y = (static_cast<float>(screen_y) - vp_y_) / vp_h_;

    // Invert Y (screen Y goes down, data Y goes up)
    norm_y = 1.0f - norm_y;

    // Map to data space
    data_x = xlim.min + norm_x * (xlim.max - xlim.min);
    data_y = ylim.min + norm_y * (ylim.max - ylim.min);
}

} // namespace plotix

#include "input.hpp"
#include "animation_controller.hpp"
#include "data_interaction.hpp"
#include "gesture_recognizer.hpp"
#include <plotix/logger.hpp>

#include <algorithm>
#include <cmath>

namespace plotix {

// Mouse button constants (matching GLFW)
namespace {
    constexpr int MOUSE_BUTTON_LEFT  = 0;
    constexpr int MOUSE_BUTTON_RIGHT = 1;
    constexpr int ACTION_PRESS   = 1;
    constexpr int ACTION_RELEASE = 0;
} // anonymous namespace

// ─── Hit-testing ────────────────────────────────────────────────────────────

Axes* InputHandler::hit_test_axes(double screen_x, double screen_y) const {
    if (!figure_) return nullptr;

    for (auto& axes_ptr : figure_->axes()) {
        if (!axes_ptr) continue;
        const auto& vp = axes_ptr->viewport();
        if (static_cast<float>(screen_x) >= vp.x &&
            static_cast<float>(screen_x) <= vp.x + vp.w &&
            static_cast<float>(screen_y) >= vp.y &&
            static_cast<float>(screen_y) <= vp.y + vp.h) {
            return axes_ptr.get();
        }
    }
    return nullptr;
}

const Rect& InputHandler::viewport_for_axes(const Axes* axes) const {
    if (axes) {
        return axes->viewport();
    }
    // Fallback: return a static rect built from stored viewport values
    static Rect fallback;
    fallback = {vp_x_, vp_y_, vp_w_, vp_h_};
    return fallback;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

InputHandler::InputHandler() = default;
InputHandler::~InputHandler() = default;

// ─── Mouse button ───────────────────────────────────────────────────────────

void InputHandler::on_mouse_button(int button, int action, double x, double y) {
    PLOTIX_LOG_DEBUG("input", "Mouse button event - button: " + std::to_string(button) + 
                      ", action: " + std::to_string(action) + 
                      ", pos: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
    
    // Hit-test to find which axes the cursor is over
    Axes* hit = hit_test_axes(x, y);
    if (hit) {
        PLOTIX_LOG_DEBUG("input", "Mouse hit axes - setting active axes");
        active_axes_ = hit;
    }

    if (!active_axes_) return;

    if (button == MOUSE_BUTTON_LEFT) {
        if (action == ACTION_PRESS && mode_ == InteractionMode::Idle && tool_mode_ == ToolMode::Pan) {
            // Cancel any running animations on this axes (new input overrides)
            if (anim_ctrl_) {
                anim_ctrl_->cancel_for_axes(active_axes_);
            }

            // Double-click detection: auto-fit with animated transition
            if (gesture_) {
                bool is_double = gesture_->on_click(x, y);
                if (is_double && anim_ctrl_) {
                    PLOTIX_LOG_DEBUG("input", "Double-click detected — animated auto-fit");
                    // Compute auto-fit target limits
                    auto old_xlim = active_axes_->x_limits();
                    auto old_ylim = active_axes_->y_limits();
                    active_axes_->auto_fit();
                    AxisLimits target_x = active_axes_->x_limits();
                    AxisLimits target_y = active_axes_->y_limits();
                    // Restore current limits so animation can interpolate
                    active_axes_->xlim(old_xlim.min, old_xlim.max);
                    active_axes_->ylim(old_ylim.min, old_ylim.max);
                    anim_ctrl_->animate_axis_limits(
                        *active_axes_, target_x, target_y,
                        AUTOFIT_ANIM_DURATION, ease::ease_out);
                    return; // Don't start a pan drag on double-click
                }
            }

            // Begin pan drag
            PLOTIX_LOG_DEBUG("input", "Starting pan drag");
            mode_ = InteractionMode::Dragging;
            drag_start_x_ = x;
            drag_start_y_ = y;
            last_move_x_ = x;
            last_move_y_ = y;
            last_move_time_ = Clock::now();
            drag_start_time_ = last_move_time_;

            auto xlim = active_axes_->x_limits();
            auto ylim = active_axes_->y_limits();
            drag_start_xlim_min_ = xlim.min;
            drag_start_xlim_max_ = xlim.max;
            drag_start_ylim_min_ = ylim.min;
            drag_start_ylim_max_ = ylim.max;
        } else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging && tool_mode_ == ToolMode::Pan) {
            PLOTIX_LOG_DEBUG("input", "Ending pan drag");
            mode_ = InteractionMode::Idle;

            // Compute release velocity for inertial pan
            if (anim_ctrl_ && active_axes_) {
                auto now = Clock::now();
                float dt_sec = std::chrono::duration<float>(now - last_move_time_).count();
                float drag_total = std::chrono::duration<float>(now - drag_start_time_).count();

                // Only apply inertia if the drag was short and recent movement exists
                if (dt_sec < 0.1f && dt_sec > 0.0f && drag_total > 0.05f) {
                    const auto& vp = viewport_for_axes(active_axes_);
                    auto xlim = active_axes_->x_limits();
                    auto ylim = active_axes_->y_limits();

                    float x_range = xlim.max - xlim.min;
                    float y_range = ylim.max - ylim.min;

                    // Screen velocity → data velocity
                    float vx_screen = static_cast<float>(x - last_move_x_) / dt_sec;
                    float vy_screen = static_cast<float>(y - last_move_y_) / dt_sec;

                    float vx_data = -vx_screen * x_range / vp.w;
                    float vy_data =  vy_screen * y_range / vp.h;

                    float speed = std::sqrt(vx_data * vx_data + vy_data * vy_data);
                    if (speed > MIN_INERTIA_VELOCITY) {
                        PLOTIX_LOG_DEBUG("input", "Inertial pan: v=(" +
                            std::to_string(vx_data) + ", " + std::to_string(vy_data) + ")");
                        anim_ctrl_->animate_inertial_pan(
                            *active_axes_, vx_data, vy_data, PAN_INERTIA_DURATION);
                    }
                }
            }
        }
    } else if (button == MOUSE_BUTTON_RIGHT) {
        if (action == ACTION_PRESS && mode_ == InteractionMode::Idle && tool_mode_ == ToolMode::BoxZoom) {
            // Cancel any running animations on this axes
            if (anim_ctrl_) {
                anim_ctrl_->cancel_for_axes(active_axes_);
            }
            // Begin box zoom
            PLOTIX_LOG_DEBUG("input", "Starting box zoom");
            mode_ = InteractionMode::Dragging;
            box_zoom_.active = true;
            box_zoom_.x0 = x;
            box_zoom_.y0 = y;
            box_zoom_.x1 = x;
            box_zoom_.y1 = y;
        } else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging && tool_mode_ == ToolMode::BoxZoom) {
            PLOTIX_LOG_DEBUG("input", "Ending box zoom");
            apply_box_zoom();
            mode_ = InteractionMode::Idle;
        }
    }
}

// ─── Mouse move ─────────────────────────────────────────────────────────────

void InputHandler::on_mouse_move(double x, double y) {
    PLOTIX_LOG_TRACE("input", "Mouse move event - pos: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
    
    // Update cursor readout regardless of mode
    Axes* hit = hit_test_axes(x, y);
    if (hit) {
        PLOTIX_LOG_TRACE("input", "Mouse move hit axes");
        // Temporarily use hit axes for screen_to_data conversion
        Axes* prev = active_axes_;
        active_axes_ = hit;
        const auto& vp = viewport_for_axes(hit);
        float saved_vp_x = vp_x_, saved_vp_y = vp_y_;
        float saved_vp_w = vp_w_, saved_vp_h = vp_h_;
        vp_x_ = vp.x; vp_y_ = vp.y; vp_w_ = vp.w; vp_h_ = vp.h;

        cursor_readout_.valid = true;
        cursor_readout_.screen_x = x;
        cursor_readout_.screen_y = y;
        screen_to_data(x, y, cursor_readout_.data_x, cursor_readout_.data_y);

        // Restore if we were in a drag with a different axes
        if (mode_ == InteractionMode::Dragging) {
            active_axes_ = prev;
            vp_x_ = saved_vp_x; vp_y_ = saved_vp_y;
            vp_w_ = saved_vp_w; vp_h_ = saved_vp_h;
        } else {
            // In idle mode, update active axes to hovered one
            active_axes_ = hit;
            // Keep viewport in sync with the new active axes
            vp_x_ = vp.x; vp_y_ = vp.y; vp_w_ = vp.w; vp_h_ = vp.h;
        }
    } else {
        cursor_readout_.valid = false;
    }

    if (!active_axes_) return;

    if (mode_ == InteractionMode::Dragging) {
        if (tool_mode_ == ToolMode::Pan) {
            // Track velocity for inertial pan
            last_move_x_ = x;
            last_move_y_ = y;
            last_move_time_ = Clock::now();

            // Pan logic
            const auto& vp = viewport_for_axes(active_axes_);

            // Compute drag delta in screen pixels
            double dx_screen = x - drag_start_x_;
            double dy_screen = y - drag_start_y_;

            // Convert pixel delta to data-space delta
            float x_range = drag_start_xlim_max_ - drag_start_xlim_min_;
            float y_range = drag_start_ylim_max_ - drag_start_ylim_min_;

            float dx_data = -static_cast<float>(dx_screen) * x_range / vp.w;
            float dy_data =  static_cast<float>(dy_screen) * y_range / vp.h;

            active_axes_->xlim(drag_start_xlim_min_ + dx_data,
                               drag_start_xlim_max_ + dx_data);
            active_axes_->ylim(drag_start_ylim_min_ + dy_data,
                               drag_start_ylim_max_ + dy_data);
        } else if (tool_mode_ == ToolMode::BoxZoom) {
            // Box zoom logic
            box_zoom_.x1 = x;
            box_zoom_.y1 = y;
        }
    }
}

// ─── Scroll ─────────────────────────────────────────────────────────────────

void InputHandler::on_scroll(double /*x_offset*/, double y_offset,
                              double cursor_x, double cursor_y) {
    // Hit-test to zoom the axes under cursor
    Axes* hit = hit_test_axes(cursor_x, cursor_y);
    if (hit) {
        active_axes_ = hit;
    }
    if (!active_axes_) return;

    const auto& vp = viewport_for_axes(active_axes_);

    // Cancel any running animations — new scroll input takes priority
    if (anim_ctrl_) {
        anim_ctrl_->cancel_for_axes(active_axes_);
    }

    auto xlim = active_axes_->x_limits();
    auto ylim = active_axes_->y_limits();

    // Compute cursor position in data space
    float saved_vp_x = vp_x_, saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_, saved_vp_h = vp_h_;
    vp_x_ = vp.x; vp_y_ = vp.y; vp_w_ = vp.w; vp_h_ = vp.h;

    float data_x, data_y;
    screen_to_data(cursor_x, cursor_y, data_x, data_y);

    vp_x_ = saved_vp_x; vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w; vp_h_ = saved_vp_h;

    // Exponential zoom: symmetric in both directions.
    // scroll up (y_offset>0) → factor<1 (zoom in), scroll down → factor>1 (zoom out)
    float factor = std::pow(1.0f / (1.0f + ZOOM_FACTOR), static_cast<float>(y_offset));
    factor = std::clamp(factor, 0.1f, 10.0f);

    // Apply zoom instantly — scroll zoom must be immediate and responsive.
    // (Animations are used for auto-fit, box zoom, and inertial pan instead.)
    float new_xmin = data_x + (xlim.min - data_x) * factor;
    float new_xmax = data_x + (xlim.max - data_x) * factor;
    float new_ymin = data_y + (ylim.min - data_y) * factor;
    float new_ymax = data_y + (ylim.max - data_y) * factor;

    active_axes_->xlim(new_xmin, new_xmax);
    active_axes_->ylim(new_ymin, new_ymax);
}

// ─── Keyboard ───────────────────────────────────────────────────────────────

void InputHandler::on_key(int key, int action, int mods) {
    if (action != ACTION_PRESS) return;

    if (key == KEY_ESCAPE) {
        // Cancel box zoom if active
        cancel_box_zoom();
        return;
    }

    if (key == KEY_R && !(mods & MOD_CONTROL)) {
        // Reset view: animated auto-fit all axes in the figure
        if (figure_) {
            for (auto& axes_ptr : figure_->axes()) {
                if (axes_ptr && anim_ctrl_) {
                    auto old_xlim = axes_ptr->x_limits();
                    auto old_ylim = axes_ptr->y_limits();
                    axes_ptr->auto_fit();
                    AxisLimits target_x = axes_ptr->x_limits();
                    AxisLimits target_y = axes_ptr->y_limits();
                    axes_ptr->xlim(old_xlim.min, old_xlim.max);
                    axes_ptr->ylim(old_ylim.min, old_ylim.max);
                    anim_ctrl_->animate_axis_limits(
                        *axes_ptr, target_x, target_y,
                        AUTOFIT_ANIM_DURATION, ease::ease_out);
                } else if (axes_ptr) {
                    axes_ptr->auto_fit();
                }
            }
        } else if (active_axes_) {
            if (anim_ctrl_) {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                active_axes_->xlim(old_xlim.min, old_xlim.max);
                active_axes_->ylim(old_ylim.min, old_ylim.max);
                anim_ctrl_->animate_axis_limits(
                    *active_axes_, target_x, target_y,
                    AUTOFIT_ANIM_DURATION, ease::ease_out);
            } else {
                active_axes_->auto_fit();
            }
        }
        return;
    }

    if (key == KEY_G && !(mods & MOD_CONTROL)) {
        // Toggle grid on active axes
        if (active_axes_) {
            active_axes_->grid(!active_axes_->grid_enabled());
        }
        return;
    }

    if (key == KEY_S && (mods & MOD_CONTROL)) {
        // Ctrl+S: save PNG
        if (save_callback_) {
            save_callback_();
        }
        return;
    }

    if (key == KEY_C && !(mods & MOD_CONTROL)) {
        // Toggle crosshair overlay
        if (data_interaction_) {
            data_interaction_->toggle_crosshair();
            PLOTIX_LOG_DEBUG("input", "Crosshair toggled: " +
                std::string(data_interaction_->crosshair_active() ? "ON" : "OFF"));
        }
        return;
    }

    if (key == KEY_A && !(mods & MOD_CONTROL)) {
        // Animated auto-fit active axes only
        if (active_axes_) {
            if (anim_ctrl_) {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                active_axes_->xlim(old_xlim.min, old_xlim.max);
                active_axes_->ylim(old_ylim.min, old_ylim.max);
                anim_ctrl_->animate_axis_limits(
                    *active_axes_, target_x, target_y,
                    AUTOFIT_ANIM_DURATION, ease::ease_out);
            } else {
                active_axes_->auto_fit();
            }
        }
        return;
    }
}

// ─── Box zoom ───────────────────────────────────────────────────────────────

void InputHandler::apply_box_zoom() {
    if (!active_axes_ || !box_zoom_.active) {
        cancel_box_zoom();
        return;
    }

    const auto& vp = viewport_for_axes(active_axes_);

    // Convert box corners from screen to data space
    float saved_vp_x = vp_x_, saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_, saved_vp_h = vp_h_;
    vp_x_ = vp.x; vp_y_ = vp.y; vp_w_ = vp.w; vp_h_ = vp.h;

    float d_x0, d_y0, d_x1, d_y1;
    screen_to_data(box_zoom_.x0, box_zoom_.y0, d_x0, d_y0);
    screen_to_data(box_zoom_.x1, box_zoom_.y1, d_x1, d_y1);

    vp_x_ = saved_vp_x; vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w; vp_h_ = saved_vp_h;

    // Ensure min < max
    float xmin = std::min(d_x0, d_x1);
    float xmax = std::max(d_x0, d_x1);
    float ymin = std::min(d_y0, d_y1);
    float ymax = std::max(d_y0, d_y1);

    // Only apply if the selection is large enough (avoid accidental clicks)
    constexpr float MIN_SELECTION_PIXELS = 5.0f;
    float dx_screen = static_cast<float>(std::abs(box_zoom_.x1 - box_zoom_.x0));
    float dy_screen = static_cast<float>(std::abs(box_zoom_.y1 - box_zoom_.y0));

    if (dx_screen > MIN_SELECTION_PIXELS && dy_screen > MIN_SELECTION_PIXELS) {
        // Animated box zoom transition
        if (anim_ctrl_) {
            AxisLimits target_x{xmin, xmax};
            AxisLimits target_y{ymin, ymax};
            anim_ctrl_->animate_axis_limits(
                *active_axes_, target_x, target_y,
                ZOOM_ANIM_DURATION, ease::ease_out);
        } else {
            active_axes_->xlim(xmin, xmax);
            active_axes_->ylim(ymin, ymax);
        }
    }

    box_zoom_.active = false;
}

void InputHandler::cancel_box_zoom() {
    if (mode_ == InteractionMode::Dragging && tool_mode_ == ToolMode::BoxZoom) {
        mode_ = InteractionMode::Idle;
    }
    box_zoom_.active = false;
}

// ─── Viewport ───────────────────────────────────────────────────────────────

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

// ─── Per-frame update ───────────────────────────────────────────────────────

void InputHandler::update(float dt) {
    if (anim_ctrl_) {
        anim_ctrl_->update(dt);
    }
}

// ─── Animation query ────────────────────────────────────────────────────────

bool InputHandler::has_active_animations() const {
    return anim_ctrl_ && anim_ctrl_->has_active_animations();
}

} // namespace plotix

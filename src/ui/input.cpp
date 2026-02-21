#include "input.hpp"

#include <algorithm>
#include <cmath>
#include <spectra/logger.hpp>

#include "animation_controller.hpp"
#include "axis_link.hpp"
#include "data_interaction.hpp"
#include "gesture_recognizer.hpp"
#include "shortcut_manager.hpp"
#include "transition_engine.hpp"

namespace spectra
{

// Mouse button constants (matching GLFW)
namespace
{
constexpr int MOUSE_BUTTON_LEFT = 0;
constexpr int MOUSE_BUTTON_RIGHT = 1;
constexpr int ACTION_PRESS = 1;
constexpr int ACTION_RELEASE = 0;
}  // anonymous namespace

// ─── Tool mode ──────────────────────────────────────────────────────────────

void InputHandler::set_tool_mode(ToolMode new_tool)
{
    if (new_tool == tool_mode_)
        return;

    // Leaving Select mode: dismiss region selection
    if (tool_mode_ == ToolMode::Select)
    {
        if (data_interaction_)
            data_interaction_->dismiss_region_select();
        region_dragging_ = false;
    }

    // Leaving Measure mode: restore previous crosshair state, reset measure
    if (tool_mode_ == ToolMode::Measure)
    {
        if (data_interaction_)
            data_interaction_->set_crosshair(crosshair_was_active_);
        measure_dragging_ = false;
        measure_click_state_ = 0;
    }

    // Entering Measure mode: auto-enable crosshair
    if (new_tool == ToolMode::Measure)
    {
        if (data_interaction_)
        {
            crosshair_was_active_ = data_interaction_->crosshair_active();
            data_interaction_->set_crosshair(true);
        }
    }

    tool_mode_ = new_tool;
}

// ─── Hit-testing ────────────────────────────────────────────────────────────

Axes* InputHandler::hit_test_axes(double screen_x, double screen_y) const
{
    if (!figure_)
        return nullptr;

    for (auto& axes_ptr : figure_->axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (static_cast<float>(screen_x) >= vp.x && static_cast<float>(screen_x) <= vp.x + vp.w
            && static_cast<float>(screen_y) >= vp.y && static_cast<float>(screen_y) <= vp.y + vp.h)
        {
            return axes_ptr.get();
        }
    }
    return nullptr;
}

const Rect& InputHandler::viewport_for_axes(const AxesBase* axes) const
{
    if (axes)
    {
        return axes->viewport();
    }
    // Fallback: return a static rect built from stored viewport values
    static Rect fallback;
    fallback = {vp_x_, vp_y_, vp_w_, vp_h_};
    return fallback;
}

AxesBase* InputHandler::hit_test_all_axes(double screen_x, double screen_y) const
{
    if (!figure_)
        return nullptr;

    for (auto& axes_ptr : figure_->all_axes())
    {
        if (!axes_ptr)
            continue;
        const auto& vp = axes_ptr->viewport();
        if (static_cast<float>(screen_x) >= vp.x && static_cast<float>(screen_x) <= vp.x + vp.w
            && static_cast<float>(screen_y) >= vp.y && static_cast<float>(screen_y) <= vp.y + vp.h)
        {
            return axes_ptr.get();
        }
    }
    return nullptr;
}

// ─── Constructor / Destructor ────────────────────────────────────────────────

InputHandler::InputHandler() = default;
InputHandler::~InputHandler() = default;

// ─── Mouse button ───────────────────────────────────────────────────────────

void InputHandler::on_mouse_button(int button, int action, int mods, double x, double y)
{
    // Update modifier state from the authoritative GLFW mods bitmask
    mods_ = mods;

    // SPECTRA_LOG_DEBUG("input",
    //                  "Mouse button event - button: " + std::to_string(button)
    //                      + ", action: " + std::to_string(action) + ", mods: " +
    //                      std::to_string(mods)
    //                      + ", pos: (" + std::to_string(x) + ", " + std::to_string(y) + ")");

    // Hit-test all axes (including 3D) first
    AxesBase* hit_base = hit_test_all_axes(x, y);
    if (hit_base)
    {
        active_axes_base_ = hit_base;
        active_axes_ = dynamic_cast<Axes*>(hit_base);
    }

    // Handle 3D axes camera interaction
    if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
    {
        if (button == MOUSE_BUTTON_LEFT && action == ACTION_PRESS)
        {
            is_3d_orbit_drag_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            mode_ = InteractionMode::Dragging;
            return;
        }
        if (button == MOUSE_BUTTON_LEFT && action == ACTION_RELEASE && is_3d_orbit_drag_)
        {
            is_3d_orbit_drag_ = false;
            mode_ = InteractionMode::Idle;
            return;
        }
        if (button == MOUSE_BUTTON_RIGHT && action == ACTION_PRESS)
        {
            is_3d_pan_drag_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            mode_ = InteractionMode::Dragging;
            return;
        }
        if (button == MOUSE_BUTTON_RIGHT && action == ACTION_RELEASE && is_3d_pan_drag_)
        {
            is_3d_pan_drag_ = false;
            mode_ = InteractionMode::Idle;
            return;
        }
        if (button == MOUSE_BUTTON_MIDDLE && action == ACTION_PRESS)
        {
            is_3d_pan_drag_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            mode_ = InteractionMode::Dragging;
            return;
        }
        if (button == MOUSE_BUTTON_MIDDLE && action == ACTION_RELEASE && is_3d_pan_drag_)
        {
            is_3d_pan_drag_ = false;
            mode_ = InteractionMode::Idle;
            return;
        }
        return;  // 3D axes don't support other interactions
    }

    // 2D hit-test (fallback for callers that need Axes*)
    Axes* hit = hit_test_axes(x, y);
    if (hit)
    {
        active_axes_ = hit;
        active_axes_base_ = hit;
        // Sync viewport so screen_to_data works correctly for this axes
        const auto& vp = hit->viewport();
        vp_x_ = vp.x;
        vp_y_ = vp.y;
        vp_w_ = vp.w;
        vp_h_ = vp.h;
    }

    if (!active_axes_)
        return;

    // Middle-mouse pan: works in ALL tool modes for 2D axes
    if (button == MOUSE_BUTTON_MIDDLE)
    {
        if (action == ACTION_PRESS && !middle_pan_dragging_ && active_axes_)
        {
            // Cancel any running animations
            if (transition_engine_)
                transition_engine_->cancel_for_axes(active_axes_);
            else if (anim_ctrl_)
                anim_ctrl_->cancel_for_axes(active_axes_);

            middle_pan_dragging_ = true;
            middle_pan_start_x_ = x;
            middle_pan_start_y_ = y;
            auto xlim = active_axes_->x_limits();
            auto ylim = active_axes_->y_limits();
            middle_pan_xlim_min_ = xlim.min;
            middle_pan_xlim_max_ = xlim.max;
            middle_pan_ylim_min_ = ylim.min;
            middle_pan_ylim_max_ = ylim.max;
            return;
        }
        if (action == ACTION_RELEASE && middle_pan_dragging_)
        {
            middle_pan_dragging_ = false;
            return;
        }
    }

    // Measure mode: left-click/drag to measure distance between two data points
    // Supports both drag (press-move-release) and two-click (click, move, click)
    if (button == MOUSE_BUTTON_LEFT && tool_mode_ == ToolMode::Measure)
    {
        if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
        {
            if (measure_click_state_ == 1)
            {
                // Second click: finalize measurement at this point
                SPECTRA_LOG_DEBUG("input", "Measure: second click placed");
                screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
                measure_click_state_ = 2;
                return;
            }

            // First press: start measurement (could be drag or first click)
            SPECTRA_LOG_DEBUG("input", "Starting measure (press)");
            measure_dragging_ = true;
            measure_click_state_ = 0;
            measure_start_screen_x_ = x;
            measure_start_screen_y_ = y;
            screen_to_data(x, y, measure_start_data_x_, measure_start_data_y_);
            measure_end_data_x_ = measure_start_data_x_;
            measure_end_data_y_ = measure_start_data_y_;
            mode_ = InteractionMode::Dragging;
            return;
        }
        if (action == ACTION_RELEASE && measure_dragging_)
        {
            screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
            measure_dragging_ = false;
            mode_ = InteractionMode::Idle;

            // Check if the mouse barely moved — treat as a click (first point)
            float dx_px = static_cast<float>(x - measure_start_screen_x_);
            float dy_px = static_cast<float>(y - measure_start_screen_y_);
            float move_dist = std::sqrt(dx_px * dx_px + dy_px * dy_px);
            constexpr float CLICK_THRESHOLD_PX = 5.0f;
            if (move_dist < CLICK_THRESHOLD_PX)
            {
                // This was a click, not a drag — enter two-click mode
                SPECTRA_LOG_DEBUG("input", "Measure: first click placed (two-click mode)");
                measure_click_state_ = 1;
            }
            else
            {
                // This was a drag — measurement is complete
                SPECTRA_LOG_DEBUG("input", "Finishing measure drag");
                measure_click_state_ = 2;
            }
            return;
        }
    }

    if (button == MOUSE_BUTTON_LEFT)
    {
        // Select mode: left-drag for region selection
        if (action == ACTION_PRESS && mode_ == InteractionMode::Idle
            && tool_mode_ == ToolMode::Select)
        {
            if (data_interaction_)
            {
                SPECTRA_LOG_DEBUG("input", "Starting region selection (Select mode)");
                data_interaction_->begin_region_select(x, y);
                region_dragging_ = true;
                return;
            }
        }

        if (action == ACTION_RELEASE && region_dragging_ && tool_mode_ == ToolMode::Select)
        {
            if (data_interaction_)
            {
                SPECTRA_LOG_DEBUG("input", "Finishing region selection");
                data_interaction_->finish_region_select();
            }
            region_dragging_ = false;
            return;
        }

        // BoxZoom tool mode: left-click to draw box zoom rectangle
        if (action == ACTION_PRESS && mode_ == InteractionMode::Idle
            && tool_mode_ == ToolMode::BoxZoom && active_axes_)
        {
            // Cancel any running animations on this axes
            if (transition_engine_)
            {
                transition_engine_->cancel_for_axes(active_axes_);
            }
            else if (anim_ctrl_)
            {
                anim_ctrl_->cancel_for_axes(active_axes_);
            }
            SPECTRA_LOG_DEBUG("input", "Starting box zoom (BoxZoom tool)");
            mode_ = InteractionMode::Dragging;
            box_zoom_.active = true;
            box_zoom_.x0 = x;
            box_zoom_.y0 = y;
            box_zoom_.x1 = x;
            box_zoom_.y1 = y;
            return;
        }

        if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging
            && tool_mode_ == ToolMode::BoxZoom)
        {
            SPECTRA_LOG_DEBUG("input", "Ending box zoom (BoxZoom tool)");
            apply_box_zoom();
            mode_ = InteractionMode::Idle;
            return;
        }

        if (action == ACTION_PRESS && mode_ == InteractionMode::Idle && tool_mode_ == ToolMode::Pan)
        {
            // Cancel any running animations on this axes (new input overrides)
            if (transition_engine_)
            {
                transition_engine_->cancel_for_axes(active_axes_);
            }
            else if (anim_ctrl_)
            {
                anim_ctrl_->cancel_for_axes(active_axes_);
            }

            // Ctrl+left-click in Pan mode → begin box zoom
            if (mods & MOD_CONTROL)
            {
                SPECTRA_LOG_DEBUG("input", "Ctrl+left-click — starting box zoom in Pan mode");
                mode_ = InteractionMode::Dragging;
                ctrl_box_zoom_active_ = true;
                box_zoom_.active = true;
                box_zoom_.x0 = x;
                box_zoom_.y0 = y;
                box_zoom_.x1 = x;
                box_zoom_.y1 = y;
                return;
            }

            // Double-click detection: auto-fit with animated transition
            if (gesture_)
            {
                bool is_double = gesture_->on_click(x, y);
                if (is_double && (transition_engine_ || anim_ctrl_))
                {
                    SPECTRA_LOG_DEBUG("input", "Double-click detected — animated auto-fit");
                    // Compute auto-fit target limits
                    auto old_xlim = active_axes_->x_limits();
                    auto old_ylim = active_axes_->y_limits();
                    active_axes_->auto_fit();
                    AxisLimits target_x = active_axes_->x_limits();
                    AxisLimits target_y = active_axes_->y_limits();
                    // Restore current limits so animation can interpolate
                    active_axes_->xlim(old_xlim.min, old_xlim.max);
                    active_axes_->ylim(old_ylim.min, old_ylim.max);
                    if (transition_engine_)
                    {
                        transition_engine_->animate_limits(*active_axes_,
                                                           target_x,
                                                           target_y,
                                                           AUTOFIT_ANIM_DURATION,
                                                           ease::ease_out);
                    }
                    else
                    {
                        anim_ctrl_->animate_axis_limits(*active_axes_,
                                                        target_x,
                                                        target_y,
                                                        AUTOFIT_ANIM_DURATION,
                                                        ease::ease_out);
                    }
                    // Propagate auto-fit to linked axes
                    if (axis_link_mgr_)
                    {
                        axis_link_mgr_->propagate_limits(active_axes_, target_x, target_y);
                    }
                    return;  // Don't start a pan drag on double-click
                }
            }

            // Begin pan drag
            SPECTRA_LOG_DEBUG("input", "Starting pan drag");
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
        }
        else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging
                 && tool_mode_ == ToolMode::Pan)
        {
            // Check if this was a Ctrl+drag box zoom
            if (ctrl_box_zoom_active_)
            {
                SPECTRA_LOG_DEBUG("input", "Ending Ctrl+drag box zoom");
                apply_box_zoom();
                mode_ = InteractionMode::Idle;
                ctrl_box_zoom_active_ = false;
                return;
            }

            SPECTRA_LOG_DEBUG("input", "Ending pan drag");
            mode_ = InteractionMode::Idle;

            // Detect click-without-drag: if the mouse barely moved, treat as a
            // click for series selection rather than a pan gesture.
            {
                float dx_px = static_cast<float>(x - drag_start_x_);
                float dy_px = static_cast<float>(y - drag_start_y_);
                float move_dist = std::sqrt(dx_px * dx_px + dy_px * dy_px);
                constexpr float CLICK_THRESHOLD_PX = 5.0f;
                if (move_dist < CLICK_THRESHOLD_PX && data_interaction_)
                {
                    // Undo the tiny pan that occurred during the drag
                    if (active_axes_)
                    {
                        active_axes_->xlim(drag_start_xlim_min_, drag_start_xlim_max_);
                        active_axes_->ylim(drag_start_ylim_min_, drag_start_ylim_max_);
                    }
                    if (data_interaction_->on_mouse_click(0, x, y))
                    {
                        return;  // Click consumed by data interaction (series selected)
                    }
                }
            }

            // Compute release velocity for inertial pan
            if ((transition_engine_ || anim_ctrl_) && active_axes_)
            {
                auto now = Clock::now();
                float dt_sec = std::chrono::duration<float>(now - last_move_time_).count();
                float drag_total = std::chrono::duration<float>(now - drag_start_time_).count();

                // Only apply inertia if the drag was short and recent movement exists
                if (dt_sec < 0.1f && dt_sec > 0.0f && drag_total > 0.05f)
                {
                    // Skip inertia if mouse barely moved — prevents spurious
                    // acceleration from sub-pixel or 1-2 px jitter on release
                    float dx_px = static_cast<float>(x - last_move_x_);
                    float dy_px = static_cast<float>(y - last_move_y_);
                    float dist_px = std::sqrt(dx_px * dx_px + dy_px * dy_px);
                    constexpr float MIN_RELEASE_DIST_PX = 2.0f;

                    if (dist_px >= MIN_RELEASE_DIST_PX)
                    {
                        const auto& vp = viewport_for_axes(active_axes_);
                        auto xlim = active_axes_->x_limits();
                        auto ylim = active_axes_->y_limits();

                        float x_range = xlim.max - xlim.min;
                        float y_range = ylim.max - ylim.min;

                        // Use a minimum dt floor to prevent velocity blow-up from
                        // sub-millisecond intervals between last move and release
                        constexpr float MIN_DT_SEC = 0.008f;  // 8ms floor
                        float effective_dt = std::max(dt_sec, MIN_DT_SEC);

                        // Screen velocity → data velocity
                        float vx_screen = dx_px / effective_dt;
                        float vy_screen = dy_px / effective_dt;

                        // Clamp screen velocity as a safety net
                        constexpr float MAX_SCREEN_VEL = 3000.0f;  // px/sec
                        vx_screen = std::clamp(vx_screen, -MAX_SCREEN_VEL, MAX_SCREEN_VEL);
                        vy_screen = std::clamp(vy_screen, -MAX_SCREEN_VEL, MAX_SCREEN_VEL);

                        float vx_data = -vx_screen * x_range / vp.w;
                        float vy_data = vy_screen * y_range / vp.h;

                        float speed = std::sqrt(vx_data * vx_data + vy_data * vy_data);
                        if (speed > MIN_INERTIA_VELOCITY)
                        {
                            SPECTRA_LOG_DEBUG("input",
                                              "Inertial pan: v=(" + std::to_string(vx_data) + ", "
                                                  + std::to_string(vy_data) + ")");
                            if (transition_engine_)
                            {
                                transition_engine_->animate_inertial_pan(
                                    *active_axes_, vx_data, vy_data, PAN_INERTIA_DURATION);
                            }
                            else if (anim_ctrl_)
                            {
                                anim_ctrl_->animate_inertial_pan(
                                    *active_axes_, vx_data, vy_data, PAN_INERTIA_DURATION);
                            }
                        }
                    }
                }
            }
        }
    }
}

// ─── Mouse move ─────────────────────────────────────────────────────────────

void InputHandler::on_mouse_move(double x, double y)
{
    SPECTRA_LOG_TRACE(
        "input", "Mouse move event - pos: (" + std::to_string(x) + ", " + std::to_string(y) + ")");

    // Handle 3D camera drag (orbit or pan)
    if (is_3d_orbit_drag_ || is_3d_pan_drag_)
    {
        if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
        {
            auto& cam = axes3d->camera();
            float dx = static_cast<float>(x - drag_start_x_);
            float dy = static_cast<float>(y - drag_start_y_);

            if (is_3d_orbit_drag_ && !orbit_locked_)
            {
                cam.orbit(dx * ORBIT_SENSITIVITY, -dy * ORBIT_SENSITIVITY);
            }
            else if (is_3d_orbit_drag_ && orbit_locked_)
            {
                // In 2D mode, orbit drag becomes pan
                const auto& vp = viewport_for_axes(axes3d);
                cam.pan(dx, dy, vp.w, vp.h);
            }
            else if (is_3d_pan_drag_)
            {
                const auto& vp = viewport_for_axes(axes3d);
                cam.pan(dx, dy, vp.w, vp.h);
            }

            drag_start_x_ = x;
            drag_start_y_ = y;
        }
        return;
    }

    // Update cursor readout regardless of mode
    Axes* hit = hit_test_axes(x, y);
    if (hit)
    {
        SPECTRA_LOG_TRACE("input", "Mouse move hit axes");
        // Temporarily use hit axes for screen_to_data conversion
        Axes* prev = active_axes_;
        active_axes_ = hit;
        const auto& vp = viewport_for_axes(hit);
        float saved_vp_x = vp_x_, saved_vp_y = vp_y_;
        float saved_vp_w = vp_w_, saved_vp_h = vp_h_;
        vp_x_ = vp.x;
        vp_y_ = vp.y;
        vp_w_ = vp.w;
        vp_h_ = vp.h;

        cursor_readout_.valid = true;
        cursor_readout_.screen_x = x;
        cursor_readout_.screen_y = y;
        screen_to_data(x, y, cursor_readout_.data_x, cursor_readout_.data_y);

        // Restore if we were in a drag with a different axes
        // (includes middle-mouse pan and measure drag which don't set mode_)
        if (mode_ == InteractionMode::Dragging || middle_pan_dragging_ || measure_dragging_)
        {
            active_axes_ = prev;
            vp_x_ = saved_vp_x;
            vp_y_ = saved_vp_y;
            vp_w_ = saved_vp_w;
            vp_h_ = saved_vp_h;
        }
        else
        {
            // In idle mode, update active axes to hovered one
            active_axes_ = hit;
            // Keep viewport in sync with the new active axes
            vp_x_ = vp.x;
            vp_y_ = vp.y;
            vp_w_ = vp.w;
            vp_h_ = vp.h;
        }
    }
    else
    {
        cursor_readout_.valid = false;
    }

    if (!active_axes_)
        return;

    // Middle-mouse pan (works in all tool modes)
    if (middle_pan_dragging_ && active_axes_)
    {
        const auto& vp = viewport_for_axes(active_axes_);
        double dx_screen = x - middle_pan_start_x_;
        double dy_screen = y - middle_pan_start_y_;
        float x_range = middle_pan_xlim_max_ - middle_pan_xlim_min_;
        float y_range = middle_pan_ylim_max_ - middle_pan_ylim_min_;
        float dx_data = -static_cast<float>(dx_screen) * x_range / vp.w;
        float dy_data = static_cast<float>(dy_screen) * y_range / vp.h;
        active_axes_->xlim(middle_pan_xlim_min_ + dx_data, middle_pan_xlim_max_ + dx_data);
        active_axes_->ylim(middle_pan_ylim_min_ + dy_data, middle_pan_ylim_max_ + dy_data);
        if (axis_link_mgr_)
            axis_link_mgr_->propagate_limits(
                active_axes_, active_axes_->x_limits(), active_axes_->y_limits());
        // Don't return — allow cursor readout and other overlays to update too
    }

    // Update measure drag (Measure mode)
    if (measure_dragging_ && tool_mode_ == ToolMode::Measure)
    {
        screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
        return;
    }

    // Update measure endpoint in two-click mode (first point placed, tracking cursor)
    if (measure_click_state_ == 1 && tool_mode_ == ToolMode::Measure)
    {
        screen_to_data(x, y, measure_end_data_x_, measure_end_data_y_);
        // Don't return — allow cursor readout to update
    }

    // Update region selection drag (Select mode)
    if (region_dragging_ && tool_mode_ == ToolMode::Select && data_interaction_)
    {
        data_interaction_->update_region_drag(x, y);
        return;
    }

    if (mode_ == InteractionMode::Dragging)
    {
        // Ctrl+drag box zoom in Pan mode: update box rect
        if (ctrl_box_zoom_active_)
        {
            box_zoom_.x1 = x;
            box_zoom_.y1 = y;
            return;
        }

        if (tool_mode_ == ToolMode::Pan)
        {
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
            float dy_data = static_cast<float>(dy_screen) * y_range / vp.h;

            active_axes_->xlim(drag_start_xlim_min_ + dx_data, drag_start_xlim_max_ + dx_data);
            active_axes_->ylim(drag_start_ylim_min_ + dy_data, drag_start_ylim_max_ + dy_data);

            // Propagate pan to linked axes
            if (axis_link_mgr_)
            {
                axis_link_mgr_->propagate_limits(
                    active_axes_, active_axes_->x_limits(), active_axes_->y_limits());
            }
        }
        else if (tool_mode_ == ToolMode::BoxZoom)
        {
            // Box zoom logic
            box_zoom_.x1 = x;
            box_zoom_.y1 = y;
        }
    }
}

// ─── Scroll ─────────────────────────────────────────────────────────────────

void InputHandler::on_scroll(double /*x_offset*/, double y_offset, double cursor_x, double cursor_y)
{
    // Hit-test all axes (including 3D) for scroll zoom
    AxesBase* hit_base = hit_test_all_axes(cursor_x, cursor_y);
    if (hit_base)
    {
        active_axes_base_ = hit_base;
        active_axes_ = dynamic_cast<Axes*>(hit_base);
    }

    // Handle 3D zoom by scaling axis limits (box stays fixed visual size)
    if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
    {
        float factor = (y_offset > 0) ? (1.0f - ZOOM_3D_FACTOR) : (1.0f + ZOOM_3D_FACTOR);
        axes3d->zoom_limits(factor);
        return;
    }

    // 2D hit-test fallback
    Axes* hit = hit_test_axes(cursor_x, cursor_y);
    if (hit)
    {
        active_axes_ = hit;
    }
    if (!active_axes_)
        return;

    const auto& vp = viewport_for_axes(active_axes_);

    // Cancel any running animations — new scroll input takes priority
    if (transition_engine_)
    {
        transition_engine_->cancel_for_axes(active_axes_);
    }
    else if (anim_ctrl_)
    {
        anim_ctrl_->cancel_for_axes(active_axes_);
    }

    auto xlim = active_axes_->x_limits();
    auto ylim = active_axes_->y_limits();

    // Compute cursor position in data space
    float saved_vp_x = vp_x_, saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_, saved_vp_h = vp_h_;
    vp_x_ = vp.x;
    vp_y_ = vp.y;
    vp_w_ = vp.w;
    vp_h_ = vp.h;

    float data_x, data_y;
    screen_to_data(cursor_x, cursor_y, data_x, data_y);

    vp_x_ = saved_vp_x;
    vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w;
    vp_h_ = saved_vp_h;

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

    // Propagate zoom to linked axes
    if (axis_link_mgr_)
    {
        axis_link_mgr_->propagate_zoom(active_axes_, data_x, data_y, factor);
    }
}

// ─── Keyboard ───────────────────────────────────────────────────────────────

void InputHandler::on_key(int key, int action, int mods)
{
    // Track modifier state for use in mouse callbacks
    mods_ = mods;

    // Delegate to ShortcutManager first — if it handles the key, we're done
    if (shortcut_mgr_ && shortcut_mgr_->on_key(key, action, mods))
    {
        return;
    }

    if (action != ACTION_PRESS)
        return;

    if (key == KEY_ESCAPE)
    {
        // Cancel box zoom if active
        cancel_box_zoom();
        // Dismiss region selection if active
        if (data_interaction_ && data_interaction_->has_region_selection())
        {
            data_interaction_->dismiss_region_select();
        }
        return;
    }

    if (key == KEY_R && !(mods & MOD_CONTROL))
    {
        // Reset view: animated auto-fit all axes in the figure
        if (figure_)
        {
            for (auto& axes_ptr : figure_->axes())
            {
                if (axes_ptr && (transition_engine_ || anim_ctrl_))
                {
                    auto old_xlim = axes_ptr->x_limits();
                    auto old_ylim = axes_ptr->y_limits();
                    axes_ptr->auto_fit();
                    AxisLimits target_x = axes_ptr->x_limits();
                    AxisLimits target_y = axes_ptr->y_limits();
                    axes_ptr->xlim(old_xlim.min, old_xlim.max);
                    axes_ptr->ylim(old_ylim.min, old_ylim.max);
                    if (transition_engine_)
                    {
                        transition_engine_->animate_limits(
                            *axes_ptr, target_x, target_y, AUTOFIT_ANIM_DURATION, ease::ease_out);
                    }
                    else
                    {
                        anim_ctrl_->animate_axis_limits(
                            *axes_ptr, target_x, target_y, AUTOFIT_ANIM_DURATION, ease::ease_out);
                    }
                }
                else if (axes_ptr)
                {
                    axes_ptr->auto_fit();
                }
            }
        }
        else if (active_axes_)
        {
            if (transition_engine_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                active_axes_->xlim(old_xlim.min, old_xlim.max);
                active_axes_->ylim(old_ylim.min, old_ylim.max);
                transition_engine_->animate_limits(
                    *active_axes_, target_x, target_y, AUTOFIT_ANIM_DURATION, ease::ease_out);
            }
            else if (anim_ctrl_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                active_axes_->xlim(old_xlim.min, old_xlim.max);
                active_axes_->ylim(old_ylim.min, old_ylim.max);
                anim_ctrl_->animate_axis_limits(
                    *active_axes_, target_x, target_y, AUTOFIT_ANIM_DURATION, ease::ease_out);
            }
            else
            {
                active_axes_->auto_fit();
            }
        }
        return;
    }

    if (key == KEY_G && !(mods & MOD_CONTROL))
    {
        // Toggle grid on active axes
        if (active_axes_)
        {
            active_axes_->grid(!active_axes_->grid_enabled());
        }
        return;
    }

    if (key == KEY_S && (mods & MOD_CONTROL))
    {
        // Ctrl+S: save PNG
        if (save_callback_)
        {
            save_callback_();
        }
        return;
    }

    if (key == KEY_C && !(mods & MOD_CONTROL))
    {
        // Toggle crosshair overlay
        if (data_interaction_)
        {
            data_interaction_->toggle_crosshair();
            SPECTRA_LOG_DEBUG(
                "input",
                "Crosshair toggled: "
                    + std::string(data_interaction_->crosshair_active() ? "ON" : "OFF"));
        }
        return;
    }

    if (key == KEY_A && !(mods & MOD_CONTROL))
    {
        // Animated auto-fit active axes only
        if (active_axes_)
        {
            if (transition_engine_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                active_axes_->xlim(old_xlim.min, old_xlim.max);
                active_axes_->ylim(old_ylim.min, old_ylim.max);
                transition_engine_->animate_limits(
                    *active_axes_, target_x, target_y, AUTOFIT_ANIM_DURATION, ease::ease_out);
            }
            else if (anim_ctrl_)
            {
                auto old_xlim = active_axes_->x_limits();
                auto old_ylim = active_axes_->y_limits();
                active_axes_->auto_fit();
                AxisLimits target_x = active_axes_->x_limits();
                AxisLimits target_y = active_axes_->y_limits();
                active_axes_->xlim(old_xlim.min, old_xlim.max);
                active_axes_->ylim(old_ylim.min, old_ylim.max);
                anim_ctrl_->animate_axis_limits(
                    *active_axes_, target_x, target_y, AUTOFIT_ANIM_DURATION, ease::ease_out);
            }
            else
            {
                active_axes_->auto_fit();
            }
        }
        return;
    }
}

// ─── Box zoom ───────────────────────────────────────────────────────────────

void InputHandler::apply_box_zoom()
{
    if (!active_axes_ || !box_zoom_.active)
    {
        cancel_box_zoom();
        return;
    }

    const auto& vp = viewport_for_axes(active_axes_);

    // Convert box corners from screen to data space
    float saved_vp_x = vp_x_, saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_, saved_vp_h = vp_h_;
    vp_x_ = vp.x;
    vp_y_ = vp.y;
    vp_w_ = vp.w;
    vp_h_ = vp.h;

    float d_x0, d_y0, d_x1, d_y1;
    screen_to_data(box_zoom_.x0, box_zoom_.y0, d_x0, d_y0);
    screen_to_data(box_zoom_.x1, box_zoom_.y1, d_x1, d_y1);

    vp_x_ = saved_vp_x;
    vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w;
    vp_h_ = saved_vp_h;

    // Ensure min < max
    float xmin = std::min(d_x0, d_x1);
    float xmax = std::max(d_x0, d_x1);
    float ymin = std::min(d_y0, d_y1);
    float ymax = std::max(d_y0, d_y1);

    // Only apply if the selection is large enough (avoid accidental clicks)
    constexpr float MIN_SELECTION_PIXELS = 5.0f;
    float dx_screen = static_cast<float>(std::abs(box_zoom_.x1 - box_zoom_.x0));
    float dy_screen = static_cast<float>(std::abs(box_zoom_.y1 - box_zoom_.y0));

    if (dx_screen > MIN_SELECTION_PIXELS && dy_screen > MIN_SELECTION_PIXELS)
    {
        // Animated box zoom transition
        AxisLimits target_x{xmin, xmax};
        AxisLimits target_y{ymin, ymax};
        if (transition_engine_)
        {
            transition_engine_->animate_limits(
                *active_axes_, target_x, target_y, ZOOM_ANIM_DURATION, ease::ease_out);
        }
        else if (anim_ctrl_)
        {
            anim_ctrl_->animate_axis_limits(
                *active_axes_, target_x, target_y, ZOOM_ANIM_DURATION, ease::ease_out);
        }
        else
        {
            active_axes_->xlim(xmin, xmax);
            active_axes_->ylim(ymin, ymax);
        }

        // Propagate box zoom to linked axes
        if (axis_link_mgr_)
        {
            axis_link_mgr_->propagate_limits(active_axes_, target_x, target_y);
        }
    }

    box_zoom_.active = false;
}

void InputHandler::cancel_box_zoom()
{
    if (mode_ == InteractionMode::Dragging
        && (tool_mode_ == ToolMode::BoxZoom || ctrl_box_zoom_active_))
    {
        mode_ = InteractionMode::Idle;
    }
    box_zoom_.active = false;
    ctrl_box_zoom_active_ = false;
}

// ─── Viewport ───────────────────────────────────────────────────────────────

void InputHandler::set_viewport(float vp_x, float vp_y, float vp_w, float vp_h)
{
    vp_x_ = vp_x;
    vp_y_ = vp_y;
    vp_w_ = vp_w;
    vp_h_ = vp_h;
}

void InputHandler::screen_to_data(double screen_x,
                                  double screen_y,
                                  float& data_x,
                                  float& data_y) const
{
    if (!active_axes_)
    {
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

void InputHandler::update(float dt)
{
    if (transition_engine_)
    {
        transition_engine_->update(dt);
    }
    if (anim_ctrl_)
    {
        anim_ctrl_->update(dt);
    }
}

// ─── Animation query ────────────────────────────────────────────────────────

bool InputHandler::has_active_animations() const
{
    if (transition_engine_ && transition_engine_->has_active_animations())
        return true;
    return anim_ctrl_ && anim_ctrl_->has_active_animations();
}

}  // namespace spectra

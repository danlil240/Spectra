#include <cmath>
#include <spectra/logger.hpp>

#include "input.hpp"
#include "ui/overlay/axis_mapping.hpp"
#include "ui/overlay/measure_math.hpp"

namespace spectra
{

namespace
{
constexpr int ACTION_PRESS   = 1;
constexpr int ACTION_RELEASE = 0;
constexpr int MOD_ALT        = 0x0004;   // GLFW_MOD_ALT
}   // anonymous namespace

// ─── Measure point: cursor -> data, snapped to the nearest sample ───────────

bool InputHandler::measure_point_at(double x, double y, double& data_x, double& data_y) const
{
    Axes* ax = measure_axes_ ? measure_axes_ : active_axes_;
    if (!ax)
        return false;
    const AxisMapping map(*ax);
    // Alt places a free point; otherwise snap to a sample within reach.
    if (!(mods_ & MOD_ALT))
    {
        const measure::SnapPoint snap = measure::snap_to_data(*ax, map, x, y, MEASURE_SNAP_PX);
        if (snap.found)
        {
            data_x = snap.x;
            data_y = snap.y;
            return true;
        }
    }
    // Free point: through axis space, so log axes invert correctly.
    data_x = map.to_data_x(x);
    data_y = map.to_data_y(y);
    return false;
}

// ─── Measure tool: drag or two-click distance measurement ───────────────────

void InputHandler::handle_mouse_button_measure(int action, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
        if (measure_click_state_ == 1)
        {
            // Second click: finalize measurement at this point
            SPECTRA_LOG_DEBUG("input", "Measure: second click placed");
            if (measure_axes_)
                measure_end_snapped_ =
                    measure_point_at(x, y, measure_end_data_x_, measure_end_data_y_);
            measure_click_state_ = 2;
            return;
        }

        // First press: start measurement (could be drag or first click)
        SPECTRA_LOG_DEBUG("input", "Starting measure (press)");
        measure_dragging_       = true;
        measure_click_state_    = 0;
        measure_axes_           = active_axes_;
        measure_start_screen_x_ = x;
        measure_start_screen_y_ = y;
        measure_start_snapped_ =
            measure_point_at(x, y, measure_start_data_x_, measure_start_data_y_);
        measure_end_data_x_  = measure_start_data_x_;
        measure_end_data_y_  = measure_start_data_y_;
        measure_end_snapped_ = measure_start_snapped_;
        mode_                = InteractionMode::Dragging;
    }
    else if (action == ACTION_RELEASE && measure_dragging_)
    {
        if (measure_axes_)
            measure_end_snapped_ = measure_point_at(x, y, measure_end_data_x_, measure_end_data_y_);
        measure_dragging_ = false;
        mode_             = InteractionMode::Idle;

        // Check if the mouse barely moved — treat as a click (first point)
        auto            dx_px              = static_cast<float>(x - measure_start_screen_x_);
        auto            dy_px              = static_cast<float>(y - measure_start_screen_y_);
        float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
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
    }
}

// ─── Measure drag/tracking on mouse move ────────────────────────────────────

void InputHandler::handle_mouse_move_measure(double x, double y)
{
    if (!measure_axes_)
        return;
    measure_end_snapped_ = measure_point_at(x, y, measure_end_data_x_, measure_end_data_y_);
}

}   // namespace spectra

#include "input.hpp"

#include <cmath>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <spectra/logger.hpp>

#include "core/axis_scale.hpp"
#include "gesture_recognizer.hpp"
#include "ui/animation/animation_controller.hpp"
#include "ui/animation/transition_engine.hpp"
#include "ui/camera/view_presets.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/data/axis_link.hpp"
#include "ui/overlay/axes3d_axis_pick.hpp"
#include "ui/overlay/data_interaction.hpp"
#include "ui/overlay/view_cube.hpp"

namespace spectra
{

namespace
{
constexpr int MOUSE_BUTTON_LEFT  = 0;
constexpr int MOUSE_BUTTON_RIGHT = 1;
constexpr int ACTION_PRESS       = 1;
constexpr int ACTION_RELEASE     = 0;

}   // anonymous namespace

void InputHandler::update_axes3d_arrow_hover(AxesBase* hit_base, double x, double y)
{
    if (is_3d_orbit_drag_ || is_3d_pan_drag_ || rclick_zoom_dragging_)
        return;

    if (figure_)
    {
        for (auto& axes_ptr : figure_->all_axes_mut())
        {
            auto* axes3d = dynamic_cast<Axes3D*>(axes_ptr.get());
            if (axes3d)
                axes3d->set_hovered_axis_arrow(Axes3D::AxisArrowHover::None);
        }
    }

    auto* axes3d = dynamic_cast<Axes3D*>(hit_base);
    if (!axes3d)
        return;

    const Axis3DArrowPick pick =
        pick_axes3d_axis_arrow(*axes3d, static_cast<float>(x), static_cast<float>(y));
    axes3d->set_hovered_axis_arrow(axis_pick_to_hover(pick));
}

void InputHandler::animate_axes3d_camera(Axes3D*       axes3d,
                                         const Camera& target,
                                         const char*   undo_label,
                                         bool          target_auto_ortho)
{
    Camera&      cam         = axes3d->camera();
    const Camera before      = cam;
    const bool   before_auto = axes3d_auto_ortho(axes3d);
    set_axes3d_auto_ortho(axes3d, target_auto_ortho);

    // Going to perspective switches now, so the move itself is seen in
    // perspective; going to orthographic switches when the move ends.
    if (target.projection_mode == Camera::ProjectionMode::Perspective)
        cam.projection_mode = Camera::ProjectionMode::Perspective;

    if (transition_engine_)
    {
        transition_engine_->cancel_for_camera(&cam);
        transition_engine_->animate_camera(cam, target, AUTOFIT_ANIM_DURATION, ease::ease_out);
    }
    else if (anim_ctrl_)
    {
        anim_ctrl_->cancel_all();
        anim_ctrl_->animate_camera(cam, target, AUTOFIT_ANIM_DURATION, ease::ease_out);
    }
    else
    {
        cam = target;
        cam.update_position_from_orbit();
    }

    // Undo / redo restore the automatic-orthographic mark with the camera,
    // so a restored preset view still returns to perspective on orbit.
    if (undo_mgr_)
    {
        Axes3D* ax = axes3d;
        undo_mgr_->push(UndoAction{undo_label,
                                   [this, ax, before, before_auto]()
                                   {
                                       ax->camera() = before;
                                       ax->camera().update_position_from_orbit();
                                       set_axes3d_auto_ortho(ax, before_auto);
                                   },
                                   [this, ax, target, target_auto_ortho]()
                                   {
                                       ax->camera() = target;
                                       ax->camera().update_position_from_orbit();
                                       set_axes3d_auto_ortho(ax, target_auto_ortho);
                                   }});
    }
}

void InputHandler::snap_axes3d_axis_view(Axes3D* axes3d, Camera::AxisView axis_view)
{
    Camera target = axes3d->camera();
    target.align_view_to_axis(axis_view);
    animate_axes3d_camera(axes3d, target, "Align 3D axis view", axes3d_auto_ortho(axes3d));
}

bool InputHandler::axes3d_auto_ortho(const Axes3D* axes3d) const
{
    return std::find(auto_ortho_axes_.begin(), auto_ortho_axes_.end(), axes3d)
           != auto_ortho_axes_.end();
}

void InputHandler::set_axes3d_auto_ortho(const Axes3D* axes3d, bool on)
{
    std::erase(auto_ortho_axes_, axes3d);
    if (on)
        auto_ortho_axes_.push_back(axes3d);
}

void InputHandler::apply_axes3d_view_preset(Axes3D* axes3d, Camera::ViewPreset preset)
{
    if (!axes3d)
        return;
    const ViewPresetPlan plan =
        plan_view_preset(axes3d->camera(), preset, axes3d_auto_ortho(axes3d));
    animate_axes3d_camera(axes3d, plan.target, "3D view preset", plan.auto_ortho);
}

void InputHandler::toggle_axes3d_projection(Axes3D* axes3d)
{
    if (!axes3d)
        return;
    if (transition_engine_)
        transition_engine_->cancel_for_camera(&axes3d->camera());
    Camera&      cam         = axes3d->camera();
    const Camera before      = cam;
    const bool   before_auto = axes3d_auto_ortho(axes3d);
    Camera       after       = toggled_projection(cam);
    after.update_position_from_orbit();
    cam = after;
    // An explicit choice: presets keep it, orbiting does not undo it.
    set_axes3d_auto_ortho(axes3d, false);
    if (undo_mgr_)
    {
        Axes3D* ax = axes3d;
        undo_mgr_->push(UndoAction{after.projection_mode == Camera::ProjectionMode::Orthographic
                                       ? "Orthographic projection"
                                       : "Perspective projection",
                                   [this, ax, before, before_auto]()
                                   {
                                       ax->camera() = before;
                                       ax->camera().update_position_from_orbit();
                                       set_axes3d_auto_ortho(ax, before_auto);
                                   },
                                   [this, ax, after]()
                                   {
                                       ax->camera() = after;
                                       ax->camera().update_position_from_orbit();
                                       set_axes3d_auto_ortho(ax, false);
                                   }});
    }
}

bool InputHandler::handle_view_cube_click(Axes3D* axes3d, double x, double y)
{
    const view_cube::Hit hit =
        view_cube::hit_test(*axes3d, static_cast<float>(x), static_cast<float>(y));
    switch (hit.kind)
    {
        case view_cube::HitKind::Face:
            SPECTRA_LOG_DEBUG("input", "View cube click — snap to preset view");
            apply_axes3d_view_preset(axes3d, hit.preset);
            return true;
        case view_cube::HitKind::ProjectionToggle:
            toggle_axes3d_projection(axes3d);
            return true;
        case view_cube::HitKind::None:
            break;
    }
    return false;
}

// ─── 3D camera interaction (orbit, right-click zoom, middle-pan) ────────────

void InputHandler::handle_mouse_button_3d(Axes3D* axes3d,
                                          int     button,
                                          int     action,
                                          double  x,
                                          double  y)
{
    if (button == MOUSE_BUTTON_LEFT && action == ACTION_PRESS)
    {
        if (handle_view_cube_click(axes3d, x, y))
        {
            is_3d_orbit_drag_ = false;
            mode_             = InteractionMode::Idle;
            drag3d_axes_      = nullptr;
            return;
        }

        if (gesture_ && gesture_->on_click(x, y))
        {
            is_3d_orbit_drag_ = false;
            mode_             = InteractionMode::Idle;
            drag3d_axes_      = nullptr;

            const Axis3DArrowPick axis_pick =
                pick_axes3d_axis_arrow(*axes3d, static_cast<float>(x), static_cast<float>(y));
            if (axis_pick != Axis3DArrowPick::None)
            {
                SPECTRA_LOG_DEBUG("input", "Double-click on 3D axis arrow — align view");
                snap_axes3d_axis_view(axes3d, axis_pick_to_camera_view(axis_pick));
                return;
            }

            SPECTRA_LOG_DEBUG("input", "Double-click on 3D axes — auto-fit");
            axes3d->auto_fit();
            return;
        }

        if (transition_engine_)
            transition_engine_->cancel_for_camera(&axes3d->camera());
        else if (anim_ctrl_)
            anim_ctrl_->cancel_all();

        is_3d_orbit_drag_ = true;
        drag_start_x_     = x;
        drag_start_y_     = y;
        mode_             = InteractionMode::Dragging;
        // Capture state for undo
        drag3d_axes_         = axes3d;
        drag3d_start_xlim_   = axes3d->x_limits();
        drag3d_start_ylim_   = axes3d->y_limits();
        drag3d_start_zlim_   = axes3d->z_limits();
        drag3d_start_camera_ = axes3d->camera();
        return;
    }
    if (button == MOUSE_BUTTON_LEFT && action == ACTION_RELEASE && is_3d_orbit_drag_)
    {
        const double dx_click = x - drag_start_x_;
        const double dy_click = y - drag_start_y_;
        const float  move_dist =
            static_cast<float>(std::sqrt(dx_click * dx_click + dy_click * dy_click));
        constexpr float CLICK_THRESHOLD_PX = 5.0f;
        const bool      was_click          = move_dist < CLICK_THRESHOLD_PX;

        is_3d_orbit_drag_ = false;
        mode_             = InteractionMode::Idle;

        if (was_click && data_interaction_)
        {
            // Short click: undo only micro-orbit jitter, not an intentional nudge away
            // from an axis-aligned snap (drag3d_start may be the snapped pose).
            auto&       cam         = axes3d->camera();
            const float angle_delta = std::abs(cam.azimuth - drag3d_start_camera_.azimuth)
                                      + std::abs(cam.elevation - drag3d_start_camera_.elevation);
            const vec3  pos_delta  = cam.position - drag3d_start_camera_.position;
            const float pos_delta2 = vec3_length_sq(pos_delta);
            if (angle_delta < 0.5f && pos_delta2 < 1e-4f)
            {
                cam = drag3d_start_camera_;
                cam.update_position_from_orbit();
            }
            data_interaction_->on_mouse_click_datatip_only(0, x, y);
            drag3d_axes_ = nullptr;
            return;
        }

        // Push undo for orbit drag
        if (undo_mgr_ && drag3d_axes_ == axes3d)
        {
            auto    before_xlim   = drag3d_start_xlim_;
            auto    before_ylim   = drag3d_start_ylim_;
            auto    before_zlim   = drag3d_start_zlim_;
            auto    before_camera = drag3d_start_camera_;
            auto    after_camera  = axes3d->camera();
            Axes3D* ax            = axes3d;
            undo_mgr_->push(UndoAction{"Orbit 3D",
                                       [ax, before_xlim, before_ylim, before_zlim, before_camera]()
                                       {
                                           ax->xlim(before_xlim.min, before_xlim.max);
                                           ax->ylim(before_ylim.min, before_ylim.max);
                                           ax->zlim(before_zlim.min, before_zlim.max);
                                           ax->camera() = before_camera;
                                           ax->camera().update_position_from_orbit();
                                       },
                                       [ax, after_camera]()
                                       {
                                           ax->camera() = after_camera;
                                           ax->camera().update_position_from_orbit();
                                       }});
        }
        drag3d_axes_ = nullptr;
        return;
    }
    if (button == MOUSE_BUTTON_RIGHT && action == ACTION_PRESS)
    {
        rclick_zoom_dragging_ = true;
        rclick_zoom_3d_       = true;
        rclick_zoom_axis_     = ZoomAxis::None;
        rclick_zoom_start_x_  = x;
        rclick_zoom_start_y_  = y;
        rclick_zoom_last_x_   = x;
        rclick_zoom_last_y_   = y;
        rclick_zoom_xlim_min_ = axes3d->x_limits().min;
        rclick_zoom_xlim_max_ = axes3d->x_limits().max;
        rclick_zoom_ylim_min_ = axes3d->y_limits().min;
        rclick_zoom_ylim_max_ = axes3d->y_limits().max;
        rclick_zoom_zlim_min_ = axes3d->z_limits().min;
        rclick_zoom_zlim_max_ = axes3d->z_limits().max;
        mode_                 = InteractionMode::Dragging;
        // Capture state for undo
        drag3d_axes_         = axes3d;
        drag3d_start_xlim_   = axes3d->x_limits();
        drag3d_start_ylim_   = axes3d->y_limits();
        drag3d_start_zlim_   = axes3d->z_limits();
        drag3d_start_camera_ = axes3d->camera();
        // Project each data axis direction to screen space so we can later
        // pick the axis most aligned with the drag direction.
        {
            const auto& vp    = axes3d->viewport();
            const auto& cam   = axes3d->camera();
            mat4        proj  = axes3d->projection_matrix(vp);
            mat4        view  = cam.view_matrix();
            mat4        model = axes3d->data_to_normalized_matrix();
            mat4        mvp   = mat4_mul(proj, mat4_mul(view, model));

            // Project a normalized-cube point to viewport screen coords.
            auto project = [&](vec3 p, float& sx, float& sy) -> bool
            {
                float cx = mvp.m[0] * p.x + mvp.m[4] * p.y + mvp.m[8] * p.z + mvp.m[12];
                float cy = mvp.m[1] * p.x + mvp.m[5] * p.y + mvp.m[9] * p.z + mvp.m[13];
                float cw = mvp.m[3] * p.x + mvp.m[7] * p.y + mvp.m[11] * p.z + mvp.m[15];
                if (cw <= 0.001f)
                    return false;
                sx = vp.x + (cx / cw + 1.0f) * 0.5f * vp.w;
                sy = vp.y + (cy / cw + 1.0f) * 0.5f * vp.h;
                return true;
            };

            // Origin of the normalized cube
            vec3  origin{0.0f, 0.0f, 0.0f};
            float ox = NAN;
            float oy = NAN;
            if (project(origin, ox, oy))
            {
                // Unit steps along each normalized-cube axis
                const float step         = 1.0f;
                vec3        axis_tips[3] = {{step, 0, 0}, {0, step, 0}, {0, 0, step}};
                for (int i = 0; i < 3; ++i)
                {
                    float tx = NAN;
                    float ty = NAN;
                    if (project(axis_tips[i], tx, ty))
                    {
                        float ddx = tx - ox;
                        float ddy = ty - oy;
                        float len = std::sqrt(ddx * ddx + ddy * ddy);
                        if (len > 1e-4f)
                        {
                            ddx /= len;
                            ddy /= len;
                        }
                        rclick_zoom_axis_sx_[i] = ddx;
                        rclick_zoom_axis_sy_[i] = ddy;
                    }
                    else
                    {
                        rclick_zoom_axis_sx_[i] = (i == 0) ? 1.0f : 0.0f;
                        rclick_zoom_axis_sy_[i] = (i == 1) ? 1.0f : 0.0f;
                    }
                }
            }
            else
            {
                rclick_zoom_axis_sx_[0] = 1.0f;
                rclick_zoom_axis_sy_[0] = 0.0f;
                rclick_zoom_axis_sx_[1] = 0.0f;
                rclick_zoom_axis_sy_[1] = 1.0f;
                rclick_zoom_axis_sx_[2] = 0.0f;
                rclick_zoom_axis_sy_[2] = 0.0f;
            }
        }
        return;
    }
    if (button == MOUSE_BUTTON_RIGHT && action == ACTION_RELEASE && rclick_zoom_dragging_
        && rclick_zoom_3d_)
    {
        rclick_zoom_dragging_ = false;
        rclick_zoom_3d_       = false;
        rclick_zoom_axis_     = ZoomAxis::None;
        mode_                 = InteractionMode::Idle;
        // Push undo for 1D zoom
        if (undo_mgr_ && drag3d_axes_ == axes3d)
        {
            auto    before_xlim = drag3d_start_xlim_;
            auto    before_ylim = drag3d_start_ylim_;
            auto    before_zlim = drag3d_start_zlim_;
            auto    after_xlim  = axes3d->x_limits();
            auto    after_ylim  = axes3d->y_limits();
            auto    after_zlim  = axes3d->z_limits();
            Axes3D* ax          = axes3d;
            undo_mgr_->push(UndoAction{"Zoom 1D 3D",
                                       [ax, before_xlim, before_ylim, before_zlim]()
                                       {
                                           ax->xlim(before_xlim.min, before_xlim.max);
                                           ax->ylim(before_ylim.min, before_ylim.max);
                                           ax->zlim(before_zlim.min, before_zlim.max);
                                       },
                                       [ax, after_xlim, after_ylim, after_zlim]()
                                       {
                                           ax->xlim(after_xlim.min, after_xlim.max);
                                           ax->ylim(after_ylim.min, after_ylim.max);
                                           ax->zlim(after_zlim.min, after_zlim.max);
                                       }});
        }
        drag3d_axes_ = nullptr;
        return;
    }
    if (button == MOUSE_BUTTON_MIDDLE && action == ACTION_PRESS)
    {
        is_3d_pan_drag_ = true;
        drag_start_x_   = x;
        drag_start_y_   = y;
        mode_           = InteractionMode::Dragging;
        // Capture state for undo
        drag3d_axes_         = axes3d;
        drag3d_start_xlim_   = axes3d->x_limits();
        drag3d_start_ylim_   = axes3d->y_limits();
        drag3d_start_zlim_   = axes3d->z_limits();
        drag3d_start_camera_ = axes3d->camera();
        return;
    }
    if (button == MOUSE_BUTTON_MIDDLE && action == ACTION_RELEASE && is_3d_pan_drag_)
    {
        is_3d_pan_drag_ = false;
        mode_           = InteractionMode::Idle;
        // Push undo for middle-mouse pan
        if (undo_mgr_ && drag3d_axes_ == axes3d)
        {
            auto    before_xlim = drag3d_start_xlim_;
            auto    before_ylim = drag3d_start_ylim_;
            auto    before_zlim = drag3d_start_zlim_;
            auto    after_xlim  = axes3d->x_limits();
            auto    after_ylim  = axes3d->y_limits();
            auto    after_zlim  = axes3d->z_limits();
            Axes3D* ax          = axes3d;
            undo_mgr_->push(UndoAction{"Pan 3D",
                                       [ax, before_xlim, before_ylim, before_zlim]()
                                       {
                                           ax->xlim(before_xlim.min, before_xlim.max);
                                           ax->ylim(before_ylim.min, before_ylim.max);
                                           ax->zlim(before_zlim.min, before_zlim.max);
                                       },
                                       [ax, after_xlim, after_ylim, after_zlim]()
                                       {
                                           ax->xlim(after_xlim.min, after_xlim.max);
                                           ax->ylim(after_ylim.min, after_ylim.max);
                                           ax->zlim(after_zlim.min, after_zlim.max);
                                       }});
        }
        drag3d_axes_ = nullptr;
        return;
    }
}

// ─── Middle-mouse pan (2D) ──────────────────────────────────────────────────

void InputHandler::handle_mouse_button_middle_pan(int action, double x, double y)
{
    if (action == ACTION_PRESS && !middle_pan_dragging_ && active_axes_)
    {
        // Cancel any running animations
        if (transition_engine_)
            transition_engine_->cancel_for_axes(active_axes_);
        else if (anim_ctrl_)
            anim_ctrl_->cancel_for_axes(active_axes_);

        middle_pan_dragging_ = true;
        middle_pan_start_x_  = x;
        middle_pan_start_y_  = y;
        auto xlim            = active_axes_->x_limits();
        auto ylim            = active_axes_->y_limits();
        middle_pan_xlim_min_ = xlim.min;
        middle_pan_xlim_max_ = xlim.max;
        middle_pan_ylim_min_ = ylim.min;
        middle_pan_ylim_max_ = ylim.max;
        begin_view_gesture();
    }
    else if (action == ACTION_RELEASE && middle_pan_dragging_)
    {
        middle_pan_dragging_ = false;
        end_view_gesture("Pan");
    }
}

// ─── Right-click drag zoom (2D) ────────────────────────────────────────────

void InputHandler::handle_mouse_button_rclick_zoom(int action, double x, double y)
{
    if (action == ACTION_PRESS && !rclick_zoom_dragging_ && active_axes_)
    {
        // Cancel any running animations
        if (transition_engine_)
            transition_engine_->cancel_for_axes(active_axes_);
        else if (anim_ctrl_)
            anim_ctrl_->cancel_for_axes(active_axes_);

        rclick_zoom_dragging_ = true;
        rclick_zoom_3d_       = false;
        rclick_zoom_axis_     = ZoomAxis::None;
        rclick_zoom_start_x_  = x;
        rclick_zoom_start_y_  = y;
        rclick_zoom_last_x_   = x;
        rclick_zoom_last_y_   = y;
        auto xlim             = active_axes_->x_limits();
        auto ylim             = active_axes_->y_limits();
        rclick_zoom_xlim_min_ = xlim.min;
        rclick_zoom_xlim_max_ = xlim.max;
        rclick_zoom_ylim_min_ = ylim.min;
        rclick_zoom_ylim_max_ = ylim.max;
        // Capture drag-start position in data space as zoom anchor
        screen_to_data(x, y, rclick_zoom_anchor_data_x_, rclick_zoom_anchor_data_y_);
        begin_view_gesture();
    }
    else if (action == ACTION_RELEASE && rclick_zoom_dragging_ && !rclick_zoom_3d_)
    {
        rclick_zoom_dragging_ = false;
        rclick_zoom_axis_     = ZoomAxis::None;
        end_view_gesture("Zoom");
    }
}

// ─── Box zoom tool (left-click) ─────────────────────────────────────────────

void InputHandler::handle_mouse_button_box_zoom(int action, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle && active_axes_)
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
        mode_            = InteractionMode::Dragging;
        box_zoom_.active = true;
        box_zoom_.x0     = x;
        box_zoom_.y0     = y;
        box_zoom_.x1     = x;
        box_zoom_.y1     = y;
    }
    else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging)
    {
        SPECTRA_LOG_DEBUG("input", "Ending box zoom (BoxZoom tool)");
        apply_box_zoom();
        mode_ = InteractionMode::Idle;
    }
}

// ─── Pan tool (left-click: drag, double-click, ctrl+box-zoom, inertia) ──────

void InputHandler::handle_mouse_button_pan(int action, int mods, double x, double y)
{
    if (action == ACTION_PRESS && mode_ == InteractionMode::Idle)
    {
        // An inertial glide still running ends here: record where it got to.
        finalize_pending_view();

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
            mode_                 = InteractionMode::Dragging;
            ctrl_box_zoom_active_ = true;
            box_zoom_.active      = true;
            box_zoom_.x0          = x;
            box_zoom_.y0          = y;
            box_zoom_.x1          = x;
            box_zoom_.y1          = y;
            return;
        }

        // Press on a 2D data tip: drag it along its series instead of panning.
        if (data_interaction_ && data_interaction_->begin_marker_drag(x, y))
        {
            mode_         = InteractionMode::Dragging;
            tip_dragging_ = true;
            drag_start_x_ = x;
            drag_start_y_ = y;
            return;
        }

        // Double-click detection: auto-fit with animated transition
        if (gesture_)
        {
            bool is_double = gesture_->on_click(x, y);
            if (is_double)
            {
                SPECTRA_LOG_DEBUG("input", "Double-click detected — animated auto-fit");
                if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
                {
                    // 3D: auto_fit resets limits + camera in one call
                    axes3d->auto_fit();
                    return;   // Don't start a pan drag on double-click
                }
                if (active_axes_ && (transition_engine_ || anim_ctrl_))
                {
                    const ViewState before = capture_view(active_axes_);
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
                    ViewState after = capture_view(active_axes_);
                    set_axes_view(after, active_axes_, target_x, target_y);
                    record_view_change(figure_, "Auto-fit", before, std::move(after));
                    return;   // Don't start a pan drag on double-click
                }
            }
        }

        // Begin pan drag
        mode_                 = InteractionMode::Dragging;
        drag_start_following_ = active_axes_->is_presented_buffer_following();
        drag_start_x_         = x;
        drag_start_y_         = y;
        last_move_x_          = x;
        last_move_y_          = y;
        last_move_time_       = Clock::now();
        drag_start_time_      = last_move_time_;

        auto xlim            = active_axes_->x_limits();
        auto ylim            = active_axes_->y_limits();
        drag_start_xlim_min_ = xlim.min;
        drag_start_xlim_max_ = xlim.max;
        drag_start_ylim_min_ = ylim.min;
        drag_start_ylim_max_ = ylim.max;
        begin_view_gesture();
    }
    else if (action == ACTION_RELEASE && mode_ == InteractionMode::Dragging)
    {
        if (tip_dragging_)
        {
            const double    dx_px              = x - drag_start_x_;
            const double    dy_px              = y - drag_start_y_;
            constexpr float CLICK_THRESHOLD_PX = 5.0f;
            const bool      moved = std::sqrt(dx_px * dx_px + dy_px * dy_px) >= CLICK_THRESHOLD_PX;
            tip_dragging_         = false;
            mode_                 = InteractionMode::Idle;
            if (data_interaction_)
                data_interaction_->end_marker_drag(x, y, moved, mods);
            return;
        }

        // Check if this was a Ctrl+drag box zoom
        if (ctrl_box_zoom_active_)
        {
            SPECTRA_LOG_DEBUG("input", "Ending Ctrl+drag box zoom");
            apply_box_zoom();
            mode_                 = InteractionMode::Idle;
            ctrl_box_zoom_active_ = false;
            return;
        }

        mode_ = InteractionMode::Idle;

        // Detect click-without-drag: if the mouse barely moved, revert tiny
        // pan jitter and keep Pan mode non-selecting.
        {
            auto            dx_px              = static_cast<float>(x - drag_start_x_);
            auto            dy_px              = static_cast<float>(y - drag_start_y_);
            float           move_dist          = std::sqrt(dx_px * dx_px + dy_px * dy_px);
            constexpr float CLICK_THRESHOLD_PX = 5.0f;
            if (move_dist < CLICK_THRESHOLD_PX)
            {
                // Undo the tiny pan that occurred during the drag; a click on a
                // live plot keeps it following (the pan paused it).
                if (active_axes_)
                {
                    active_axes_->xlim(drag_start_xlim_min_, drag_start_xlim_max_);
                    active_axes_->ylim(drag_start_ylim_min_, drag_start_ylim_max_);
                    if (drag_start_following_)
                        active_axes_->resume_follow();
                }
                if (view_gesture_open_ && view_gesture_figure_)
                    apply_view(view_gesture_figure_, view_gesture_before_);   // linked axes too
                view_gesture_open_ = false;
                view_gesture_before_.clear();

                // Pan-mode click behavior: select/highlight nearest point (data tip).
                if (data_interaction_)
                {
                    data_interaction_->on_mouse_click_datatip_only(0, x, y, mods);
                }
                return;
            }
        }

        // Compute release velocity for inertial pan
        if ((transition_engine_ || anim_ctrl_) && active_axes_)
        {
            auto  now        = Clock::now();
            float dt_sec     = std::chrono::duration<float>(now - last_move_time_).count();
            float drag_total = std::chrono::duration<float>(now - drag_start_time_).count();

            // Only apply inertia if the drag was short and recent movement exists
            if (dt_sec < 0.1f && dt_sec > 0.0f && drag_total > 0.05f)
            {
                // Skip inertia if mouse barely moved — prevents spurious
                // acceleration from sub-pixel or 1-2 px jitter on release
                auto            dx_px               = static_cast<float>(x - last_move_x_);
                auto            dy_px               = static_cast<float>(y - last_move_y_);
                float           dist_px             = std::sqrt(dx_px * dx_px + dy_px * dy_px);
                constexpr float MIN_RELEASE_DIST_PX = 2.0f;

                if (dist_px >= MIN_RELEASE_DIST_PX)
                {
                    // Velocity in axis space (decades / octaves per second on a
                    // log axis) so the glide continues the drag at the same
                    // screen speed; the animation applies it the same way.
                    const auto&      vp = viewport_for_axes(active_axes_);
                    const AxisLimits xlim =
                        axis_space_limits(active_axes_->xscale(), active_axes_->x_limits());
                    const AxisLimits ylim =
                        axis_space_limits(active_axes_->yscale(), active_axes_->y_limits());

                    double x_range = xlim.max - xlim.min;
                    double y_range = ylim.max - ylim.min;

                    // Use a minimum dt floor to prevent velocity blow-up from
                    // sub-millisecond intervals between last move and release
                    constexpr float MIN_DT_SEC   = 0.008f;   // 8ms floor
                    float           effective_dt = std::max(dt_sec, MIN_DT_SEC);

                    // Screen velocity → axis-space velocity
                    double vx_screen = dx_px / effective_dt;
                    double vy_screen = dy_px / effective_dt;

                    // Clamp screen velocity as a safety net
                    constexpr double MAX_SCREEN_VEL = 3000.0;   // px/sec
                    vx_screen = std::clamp(vx_screen, -MAX_SCREEN_VEL, MAX_SCREEN_VEL);
                    vy_screen = std::clamp(vy_screen, -MAX_SCREEN_VEL, MAX_SCREEN_VEL);

                    auto vx_data = static_cast<float>(-vx_screen * x_range / vp.w);
                    auto vy_data = static_cast<float>(vy_screen * y_range / vp.h);

                    float speed = std::sqrt(vx_data * vx_data + vy_data * vy_data);
                    if (speed > MIN_INERTIA_VELOCITY)
                    {
                        SPECTRA_LOG_DEBUG("input",
                                          "Inertial pan: v=(" + std::to_string(vx_data) + ", "
                                              + std::to_string(vy_data) + ")");
                        if (transition_engine_)
                        {
                            transition_engine_->animate_inertial_pan(*active_axes_,
                                                                     vx_data,
                                                                     vy_data,
                                                                     PAN_INERTIA_DURATION);
                        }
                        else if (anim_ctrl_)
                        {
                            anim_ctrl_->animate_inertial_pan(*active_axes_,
                                                             vx_data,
                                                             vy_data,
                                                             PAN_INERTIA_DURATION);
                        }
                    }
                }
            }
        }

        // One history entry per drag; an inertial glide is folded into it
        // when it ends (finalize_pending_view).
        end_view_gesture("Pan", has_active_animations());
    }
}

// ─── 3D camera drag (orbit/pan) on mouse move ──────────────────────────────

void InputHandler::handle_mouse_move_3d_drag(double x, double y)
{
    if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
    {
        auto& cam = axes3d->camera();
        auto  dx  = static_cast<float>(x - drag_start_x_);
        auto  dy  = static_cast<float>(y - drag_start_y_);

        if (is_3d_orbit_drag_)
        {
            if (transition_engine_)
                transition_engine_->cancel_for_camera(&cam);
            else if (anim_ctrl_)
                anim_ctrl_->cancel_all();
            // A view preset switched to orthographic; orbiting away from the
            // axis view returns to perspective (an explicit choice is kept).
            // The mark stays: a click that undoes jitter restores the
            // orthographic preset camera, which should still count as automatic.
            if (cam.projection_mode == Camera::ProjectionMode::Orthographic
                && axes3d_auto_ortho(axes3d) && (dx != 0.0f || dy != 0.0f))
                cam.projection_mode = Camera::ProjectionMode::Perspective;
            cam.orbit(dx * ORBIT_SENSITIVITY, -dy * ORBIT_SENSITIVITY);
        }
        else if (is_3d_pan_drag_)
        {
            const auto& vp = viewport_for_axes(axes3d);
            axes3d->pan_limits(dx, dy, vp.w, vp.h);
            if (axis_link_mgr_)
                axis_link_mgr_->propagate_from_3d(axes3d);
        }

        drag_start_x_ = x;
        drag_start_y_ = y;
    }
}

// ─── Right-click drag zoom on mouse move ────────────────────────────────────

void InputHandler::handle_mouse_move_rclick_zoom(double x, double y)
{
    double dx_total     = x - rclick_zoom_start_x_;
    double dy_total     = y - rclick_zoom_start_y_;
    double dx_delta     = x - rclick_zoom_last_x_;
    double dy_delta     = y - rclick_zoom_last_y_;
    rclick_zoom_last_x_ = x;
    rclick_zoom_last_y_ = y;

    // Lock axis direction once drag exceeds threshold
    if (rclick_zoom_axis_ == ZoomAxis::None)
    {
        double abs_dx = std::abs(dx_total);
        double abs_dy = std::abs(dy_total);
        if (abs_dx >= RCLICK_AXIS_LOCK_THRESHOLD || abs_dy >= RCLICK_AXIS_LOCK_THRESHOLD)
        {
            if (rclick_zoom_3d_)
            {
                // 3D: pick the axis whose screen-projected direction best aligns
                // with the drag direction (camera-aware, works at any view angle).
                auto drag_len = static_cast<float>(std::sqrt(abs_dx * abs_dx + abs_dy * abs_dy));
                if (drag_len > 1e-4f)
                {
                    float ndx      = static_cast<float>(dx_total) / drag_len;
                    float ndy      = static_cast<float>(dy_total) / drag_len;
                    float best_dot = -1.0f;
                    int   best_i   = 0;
                    for (int i = 0; i < 3; ++i)
                    {
                        // Use absolute dot product — axis direction or its opposite both zoom
                        float d =
                            std::abs(ndx * rclick_zoom_axis_sx_[i] + ndy * rclick_zoom_axis_sy_[i]);
                        if (d > best_dot)
                        {
                            best_dot = d;
                            best_i   = i;
                        }
                    }
                    static constexpr ZoomAxis kMap[3] = {ZoomAxis::X, ZoomAxis::Y, ZoomAxis::Z};
                    rclick_zoom_axis_                 = kMap[best_i];
                }
            }
            else
            {
                // 2D: 15° cones near horizontal/vertical are pure 1D.
                // Middle angles use proportional XY blending.
                constexpr double kRadToDeg = 57.29577951308232;
                auto angle_deg = static_cast<float>(std::atan2(abs_dy, abs_dx) * kRadToDeg);
                if (angle_deg <= RCLICK_AXIS_1D_THRESHOLD_DEG)
                {
                    rclick_zoom_axis_ = ZoomAxis::X;
                }
                else if (angle_deg >= (90.0f - RCLICK_AXIS_1D_THRESHOLD_DEG))
                {
                    rclick_zoom_axis_ = ZoomAxis::Y;
                }
                else
                {
                    rclick_zoom_axis_ = ZoomAxis::XY;
                }
            }
        }
    }

    // Apply zoom once axis is locked
    if (rclick_zoom_axis_ != ZoomAxis::None)
    {
        // Use the dominant delta component for zoom magnitude
        // Drag right/up = zoom in (shrink range), drag left/down = zoom out (expand range)
        float pixel_delta = 0.0f;
        if (rclick_zoom_axis_ == ZoomAxis::X)
            pixel_delta = static_cast<float>(dx_delta);
        else if (rclick_zoom_axis_ == ZoomAxis::Y)
            pixel_delta = static_cast<float>(-dy_delta);   // screen Y inverted
        else if (rclick_zoom_axis_ == ZoomAxis::XY || rclick_zoom_axis_ == ZoomAxis::Z)
            pixel_delta = static_cast<float>(dx_delta - dy_delta) * 0.5f;

        float factor = 1.0f - pixel_delta * RCLICK_ZOOM_SENSITIVITY;
        factor       = std::clamp(factor, 0.9f, 1.1f);   // limit per-frame zoom step

        float factor_x = factor;
        float factor_y = factor;
        if (!rclick_zoom_3d_ && rclick_zoom_axis_ == ZoomAxis::XY)
        {
            // Blend each axis toward identity based on drag direction:
            // near-horizontal => mostly X, near-vertical => mostly Y.
            double abs_dx_total = std::abs(dx_total);
            double abs_dy_total = std::abs(dy_total);
            double sum          = abs_dx_total + abs_dy_total;
            float  x_weight     = 0.5f;
            float  y_weight     = 0.5f;
            if (sum > 1e-6)
            {
                x_weight = static_cast<float>(abs_dx_total / sum);
                y_weight = static_cast<float>(abs_dy_total / sum);
            }

            factor_x = 1.0f + (factor - 1.0f) * x_weight;
            factor_y = 1.0f + (factor - 1.0f) * y_weight;
        }

        if (rclick_zoom_3d_)
        {
            if (auto* axes3d = dynamic_cast<Axes3D*>(active_axes_base_))
            {
                if (rclick_zoom_axis_ == ZoomAxis::X)
                    axes3d->zoom_limits_x(factor);
                else if (rclick_zoom_axis_ == ZoomAxis::Y)
                    axes3d->zoom_limits_y(factor);
                else if (rclick_zoom_axis_ == ZoomAxis::Z)
                    axes3d->zoom_limits_z(factor);
                if (axis_link_mgr_)
                    axis_link_mgr_->propagate_from_3d(axes3d);
            }
        }
        else if (active_axes_)
        {
            const double anchor_x = rclick_zoom_anchor_data_x_;
            const double anchor_y = rclick_zoom_anchor_data_y_;

            if (active_axes_->is_presented_buffer_following()
                && (rclick_zoom_axis_ == ZoomAxis::X || rclick_zoom_axis_ == ZoomAxis::XY))
            {
                float seconds = active_axes_->presented_buffer_seconds();
                seconds       = std::clamp(seconds * factor_x, 0.1f, 86400.0f);
                active_axes_->presented_buffer(seconds);
            }
            else if (rclick_zoom_axis_ == ZoomAxis::X || rclick_zoom_axis_ == ZoomAxis::XY)
            {
                // Zoom anchored at drag-start X position:
                const AxisLimits z        = zoom_limits_about(active_axes_->xscale(),
                                                       active_axes_->x_limits(),
                                                       anchor_x,
                                                       factor_x);
                double           new_xmin = z.min;
                double           new_xmax = z.max;
                {
                    double abs_max   = std::max(std::abs(new_xmin), std::abs(new_xmax));
                    double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
                    if (min_range < 1e-300)
                        min_range = 1e-300;
                    if (new_xmax - new_xmin < min_range)
                    {
                        double mid = (new_xmin + new_xmax) * 0.5;
                        new_xmin   = mid - min_range * 0.5;
                        new_xmax   = mid + min_range * 0.5;
                    }
                }
                active_axes_->xlim(new_xmin, new_xmax);
            }

            if (rclick_zoom_axis_ == ZoomAxis::Y || rclick_zoom_axis_ == ZoomAxis::XY)
            {
                // Zoom anchored at drag-start Y position
                const AxisLimits z        = zoom_limits_about(active_axes_->yscale(),
                                                       active_axes_->y_limits(),
                                                       anchor_y,
                                                       factor_y);
                double           new_ymin = z.min;
                double           new_ymax = z.max;
                {
                    double abs_max   = std::max(std::abs(new_ymin), std::abs(new_ymax));
                    double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
                    if (min_range < 1e-300)
                        min_range = 1e-300;
                    if (new_ymax - new_ymin < min_range)
                    {
                        double mid = (new_ymin + new_ymax) * 0.5;
                        new_ymin   = mid - min_range * 0.5;
                        new_ymax   = mid + min_range * 0.5;
                    }
                }
                active_axes_->ylim(new_ymin, new_ymax);
            }
            if (axis_link_mgr_)
                axis_link_mgr_->propagate_limits(active_axes_,
                                                 active_axes_->x_limits(),
                                                 active_axes_->y_limits());
        }
    }
}

// ─── Pan/BoxZoom drag on mouse move ─────────────────────────────────────────

void InputHandler::handle_mouse_move_pan_drag(double x, double y)
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
        last_move_x_    = x;
        last_move_y_    = y;
        last_move_time_ = Clock::now();

        // Pan logic
        const auto& vp = viewport_for_axes(active_axes_);

        // Compute drag delta in screen pixels
        double dx_screen = x - drag_start_x_;
        double dy_screen = y - drag_start_y_;

        // Convert pixel delta to a fraction of the axis span and shift in
        // axis space (double precision to avoid catastrophic cancellation at
        // deep zoom; log axes pan by whole ratios instead of going negative).
        const AxisLimits new_x = pan_limits(active_axes_->xscale(),
                                            {drag_start_xlim_min_, drag_start_xlim_max_},
                                            -dx_screen / vp.w);
        const AxisLimits new_y = pan_limits(active_axes_->yscale(),
                                            {drag_start_ylim_min_, drag_start_ylim_max_},
                                            dy_screen / vp.h);

        active_axes_->xlim(new_x.min, new_x.max);
        active_axes_->ylim(new_y.min, new_y.max);

        // Propagate pan to linked axes
        if (axis_link_mgr_)
        {
            axis_link_mgr_->propagate_limits(active_axes_,
                                             active_axes_->x_limits(),
                                             active_axes_->y_limits());
        }
    }
    else if (tool_mode_ == ToolMode::BoxZoom)
    {
        // Box zoom logic
        box_zoom_.x1 = x;
        box_zoom_.y1 = y;
    }
}

// ─── Scroll: 3D zoom ───────────────────────────────────────────────────────

void InputHandler::handle_scroll_3d(Axes3D* axes3d, double y_offset)
{
    auto  before_xlim = axes3d->x_limits();
    auto  before_ylim = axes3d->y_limits();
    auto  before_zlim = axes3d->z_limits();
    float factor      = (y_offset > 0) ? (1.0f - ZOOM_3D_FACTOR) : (1.0f + ZOOM_3D_FACTOR);
    axes3d->zoom_limits(factor);
    if (axis_link_mgr_)
        axis_link_mgr_->propagate_from_3d(axes3d);
    if (undo_mgr_)
    {
        auto    after_xlim = axes3d->x_limits();
        auto    after_ylim = axes3d->y_limits();
        auto    after_zlim = axes3d->z_limits();
        Axes3D* ax         = axes3d;
        undo_mgr_->push(UndoAction{"Zoom 3D",
                                   [ax, before_xlim, before_ylim, before_zlim]()
                                   {
                                       ax->xlim(before_xlim.min, before_xlim.max);
                                       ax->ylim(before_ylim.min, before_ylim.max);
                                       ax->zlim(before_zlim.min, before_zlim.max);
                                   },
                                   [ax, after_xlim, after_ylim, after_zlim]()
                                   {
                                       ax->xlim(after_xlim.min, after_xlim.max);
                                       ax->ylim(after_ylim.min, after_ylim.max);
                                       ax->zlim(after_zlim.min, after_zlim.max);
                                   }});
    }
}

// ─── Scroll: 2D zoom / horizontal pan ───────────────────────────────────────

void InputHandler::handle_scroll_2d(double   x_offset,
                                    double   y_offset,
                                    double   cursor_x,
                                    double   cursor_y,
                                    ViewAxes axes)
{
    if (!active_axes_)
        return;

    // An inertial glide or animated zoom still running ends here.
    finalize_pending_view();
    cancel_view_animations(active_axes_);

    const auto& vp = viewport_for_axes(active_axes_);

    // Compute cursor position in data space
    float saved_vp_x = vp_x_;
    float saved_vp_y = vp_y_;
    float saved_vp_w = vp_w_;
    float saved_vp_h = vp_h_;
    vp_x_            = vp.x;
    vp_y_            = vp.y;
    vp_w_            = vp.w;
    vp_h_            = vp.h;

    double data_x = NAN;
    double data_y = NAN;
    screen_to_data(cursor_x, cursor_y, data_x, data_y);

    vp_x_ = saved_vp_x;
    vp_y_ = saved_vp_y;
    vp_w_ = saved_vp_w;
    vp_h_ = saved_vp_h;

    const bool zoom_x = view_axes_has_x(axes);
    const bool zoom_y = view_axes_has_y(axes);

    // Exponential zoom: symmetric in both directions.
    // scroll up (y_offset>0) → factor<1 (zoom in), scroll down → factor>1 (zoom out)
    float factor = std::pow(1.0f / (1.0f + ZOOM_FACTOR), static_cast<float>(y_offset));
    factor       = std::clamp(factor, 0.1f, 10.0f);

    // Live views keep following while the user changes the visible history
    // span with the wheel. Manual Y overrides are handled separately.
    if (y_offset != 0.0 && zoom_x && active_axes_->is_presented_buffer_following()
        && active_axes_->has_presented_buffer())
    {
        float seconds = active_axes_->presented_buffer_seconds();
        seconds       = std::clamp(seconds * factor, 0.1f, 86400.0f);
        active_axes_->presented_buffer(seconds);
        return;
    }

    const ViewState before = capture_view(active_axes_);
    const char*     label  = "Zoom";
    std::string     key;

    // Apply zoom instantly — scroll zoom must be immediate and responsive.
    // (Animations are used for auto-fit, box zoom, and inertial pan instead.)
    // Double precision preserves deep zoom; log / sqrt axes zoom in axis
    // space so the cursor stays fixed on screen.
    if (y_offset != 0.0)
    {
        if (zoom_x)
        {
            const AxisLimits zx = clamp_min_span(zoom_limits_about(active_axes_->xscale(),
                                                                   active_axes_->x_limits(),
                                                                   data_x,
                                                                   factor));
            active_axes_->xlim(zx.min, zx.max);
        }
        if (zoom_y)
        {
            const AxisLimits zy = clamp_min_span(zoom_limits_about(active_axes_->yscale(),
                                                                   active_axes_->y_limits(),
                                                                   data_y,
                                                                   factor));
            active_axes_->ylim(zy.min, zy.max);
        }
        label = axes == ViewAxes::X ? "Zoom X" : (axes == ViewAxes::Y ? "Zoom Y" : "Zoom");
        key   = "zoom";
    }

    // Horizontal wheel / trackpad swipe pans X (positive = scroll left).
    if (x_offset != 0.0 && axes != ViewAxes::Y)
    {
        const AxisLimits px = pan_limits(active_axes_->xscale(),
                                         active_axes_->x_limits(),
                                         -x_offset * WHEEL_PAN_FRACTION);
        active_axes_->xlim(px.min, px.max);
        if (key.empty())
        {
            label = "Pan X";
            key   = "pan";
        }
    }

    if (key.empty())
        return;

    // Linked axes follow (each takes the linked dimension's new limits).
    if (axis_link_mgr_)
        axis_link_mgr_->propagate_limits(active_axes_,
                                         active_axes_->x_limits(),
                                         active_axes_->y_limits());

    // One history entry per wheel burst on this axes / axis combination.
    key += ":" + std::to_string(reinterpret_cast<std::uintptr_t>(active_axes_)) + ":"
           + std::to_string(static_cast<int>(axes));
    record_view_change(figure_,
                       label,
                       before,
                       capture_view(active_axes_),
                       std::move(key),
                       WHEEL_COALESCE_WINDOW);
}

// ─── View history ───────────────────────────────────────────────────────────

ViewState InputHandler::capture_view(Axes* ax) const
{
    ViewState state;
    if (!ax)
        return state;
    state.push_back({ax, ax->x_limits(), ax->y_limits()});
    if (axis_link_mgr_)
    {
        for (Axes* peer : axis_link_mgr_->linked_peers(ax))
            if (peer && !find_axes_view(state, peer))
                state.push_back({peer, peer->x_limits(), peer->y_limits()});
    }
    return state;
}

ViewState InputHandler::capture_figure_view(const Figure& fig)
{
    ViewState state;
    for (const auto& ax : fig.axes())
        if (ax)
            state.push_back({ax.get(), ax->x_limits(), ax->y_limits()});
    return state;
}

void InputHandler::cancel_view_animations(Axes* ax)
{
    if (!ax)
        return;
    if (transition_engine_)
        transition_engine_->cancel_for_axes(ax);
    if (anim_ctrl_)
        anim_ctrl_->cancel_for_axes(ax);
}

void InputHandler::apply_view(Figure* fig, const ViewState& state)
{
    if (!fig)
        return;
    for (const auto& v : state)
    {
        const auto& all = fig->axes();
        if (std::any_of(all.begin(), all.end(), [&](const auto& p) { return p.get() == v.axes; }))
            cancel_view_animations(v.axes);
    }
    apply_view_state(*fig, state);
}

void InputHandler::record_view_change(Figure*                             fig,
                                      std::string                         label,
                                      ViewState                           before,
                                      ViewState                           after,
                                      std::string                         coalesce_key,
                                      std::chrono::steady_clock::duration coalesce_window)
{
    if (!fig)
        fig = figure_;

    if (!fig)
    {
        // No figure to keep a history for (bare embedding): undo only.
        if (undo_mgr_ && !same_view(before, after))
        {
            auto restore = [](const ViewState& st)
            {
                for (const auto& v : st)
                {
                    v.axes->xlim(v.x.min, v.x.max);
                    v.axes->ylim(v.y.min, v.y.max);
                }
            };
            undo_mgr_->push(UndoAction{label,
                                       [restore, before]() { restore(before); },
                                       [restore, after]() { restore(after); }});
        }
        return;
    }

    // A new change ends the previous animated one: where it started is
    // where the previous one ended.
    finalize_pending_view(&before);

    std::shared_ptr<ViewHistory> history = view_history_.history(fig);

    // Coalescing extends the newest entry and its undo step in place, which
    // is only right while that undo step is still on top of the stack and
    // nothing was undone / redone since (another action pushed in between,
    // or an undo whose redo a new step must clear).
    if (undo_mgr_ && !coalesce_key.empty())
    {
        auto last = history->last_entry();
        if (last
            && (view_undo_entry_.lock() != last || undo_mgr_->revision() != view_undo_revision_))
            history->seal_last();
    }
    auto result = history->push(label,
                                std::move(before),
                                std::move(after),
                                std::move(coalesce_key),
                                coalesce_window);
    if (!result.entry)
        return;

    // A coalesced step extends the undo step pushed with the entry.
    if (result.coalesced || !undo_mgr_)
        return;

    std::weak_ptr<ViewHistory> weak  = history;
    auto                       entry = result.entry;
    undo_mgr_->push(UndoAction{entry->label,
                               [this, weak, entry, fig]()
                               {
                                   auto h = weak.lock();
                                   if (!h)
                                       return;   // figure closed
                                   finalize_pending_view();
                                   apply_view(fig, h->undo_entry(entry));
                               },
                               [this, weak, entry, fig]()
                               {
                                   auto h = weak.lock();
                                   if (!h)
                                       return;
                                   finalize_pending_view();
                                   apply_view(fig, h->redo_entry(entry));
                               }});
    view_undo_entry_    = entry;
    view_undo_revision_ = undo_mgr_->revision();
}

void InputHandler::commit_axes_limits_edit(Figure*     fig,
                                           Axes*       ax,
                                           AxisLimits  old_x,
                                           AxisLimits  old_y,
                                           std::string coalesce_key)
{
    if (!ax)
        return;
    ViewState before = capture_view(ax);
    set_axes_view(before, ax, old_x, old_y);
    if (axis_link_mgr_)
        axis_link_mgr_->propagate_limits(ax, ax->x_limits(), ax->y_limits());
    ViewState after = capture_view(ax);

    // The axes must belong to the figure whose history records it.
    if (!fig)
        fig = figure_;
    if (fig)
    {
        const auto& all = fig->axes();
        if (std::none_of(all.begin(), all.end(), [&](const auto& p) { return p.get() == ax; }))
            fig = nullptr;
    }
    if (!fig)
        return;
    record_view_change(fig, "Edit axis limits", std::move(before), std::move(after), coalesce_key);
}

void InputHandler::finalize_pending_view(const ViewState* end_state)
{
    if (!pending_view_entry_)
        return;
    auto    entry        = std::move(pending_view_entry_);
    Figure* fig          = pending_view_figure_;
    pending_view_entry_  = nullptr;
    pending_view_figure_ = nullptr;
    if (!fig || !view_history_.find(fig))
        return;
    const auto& all = fig->axes();
    for (auto& v : entry->after)
    {
        if (const AxesView* known = end_state ? find_axes_view(*end_state, v.axes) : nullptr)
        {
            v.x = known->x;
            v.y = known->y;
        }
        else if (std::any_of(all.begin(),
                             all.end(),
                             [&](const auto& p) { return p.get() == v.axes; }))
        {
            v.x = v.axes->x_limits();
            v.y = v.axes->y_limits();
        }
    }
}

bool InputHandler::can_view_back(const Figure* fig) const
{
    const ViewHistory* h = view_history_.find(fig ? fig : figure_);
    return h && h->can_back();
}

bool InputHandler::can_view_forward(const Figure* fig) const
{
    const ViewHistory* h = view_history_.find(fig ? fig : figure_);
    return h && h->can_forward();
}

bool InputHandler::view_back(Figure* fig)
{
    if (!fig)
        fig = figure_;
    if (!fig)
        return false;
    finalize_pending_view();
    ViewHistory* h = view_history_.find(fig);
    if (!h)
        return false;
    const ViewState* state = h->back();
    if (!state)
        return false;
    apply_view(fig, *state);
    return true;
}

bool InputHandler::view_forward(Figure* fig)
{
    if (!fig)
        fig = figure_;
    if (!fig)
        return false;
    finalize_pending_view();
    ViewHistory* h = view_history_.find(fig);
    if (!h)
        return false;
    const ViewState* state = h->forward();
    if (!state)
        return false;
    apply_view(fig, *state);
    return true;
}

void InputHandler::begin_view_gesture()
{
    finalize_pending_view();
    view_gesture_open_   = active_axes_ != nullptr;
    view_gesture_figure_ = figure_;
    view_gesture_before_ = capture_view(active_axes_);
}

void InputHandler::end_view_gesture(const char* label, bool settles_later)
{
    if (!view_gesture_open_)
        return;
    view_gesture_open_ = false;
    if (view_gesture_before_.empty())
        return;
    Axes*     ax    = view_gesture_before_.front().axes;
    Figure*   fig   = view_gesture_figure_ ? view_gesture_figure_ : figure_;
    ViewState after = capture_view(ax);
    record_view_change(fig, label, std::move(view_gesture_before_), std::move(after));
    view_gesture_before_.clear();

    // An inertial glide keeps moving the view: its end state is folded into
    // this entry once it stops (finalize_pending_view).
    if (settles_later && fig)
    {
        if (ViewHistory* h = view_history_.find(fig); h && h->cursor() == h->size() && h->size())
        {
            pending_view_entry_  = view_history_.history(fig)->last_entry();
            pending_view_figure_ = fig;
        }
    }
}

// ─── Axis gutters ───────────────────────────────────────────────────────────

AxisGutter InputHandler::hit_test_gutter(double screen_x, double screen_y, Axes** out_axes) const
{
    if (out_axes)
        *out_axes = nullptr;
    if (!figure_)
        return AxisGutter::None;
    for (const auto& ax : figure_->axes())
    {
        if (!ax)
            continue;
        const AxisGutter g = hit_test_axis_gutter(ax->viewport(), screen_x, screen_y);
        if (g != AxisGutter::None)
        {
            if (out_axes)
                *out_axes = ax.get();
            return g;
        }
    }
    return AxisGutter::None;
}

void InputHandler::handle_gutter_press(AxisGutter gutter,
                                       Axes*      axes,
                                       int        button,
                                       double     x,
                                       double     y)
{
    active_axes_       = axes;
    active_axes_base_  = axes;
    last_pressed_axes_ = axes;
    cancel_view_animations(axes);
    begin_view_gesture();
    gutter_drag_        = gutter;
    gutter_drag_button_ = button;
    gutter_drag_x_      = x;
    gutter_drag_y_      = y;
    gutter_drag_lim_    = gutter == AxisGutter::X ? axes->x_limits() : axes->y_limits();
}

void InputHandler::handle_gutter_move(double x, double y)
{
    if (!active_axes_)
        return;
    const auto& vp = active_axes_->viewport();
    if (gutter_drag_ == AxisGutter::X && vp.w > 0.0f)
    {
        const AxisLimits l =
            pan_limits(active_axes_->xscale(), gutter_drag_lim_, -(x - gutter_drag_x_) / vp.w);
        active_axes_->xlim(l.min, l.max);
    }
    else if (gutter_drag_ == AxisGutter::Y && vp.h > 0.0f)
    {
        const AxisLimits l =
            pan_limits(active_axes_->yscale(), gutter_drag_lim_, (y - gutter_drag_y_) / vp.h);
        active_axes_->ylim(l.min, l.max);
    }
    if (axis_link_mgr_)
        axis_link_mgr_->propagate_limits(active_axes_,
                                         active_axes_->x_limits(),
                                         active_axes_->y_limits());
}

void InputHandler::end_gutter_drag(bool commit)
{
    const AxisGutter g = gutter_drag_;
    gutter_drag_       = AxisGutter::None;
    if (g == AxisGutter::None)
        return;
    if (commit)
    {
        end_view_gesture(g == AxisGutter::X ? "Pan X" : "Pan Y");
        return;
    }
    // Cancelled (Esc): put the axis back where the drag started.
    if (view_gesture_open_ && view_gesture_figure_)
        apply_view(view_gesture_figure_, view_gesture_before_);
    view_gesture_open_ = false;
    view_gesture_before_.clear();
}

}   // namespace spectra

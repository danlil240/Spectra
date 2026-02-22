#include "animation_controller.hpp"

#include <algorithm>
#include <cmath>

#include "camera.hpp"

namespace spectra
{

// ─── Animate axis limits ────────────────────────────────────────────────────

AnimationController::AnimId AnimationController::animate_axis_limits(Axes&      axes,
                                                                     AxisLimits target_x,
                                                                     AxisLimits target_y,
                                                                     float      duration_sec,
                                                                     EasingFn   easing)
{
    // Cancel any existing limit animation on this axes to avoid conflicts
    cancel_for_axes(&axes);

    AnimId    id = next_id_++;
    LimitAnim anim;
    anim.id       = id;
    anim.axes     = &axes;
    anim.start_x  = axes.x_limits();
    anim.start_y  = axes.y_limits();
    anim.target_x = target_x;
    anim.target_y = target_y;
    anim.duration = duration_sec;
    anim.easing   = easing;
    anim.elapsed  = 0.0f;
    anim.finished = false;
    limit_anims_.push_back(anim);
    return id;
}

// ─── Animate inertial pan ───────────────────────────────────────────────────

AnimationController::AnimId AnimationController::animate_inertial_pan(Axes& axes,
                                                                      float vx_data,
                                                                      float vy_data,
                                                                      float duration_sec)
{
    // Cancel any existing inertial pan on this axes
    for (auto& a : inertial_anims_)
    {
        if (a.axes == &axes && !a.finished)
        {
            a.finished = true;
        }
    }

    AnimId          id = next_id_++;
    InertialPanAnim anim;
    anim.id       = id;
    anim.axes     = &axes;
    anim.vx_data  = vx_data;
    anim.vy_data  = vy_data;
    anim.duration = duration_sec;
    anim.elapsed  = 0.0f;
    anim.finished = false;
    inertial_anims_.push_back(anim);
    return id;
}

// ─── Animate camera ─────────────────────────────────────────────────────────

AnimationController::AnimId AnimationController::animate_camera(Camera&       camera,
                                                                const Camera& target,
                                                                float         duration_sec,
                                                                EasingFn      easing)
{
    AnimId     id = next_id_++;
    CameraAnim anim;
    anim.id                = id;
    anim.camera            = &camera;
    anim.start_azimuth     = camera.azimuth;
    anim.start_elevation   = camera.elevation;
    anim.start_distance    = camera.distance;
    anim.start_fov         = camera.fov;
    anim.start_ortho_size  = camera.ortho_size;
    anim.target_azimuth    = target.azimuth;
    anim.target_elevation  = target.elevation;
    anim.target_distance   = target.distance;
    anim.target_fov        = target.fov;
    anim.target_ortho_size = target.ortho_size;
    anim.duration          = duration_sec;
    anim.easing            = easing;
    anim.elapsed           = 0.0f;
    anim.finished          = false;
    camera_anims_.push_back(anim);
    return id;
}

// ─── Cancel ─────────────────────────────────────────────────────────────────

void AnimationController::cancel(AnimId id)
{
    for (auto& a : limit_anims_)
    {
        if (a.id == id)
            a.finished = true;
    }
    for (auto& a : inertial_anims_)
    {
        if (a.id == id)
            a.finished = true;
    }
    for (auto& a : camera_anims_)
    {
        if (a.id == id)
            a.finished = true;
    }
}

void AnimationController::cancel_for_axes(Axes* axes)
{
    for (auto& a : limit_anims_)
    {
        if (a.axes == axes)
            a.finished = true;
    }
    for (auto& a : inertial_anims_)
    {
        if (a.axes == axes)
            a.finished = true;
    }
}

void AnimationController::cancel_all()
{
    for (auto& a : limit_anims_)
        a.finished = true;
    for (auto& a : inertial_anims_)
        a.finished = true;
    for (auto& a : camera_anims_)
        a.finished = true;
}

// ─── Update ─────────────────────────────────────────────────────────────────

void AnimationController::update(float dt)
{
    // Update limit animations
    for (auto& a : limit_anims_)
    {
        if (a.finished)
            continue;

        a.elapsed += dt;
        float t     = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);
        float eased = a.easing(t);

        float xmin = a.start_x.min + (a.target_x.min - a.start_x.min) * eased;
        float xmax = a.start_x.max + (a.target_x.max - a.start_x.max) * eased;
        float ymin = a.start_y.min + (a.target_y.min - a.start_y.min) * eased;
        float ymax = a.start_y.max + (a.target_y.max - a.start_y.max) * eased;

        a.axes->xlim(xmin, xmax);
        a.axes->ylim(ymin, ymax);

        if (t >= 1.0f)
        {
            // Snap to exact target
            a.axes->xlim(a.target_x.min, a.target_x.max);
            a.axes->ylim(a.target_y.min, a.target_y.max);
            a.finished = true;
        }
    }

    // Update inertial pan animations (quadratic deceleration)
    for (auto& a : inertial_anims_)
    {
        if (a.finished)
            continue;

        a.elapsed += dt;
        float t = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);

        // Deceleration: velocity = v0 * (1 - t)^2
        float decay = (1.0f - t) * (1.0f - t);
        float vx    = a.vx_data * decay;
        float vy    = a.vy_data * decay;

        // Apply velocity as displacement this frame
        auto xlim = a.axes->x_limits();
        auto ylim = a.axes->y_limits();

        a.axes->xlim(xlim.min + vx * dt, xlim.max + vx * dt);
        a.axes->ylim(ylim.min + vy * dt, ylim.max + vy * dt);

        if (t >= 1.0f)
        {
            a.finished = true;
        }
    }

    // Update camera animations
    for (auto& a : camera_anims_)
    {
        if (a.finished)
            continue;

        a.elapsed += dt;
        float t     = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);
        float eased = a.easing(t);

        a.camera->azimuth   = a.start_azimuth + (a.target_azimuth - a.start_azimuth) * eased;
        a.camera->elevation = a.start_elevation + (a.target_elevation - a.start_elevation) * eased;
        a.camera->distance  = a.start_distance + (a.target_distance - a.start_distance) * eased;
        a.camera->fov       = a.start_fov + (a.target_fov - a.start_fov) * eased;
        a.camera->ortho_size =
            a.start_ortho_size + (a.target_ortho_size - a.start_ortho_size) * eased;

        a.camera->update_position_from_orbit();

        if (t >= 1.0f)
        {
            a.camera->azimuth    = a.target_azimuth;
            a.camera->elevation  = a.target_elevation;
            a.camera->distance   = a.target_distance;
            a.camera->fov        = a.target_fov;
            a.camera->ortho_size = a.target_ortho_size;
            a.camera->update_position_from_orbit();
            a.finished = true;
        }
    }

    gc();
}

// ─── Queries ────────────────────────────────────────────────────────────────

bool AnimationController::has_active_animations() const
{
    for (const auto& a : limit_anims_)
        if (!a.finished)
            return true;
    for (const auto& a : inertial_anims_)
        if (!a.finished)
            return true;
    for (const auto& a : camera_anims_)
        if (!a.finished)
            return true;
    return false;
}

size_t AnimationController::active_count() const
{
    size_t count = 0;
    for (const auto& a : limit_anims_)
        if (!a.finished)
            ++count;
    for (const auto& a : inertial_anims_)
        if (!a.finished)
            ++count;
    for (const auto& a : camera_anims_)
        if (!a.finished)
            ++count;
    return count;
}

bool AnimationController::get_pending_target(const Axes* axes,
                                             AxisLimits& out_x,
                                             AxisLimits& out_y) const
{
    for (const auto& a : limit_anims_)
    {
        if (a.axes == axes && !a.finished)
        {
            out_x = a.target_x;
            out_y = a.target_y;
            return true;
        }
    }
    return false;
}

// ─── GC ─────────────────────────────────────────────────────────────────────

void AnimationController::gc()
{
    std::erase_if(limit_anims_, [](const LimitAnim& a) { return a.finished; });
    std::erase_if(inertial_anims_, [](const InertialPanAnim& a) { return a.finished; });
}

}   // namespace spectra

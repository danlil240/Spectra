#include "transition_engine.hpp"

#include <algorithm>
#include <cmath>

namespace plotix
{

// ─── Animate float ──────────────────────────────────────────────────────────

TransitionEngine::AnimId TransitionEngine::animate(float& target,
                                                   float end,
                                                   float duration,
                                                   EasingFunc easing)
{
    std::lock_guard lock(mutex_);

    // Cancel any existing animation on the same target pointer
    for (auto& a : float_anims_)
    {
        if (a.target == &target && !a.finished)
        {
            a.finished = true;
        }
    }

    AnimId id = next_id_++;
    FloatAnim anim;
    anim.id = id;
    anim.target = &target;
    anim.start = target;
    anim.end = end;
    anim.duration = duration;
    anim.easing = std::move(easing);
    anim.elapsed = 0.0f;
    anim.finished = false;
    float_anims_.push_back(std::move(anim));
    return id;
}

// ─── Animate Color ──────────────────────────────────────────────────────────

TransitionEngine::AnimId TransitionEngine::animate(Color& target,
                                                   Color end,
                                                   float duration,
                                                   EasingFunc easing)
{
    std::lock_guard lock(mutex_);

    // Cancel any existing animation on the same target pointer
    for (auto& a : color_anims_)
    {
        if (a.target == &target && !a.finished)
        {
            a.finished = true;
        }
    }

    AnimId id = next_id_++;
    ColorAnim anim;
    anim.id = id;
    anim.target = &target;
    anim.start = target;
    anim.end = end;
    anim.duration = duration;
    anim.easing = std::move(easing);
    anim.elapsed = 0.0f;
    anim.finished = false;
    color_anims_.push_back(std::move(anim));
    return id;
}

// ─── Animate AxisLimits ─────────────────────────────────────────────────────

TransitionEngine::AnimId TransitionEngine::animate_limits(
    Axes& axes, AxisLimits target_x, AxisLimits target_y, float duration, EasingFunc easing)
{
    std::lock_guard lock(mutex_);

    // Cancel any existing limit animation on this axes (already under lock)
    cancel_for_axes_unlocked(&axes);

    AnimId id = next_id_++;
    LimitAnim anim;
    anim.id = id;
    anim.axes = &axes;
    anim.start_x = axes.x_limits();
    anim.start_y = axes.y_limits();
    anim.target_x = target_x;
    anim.target_y = target_y;
    anim.duration = duration;
    anim.easing = std::move(easing);
    anim.elapsed = 0.0f;
    anim.finished = false;
    limit_anims_.push_back(std::move(anim));
    return id;
}

// ─── Animate inertial pan ───────────────────────────────────────────────────

TransitionEngine::AnimId TransitionEngine::animate_inertial_pan(Axes& axes,
                                                                float vx_data,
                                                                float vy_data,
                                                                float duration)
{
    std::lock_guard lock(mutex_);

    // Cancel any existing inertial pan on this axes
    for (auto& a : inertial_anims_)
    {
        if (a.axes == &axes && !a.finished)
        {
            a.finished = true;
        }
    }

    AnimId id = next_id_++;
    InertialPanAnim anim;
    anim.id = id;
    anim.axes = &axes;
    anim.vx_data = vx_data;
    anim.vy_data = vy_data;
    anim.duration = duration;
    anim.elapsed = 0.0f;
    anim.finished = false;
    inertial_anims_.push_back(std::move(anim));
    return id;
}

// ─── Animate Camera ────────────────────────────────────────────────────────────

TransitionEngine::AnimId TransitionEngine::animate_camera(Camera& cam,
                                                          Camera target,
                                                          float duration,
                                                          EasingFunc easing)
{
    std::lock_guard lock(mutex_);

    // Cancel any existing camera animation on the same target
    for (auto& a : camera_anims_)
    {
        if (a.cam == &cam && !a.finished)
        {
            a.finished = true;
        }
    }

    AnimId id = next_id_++;
    CameraAnim anim;
    anim.id = id;
    anim.cam = &cam;
    anim.start = cam;
    anim.end = target;
    anim.duration = duration;
    anim.easing = std::move(easing);
    anim.elapsed = 0.0f;
    anim.finished = false;
    camera_anims_.push_back(std::move(anim));
    return id;
}

// ─── Cancel ─────────────────────────────────────────────────────────────────

void TransitionEngine::cancel(AnimId id)
{
    std::lock_guard lock(mutex_);
    for (auto& a : float_anims_)
        if (a.id == id)
            a.finished = true;
    for (auto& a : color_anims_)
        if (a.id == id)
            a.finished = true;
    for (auto& a : limit_anims_)
        if (a.id == id)
            a.finished = true;
    for (auto& a : inertial_anims_)
        if (a.id == id)
            a.finished = true;
    for (auto& a : camera_anims_)
        if (a.id == id)
            a.finished = true;
}

void TransitionEngine::cancel_for_axes(Axes* axes)
{
    std::lock_guard lock(mutex_);
    cancel_for_axes_unlocked(axes);
}

void TransitionEngine::cancel_for_axes_unlocked(Axes* axes)
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

void TransitionEngine::cancel_for_camera(Camera* cam)
{
    std::lock_guard lock(mutex_);
    for (auto& a : camera_anims_)
    {
        if (a.cam == cam)
            a.finished = true;
    }
}

void TransitionEngine::cancel_all()
{
    std::lock_guard lock(mutex_);
    for (auto& a : float_anims_)
        a.finished = true;
    for (auto& a : color_anims_)
        a.finished = true;
    for (auto& a : limit_anims_)
        a.finished = true;
    for (auto& a : inertial_anims_)
        a.finished = true;
    for (auto& a : camera_anims_)
        a.finished = true;
}

// ─── Update ─────────────────────────────────────────────────────────────────

void TransitionEngine::update(float dt)
{
    std::lock_guard lock(mutex_);

    // Update float animations
    for (auto& a : float_anims_)
    {
        if (a.finished)
            continue;

        a.elapsed += dt;
        float t = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);
        float eased = a.easing(t);

        *a.target = a.start + (a.end - a.start) * eased;

        if (t >= 1.0f)
        {
            *a.target = a.end;  // snap to exact target
            a.finished = true;
        }
    }

    // Update color animations
    for (auto& a : color_anims_)
    {
        if (a.finished)
            continue;

        a.elapsed += dt;
        float t = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);
        float eased = a.easing(t);

        a.target->r = a.start.r + (a.end.r - a.start.r) * eased;
        a.target->g = a.start.g + (a.end.g - a.start.g) * eased;
        a.target->b = a.start.b + (a.end.b - a.start.b) * eased;
        a.target->a = a.start.a + (a.end.a - a.start.a) * eased;

        if (t >= 1.0f)
        {
            *a.target = a.end;
            a.finished = true;
        }
    }

    // Update limit animations
    for (auto& a : limit_anims_)
    {
        if (a.finished)
            continue;

        a.elapsed += dt;
        float t = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);
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
        float vx = a.vx_data * decay;
        float vy = a.vy_data * decay;

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
        float t = std::clamp(a.elapsed / a.duration, 0.0f, 1.0f);
        float eased = a.easing(t);

        a.cam->azimuth = a.start.azimuth + (a.end.azimuth - a.start.azimuth) * eased;
        a.cam->elevation = a.start.elevation + (a.end.elevation - a.start.elevation) * eased;
        a.cam->distance = a.start.distance + (a.end.distance - a.start.distance) * eased;
        a.cam->fov = a.start.fov + (a.end.fov - a.start.fov) * eased;
        a.cam->ortho_size = a.start.ortho_size + (a.end.ortho_size - a.start.ortho_size) * eased;

        // Lerp target
        a.cam->target.x = a.start.target.x + (a.end.target.x - a.start.target.x) * eased;
        a.cam->target.y = a.start.target.y + (a.end.target.y - a.start.target.y) * eased;
        a.cam->target.z = a.start.target.z + (a.end.target.z - a.start.target.z) * eased;

        a.cam->update_position_from_orbit();

        if (t >= 1.0f)
        {
            // Snap to exact target
            a.cam->azimuth = a.end.azimuth;
            a.cam->elevation = a.end.elevation;
            a.cam->distance = a.end.distance;
            a.cam->fov = a.end.fov;
            a.cam->ortho_size = a.end.ortho_size;
            a.cam->target = a.end.target;
            a.cam->update_position_from_orbit();
            a.finished = true;
        }
    }

    gc();
}

// ─── Queries ────────────────────────────────────────────────────────────────

bool TransitionEngine::has_active_animations() const
{
    std::lock_guard lock(mutex_);
    for (const auto& a : float_anims_)
        if (!a.finished)
            return true;
    for (const auto& a : color_anims_)
        if (!a.finished)
            return true;
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

size_t TransitionEngine::active_count() const
{
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (const auto& a : float_anims_)
        if (!a.finished)
            ++count;
    for (const auto& a : color_anims_)
        if (!a.finished)
            ++count;
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

bool TransitionEngine::get_pending_target(const Axes* axes,
                                          AxisLimits& out_x,
                                          AxisLimits& out_y) const
{
    std::lock_guard lock(mutex_);
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

void TransitionEngine::gc()
{
    std::erase_if(float_anims_, [](const FloatAnim& a) { return a.finished; });
    std::erase_if(color_anims_, [](const ColorAnim& a) { return a.finished; });
    std::erase_if(limit_anims_, [](const LimitAnim& a) { return a.finished; });
    std::erase_if(inertial_anims_, [](const InertialPanAnim& a) { return a.finished; });
    std::erase_if(camera_anims_, [](const CameraAnim& a) { return a.finished; });
}

}  // namespace plotix

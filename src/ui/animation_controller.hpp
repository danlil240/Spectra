#pragma once

#include <plotix/axes.hpp>
#include <plotix/animator.hpp>

#include <cstdint>
#include <vector>

namespace plotix {

// Manages active UI animations (zoom transitions, pan inertia, auto-fit).
// Called once per frame from the main loop. All animations are cancelable
// by new user input — no animation queue buildup.
class AnimationController {
public:
    using AnimId = uint32_t;

    AnimationController() = default;

    // Animate axis limits from current values to target over duration_sec.
    // Returns an ID that can be used to cancel the animation.
    AnimId animate_axis_limits(Axes& axes,
                               AxisLimits target_x, AxisLimits target_y,
                               float duration_sec, EasingFn easing);

    // Animate inertial pan: applies a velocity that decelerates to zero.
    AnimId animate_inertial_pan(Axes& axes,
                                float vx_data, float vy_data,
                                float duration_sec);

    // Cancel a specific animation by ID.
    void cancel(AnimId id);

    // Cancel all animations targeting a specific Axes.
    void cancel_for_axes(Axes* axes);

    // Cancel every active animation.
    void cancel_all();

    // Advance all active animations by dt seconds. Call once per frame.
    void update(float dt);

    // True if any animation is still running.
    bool has_active_animations() const;

    // Number of currently active animations.
    size_t active_count() const;

    // If a limit animation is active for this axes, return its target.
    // Returns false if no active limit animation exists for the axes.
    bool get_pending_target(const Axes* axes,
                            AxisLimits& out_x, AxisLimits& out_y) const;

private:
    struct LimitAnim {
        AnimId     id;
        Axes*      axes;
        AxisLimits start_x, start_y;
        AxisLimits target_x, target_y;
        float      elapsed   = 0.0f;
        float      duration  = 0.15f;
        EasingFn   easing    = ease::ease_out;
        bool       finished  = false;
    };

    struct InertialPanAnim {
        AnimId id;
        Axes*  axes;
        float  vx_data;       // initial velocity in data-space units/sec
        float  vy_data;
        float  elapsed  = 0.0f;
        float  duration = 0.3f;
        bool   finished = false;
    };

    AnimId next_id_ = 1;

    std::vector<LimitAnim>       limit_anims_;
    std::vector<InertialPanAnim> inertial_anims_;

    void gc();  // Remove finished animations
};

} // namespace plotix

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <spectra/animator.hpp>
#include <spectra/axes.hpp>
#include <spectra/camera.hpp>
#include <spectra/color.hpp>
#include <vector>

namespace spectra
{

// Unified animation system for all UI transitions.
// Supports float, Color, and AxisLimits interpolation with arbitrary easing.
// All animations are cancelable. update() is called once per frame.
// Thread-safe: animate/cancel may be called from any thread.
class TransitionEngine
{
   public:
    using AnimId = uint32_t;

    // Easing function type: accepts normalized t in [0,1], returns eased value.
    // Supports both free functions (EasingFn) and stateful objects (CubicBezier).
    using EasingFunc = std::function<float(float)>;

    TransitionEngine()  = default;
    ~TransitionEngine() = default;

    TransitionEngine(const TransitionEngine&)            = delete;
    TransitionEngine& operator=(const TransitionEngine&) = delete;

    // ─── Animate float ──────────────────────────────────────────────────
    // Smoothly transitions *target from its current value to `end` over
    // `duration` seconds using the given easing function.
    // If an animation already targets the same pointer, it is replaced.
    AnimId animate(float& target, float end, float duration, EasingFunc easing = ease::ease_out);

    // ─── Animate Color ──────────────────────────────────────────────────
    // Interpolates each RGBA channel independently.
    AnimId animate(Color& target, Color end, float duration, EasingFunc easing = ease::ease_out);

    // ─── Animate AxisLimits ─────────────────────────────────────────────
    // Transitions both X and Y limits of an Axes simultaneously.
    // Cancels any existing limit animation on the same Axes.
    AnimId animate_limits(Axes&      axes,
                          AxisLimits target_x,
                          AxisLimits target_y,
                          float      duration,
                          EasingFunc easing = ease::ease_out);

    // ─── Animate inertial pan ───────────────────────────────────────────
    // Applies a decelerating velocity to axis limits (for drag release).
    // ─── Animate Camera ─────────────────────────────────────────────
    // Smoothly transitions a Camera from its current state to `target`
    // over `duration` seconds. Interpolates azimuth, elevation, distance,
    // fov, and ortho_size, then calls update_position_from_orbit().
    AnimId animate_camera(Camera&    cam,
                          Camera     target,
                          float      duration,
                          EasingFunc easing = ease::ease_out);

    AnimId animate_inertial_pan(Axes& axes, float vx_data, float vy_data, float duration);

    // ─── Cancel ─────────────────────────────────────────────────────────
    // Cancel a specific animation by ID.
    void cancel(AnimId id);

    // Cancel all animations targeting a specific Axes.
    void cancel_for_axes(Axes* axes);

    // Cancel all animations targeting a specific Camera.
    void cancel_for_camera(Camera* cam);

    // Cancel all active animations.
    void cancel_all();

    // ─── Update ─────────────────────────────────────────────────────────
    // Advance all active animations by dt seconds. Call once per frame.
    void update(float dt);

    // ─── Queries ────────────────────────────────────────────────────────
    // True if any animation is still running.
    bool has_active_animations() const;

    // Number of currently active animations.
    size_t active_count() const;

    // If a limit animation is active for this axes, return its target.
    bool get_pending_target(const Axes* axes, AxisLimits& out_x, AxisLimits& out_y) const;

   private:
    // ─── Animation records ──────────────────────────────────────────────

    struct FloatAnim
    {
        AnimId     id;
        float*     target;
        float      start;
        float      end;
        float      elapsed  = 0.0f;
        float      duration = 0.15f;
        EasingFunc easing;
        bool       finished = false;
    };

    struct ColorAnim
    {
        AnimId     id;
        Color*     target;
        Color      start;
        Color      end;
        float      elapsed  = 0.0f;
        float      duration = 0.15f;
        EasingFunc easing;
        bool       finished = false;
    };

    struct LimitAnim
    {
        AnimId     id;
        Axes*      axes;
        AxisLimits start_x, start_y;
        AxisLimits target_x, target_y;
        float      elapsed  = 0.0f;
        float      duration = 0.15f;
        EasingFunc easing;
        bool       finished = false;
    };

    struct InertialPanAnim
    {
        AnimId id;
        Axes*  axes;
        float  vx_data;
        float  vy_data;
        float  elapsed  = 0.0f;
        float  duration = 0.3f;
        bool   finished = false;
    };

    struct CameraAnim
    {
        AnimId     id;
        Camera*    cam;
        Camera     start;
        Camera     end;
        float      elapsed  = 0.0f;
        float      duration = 0.3f;
        EasingFunc easing;
        bool       finished = false;
    };

    AnimId next_id_ = 1;

    std::vector<FloatAnim>       float_anims_;
    std::vector<ColorAnim>       color_anims_;
    std::vector<LimitAnim>       limit_anims_;
    std::vector<InertialPanAnim> inertial_anims_;
    std::vector<CameraAnim>      camera_anims_;

    mutable std::mutex mutex_;

    void gc();                                   // Remove finished animations (called under lock)
    void cancel_for_axes_unlocked(Axes* axes);   // Internal: caller must hold mutex_
};

}   // namespace spectra

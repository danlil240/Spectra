#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <spectra/axes.hpp>
#include <spectra/camera.hpp>
#include <spectra/color.hpp>
#include <spectra/math3d.hpp>
#include <string>

namespace spectra
{

class Axes3D;
class TransitionEngine;
class Figure;

// Transition direction for 2D↔3D mode switching.
enum class ModeTransitionDirection
{
    To3D,   // 2D → 3D: camera lifts from flat top-down to orbit view
    To2D,   // 3D → 2D: camera flattens from orbit to top-down orthographic
};

// Current state of a mode transition.
enum class ModeTransitionState
{
    Idle,        // No transition active
    Animating,   // Transition in progress
    Finished,    // Transition completed (auto-resets to Idle on next query)
};

// Snapshot of 2D axes state for transition interpolation.
struct ModeTransition2DState
{
    AxisLimits  xlim{0.0, 1.0};
    AxisLimits  ylim{0.0, 1.0};
    std::string xlabel;
    std::string ylabel;
    std::string title;
    bool        grid_enabled = true;
};

// Snapshot of 3D axes state for transition interpolation.
struct ModeTransition3DState
{
    AxisLimits xlim{0.0, 1.0};
    AxisLimits ylim{0.0, 1.0};
    AxisLimits zlim{0.0, 1.0};
    Camera     camera;
    int        grid_planes       = 1;   // GridPlane::XY
    bool       show_bounding_box = true;
    bool       lighting_enabled  = true;
    vec3       light_dir{1.0f, 1.0f, 1.0f};
};

// Callback invoked each frame during a transition with progress t in [0,1].
using ModeTransitionCallback = std::function<void(float t)>;

// Callback invoked when a transition completes.
using ModeTransitionCompleteCallback = std::function<void(ModeTransitionDirection direction)>;

// ModeTransition — orchestrates animated transitions between 2D and 3D views.
//
// When transitioning 2D→3D:
//   - Camera starts at top-down orthographic (elevation=90°, ortho mode)
//   - Animates to the target 3D camera state (perspective, orbit angle)
//   - Z-axis limits fade in from zero range to target range
//   - Grid planes transition from flat XY to configured 3D planes
//
// When transitioning 3D→2D:
//   - Camera animates from current 3D state to top-down orthographic
//   - Z-axis limits collapse to zero range
//   - Grid planes transition to flat XY only
//
// Thread-safe: all public methods lock an internal mutex.
class ModeTransition
{
   public:
    ModeTransition()  = default;
    ~ModeTransition() = default;

    ModeTransition(const ModeTransition&)            = delete;
    ModeTransition& operator=(const ModeTransition&) = delete;

    // ─── Configuration ──────────────────────────────────────────────────

    // Set the default transition duration in seconds.
    void  set_duration(float seconds);
    float duration() const;

    // Set an easing function for the transition. Default: ease_in_out.
    using EasingFunc = std::function<float(float)>;
    void set_easing(EasingFunc easing);

    // ─── Transition triggers ────────────────────────────────────────────

    // Begin a 2D→3D transition. Captures the current 2D state and animates
    // toward the given 3D target state. If target_3d is nullptr, uses defaults.
    // Returns a transition ID (0 if already animating).
    uint32_t begin_to_3d(const ModeTransition2DState& from_2d,
                         const ModeTransition3DState& target_3d);

    // Begin a 3D→2D transition. Captures the current 3D state and animates
    // toward a flat 2D view.
    // Returns a transition ID (0 if already animating).
    uint32_t begin_to_2d(const ModeTransition3DState& from_3d,
                         const ModeTransition2DState& target_2d);

    // Cancel any active transition immediately.
    void cancel();

    // ─── Update ─────────────────────────────────────────────────────────

    // Advance the transition by dt seconds. Call once per frame.
    // Returns the current interpolated camera state.
    void update(float dt);

    // ─── Queries ────────────────────────────────────────────────────────

    ModeTransitionState     state() const;
    ModeTransitionDirection direction() const;

    // Progress in [0,1]. Returns 0 if idle.
    float progress() const;

    // Get the current interpolated camera state during transition.
    Camera interpolated_camera() const;

    // Get the current interpolated axis limits during transition.
    AxisLimits interpolated_xlim() const;
    AxisLimits interpolated_ylim() const;
    AxisLimits interpolated_zlim() const;

    // Get the current interpolated grid planes (as int bitmask).
    int interpolated_grid_planes() const;

    // Get the current interpolated opacity for 3D elements (0=hidden, 1=visible).
    // Used for fading in/out bounding box, z-axis labels, etc.
    float element_3d_opacity() const;

    // True if any transition is active.
    bool is_active() const;

    // ─── Callbacks ──────────────────────────────────────────────────────

    // Called each frame during transition with eased progress t.
    void set_on_progress(ModeTransitionCallback cb);

    // Called when transition completes.
    void set_on_complete(ModeTransitionCompleteCallback cb);

    // ─── Serialization ──────────────────────────────────────────────────

    std::string serialize() const;
    bool        deserialize(const std::string& json);

   private:
    mutable std::mutex mutex_;

    float      duration_ = 0.6f;   // Default 600ms
    EasingFunc easing_;            // nullptr = default ease_in_out

    ModeTransitionState     state_     = ModeTransitionState::Idle;
    ModeTransitionDirection direction_ = ModeTransitionDirection::To3D;

    float    elapsed_    = 0.0f;
    uint32_t next_id_    = 1;
    uint32_t current_id_ = 0;

    // Start/end states
    ModeTransition2DState state_2d_;
    ModeTransition3DState state_3d_;

    // Interpolated state (updated each frame)
    Camera     interp_camera_;
    AxisLimits interp_xlim_;
    AxisLimits interp_ylim_;
    AxisLimits interp_zlim_;
    int        interp_grid_planes_ = 1;
    float      interp_3d_opacity_  = 0.0f;

    ModeTransitionCallback         on_progress_;
    ModeTransitionCompleteCallback on_complete_;

    // Internal helpers
    float  compute_eased_t() const;   // Must be called under lock
    void   interpolate_to_3d(float t);
    void   interpolate_to_2d(float t);
    Camera make_top_down_camera(const ModeTransition2DState& s2d) const;
};

}   // namespace spectra

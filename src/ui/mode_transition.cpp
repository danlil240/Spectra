#include "mode_transition.hpp"

#include <plotix/animator.hpp>
#include <plotix/axes3d.hpp>

#include <cmath>
#include <sstream>

namespace plotix {

// ─── Configuration ──────────────────────────────────────────────────────────

void ModeTransition::set_duration(float seconds) {
    std::lock_guard lock(mutex_);
    duration_ = seconds > 0.0f ? seconds : 0.01f;
}

float ModeTransition::duration() const {
    std::lock_guard lock(mutex_);
    return duration_;
}

void ModeTransition::set_easing(EasingFunc easing) {
    std::lock_guard lock(mutex_);
    easing_ = std::move(easing);
}

// ─── Transition triggers ────────────────────────────────────────────────────

uint32_t ModeTransition::begin_to_3d(const ModeTransition2DState& from_2d,
                                      const ModeTransition3DState& target_3d) {
    std::lock_guard lock(mutex_);
    if (state_ == ModeTransitionState::Animating) return 0;

    state_ = ModeTransitionState::Animating;
    direction_ = ModeTransitionDirection::To3D;
    elapsed_ = 0.0f;
    current_id_ = next_id_++;

    state_2d_ = from_2d;
    state_3d_ = target_3d;

    // Initialize interpolated state to the 2D starting point
    interp_camera_ = make_top_down_camera(from_2d);
    interp_xlim_ = from_2d.xlim;
    interp_ylim_ = from_2d.ylim;
    interp_zlim_ = {0.0f, 0.0f};  // Z starts collapsed
    interp_grid_planes_ = 1;  // XY only
    interp_3d_opacity_ = 0.0f;

    return current_id_;
}

uint32_t ModeTransition::begin_to_2d(const ModeTransition3DState& from_3d,
                                      const ModeTransition2DState& target_2d) {
    std::lock_guard lock(mutex_);
    if (state_ == ModeTransitionState::Animating) return 0;

    state_ = ModeTransitionState::Animating;
    direction_ = ModeTransitionDirection::To2D;
    elapsed_ = 0.0f;
    current_id_ = next_id_++;

    state_2d_ = target_2d;
    state_3d_ = from_3d;

    // Initialize interpolated state to the 3D starting point
    interp_camera_ = from_3d.camera;
    interp_xlim_ = from_3d.xlim;
    interp_ylim_ = from_3d.ylim;
    interp_zlim_ = from_3d.zlim;
    interp_grid_planes_ = from_3d.grid_planes;
    interp_3d_opacity_ = 1.0f;

    return current_id_;
}

void ModeTransition::cancel() {
    std::lock_guard lock(mutex_);
    state_ = ModeTransitionState::Idle;
    elapsed_ = 0.0f;
    current_id_ = 0;
}

// ─── Update ─────────────────────────────────────────────────────────────────

void ModeTransition::update(float dt) {
    ModeTransitionCompleteCallback complete_cb;
    ModeTransitionCallback progress_cb;
    float eased_t = 0.0f;
    ModeTransitionDirection dir;

    {
        std::lock_guard lock(mutex_);
        if (state_ != ModeTransitionState::Animating) return;

        elapsed_ += dt;
        eased_t = compute_eased_t();
        dir = direction_;

        if (direction_ == ModeTransitionDirection::To3D) {
            interpolate_to_3d(eased_t);
        } else {
            interpolate_to_2d(eased_t);
        }

        progress_cb = on_progress_;

        if (elapsed_ >= duration_) {
            state_ = ModeTransitionState::Finished;
            complete_cb = on_complete_;
        }
    }

    // Fire callbacks outside the lock to avoid deadlocks
    if (progress_cb) progress_cb(eased_t);
    if (complete_cb) complete_cb(dir);
}

// ─── Queries ────────────────────────────────────────────────────────────────

ModeTransitionState ModeTransition::state() const {
    std::lock_guard lock(mutex_);
    if (state_ == ModeTransitionState::Finished) {
        // Auto-reset to Idle on query
        const_cast<ModeTransition*>(this)->state_ = ModeTransitionState::Idle;
        return ModeTransitionState::Finished;
    }
    return state_;
}

ModeTransitionDirection ModeTransition::direction() const {
    std::lock_guard lock(mutex_);
    return direction_;
}

float ModeTransition::progress() const {
    std::lock_guard lock(mutex_);
    if (state_ == ModeTransitionState::Idle) return 0.0f;
    return compute_eased_t();
}

Camera ModeTransition::interpolated_camera() const {
    std::lock_guard lock(mutex_);
    return interp_camera_;
}

AxisLimits ModeTransition::interpolated_xlim() const {
    std::lock_guard lock(mutex_);
    return interp_xlim_;
}

AxisLimits ModeTransition::interpolated_ylim() const {
    std::lock_guard lock(mutex_);
    return interp_ylim_;
}

AxisLimits ModeTransition::interpolated_zlim() const {
    std::lock_guard lock(mutex_);
    return interp_zlim_;
}

int ModeTransition::interpolated_grid_planes() const {
    std::lock_guard lock(mutex_);
    return interp_grid_planes_;
}

float ModeTransition::element_3d_opacity() const {
    std::lock_guard lock(mutex_);
    return interp_3d_opacity_;
}

bool ModeTransition::is_active() const {
    std::lock_guard lock(mutex_);
    return state_ == ModeTransitionState::Animating;
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void ModeTransition::set_on_progress(ModeTransitionCallback cb) {
    std::lock_guard lock(mutex_);
    on_progress_ = std::move(cb);
}

void ModeTransition::set_on_complete(ModeTransitionCompleteCallback cb) {
    std::lock_guard lock(mutex_);
    on_complete_ = std::move(cb);
}

// ─── Serialization ──────────────────────────────────────────────────────────

std::string ModeTransition::serialize() const {
    std::lock_guard lock(mutex_);
    std::ostringstream os;
    os << "{";
    os << "\"duration\":" << duration_ << ",";
    os << "\"direction\":" << static_cast<int>(direction_) << ",";
    os << "\"state\":" << static_cast<int>(state_);
    os << "}";
    return os.str();
}

bool ModeTransition::deserialize(const std::string& json) {
    std::lock_guard lock(mutex_);
    // Minimal parse: extract duration and direction
    auto find_number = [&](const std::string& key) -> float {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return 0.0f;
        pos += search.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        try {
            return std::stof(json.substr(pos));
        } catch (...) {
            return 0.0f;
        }
    };

    float d = find_number("duration");
    if (d > 0.0f) duration_ = d;

    int dir = static_cast<int>(find_number("direction"));
    if (dir == 0 || dir == 1) {
        direction_ = static_cast<ModeTransitionDirection>(dir);
    }

    // Transitions are not restored as active -- always idle after deserialize
    state_ = ModeTransitionState::Idle;
    elapsed_ = 0.0f;

    return true;
}

// ─── Internal helpers ───────────────────────────────────────────────────────

float ModeTransition::compute_eased_t() const {
    float raw_t = (duration_ > 0.0f) ? (elapsed_ / duration_) : 1.0f;
    if (raw_t > 1.0f) raw_t = 1.0f;
    if (raw_t < 0.0f) raw_t = 0.0f;

    if (easing_) return easing_(raw_t);

    // Default ease_in_out (smoothstep)
    return raw_t * raw_t * (3.0f - 2.0f * raw_t);
}

static float lerp_f(float a, float b, float t) {
    return a + (b - a) * t;
}

static AxisLimits lerp_limits(const AxisLimits& a, const AxisLimits& b, float t) {
    return {lerp_f(a.min, b.min, t), lerp_f(a.max, b.max, t)};
}

void ModeTransition::interpolate_to_3d(float t) {
    // Camera: start from top-down ortho, end at target 3D camera
    Camera top_down = make_top_down_camera(state_2d_);
    const Camera& target = state_3d_.camera;

    interp_camera_.azimuth = lerp_f(top_down.azimuth, target.azimuth, t);
    interp_camera_.elevation = lerp_f(top_down.elevation, target.elevation, t);
    interp_camera_.distance = lerp_f(top_down.distance, target.distance, t);
    interp_camera_.fov = lerp_f(top_down.fov, target.fov, t);
    interp_camera_.ortho_size = lerp_f(top_down.ortho_size, target.ortho_size, t);
    interp_camera_.near_clip = lerp_f(top_down.near_clip, target.near_clip, t);
    interp_camera_.far_clip = lerp_f(top_down.far_clip, target.far_clip, t);
    interp_camera_.target = vec3_lerp(top_down.target, target.target, t);

    // Projection mode: switch to perspective at t=0.5
    interp_camera_.projection_mode = (t < 0.5f)
        ? Camera::ProjectionMode::Orthographic
        : Camera::ProjectionMode::Perspective;

    interp_camera_.update_position_from_orbit();

    // Axis limits: X/Y interpolate from 2D to 3D, Z fades in
    interp_xlim_ = lerp_limits(state_2d_.xlim, state_3d_.xlim, t);
    interp_ylim_ = lerp_limits(state_2d_.ylim, state_3d_.ylim, t);
    AxisLimits z_collapsed = {
        (state_3d_.zlim.min + state_3d_.zlim.max) * 0.5f,
        (state_3d_.zlim.min + state_3d_.zlim.max) * 0.5f
    };
    interp_zlim_ = lerp_limits(z_collapsed, state_3d_.zlim, t);

    // Grid planes: switch at t=0.7 (keep XY-only during most of transition)
    interp_grid_planes_ = (t < 0.7f) ? 1 : state_3d_.grid_planes;

    // 3D element opacity: fade in over the full transition
    interp_3d_opacity_ = t;
}

void ModeTransition::interpolate_to_2d(float t) {
    // Camera: start from 3D camera, end at top-down ortho
    Camera top_down = make_top_down_camera(state_2d_);
    const Camera& start = state_3d_.camera;

    interp_camera_.azimuth = lerp_f(start.azimuth, top_down.azimuth, t);
    interp_camera_.elevation = lerp_f(start.elevation, top_down.elevation, t);
    interp_camera_.distance = lerp_f(start.distance, top_down.distance, t);
    interp_camera_.fov = lerp_f(start.fov, top_down.fov, t);
    interp_camera_.ortho_size = lerp_f(start.ortho_size, top_down.ortho_size, t);
    interp_camera_.near_clip = lerp_f(start.near_clip, top_down.near_clip, t);
    interp_camera_.far_clip = lerp_f(start.far_clip, top_down.far_clip, t);
    interp_camera_.target = vec3_lerp(start.target, top_down.target, t);

    // Projection mode: switch to orthographic at t=0.5
    interp_camera_.projection_mode = (t < 0.5f)
        ? Camera::ProjectionMode::Perspective
        : Camera::ProjectionMode::Orthographic;

    interp_camera_.update_position_from_orbit();

    // Axis limits: X/Y interpolate from 3D to 2D, Z collapses
    interp_xlim_ = lerp_limits(state_3d_.xlim, state_2d_.xlim, t);
    interp_ylim_ = lerp_limits(state_3d_.ylim, state_2d_.ylim, t);
    AxisLimits z_collapsed = {
        (state_3d_.zlim.min + state_3d_.zlim.max) * 0.5f,
        (state_3d_.zlim.min + state_3d_.zlim.max) * 0.5f
    };
    interp_zlim_ = lerp_limits(state_3d_.zlim, z_collapsed, t);

    // Grid planes: switch to XY-only at t=0.3
    interp_grid_planes_ = (t < 0.3f) ? state_3d_.grid_planes : 1;

    // 3D element opacity: fade out over the full transition
    interp_3d_opacity_ = 1.0f - t;
}

Camera ModeTransition::make_top_down_camera(const ModeTransition2DState& s2d) const {
    Camera cam;
    cam.projection_mode = Camera::ProjectionMode::Orthographic;
    cam.azimuth = 0.0f;
    cam.elevation = 90.0f;  // Looking straight down
    cam.distance = Axes3D::box_half_size() * 2.0f * 2.2f;
    cam.target = {0.0f, 0.0f, 0.0f};
    cam.up = {0.0f, 0.0f, -1.0f};  // Z-up when looking down Y
    cam.fov = 45.0f;
    cam.near_clip = 0.01f;
    cam.far_clip = 1000.0f;

    // Ortho size based on 2D axis range
    float x_range = s2d.xlim.max - s2d.xlim.min;
    float y_range = s2d.ylim.max - s2d.ylim.min;
    cam.ortho_size = std::max(x_range, y_range) * 0.6f;

    cam.update_position_from_orbit();
    return cam;
}

} // namespace plotix

#include "camera_animator.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace plotix {

// ─── Path mode ──────────────────────────────────────────────────────────────

CameraPathMode CameraAnimator::path_mode() const {
    std::lock_guard lock(mutex_);
    return path_mode_;
}

void CameraAnimator::set_path_mode(CameraPathMode mode) {
    std::lock_guard lock(mutex_);
    path_mode_ = mode;
}

// ─── Keyframe management ────────────────────────────────────────────────────

void CameraAnimator::add_keyframe(float time, const Camera& camera) {
    add_keyframe(CameraKeyframe{time, camera});
}

void CameraAnimator::add_keyframe(const CameraKeyframe& kf) {
    std::lock_guard lock(mutex_);

    // Replace existing keyframe at same time
    for (auto& existing : keyframes_) {
        if (std::abs(existing.time - kf.time) < 0.001f) {
            existing.camera = kf.camera;
            return;
        }
    }

    keyframes_.push_back(kf);
    sort_keyframes();
}

bool CameraAnimator::remove_keyframe(float time, float tolerance) {
    std::lock_guard lock(mutex_);

    auto it = std::find_if(keyframes_.begin(), keyframes_.end(),
        [time, tolerance](const CameraKeyframe& kf) {
            return std::abs(kf.time - time) < tolerance;
        });

    if (it != keyframes_.end()) {
        keyframes_.erase(it);
        return true;
    }
    return false;
}

void CameraAnimator::clear() {
    std::lock_guard lock(mutex_);
    keyframes_.clear();
}

// ─── Queries ────────────────────────────────────────────────────────────────

const std::vector<CameraKeyframe>& CameraAnimator::keyframes() const {
    return keyframes_;
}

size_t CameraAnimator::keyframe_count() const {
    std::lock_guard lock(mutex_);
    return keyframes_.size();
}

bool CameraAnimator::empty() const {
    std::lock_guard lock(mutex_);
    return keyframes_.empty();
}

float CameraAnimator::duration() const {
    std::lock_guard lock(mutex_);
    if (keyframes_.empty()) return 0.0f;
    return keyframes_.back().time;
}

// ─── Evaluation ─────────────────────────────────────────────────────────────

Camera CameraAnimator::evaluate(float time) const {
    std::lock_guard lock(mutex_);

    if (keyframes_.empty()) {
        return Camera{};
    }

    if (path_mode_ == CameraPathMode::Orbit) {
        return evaluate_orbit(time);
    } else {
        return evaluate_free_flight(time);
    }
}

void CameraAnimator::apply(float time, Camera& cam) const {
    cam = evaluate(time);
}

// ─── Target camera binding ──────────────────────────────────────────────────

void CameraAnimator::set_target_camera(Camera* cam) {
    std::lock_guard lock(mutex_);
    target_camera_ = cam;
}

Camera* CameraAnimator::target_camera() const {
    std::lock_guard lock(mutex_);
    return target_camera_;
}

void CameraAnimator::evaluate_at(float time) {
    std::lock_guard lock(mutex_);
    if (!target_camera_ || keyframes_.empty()) return;

    Camera result;
    if (path_mode_ == CameraPathMode::Orbit) {
        result = evaluate_orbit(time);
    } else {
        result = evaluate_free_flight(time);
    }
    *target_camera_ = result;
}

// ─── Convenience ────────────────────────────────────────────────────────────

void CameraAnimator::create_orbit_animation(const Camera& base,
                                             float start_azimuth, float end_azimuth,
                                             float duration_seconds) {
    std::lock_guard lock(mutex_);
    keyframes_.clear();
    path_mode_ = CameraPathMode::Orbit;

    Camera start_cam = base;
    start_cam.azimuth = start_azimuth;
    start_cam.update_position_from_orbit();
    keyframes_.push_back(CameraKeyframe{0.0f, start_cam});

    Camera end_cam = base;
    end_cam.azimuth = end_azimuth;
    end_cam.update_position_from_orbit();
    keyframes_.push_back(CameraKeyframe{duration_seconds, end_cam});
}

void CameraAnimator::create_turntable(const Camera& base, float duration_seconds) {
    create_orbit_animation(base, base.azimuth, base.azimuth + 360.0f, duration_seconds);
}

// ─── Serialization ──────────────────────────────────────────────────────────

std::string CameraAnimator::serialize() const {
    std::lock_guard lock(mutex_);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    oss << "{\"path_mode\":" << static_cast<int>(path_mode_)
        << ",\"keyframes\":[";

    for (size_t i = 0; i < keyframes_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"time\":" << keyframes_[i].time
            << ",\"camera\":" << keyframes_[i].camera.serialize()
            << "}";
    }

    oss << "]}";
    return oss.str();
}

bool CameraAnimator::deserialize(const std::string& json) {
    std::lock_guard lock(mutex_);

    // Parse path_mode
    auto mode_pos = json.find("\"path_mode\":");
    if (mode_pos == std::string::npos) return false;
    mode_pos += 12; // strlen("\"path_mode\":")
    int mode_val = std::stoi(json.substr(mode_pos));
    path_mode_ = static_cast<CameraPathMode>(mode_val);

    // Parse keyframes array
    keyframes_.clear();
    auto kf_arr_pos = json.find("\"keyframes\":[");
    if (kf_arr_pos == std::string::npos) return false;
    kf_arr_pos += 13; // strlen("\"keyframes\":[")

    // Find matching closing bracket
    size_t pos = kf_arr_pos;
    while (pos < json.size()) {
        // Find next keyframe object
        auto obj_start = json.find("{\"time\":", pos);
        if (obj_start == std::string::npos) break;

        // Parse time
        auto time_pos = obj_start + 8; // strlen("{\"time\":")
        float time = std::stof(json.substr(time_pos));

        // Parse camera sub-object
        auto cam_pos = json.find("\"camera\":", obj_start);
        if (cam_pos == std::string::npos) break;
        cam_pos += 9; // strlen("\"camera\":")

        // Find the camera JSON object boundaries
        int brace_count = 0;
        size_t cam_start = cam_pos;
        size_t cam_end = cam_pos;
        for (size_t i = cam_pos; i < json.size(); ++i) {
            if (json[i] == '{') brace_count++;
            else if (json[i] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    cam_end = i + 1;
                    break;
                }
            }
        }

        Camera cam;
        cam.deserialize(json.substr(cam_start, cam_end - cam_start));
        keyframes_.push_back(CameraKeyframe{time, cam});

        pos = cam_end;
    }

    sort_keyframes();
    return true;
}

// ─── Private helpers ────────────────────────────────────────────────────────

void CameraAnimator::sort_keyframes() {
    std::sort(keyframes_.begin(), keyframes_.end(),
        [](const CameraKeyframe& a, const CameraKeyframe& b) {
            return a.time < b.time;
        });
}

std::pair<int, int> CameraAnimator::find_bracket(float time) const {
    if (keyframes_.empty()) return {-1, -1};
    if (keyframes_.size() == 1) return {0, 0};

    // Before first keyframe
    if (time <= keyframes_.front().time) return {0, 0};

    // After last keyframe
    if (time >= keyframes_.back().time) {
        int last = static_cast<int>(keyframes_.size()) - 1;
        return {last, last};
    }

    // Binary search for bracket
    for (int i = 0; i < static_cast<int>(keyframes_.size()) - 1; ++i) {
        if (time >= keyframes_[i].time && time <= keyframes_[i + 1].time) {
            return {i, i + 1};
        }
    }

    int last = static_cast<int>(keyframes_.size()) - 1;
    return {last, last};
}

Camera CameraAnimator::evaluate_orbit(float time) const {
    auto [a, b] = find_bracket(time);

    if (a == -1) return Camera{};
    if (a == b) return keyframes_[a].camera;

    const Camera& cam_a = keyframes_[a].camera;
    const Camera& cam_b = keyframes_[b].camera;
    float seg_duration = keyframes_[b].time - keyframes_[a].time;

    float t = 0.0f;
    if (seg_duration > 1e-6f) {
        t = (time - keyframes_[a].time) / seg_duration;
    }
    t = std::clamp(t, 0.0f, 1.0f);

    Camera result = cam_a;

    // Lerp spherical coordinates
    result.azimuth   = cam_a.azimuth   + (cam_b.azimuth   - cam_a.azimuth)   * t;
    result.elevation = cam_a.elevation + (cam_b.elevation - cam_a.elevation) * t;
    result.distance  = cam_a.distance  + (cam_b.distance  - cam_a.distance)  * t;
    result.fov       = cam_a.fov       + (cam_b.fov       - cam_a.fov)       * t;
    result.ortho_size = cam_a.ortho_size + (cam_b.ortho_size - cam_a.ortho_size) * t;

    // Lerp target position
    result.target.x = cam_a.target.x + (cam_b.target.x - cam_a.target.x) * t;
    result.target.y = cam_a.target.y + (cam_b.target.y - cam_a.target.y) * t;
    result.target.z = cam_a.target.z + (cam_b.target.z - cam_a.target.z) * t;

    // Recompute position from interpolated orbit parameters
    result.update_position_from_orbit();

    return result;
}

Camera CameraAnimator::evaluate_free_flight(float time) const {
    auto [a, b] = find_bracket(time);

    if (a == -1) return Camera{};
    if (a == b) return keyframes_[a].camera;

    const Camera& cam_a = keyframes_[a].camera;
    const Camera& cam_b = keyframes_[b].camera;
    float seg_duration = keyframes_[b].time - keyframes_[a].time;

    float t = 0.0f;
    if (seg_duration > 1e-6f) {
        t = (time - keyframes_[a].time) / seg_duration;
    }
    t = std::clamp(t, 0.0f, 1.0f);

    Camera result;

    // Lerp position
    result.position.x = cam_a.position.x + (cam_b.position.x - cam_a.position.x) * t;
    result.position.y = cam_a.position.y + (cam_b.position.y - cam_a.position.y) * t;
    result.position.z = cam_a.position.z + (cam_b.position.z - cam_a.position.z) * t;

    // Lerp target
    result.target.x = cam_a.target.x + (cam_b.target.x - cam_a.target.x) * t;
    result.target.y = cam_a.target.y + (cam_b.target.y - cam_a.target.y) * t;
    result.target.z = cam_a.target.z + (cam_b.target.z - cam_a.target.z) * t;

    // Slerp orientation (up vector derived from quaternion)
    quat q_a = orientation_from_camera(cam_a);
    quat q_b = orientation_from_camera(cam_b);
    quat q_interp = quat_slerp(q_a, q_b, t);

    // Recover up vector from slerped orientation
    // The up vector is the Y axis of the rotation matrix
    mat4 rot = quat_to_mat4(q_interp);
    result.up.x = rot.m[4];  // Column 1, row 0
    result.up.y = rot.m[5];  // Column 1, row 1
    result.up.z = rot.m[6];  // Column 1, row 2
    result.up = vec3_normalize(result.up);

    // Lerp scalar params
    result.fov       = cam_a.fov       + (cam_b.fov       - cam_a.fov)       * t;
    result.distance  = cam_a.distance  + (cam_b.distance  - cam_a.distance)  * t;
    result.ortho_size = cam_a.ortho_size + (cam_b.ortho_size - cam_a.ortho_size) * t;
    result.near_clip = cam_a.near_clip + (cam_b.near_clip - cam_a.near_clip) * t;
    result.far_clip  = cam_a.far_clip  + (cam_b.far_clip  - cam_a.far_clip)  * t;

    // Lerp orbit params (so they stay in sync if user switches modes)
    result.azimuth   = cam_a.azimuth   + (cam_b.azimuth   - cam_a.azimuth)   * t;
    result.elevation = cam_a.elevation + (cam_b.elevation - cam_a.elevation) * t;

    result.projection_mode = cam_a.projection_mode;

    return result;
}

quat CameraAnimator::orientation_from_camera(const Camera& cam) {
    // Build an orientation quaternion from the camera's view axes.
    // Forward = normalize(target - position)
    // Right = normalize(cross(forward, up))
    // True up = cross(right, forward)

    vec3 forward = vec3_normalize(cam.target - cam.position);
    float len = vec3_length(cam.target - cam.position);
    if (len < 1e-6f) {
        return quat_identity();
    }

    vec3 right = vec3_normalize(vec3_cross(forward, cam.up));
    float right_len = vec3_length(vec3_cross(forward, cam.up));
    if (right_len < 1e-6f) {
        return quat_identity();
    }

    vec3 true_up = vec3_cross(right, forward);

    // Build rotation matrix from axes (columns: right, up, -forward)
    // Then extract quaternion.
    mat4 rot = mat4_identity();
    rot.m[0] = right.x;     rot.m[1] = right.y;     rot.m[2] = right.z;
    rot.m[4] = true_up.x;   rot.m[5] = true_up.y;   rot.m[6] = true_up.z;
    rot.m[8] = -forward.x;  rot.m[9] = -forward.y;  rot.m[10] = -forward.z;

    // Extract quaternion from rotation matrix
    // Using Shepperd's method for numerical stability
    float trace = rot.m[0] + rot.m[5] + rot.m[10];
    quat q;

    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (rot.m[6] - rot.m[9]) / s;
        q.y = (rot.m[8] - rot.m[2]) / s;
        q.z = (rot.m[1] - rot.m[4]) / s;
    } else if (rot.m[0] > rot.m[5] && rot.m[0] > rot.m[10]) {
        float s = std::sqrt(1.0f + rot.m[0] - rot.m[5] - rot.m[10]) * 2.0f;
        q.w = (rot.m[6] - rot.m[9]) / s;
        q.x = 0.25f * s;
        q.y = (rot.m[4] + rot.m[1]) / s;
        q.z = (rot.m[8] + rot.m[2]) / s;
    } else if (rot.m[5] > rot.m[10]) {
        float s = std::sqrt(1.0f + rot.m[5] - rot.m[0] - rot.m[10]) * 2.0f;
        q.w = (rot.m[8] - rot.m[2]) / s;
        q.x = (rot.m[4] + rot.m[1]) / s;
        q.y = 0.25f * s;
        q.z = (rot.m[9] + rot.m[6]) / s;
    } else {
        float s = std::sqrt(1.0f + rot.m[10] - rot.m[0] - rot.m[5]) * 2.0f;
        q.w = (rot.m[1] - rot.m[4]) / s;
        q.x = (rot.m[8] + rot.m[2]) / s;
        q.y = (rot.m[9] + rot.m[6]) / s;
        q.z = 0.25f * s;
    }

    // Normalize
    float mag = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (mag > 1e-6f) {
        q.x /= mag; q.y /= mag; q.z /= mag; q.w /= mag;
    }

    return q;
}

void CameraAnimator::apply_orientation(Camera& cam, const quat& q, float distance) {
    mat4 rot = quat_to_mat4(q);

    // Extract axes from rotation matrix
    // Forward = -Z column (negated because camera looks along -Z)
    vec3 forward;
    forward.x = -rot.m[8];
    forward.y = -rot.m[9];
    forward.z = -rot.m[10];
    forward = vec3_normalize(forward);

    // Up = Y column
    cam.up.x = rot.m[4];
    cam.up.y = rot.m[5];
    cam.up.z = rot.m[6];
    cam.up = vec3_normalize(cam.up);

    // Recompute position from target and forward
    cam.position = cam.target - forward * distance;
}

} // namespace plotix

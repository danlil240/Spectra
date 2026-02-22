#pragma once

#include <cstdint>
#include <mutex>
#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>
#include <string>
#include <vector>

namespace spectra
{

// Camera keyframe — stores a full camera snapshot at a point in time.
struct CameraKeyframe
{
    float  time = 0.0f;
    Camera camera;

    CameraKeyframe() = default;
    CameraKeyframe(float t, const Camera& cam) : time(t), camera(cam) {}
};

// Animation path mode for camera keyframe interpolation.
enum class CameraPathMode
{
    Orbit,        // Interpolates azimuth, elevation, distance, fov (spherical coords)
    FreeFlight,   // Interpolates position, target, up via slerp for orientation
};

// CameraAnimator — manages camera keyframe animation with orbit or free-flight paths.
//
// Supports two interpolation strategies:
//   - OrbitPath: lerp on spherical coordinates (azimuth, elevation, distance, fov).
//     Best for turntable-style animations around a fixed target.
//   - FreeFlightPath: slerp on orientation quaternion + lerp on position.
//     Best for fly-through animations with arbitrary camera movement.
//
// Thread-safe: all public methods lock an internal mutex.
class CameraAnimator
{
   public:
    CameraAnimator()  = default;
    ~CameraAnimator() = default;

    CameraAnimator(const CameraAnimator&)            = delete;
    CameraAnimator& operator=(const CameraAnimator&) = delete;

    // ─── Path mode ──────────────────────────────────────────────────────

    CameraPathMode path_mode() const;
    void           set_path_mode(CameraPathMode mode);

    // ─── Keyframe management ────────────────────────────────────────────

    // Add a keyframe. If one exists at the same time (within tolerance), replace it.
    void add_keyframe(float time, const Camera& camera);
    void add_keyframe(const CameraKeyframe& kf);

    // Remove keyframe at the given time (within tolerance).
    bool remove_keyframe(float time, float tolerance = 0.001f);

    // Clear all keyframes.
    void clear();

    // ─── Queries ────────────────────────────────────────────────────────

    const std::vector<CameraKeyframe>& keyframes() const;
    size_t                             keyframe_count() const;
    bool                               empty() const;

    // Total duration (time of last keyframe).
    float duration() const;

    // ─── Evaluation ─────────────────────────────────────────────────────

    // Evaluate the camera state at a given time.
    // Returns an interpolated Camera based on the current path mode.
    Camera evaluate(float time) const;

    // Evaluate and apply directly to a Camera reference.
    void apply(float time, Camera& cam) const;

    // ─── Target camera binding ──────────────────────────────────────

    // Set a target camera that evaluate_at() will write to.
    void    set_target_camera(Camera* cam);
    Camera* target_camera() const;

    // Evaluate at the given time and apply to the bound target camera.
    // No-op if no target camera is set or no keyframes exist.
    void evaluate_at(float time);

    // ─── Convenience ────────────────────────────────────────────────────

    // Create a simple orbit animation: rotating from start_azimuth to end_azimuth
    // over the given duration, using the camera's current state as base.
    // Adds two keyframes (start and end).
    void create_orbit_animation(const Camera& base,
                                float         start_azimuth,
                                float         end_azimuth,
                                float         duration_seconds);

    // Create a full 360° turntable orbit animation.
    void create_turntable(const Camera& base, float duration_seconds);

    // ─── Serialization ──────────────────────────────────────────────────

    std::string serialize() const;
    bool        deserialize(const std::string& json);

   private:
    mutable std::mutex mutex_;

    CameraPathMode              path_mode_ = CameraPathMode::Orbit;
    std::vector<CameraKeyframe> keyframes_;   // Always sorted by time
    Camera*                     target_camera_ = nullptr;

    void sort_keyframes();

    // Interpolation helpers (called under lock)
    Camera evaluate_orbit(float time) const;
    Camera evaluate_free_flight(float time) const;

    // Find bracketing keyframes for a given time.
    // Returns indices (a, b) such that keyframes_[a].time <= time <= keyframes_[b].time.
    // Returns (-1, -1) if empty, (0, 0) if single keyframe.
    std::pair<int, int> find_bracket(float time) const;

    // Slerp helper: extract orientation quaternion from position/target/up.
    static quat orientation_from_camera(const Camera& cam);

    // Apply slerp'd orientation back to camera.
    static void apply_orientation(Camera& cam, const quat& q, float distance);
};

}   // namespace spectra

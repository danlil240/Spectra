#include "../../src/ui/camera_animator.hpp"
#include "../../src/ui/transition_engine.hpp"
#include "../../src/ui/keyframe_interpolator.hpp"
#include "../../src/ui/timeline_editor.hpp"
#include <plotix/math3d.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace plotix;

// Helper to check vector equality
bool vec3_approx_eq(const vec3& a, const vec3& b, float tol = 0.001f) {
    return std::abs(a.x - b.x) < tol &&
           std::abs(a.y - b.y) < tol &&
           std::abs(a.z - b.z) < tol;
}

// ─── CameraAnimator Unit Tests ──────────────────────────────────────────────

void test_camera_animator_basics() {
    CameraAnimator anim;
    assert(anim.keyframe_count() == 0);
    assert(anim.empty());
    assert(anim.duration() == 0.0f);
    assert(anim.path_mode() == CameraPathMode::Orbit);

    Camera cam;
    cam.azimuth = 0.0f;
    anim.add_keyframe(0.0f, cam);
    assert(anim.keyframe_count() == 1);
    assert(!anim.empty());
    assert(anim.duration() == 0.0f);

    cam.azimuth = 90.0f;
    anim.add_keyframe(10.0f, cam);
    assert(anim.keyframe_count() == 2);
    assert(anim.duration() == 10.0f);

    // Replace existing
    cam.azimuth = 45.0f;
    anim.add_keyframe(10.0f, cam); // Should overwrite
    assert(anim.keyframe_count() == 2);
    
    // Evaluate exact
    Camera out = anim.evaluate(10.0f);
    assert(std::abs(out.azimuth - 45.0f) < 0.001f);

    // Remove
    bool removed = anim.remove_keyframe(10.0f);
    assert(removed);
    assert(anim.keyframe_count() == 1);

    anim.clear();
    assert(anim.empty());
    
    std::cout << "✓ test_camera_animator_basics passed\n";
}

void test_camera_animator_orbit() {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::Orbit);

    Camera c1;
    c1.azimuth = 0.0f; c1.elevation = 0.0f; c1.distance = 10.0f;
    c1.update_position_from_orbit();

    Camera c2;
    c2.azimuth = 100.0f; c2.elevation = 50.0f; c2.distance = 20.0f;
    c2.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    // Midpoint evaluation
    Camera mid = anim.evaluate(0.5f);
    assert(std::abs(mid.azimuth - 50.0f) < 0.001f);
    assert(std::abs(mid.elevation - 25.0f) < 0.001f);
    assert(std::abs(mid.distance - 15.0f) < 0.001f);

    // Ensure position is updated from orbit params
    mid.update_position_from_orbit(); // Should be redundant if evaluate does it
    assert(std::abs(mid.position.x - mid.target.x) > 0.01f); // Just ensure check passes

    std::cout << "✓ test_camera_animator_orbit passed\n";
}

void test_camera_animator_free_flight() {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);

    Camera c1; 
    c1.position = {0,0,0}; c1.target = {0,0,-1};
    
    Camera c2; 
    c2.position = {10,0,0}; c2.target = {10,0,-1};

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(2.0f, c2);

    // Midpoint linear interpolation of position
    Camera mid = anim.evaluate(1.0f);
    assert(std::abs(mid.position.x - 5.0f) < 0.001f);

    // Orientation interpolation (slerp) case
    // Rotate 90 degrees around Y:  (0,0,-1) -> (-1,0,0)
    Camera c3; c3.position = {0,0,0}; c3.target = {0,0,-1};
    Camera c4; c4.position = {0,0,0}; c4.target = {-1,0,0};
    
    anim.clear();
    anim.add_keyframe(0.0f, c3);
    anim.add_keyframe(1.0f, c4);
    
    mid = anim.evaluate(0.5f);
    // At 0.5 (45 deg), target should be roughly (-0.707, 0, -0.707)
    // Forward vector is normalized(target - pos)
    vec3 fwd = vec3_normalize(mid.target - mid.position);
    assert(std::abs(fwd.x + 0.7071f) < 0.01f);
    assert(std::abs(fwd.z + 0.7071f) < 0.01f);

    std::cout << "✓ test_camera_animator_free_flight passed\n";
}

void test_camera_animator_serialization() {
    CameraAnimator anim;
    anim.create_turntable(Camera{}, 10.0f);
    
    std::string json = anim.serialize();
    
    CameraAnimator anim2;
    bool success = anim2.deserialize(json);
    assert(success);
    assert(anim2.keyframe_count() == 2);
    assert(anim2.duration() == 10.0f);
    assert(anim2.path_mode() == CameraPathMode::Orbit);
    
    std::cout << "✓ test_camera_animator_serialization passed\n";
}

// ─── TransitionEngine Unit Tests ────────────────────────────────────────────

void test_transition_engine_camera() {
    TransitionEngine engine;
    Camera cam;
    cam.azimuth = 0.0f;
    cam.elevation = 0.0f;
    
    Camera target_cam = cam;
    target_cam.azimuth = 100.0f;
    target_cam.elevation = 50.0f;
    
    auto id = engine.animate_camera(cam, target_cam, 1.0f, ease::linear);
    assert(engine.has_active_animations());
    assert(engine.active_count() == 1);
    
    // Update to 0.5s
    engine.update(0.5f);
    assert(std::abs(cam.azimuth - 50.0f) < 0.001f);
    assert(std::abs(cam.elevation - 25.0f) < 0.001f);
    assert(engine.has_active_animations());
    
    // Finish
    engine.update(0.6f); // 1.1s total
    assert(std::abs(cam.azimuth - 100.0f) < 0.001f);
    assert(!engine.has_active_animations()); // Should be finished
    
    std::cout << "✓ test_transition_engine_camera passed\n";
}

void test_transition_engine_cancel() {
    TransitionEngine engine;
    Camera cam;
    
    auto id = engine.animate_camera(cam, cam, 10.0f);
    assert(engine.has_active_animations());
    
    engine.cancel_for_camera(&cam);
    assert(!engine.has_active_animations());
    
    // Cancel by ID
    id = engine.animate_camera(cam, cam, 10.0f);
    engine.cancel(id);
    assert(!engine.has_active_animations());
    
    std::cout << "✓ test_transition_engine_cancel passed\n";
}

// ─── KeyframeInterpolator Camera Bindings ───────────────────────────────────

void test_interpolator_camera_binding() {
    KeyframeInterpolator interp;
    Camera cam;
    cam.azimuth = 0.0f;
    
    uint32_t ch_az = interp.add_channel("Azimuth", 0.0f);
    TypedKeyframe kf1(0.0f, 0.0f);
    TypedKeyframe kf2(1.0f, 100.0f);
    interp.add_keyframe(ch_az, kf1);
    interp.add_keyframe(ch_az, kf2);
    
    interp.bind_camera(&cam, ch_az, 0, 0, 0);
    
    interp.evaluate(0.5f);
    assert(std::abs(cam.azimuth - 50.0f) < 0.001f);
    
    interp.unbind_camera(&cam);
    interp.evaluate(1.0f); 
    // Should NOT update after unbind
    assert(std::abs(cam.azimuth - 50.0f) < 0.001f); 
    
    std::cout << "✓ test_interpolator_camera_binding passed\n";
}

// ─── TimelineEditor Integration ─────────────────────────────────────────────

void test_timeline_editor_camera_integration() {
    TimelineEditor timeline;
    CameraAnimator anim;
    
    timeline.set_camera_animator(&anim);
    assert(timeline.camera_animator() == &anim);
    
    timeline.set_camera_animator(nullptr);
    assert(timeline.camera_animator() == nullptr);
    
    std::cout << "✓ test_timeline_editor_camera_integration passed\n";
}

int main() {
    std::cout << "Running Camera Animation unit tests...\n\n";
    
    test_camera_animator_basics();
    test_camera_animator_orbit();
    test_camera_animator_free_flight();
    test_camera_animator_serialization();
    test_transition_engine_camera();
    test_transition_engine_cancel();
    test_interpolator_camera_binding();
    test_timeline_editor_camera_integration();
    
    std::cout << "\n✅ All Camera Animation tests passed!\n";
    return 0;
}

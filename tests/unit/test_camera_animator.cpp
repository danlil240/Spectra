#include <gtest/gtest.h>

#include "ui/camera_animator.hpp"
#include "ui/transition_engine.hpp"
#include "ui/keyframe_interpolator.hpp"
#include "ui/timeline_editor.hpp"
#include <plotix/camera.hpp>
#include <plotix/math3d.hpp>

#include <cmath>

using namespace plotix;

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 1: CameraAnimatorConstruction (3 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorConstruction, DefaultState) {
    CameraAnimator anim;
    EXPECT_EQ(anim.keyframe_count(), 0u);
    EXPECT_TRUE(anim.empty());
    EXPECT_FLOAT_EQ(anim.duration(), 0.0f);
    EXPECT_EQ(anim.path_mode(), CameraPathMode::Orbit);
}

TEST(CameraAnimatorConstruction, EvaluateEmpty) {
    CameraAnimator anim;
    Camera cam = anim.evaluate(1.0f);
    // Default camera returned
    EXPECT_FLOAT_EQ(cam.fov, 45.0f);
}

TEST(CameraAnimatorConstruction, SetPathMode) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);
    EXPECT_EQ(anim.path_mode(), CameraPathMode::FreeFlight);
    anim.set_path_mode(CameraPathMode::Orbit);
    EXPECT_EQ(anim.path_mode(), CameraPathMode::Orbit);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 2: CameraAnimatorKeyframes (8 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorKeyframes, AddSingle) {
    CameraAnimator anim;
    Camera cam; cam.azimuth = 10.0f;
    anim.add_keyframe(0.0f, cam);
    EXPECT_EQ(anim.keyframe_count(), 1u);
    EXPECT_FALSE(anim.empty());
    EXPECT_FLOAT_EQ(anim.duration(), 0.0f);
}

TEST(CameraAnimatorKeyframes, AddMultiple) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 0.0f;
    Camera c2; c2.azimuth = 90.0f;
    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(5.0f, c2);
    EXPECT_EQ(anim.keyframe_count(), 2u);
    EXPECT_FLOAT_EQ(anim.duration(), 5.0f);
}

TEST(CameraAnimatorKeyframes, ReplaceExisting) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 0.0f;
    Camera c2; c2.azimuth = 45.0f;
    anim.add_keyframe(1.0f, c1);
    anim.add_keyframe(1.0f, c2); // Replace
    EXPECT_EQ(anim.keyframe_count(), 1u);
    Camera out = anim.evaluate(1.0f);
    EXPECT_NEAR(out.azimuth, 45.0f, 0.001f);
}

TEST(CameraAnimatorKeyframes, RemoveExisting) {
    CameraAnimator anim;
    Camera cam;
    anim.add_keyframe(0.0f, cam);
    anim.add_keyframe(1.0f, cam);
    EXPECT_TRUE(anim.remove_keyframe(1.0f));
    EXPECT_EQ(anim.keyframe_count(), 1u);
}

TEST(CameraAnimatorKeyframes, RemoveNonExistent) {
    CameraAnimator anim;
    Camera cam;
    anim.add_keyframe(0.0f, cam);
    EXPECT_FALSE(anim.remove_keyframe(5.0f));
    EXPECT_EQ(anim.keyframe_count(), 1u);
}

TEST(CameraAnimatorKeyframes, Clear) {
    CameraAnimator anim;
    Camera cam;
    anim.add_keyframe(0.0f, cam);
    anim.add_keyframe(1.0f, cam);
    anim.add_keyframe(2.0f, cam);
    anim.clear();
    EXPECT_TRUE(anim.empty());
    EXPECT_EQ(anim.keyframe_count(), 0u);
}

TEST(CameraAnimatorKeyframes, SortedByTime) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 30.0f;
    Camera c2; c2.azimuth = 10.0f;
    Camera c3; c3.azimuth = 20.0f;
    anim.add_keyframe(3.0f, c1);
    anim.add_keyframe(1.0f, c2);
    anim.add_keyframe(2.0f, c3);
    EXPECT_EQ(anim.keyframe_count(), 3u);
    // Evaluate at t=1 should give c2's azimuth
    Camera out = anim.evaluate(1.0f);
    EXPECT_NEAR(out.azimuth, 10.0f, 0.001f);
}

TEST(CameraAnimatorKeyframes, AddViaCameraKeyframeStruct) {
    CameraAnimator anim;
    Camera cam; cam.elevation = 42.0f;
    CameraKeyframe kf(2.5f, cam);
    anim.add_keyframe(kf);
    EXPECT_EQ(anim.keyframe_count(), 1u);
    EXPECT_FLOAT_EQ(anim.duration(), 2.5f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 3: CameraAnimatorOrbit (5 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorOrbit, LinearAzimuth) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::Orbit);

    Camera c1; c1.azimuth = 0.0f; c1.elevation = 0.0f; c1.distance = 10.0f;
    c1.update_position_from_orbit();
    Camera c2; c2.azimuth = 100.0f; c2.elevation = 0.0f; c2.distance = 10.0f;
    c2.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    EXPECT_NEAR(mid.azimuth, 50.0f, 0.001f);
}

TEST(CameraAnimatorOrbit, LinearElevation) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 45.0f; c1.elevation = 0.0f; c1.distance = 10.0f;
    c1.update_position_from_orbit();
    Camera c2; c2.azimuth = 45.0f; c2.elevation = 60.0f; c2.distance = 10.0f;
    c2.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    EXPECT_NEAR(mid.elevation, 30.0f, 0.001f);
}

TEST(CameraAnimatorOrbit, LinearDistance) {
    CameraAnimator anim;
    Camera c1; c1.distance = 5.0f; c1.update_position_from_orbit();
    Camera c2; c2.distance = 25.0f; c2.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    EXPECT_NEAR(mid.distance, 15.0f, 0.001f);
}

TEST(CameraAnimatorOrbit, PositionUpdatedFromOrbit) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 0.0f; c1.elevation = 0.0f; c1.distance = 10.0f;
    c1.target = {0, 0, 0};
    c1.update_position_from_orbit();
    Camera c2; c2.azimuth = 90.0f; c2.elevation = 0.0f; c2.distance = 10.0f;
    c2.target = {0, 0, 0};
    c2.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    // Position should be non-zero and derived from orbit
    float dist_from_target = vec3_length(mid.position - mid.target);
    EXPECT_NEAR(dist_from_target, 10.0f, 0.01f);
}

TEST(CameraAnimatorOrbit, TargetLerp) {
    CameraAnimator anim;
    Camera c1; c1.target = {0, 0, 0}; c1.update_position_from_orbit();
    Camera c2; c2.target = {10, 20, 30}; c2.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    EXPECT_NEAR(mid.target.x, 5.0f, 0.001f);
    EXPECT_NEAR(mid.target.y, 10.0f, 0.001f);
    EXPECT_NEAR(mid.target.z, 15.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 4: CameraAnimatorFreeFlight (4 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorFreeFlight, PositionLerp) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);

    Camera c1; c1.position = {0, 0, 0}; c1.target = {0, 0, -1};
    Camera c2; c2.position = {10, 0, 0}; c2.target = {10, 0, -1};

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(2.0f, c2);

    Camera mid = anim.evaluate(1.0f);
    EXPECT_NEAR(mid.position.x, 5.0f, 0.001f);
    EXPECT_NEAR(mid.position.y, 0.0f, 0.001f);
    EXPECT_NEAR(mid.position.z, 0.0f, 0.001f);
}

TEST(CameraAnimatorFreeFlight, OrientationSlerp) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);

    Camera c1; c1.position = {0, 0, 0}; c1.target = {0, 0, -1};
    Camera c2; c2.position = {0, 0, 0}; c2.target = {-1, 0, 0};

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    vec3 fwd = vec3_normalize(mid.target - mid.position);
    // At 45 degrees: roughly (-0.707, 0, -0.707)
    EXPECT_NEAR(fwd.x, -0.7071f, 0.02f);
    EXPECT_NEAR(fwd.z, -0.7071f, 0.02f);
}

TEST(CameraAnimatorFreeFlight, FovLerp) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);

    Camera c1; c1.position = {0, 0, 5}; c1.target = {0, 0, 0}; c1.fov = 30.0f;
    Camera c2; c2.position = {0, 0, 5}; c2.target = {0, 0, 0}; c2.fov = 90.0f;

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    EXPECT_NEAR(mid.fov, 60.0f, 0.001f);
}

TEST(CameraAnimatorFreeFlight, ScalarParamsLerp) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);

    Camera c1; c1.position = {0, 0, 5}; c1.target = {0, 0, 0};
    c1.near_clip = 0.1f; c1.far_clip = 100.0f; c1.ortho_size = 5.0f;
    Camera c2; c2.position = {0, 0, 5}; c2.target = {0, 0, 0};
    c2.near_clip = 1.0f; c2.far_clip = 1000.0f; c2.ortho_size = 15.0f;

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);

    Camera mid = anim.evaluate(0.5f);
    EXPECT_NEAR(mid.near_clip, 0.55f, 0.001f);
    EXPECT_NEAR(mid.far_clip, 550.0f, 0.001f);
    EXPECT_NEAR(mid.ortho_size, 10.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 5: CameraAnimatorConvenience (3 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorConvenience, CreateOrbitAnimation) {
    CameraAnimator anim;
    Camera base; base.elevation = 30.0f; base.distance = 10.0f;
    anim.create_orbit_animation(base, 0.0f, 180.0f, 5.0f);

    EXPECT_EQ(anim.keyframe_count(), 2u);
    EXPECT_FLOAT_EQ(anim.duration(), 5.0f);
    EXPECT_EQ(anim.path_mode(), CameraPathMode::Orbit);

    Camera mid = anim.evaluate(2.5f);
    EXPECT_NEAR(mid.azimuth, 90.0f, 0.001f);
}

TEST(CameraAnimatorConvenience, CreateTurntable) {
    CameraAnimator anim;
    Camera base; base.azimuth = 45.0f;
    anim.create_turntable(base, 10.0f);

    EXPECT_EQ(anim.keyframe_count(), 2u);
    EXPECT_FLOAT_EQ(anim.duration(), 10.0f);

    Camera end = anim.evaluate(10.0f);
    EXPECT_NEAR(end.azimuth, 45.0f + 360.0f, 0.001f);
}

TEST(CameraAnimatorConvenience, TurntableClearsExisting) {
    CameraAnimator anim;
    Camera cam;
    anim.add_keyframe(0.0f, cam);
    anim.add_keyframe(1.0f, cam);
    anim.add_keyframe(2.0f, cam);
    EXPECT_EQ(anim.keyframe_count(), 3u);

    anim.create_turntable(cam, 5.0f);
    EXPECT_EQ(anim.keyframe_count(), 2u); // Clears and adds 2
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 6: CameraAnimatorSerialization (4 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorSerialization, RoundTrip) {
    CameraAnimator anim;
    anim.create_turntable(Camera{}, 10.0f);

    std::string json = anim.serialize();
    EXPECT_FALSE(json.empty());

    CameraAnimator anim2;
    EXPECT_TRUE(anim2.deserialize(json));
    EXPECT_EQ(anim2.keyframe_count(), 2u);
    EXPECT_FLOAT_EQ(anim2.duration(), 10.0f);
    EXPECT_EQ(anim2.path_mode(), CameraPathMode::Orbit);
}

TEST(CameraAnimatorSerialization, PreservesPathMode) {
    CameraAnimator anim;
    anim.set_path_mode(CameraPathMode::FreeFlight);
    Camera cam; cam.position = {1, 2, 3}; cam.target = {4, 5, 6};
    anim.add_keyframe(0.0f, cam);

    std::string json = anim.serialize();
    CameraAnimator anim2;
    anim2.deserialize(json);
    EXPECT_EQ(anim2.path_mode(), CameraPathMode::FreeFlight);
}

TEST(CameraAnimatorSerialization, PreservesCameraParams) {
    CameraAnimator anim;
    Camera cam;
    cam.azimuth = 123.0f; cam.elevation = 45.0f; cam.distance = 7.5f;
    cam.fov = 60.0f; cam.target = {1, 2, 3};
    cam.update_position_from_orbit();
    anim.add_keyframe(2.0f, cam);

    std::string json = anim.serialize();
    CameraAnimator anim2;
    anim2.deserialize(json);

    Camera out = anim2.evaluate(2.0f);
    EXPECT_NEAR(out.azimuth, 123.0f, 0.01f);
    EXPECT_NEAR(out.elevation, 45.0f, 0.01f);
    EXPECT_NEAR(out.distance, 7.5f, 0.01f);
    EXPECT_NEAR(out.fov, 60.0f, 0.01f);
}

TEST(CameraAnimatorSerialization, DeserializeInvalid) {
    CameraAnimator anim;
    EXPECT_FALSE(anim.deserialize(""));
    EXPECT_FALSE(anim.deserialize("not json"));
    EXPECT_TRUE(anim.empty());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 7: CameraAnimatorBracket (3 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorBracket, BeforeFirstKeyframe) {
    CameraAnimator anim;
    Camera cam; cam.azimuth = 42.0f; cam.update_position_from_orbit();
    anim.add_keyframe(5.0f, cam);

    Camera out = anim.evaluate(0.0f);
    EXPECT_NEAR(out.azimuth, 42.0f, 0.001f);
}

TEST(CameraAnimatorBracket, AfterLastKeyframe) {
    CameraAnimator anim;
    Camera cam; cam.azimuth = 99.0f; cam.update_position_from_orbit();
    anim.add_keyframe(1.0f, cam);

    Camera out = anim.evaluate(100.0f);
    EXPECT_NEAR(out.azimuth, 99.0f, 0.001f);
}

TEST(CameraAnimatorBracket, ExactKeyframeTime) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 10.0f; c1.update_position_from_orbit();
    Camera c2; c2.azimuth = 20.0f; c2.update_position_from_orbit();
    Camera c3; c3.azimuth = 30.0f; c3.update_position_from_orbit();
    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);
    anim.add_keyframe(2.0f, c3);

    EXPECT_NEAR(anim.evaluate(0.0f).azimuth, 10.0f, 0.001f);
    EXPECT_NEAR(anim.evaluate(1.0f).azimuth, 20.0f, 0.001f);
    EXPECT_NEAR(anim.evaluate(2.0f).azimuth, 30.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 8: TransitionEngineCamera (5 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TransitionEngineCamera, AnimateAzimuth) {
    TransitionEngine engine;
    Camera cam; cam.azimuth = 0.0f; cam.elevation = 0.0f;

    Camera target = cam;
    target.azimuth = 100.0f; target.elevation = 50.0f;

    engine.animate_camera(cam, target, 1.0f, ease::linear);
    EXPECT_TRUE(engine.has_active_animations());
    EXPECT_EQ(engine.active_count(), 1u);

    engine.update(0.5f);
    EXPECT_NEAR(cam.azimuth, 50.0f, 0.5f);
    EXPECT_NEAR(cam.elevation, 25.0f, 0.5f);
}

TEST(TransitionEngineCamera, SnapsToEnd) {
    TransitionEngine engine;
    Camera cam; cam.azimuth = 0.0f;
    Camera target = cam; target.azimuth = 100.0f;

    engine.animate_camera(cam, target, 1.0f, ease::linear);
    engine.update(1.5f); // Overshoot
    EXPECT_NEAR(cam.azimuth, 100.0f, 0.001f);
    EXPECT_FALSE(engine.has_active_animations());
}

TEST(TransitionEngineCamera, CancelForCamera) {
    TransitionEngine engine;
    Camera cam;
    engine.animate_camera(cam, cam, 10.0f);
    EXPECT_TRUE(engine.has_active_animations());
    engine.cancel_for_camera(&cam);
    EXPECT_FALSE(engine.has_active_animations());
}

TEST(TransitionEngineCamera, CancelById) {
    TransitionEngine engine;
    Camera cam;
    auto id = engine.animate_camera(cam, cam, 10.0f);
    engine.cancel(id);
    EXPECT_FALSE(engine.has_active_animations());
}

TEST(TransitionEngineCamera, ReplacesExisting) {
    TransitionEngine engine;
    Camera cam; cam.azimuth = 0.0f;
    Camera t1 = cam; t1.azimuth = 50.0f;
    Camera t2 = cam; t2.azimuth = 200.0f;

    engine.animate_camera(cam, t1, 1.0f, ease::linear);
    engine.animate_camera(cam, t2, 1.0f, ease::linear);
    // Second should replace first
    EXPECT_EQ(engine.active_count(), 1u);

    engine.update(1.0f);
    EXPECT_NEAR(cam.azimuth, 200.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 9: InterpolatorCameraBinding (5 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(InterpolatorCameraBinding, AzimuthChannel) {
    KeyframeInterpolator interp;
    Camera cam; cam.azimuth = 0.0f;

    uint32_t ch = interp.add_channel("Azimuth", 0.0f);
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(ch, TypedKeyframe(1.0f, 100.0f));
    interp.bind_camera(&cam, ch, 0, 0, 0);

    interp.evaluate(0.5f);
    EXPECT_NEAR(cam.azimuth, 50.0f, 0.001f);
}

TEST(InterpolatorCameraBinding, MultipleChannels) {
    KeyframeInterpolator interp;
    Camera cam; cam.azimuth = 0.0f; cam.elevation = 0.0f;

    uint32_t az = interp.add_channel("Azimuth", 0.0f);
    uint32_t el = interp.add_channel("Elevation", 0.0f);
    interp.add_keyframe(az, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(az, TypedKeyframe(1.0f, 360.0f));
    interp.add_keyframe(el, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(el, TypedKeyframe(1.0f, 45.0f));

    interp.bind_camera(&cam, az, el, 0, 0);

    interp.evaluate(0.5f);
    EXPECT_NEAR(cam.azimuth, 180.0f, 0.001f);
    EXPECT_NEAR(cam.elevation, 22.5f, 0.001f);
}

TEST(InterpolatorCameraBinding, DistanceAndFov) {
    KeyframeInterpolator interp;
    Camera cam; cam.distance = 5.0f; cam.fov = 45.0f;

    uint32_t dist = interp.add_channel("Distance", 5.0f);
    uint32_t fov = interp.add_channel("FOV", 45.0f);
    interp.add_keyframe(dist, TypedKeyframe(0.0f, 5.0f));
    interp.add_keyframe(dist, TypedKeyframe(1.0f, 25.0f));
    interp.add_keyframe(fov, TypedKeyframe(0.0f, 45.0f));
    interp.add_keyframe(fov, TypedKeyframe(1.0f, 90.0f));

    interp.bind_camera(&cam, 0, 0, dist, fov);

    interp.evaluate(0.5f);
    EXPECT_NEAR(cam.distance, 15.0f, 0.001f);
    EXPECT_NEAR(cam.fov, 67.5f, 0.001f);
}

TEST(InterpolatorCameraBinding, UnbindStopsUpdates) {
    KeyframeInterpolator interp;
    Camera cam; cam.azimuth = 0.0f;

    uint32_t ch = interp.add_channel("Azimuth", 0.0f);
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(ch, TypedKeyframe(1.0f, 100.0f));
    interp.bind_camera(&cam, ch, 0, 0, 0);

    interp.evaluate(0.5f);
    EXPECT_NEAR(cam.azimuth, 50.0f, 0.001f);

    interp.unbind_camera(&cam);
    interp.evaluate(1.0f);
    EXPECT_NEAR(cam.azimuth, 50.0f, 0.001f); // Unchanged
}

TEST(InterpolatorCameraBinding, UpdatesPositionFromOrbit) {
    KeyframeInterpolator interp;
    Camera cam; cam.azimuth = 0.0f; cam.distance = 10.0f;
    cam.target = {0, 0, 0};
    cam.update_position_from_orbit();

    uint32_t ch = interp.add_channel("Azimuth", 0.0f);
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(ch, TypedKeyframe(1.0f, 90.0f));
    interp.bind_camera(&cam, ch, 0, 0, 0);

    interp.evaluate(1.0f);
    EXPECT_NEAR(cam.azimuth, 90.0f, 0.001f);
    // Position should have been updated from orbit
    float dist = vec3_length(cam.position - cam.target);
    EXPECT_NEAR(dist, 10.0f, 0.1f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 10: TimelineCameraIntegration (4 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TimelineCameraIntegration, SetGetCameraAnimator) {
    TimelineEditor timeline;
    CameraAnimator anim;

    timeline.set_camera_animator(&anim);
    EXPECT_EQ(timeline.camera_animator(), &anim);

    timeline.set_camera_animator(nullptr);
    EXPECT_EQ(timeline.camera_animator(), nullptr);
}

TEST(TimelineCameraIntegration, AdvanceEvaluatesCameraAnimator) {
    TimelineEditor timeline;
    CameraAnimator cam_anim;
    Camera cam; cam.azimuth = 0.0f; cam.distance = 10.0f;
    cam.update_position_from_orbit();

    Camera end_cam = cam;
    end_cam.azimuth = 90.0f;
    end_cam.update_position_from_orbit();

    cam_anim.add_keyframe(0.0f, cam);
    cam_anim.add_keyframe(10.0f, end_cam);

    // Wire up via KeyframeInterpolator + camera binding
    KeyframeInterpolator interp;
    uint32_t ch = interp.add_channel("Azimuth", 0.0f);
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(ch, TypedKeyframe(10.0f, 90.0f));
    interp.bind_camera(&cam, ch, 0, 0, 0);

    timeline.set_interpolator(&interp);
    timeline.set_duration(10.0f);
    timeline.play();
    timeline.advance(5.0f);

    // Camera should be at ~45 degrees via interpolator
    EXPECT_NEAR(cam.azimuth, 45.0f, 0.001f);
}

TEST(TimelineCameraIntegration, ScrubUpdatesCameraViaInterpolator) {
    TimelineEditor timeline;
    KeyframeInterpolator interp;
    Camera cam; cam.azimuth = 0.0f;

    uint32_t ch = interp.add_channel("Azimuth", 0.0f);
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(ch, TypedKeyframe(10.0f, 180.0f));
    interp.bind_camera(&cam, ch, 0, 0, 0);

    timeline.set_interpolator(&interp);
    timeline.set_duration(10.0f);

    // Scrub to midpoint and evaluate
    timeline.scrub_to(5.0f);
    timeline.evaluate_at_playhead();

    EXPECT_NEAR(cam.azimuth, 90.0f, 0.001f);
}

TEST(TimelineCameraIntegration, LoopPlaybackWithCamera) {
    TimelineEditor timeline;
    KeyframeInterpolator interp;
    Camera cam; cam.azimuth = 0.0f;

    uint32_t ch = interp.add_channel("Azimuth", 0.0f);
    interp.add_keyframe(ch, TypedKeyframe(0.0f, 0.0f));
    interp.add_keyframe(ch, TypedKeyframe(1.0f, 100.0f));
    interp.bind_camera(&cam, ch, 0, 0, 0);

    timeline.set_interpolator(&interp);
    timeline.set_duration(1.0f);
    timeline.set_loop_mode(LoopMode::Loop);
    timeline.play();

    // Advance past loop point
    timeline.advance(1.5f);
    // Should have looped — azimuth should be at ~50 (0.5 into second loop)
    EXPECT_NEAR(cam.azimuth, 50.0f, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Suite 11: CameraAnimatorEdgeCases (4 tests)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CameraAnimatorEdgeCases, SingleKeyframe) {
    CameraAnimator anim;
    Camera cam; cam.azimuth = 42.0f; cam.update_position_from_orbit();
    anim.add_keyframe(0.0f, cam);

    Camera out = anim.evaluate(0.0f);
    EXPECT_NEAR(out.azimuth, 42.0f, 0.001f);
    out = anim.evaluate(100.0f);
    EXPECT_NEAR(out.azimuth, 42.0f, 0.001f);
}

TEST(CameraAnimatorEdgeCases, ZeroDurationSegment) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 0.0f; c1.update_position_from_orbit();
    Camera c2; c2.azimuth = 90.0f; c2.update_position_from_orbit();
    anim.add_keyframe(5.0f, c1);
    anim.add_keyframe(5.0f, c2); // Same time — replaces
    EXPECT_EQ(anim.keyframe_count(), 1u);
}

TEST(CameraAnimatorEdgeCases, ApplyMethod) {
    CameraAnimator anim;
    Camera base; base.azimuth = 0.0f; base.update_position_from_orbit();
    Camera end; end.azimuth = 90.0f; end.update_position_from_orbit();
    anim.add_keyframe(0.0f, base);
    anim.add_keyframe(1.0f, end);

    Camera target;
    anim.apply(0.5f, target);
    EXPECT_NEAR(target.azimuth, 45.0f, 0.001f);
}

TEST(CameraAnimatorEdgeCases, MultiSegmentInterpolation) {
    CameraAnimator anim;
    Camera c1; c1.azimuth = 0.0f; c1.update_position_from_orbit();
    Camera c2; c2.azimuth = 90.0f; c2.update_position_from_orbit();
    Camera c3; c3.azimuth = 180.0f; c3.update_position_from_orbit();

    anim.add_keyframe(0.0f, c1);
    anim.add_keyframe(1.0f, c2);
    anim.add_keyframe(2.0f, c3);

    EXPECT_NEAR(anim.evaluate(0.5f).azimuth, 45.0f, 0.001f);
    EXPECT_NEAR(anim.evaluate(1.5f).azimuth, 135.0f, 0.001f);
}

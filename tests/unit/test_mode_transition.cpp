#include <atomic>
#include <cmath>
#include <gtest/gtest.h>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <thread>

#include "ui/animation/mode_transition.hpp"

using namespace spectra;

// ─── Construction ───────────────────────────────────────────────────────────

TEST(ModeTransitionConstruction, DefaultState)
{
    ModeTransition mt;
    EXPECT_EQ(mt.state(), ModeTransitionState::Idle);
    EXPECT_FALSE(mt.is_active());
    EXPECT_FLOAT_EQ(mt.progress(), 0.0f);
}

TEST(ModeTransitionConstruction, DefaultDuration)
{
    ModeTransition mt;
    EXPECT_FLOAT_EQ(mt.duration(), 0.6f);
}

TEST(ModeTransitionConstruction, SetDuration)
{
    ModeTransition mt;
    mt.set_duration(1.5f);
    EXPECT_FLOAT_EQ(mt.duration(), 1.5f);
}

TEST(ModeTransitionConstruction, SetDurationClampsPositive)
{
    ModeTransition mt;
    mt.set_duration(-1.0f);
    EXPECT_GT(mt.duration(), 0.0f);
}

// ─── To3D Transition ────────────────────────────────────────────────────────

TEST(ModeTransitionTo3D, BeginReturnsNonZeroId)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    auto                  id = mt.begin_to_3d(s2d, s3d);
    EXPECT_GT(id, 0u);
}

TEST(ModeTransitionTo3D, StateBecomesAnimating)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    EXPECT_TRUE(mt.is_active());
}

TEST(ModeTransitionTo3D, DirectionIsTo3D)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    EXPECT_EQ(mt.direction(), ModeTransitionDirection::To3D);
}

TEST(ModeTransitionTo3D, RejectsWhileAnimating)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    auto id2 = mt.begin_to_3d(s2d, s3d);
    EXPECT_EQ(id2, 0u);
}

TEST(ModeTransitionTo3D, InitialOpacityIsZero)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    EXPECT_FLOAT_EQ(mt.element_3d_opacity(), 0.0f);
}

TEST(ModeTransitionTo3D, InitialZLimMatchesTarget)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.zlim = {-5.0f, 5.0f};
    mt.begin_to_3d(s2d, s3d);
    auto zlim = mt.interpolated_zlim();
    // Axis limits stay constant — never interpolated
    EXPECT_NEAR(zlim.min, -5.0f, 0.01f);
    EXPECT_NEAR(zlim.max, 5.0f, 0.01f);
}

TEST(ModeTransitionTo3D, ProgressIncreasesWithUpdate)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.5f);
    EXPECT_GT(mt.progress(), 0.0f);
    EXPECT_LT(mt.progress(), 1.0f);
}

TEST(ModeTransitionTo3D, CompletesAfterDuration)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.6f);
    EXPECT_FALSE(mt.is_active());
    EXPECT_EQ(mt.state(), ModeTransitionState::Finished);
}

TEST(ModeTransitionTo3D, OpacityReachesOneAtEnd)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    mt.set_easing([](float t) { return t; });   // Linear
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.5f);
    EXPECT_NEAR(mt.element_3d_opacity(), 1.0f, 0.01f);
}

TEST(ModeTransitionTo3D, ZLimExpandsAtEnd)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    mt.set_easing([](float t) { return t; });   // Linear
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.zlim = {-5.0f, 5.0f};
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.5f);
    auto zlim = mt.interpolated_zlim();
    EXPECT_NEAR(zlim.min, -5.0f, 0.1f);
    EXPECT_NEAR(zlim.max, 5.0f, 0.1f);
}

TEST(ModeTransitionTo3D, XLimStaysConstant)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    mt.set_easing([](float t) { return t; });   // Linear
    ModeTransition2DState s2d;
    s2d.xlim = {0.0f, 10.0f};
    ModeTransition3DState s3d;
    s3d.xlim = {-5.0f, 5.0f};
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.5f);   // t=0.5
    auto xlim = mt.interpolated_xlim();
    // Axis limits stay at 3D target — never interpolated
    EXPECT_NEAR(xlim.min, -5.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 5.0f, 0.01f);
}

// ─── To2D Transition ────────────────────────────────────────────────────────

TEST(ModeTransitionTo2D, BeginReturnsNonZeroId)
{
    ModeTransition        mt;
    ModeTransition3DState s3d;
    ModeTransition2DState s2d;
    auto                  id = mt.begin_to_2d(s3d, s2d);
    EXPECT_GT(id, 0u);
}

TEST(ModeTransitionTo2D, DirectionIsTo2D)
{
    ModeTransition        mt;
    ModeTransition3DState s3d;
    ModeTransition2DState s2d;
    mt.begin_to_2d(s3d, s2d);
    EXPECT_EQ(mt.direction(), ModeTransitionDirection::To2D);
}

TEST(ModeTransitionTo2D, InitialOpacityIsOne)
{
    ModeTransition        mt;
    ModeTransition3DState s3d;
    ModeTransition2DState s2d;
    mt.begin_to_2d(s3d, s2d);
    EXPECT_FLOAT_EQ(mt.element_3d_opacity(), 1.0f);
}

TEST(ModeTransitionTo2D, OpacityReachesZeroAtEnd)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    mt.set_easing([](float t) { return t; });
    ModeTransition3DState s3d;
    ModeTransition2DState s2d;
    mt.begin_to_2d(s3d, s2d);
    mt.update(0.5f);
    EXPECT_NEAR(mt.element_3d_opacity(), 0.0f, 0.01f);
}

TEST(ModeTransitionTo2D, ZLimStaysConstant)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    mt.set_easing([](float t) { return t; });
    ModeTransition3DState s3d;
    s3d.zlim = {-5.0f, 5.0f};
    ModeTransition2DState s2d;
    mt.begin_to_2d(s3d, s2d);
    mt.update(0.5f);
    auto zlim = mt.interpolated_zlim();
    // Axis limits stay constant — never interpolated
    EXPECT_NEAR(zlim.min, -5.0f, 0.01f);
    EXPECT_NEAR(zlim.max, 5.0f, 0.01f);
}

TEST(ModeTransitionTo2D, CompletesAfterDuration)
{
    ModeTransition mt;
    mt.set_duration(0.3f);
    ModeTransition3DState s3d;
    ModeTransition2DState s2d;
    mt.begin_to_2d(s3d, s2d);
    mt.update(0.4f);
    EXPECT_FALSE(mt.is_active());
}

// ─── Camera Interpolation ───────────────────────────────────────────────────

TEST(ModeTransitionCamera, TopDownStartsOrthographic)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.camera.projection_mode = Camera::ProjectionMode::Perspective;
    mt.begin_to_3d(s2d, s3d);
    auto cam = mt.interpolated_camera();
    EXPECT_EQ(cam.projection_mode, Camera::ProjectionMode::Orthographic);
}

TEST(ModeTransitionCamera, SwitchesToPerspectiveAtMidpoint)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    mt.set_easing([](float t) { return t; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.camera.projection_mode = Camera::ProjectionMode::Perspective;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.6f);   // t=0.6 > 0.5 threshold
    auto cam = mt.interpolated_camera();
    EXPECT_EQ(cam.projection_mode, Camera::ProjectionMode::Perspective);
}

TEST(ModeTransitionCamera, PositionInterpolatesTo3D)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    mt.set_easing([](float t) { return t; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.camera.elevation = 30.0f;
    s3d.camera.distance  = 10.0f;
    s3d.camera.update_position_from_orbit();
    mt.begin_to_3d(s2d, s3d);
    mt.update(1.0f);
    auto cam = mt.interpolated_camera();
    // Position should reach the 3D target camera position
    EXPECT_NEAR(cam.position.x, s3d.camera.position.x, 0.5f);
    EXPECT_NEAR(cam.position.y, s3d.camera.position.y, 0.5f);
    EXPECT_NEAR(cam.position.z, s3d.camera.position.z, 0.5f);
}

TEST(ModeTransitionCamera, TargetInterpolatesTo3D)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    mt.set_easing([](float t) { return t; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.camera.target   = {1.0f, 2.0f, 3.0f};
    s3d.camera.azimuth  = 45.0f;
    s3d.camera.distance = 10.0f;
    s3d.camera.update_position_from_orbit();
    mt.begin_to_3d(s2d, s3d);
    mt.update(1.0f);
    auto cam = mt.interpolated_camera();
    // Target should reach the 3D camera target
    EXPECT_NEAR(cam.target.x, 1.0f, 0.5f);
    EXPECT_NEAR(cam.target.y, 2.0f, 0.5f);
    EXPECT_NEAR(cam.target.z, 3.0f, 0.5f);
}

// ─── Grid Planes ────────────────────────────────────────────────────────────

TEST(ModeTransitionGrid, StartsAtTargetPlanes)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.grid_planes = 7;   // All planes
    mt.begin_to_3d(s2d, s3d);
    // Grid planes stay constant — never changed during transition
    EXPECT_EQ(mt.interpolated_grid_planes(), 7);
}

TEST(ModeTransitionGrid, SwitchesToTargetPlanesLate)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    mt.set_easing([](float t) { return t; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.grid_planes = 7;   // All planes
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.8f);   // t=0.8 > 0.7 threshold
    EXPECT_EQ(mt.interpolated_grid_planes(), 7);
}

// ─── Cancel ─────────────────────────────────────────────────────────────────

TEST(ModeTransitionCancel, CancelStopsTransition)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    EXPECT_TRUE(mt.is_active());
    mt.cancel();
    EXPECT_FALSE(mt.is_active());
    EXPECT_EQ(mt.state(), ModeTransitionState::Idle);
}

TEST(ModeTransitionCancel, CanBeginAfterCancel)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.cancel();
    auto id = mt.begin_to_3d(s2d, s3d);
    EXPECT_GT(id, 0u);
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

TEST(ModeTransitionCallbacks, ProgressCallbackFires)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    float last_t = -1.0f;
    mt.set_on_progress([&](float t) { last_t = t; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.25f);
    EXPECT_GT(last_t, 0.0f);
}

TEST(ModeTransitionCallbacks, CompleteCallbackFires)
{
    ModeTransition mt;
    mt.set_duration(0.1f);
    ModeTransitionDirection completed_dir = ModeTransitionDirection::To2D;
    mt.set_on_complete([&](ModeTransitionDirection dir) { completed_dir = dir; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.2f);
    EXPECT_EQ(completed_dir, ModeTransitionDirection::To3D);
}

TEST(ModeTransitionCallbacks, CompleteCallbackFiresTo2D)
{
    ModeTransition mt;
    mt.set_duration(0.1f);
    ModeTransitionDirection completed_dir = ModeTransitionDirection::To3D;
    mt.set_on_complete([&](ModeTransitionDirection dir) { completed_dir = dir; });
    ModeTransition3DState s3d;
    ModeTransition2DState s2d;
    mt.begin_to_2d(s3d, s2d);
    mt.update(0.2f);
    EXPECT_EQ(completed_dir, ModeTransitionDirection::To2D);
}

// ─── Easing ─────────────────────────────────────────────────────────────────

TEST(ModeTransitionEasing, CustomEasingApplied)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    mt.set_easing([](float t) { return t * t; });   // Quadratic ease-in
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.5f);
    // With quadratic easing, progress at t=0.5 should be 0.25
    float p = mt.progress();
    EXPECT_NEAR(p, 0.25f, 0.01f);
}

TEST(ModeTransitionEasing, DefaultSmoothstep)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.5f);
    // Smoothstep at t=0.5 should be 0.5
    float p = mt.progress();
    EXPECT_NEAR(p, 0.5f, 0.01f);
}

// ─── Serialization ──────────────────────────────────────────────────────────

TEST(ModeTransitionSerialization, RoundTrip)
{
    ModeTransition mt;
    mt.set_duration(1.2f);
    std::string json = mt.serialize();
    EXPECT_FALSE(json.empty());

    ModeTransition mt2;
    EXPECT_TRUE(mt2.deserialize(json));
    EXPECT_NEAR(mt2.duration(), 1.2f, 0.01f);
}

TEST(ModeTransitionSerialization, DeserializeResetsToIdle)
{
    ModeTransition        mt;
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    std::string json = mt.serialize();

    ModeTransition mt2;
    mt2.deserialize(json);
    EXPECT_EQ(mt2.state(), ModeTransitionState::Idle);
    EXPECT_FALSE(mt2.is_active());
}

TEST(ModeTransitionSerialization, EmptyJsonHandled)
{
    ModeTransition mt;
    EXPECT_TRUE(mt.deserialize("{}"));
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST(ModeTransitionEdge, UpdateWhenIdle)
{
    ModeTransition mt;
    mt.update(0.1f);   // Should not crash
    EXPECT_EQ(mt.state(), ModeTransitionState::Idle);
}

TEST(ModeTransitionEdge, ZeroDuration)
{
    ModeTransition mt;
    mt.set_duration(0.0f);   // Clamped to 0.01
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.02f);
    EXPECT_FALSE(mt.is_active());
}

TEST(ModeTransitionEdge, VeryLargeDt)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(100.0f);
    EXPECT_FALSE(mt.is_active());
}

TEST(ModeTransitionEdge, MultipleSmallUpdates)
{
    ModeTransition mt;
    mt.set_duration(0.5f);
    mt.set_easing([](float t) { return t; });
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    s3d.zlim = {-1.0f, 1.0f};
    mt.begin_to_3d(s2d, s3d);
    for (int i = 0; i < 100; ++i)
    {
        mt.update(0.01f);
    }
    // 100 * 0.01 = 1.0s > 0.5s duration, so should be finished
    EXPECT_FALSE(mt.is_active());
    // After completion, z limits should be at target
    auto zlim = mt.interpolated_zlim();
    EXPECT_NEAR(zlim.min, -1.0f, 0.1f);
    EXPECT_NEAR(zlim.max, 1.0f, 0.1f);
}

TEST(ModeTransitionEdge, CanBeginAfterFinished)
{
    ModeTransition mt;
    mt.set_duration(0.1f);
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);
    mt.update(0.2f);
    // state() returns Finished and auto-resets to Idle
    EXPECT_EQ(mt.state(), ModeTransitionState::Finished);
    // Now should be Idle
    auto id = mt.begin_to_2d(s3d, s2d);
    EXPECT_GT(id, 0u);
}

// ─── Thread Safety ──────────────────────────────────────────────────────────

TEST(ModeTransitionThread, ConcurrentUpdateAndQuery)
{
    ModeTransition mt;
    mt.set_duration(1.0f);
    ModeTransition2DState s2d;
    ModeTransition3DState s3d;
    mt.begin_to_3d(s2d, s3d);

    std::atomic<bool> done{false};
    std::thread       updater(
        [&]
        {
            for (int i = 0; i < 100 && !done; ++i)
            {
                mt.update(0.01f);
            }
            done = true;
        });

    // Query from main thread while updating
    for (int i = 0; i < 100 && !done; ++i)
    {
        (void)mt.progress();
        (void)mt.interpolated_camera();
        (void)mt.element_3d_opacity();
    }

    updater.join();
    // No crash = pass
}

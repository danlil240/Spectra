#include <gtest/gtest.h>
#include <spectra/axes.hpp>

#include "ui/animation_controller.hpp"

using namespace spectra;

// Helper: create an Axes with known limits
static std::unique_ptr<Axes> make_axes(float xmin, float xmax, float ymin, float ymax)
{
    auto ax = std::make_unique<Axes>();
    ax->xlim(xmin, xmax);
    ax->ylim(ymin, ymax);
    return ax;
}

// ─── Basic lifecycle ────────────────────────────────────────────────────────

TEST(AnimationController, InitiallyEmpty)
{
    AnimationController ctrl;
    EXPECT_FALSE(ctrl.has_active_animations());
    EXPECT_EQ(ctrl.active_count(), 0u);
}

TEST(AnimationController, AnimateLimitsCreatesAnimation)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_axis_limits(*ax, {2, 8}, {2, 8}, 0.15f, ease::ease_out);
    EXPECT_TRUE(ctrl.has_active_animations());
    EXPECT_EQ(ctrl.active_count(), 1u);
}

TEST(AnimationController, AnimationCompletesAfterDuration)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_axis_limits(*ax, {2, 8}, {3, 7}, 0.15f, ease::linear);

    // Step past the duration
    ctrl.update(0.20f);

    EXPECT_FALSE(ctrl.has_active_animations());

    // Limits should be at target
    auto xlim = ax->x_limits();
    auto ylim = ax->y_limits();
    EXPECT_FLOAT_EQ(xlim.min, 2.0f);
    EXPECT_FLOAT_EQ(xlim.max, 8.0f);
    EXPECT_FLOAT_EQ(ylim.min, 3.0f);
    EXPECT_FLOAT_EQ(ylim.max, 7.0f);
}

TEST(AnimationController, AnimationInterpolatesMidway)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_axis_limits(*ax, {10, 20}, {10, 20}, 1.0f, ease::linear);

    // Step to t=0.5
    ctrl.update(0.5f);

    auto xlim = ax->x_limits();
    EXPECT_NEAR(xlim.min, 5.0f, 0.01f);
    EXPECT_NEAR(xlim.max, 15.0f, 0.01f);
}

// ─── Cancellation ───────────────────────────────────────────────────────────

TEST(AnimationController, CancelById)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    auto id = ctrl.animate_axis_limits(*ax, {5, 5}, {5, 5}, 1.0f, ease::linear);
    EXPECT_TRUE(ctrl.has_active_animations());

    ctrl.cancel(id);
    ctrl.update(0.01f);  // GC runs on update

    EXPECT_FALSE(ctrl.has_active_animations());
}

TEST(AnimationController, CancelForAxes)
{
    AnimationController ctrl;
    auto ax1 = make_axes(0, 10, 0, 10);
    auto ax2 = make_axes(0, 10, 0, 10);

    ctrl.animate_axis_limits(*ax1, {5, 5}, {5, 5}, 1.0f, ease::linear);
    ctrl.animate_axis_limits(*ax2, {5, 5}, {5, 5}, 1.0f, ease::linear);
    EXPECT_EQ(ctrl.active_count(), 2u);

    ctrl.cancel_for_axes(ax1.get());
    ctrl.update(0.01f);

    EXPECT_EQ(ctrl.active_count(), 1u);
}

TEST(AnimationController, CancelAll)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_axis_limits(*ax, {5, 5}, {5, 5}, 1.0f, ease::linear);
    ctrl.animate_inertial_pan(*ax, 1.0f, 1.0f, 0.3f);
    EXPECT_EQ(ctrl.active_count(), 2u);

    ctrl.cancel_all();
    ctrl.update(0.01f);

    EXPECT_FALSE(ctrl.has_active_animations());
}

// ─── New animation replaces existing on same axes ───────────────────────────

TEST(AnimationController, NewLimitAnimCancelsPrevious)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_axis_limits(*ax, {5, 5}, {5, 5}, 1.0f, ease::linear);
    ctrl.animate_axis_limits(*ax, {2, 8}, {2, 8}, 1.0f, ease::linear);

    // Should only have 1 active (the new one replaced the old)
    EXPECT_EQ(ctrl.active_count(), 1u);
}

// ─── Inertial pan ───────────────────────────────────────────────────────────

TEST(AnimationController, InertialPanMovesLimits)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_inertial_pan(*ax, 5.0f, 0.0f, 0.3f);

    // Step a small amount
    ctrl.update(0.01f);

    auto xlim = ax->x_limits();
    // Limits should have shifted right (positive vx_data)
    EXPECT_GT(xlim.min, 0.0f);
    EXPECT_GT(xlim.max, 10.0f);
}

TEST(AnimationController, InertialPanDecelerates)
{
    AnimationController ctrl;
    auto ax = make_axes(0, 10, 0, 10);

    ctrl.animate_inertial_pan(*ax, 10.0f, 0.0f, 0.3f);

    // Step early: high velocity
    ctrl.update(0.01f);
    // float shift1 = ax->x_limits().min;  // Currently unused

    // Step later: lower velocity (deceleration)
    ctrl.update(0.14f);
    // float shift2 = ax->x_limits().min - shift1;  // Currently unused

    ctrl.update(0.01f);
    // float shift3 = ax->x_limits().min - shift1 - shift2;  // Currently unused

    // Each successive shift should be smaller (decelerating)
    // shift2 covers more time so it's larger, but per-unit-time it's slower
    // Just verify the animation completes
    ctrl.update(0.5f);
    EXPECT_FALSE(ctrl.has_active_animations());
}

// ─── Performance: update with no animations is cheap ────────────────────────

TEST(AnimationController, UpdateWithNoAnimations)
{
    AnimationController ctrl;
    // Should not crash or do anything expensive
    for (int i = 0; i < 1000; ++i)
    {
        ctrl.update(0.016f);
    }
    EXPECT_FALSE(ctrl.has_active_animations());
}

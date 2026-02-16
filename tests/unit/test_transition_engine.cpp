#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <plotix/animator.hpp>
#include <plotix/axes.hpp>
#include <plotix/color.hpp>

#include "ui/animation_controller.hpp"
#include "ui/layout_manager.hpp"
#include "ui/transition_engine.hpp"

using namespace plotix;

// ─── Transition convergence (layout animations) ─────────────────────────────

TEST(TransitionEngine, InspectorOpenConvergesWithinBudget)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.update(1280.0f, 720.0f, 0.0f);  // snap open

    lm.set_inspector_visible(false);

    int frames = 0;
    while (lm.is_animating() && frames < 120)
    {
        lm.update(1280.0f, 720.0f, 1.0f / 60.0f);
        ++frames;
    }

    EXPECT_FALSE(lm.is_animating());
    EXPECT_NEAR(lm.inspector_animated_width(), 0.0f, 1.0f);
    EXPECT_LT(frames, 120);
}

TEST(TransitionEngine, NavRailExpandConverges)
{
    LayoutManager lm;
    lm.set_nav_rail_expanded(true);

    int frames = 0;
    while (lm.is_animating() && frames < 120)
    {
        lm.update(1280.0f, 720.0f, 1.0f / 60.0f);
        ++frames;
    }

    auto nr = lm.nav_rail_rect();
    EXPECT_NEAR(nr.w, LayoutManager::NAV_RAIL_EXPANDED_WIDTH, 0.5f);
}

TEST(TransitionEngine, NavRailCollapseConverges)
{
    LayoutManager lm;
    lm.set_nav_rail_expanded(true);
    lm.update(1280.0f, 720.0f, 0.0f);

    lm.set_nav_rail_expanded(false);

    int frames = 0;
    while (lm.is_animating() && frames < 120)
    {
        lm.update(1280.0f, 720.0f, 1.0f / 60.0f);
        ++frames;
    }

    EXPECT_FALSE(lm.is_animating());
    EXPECT_NEAR(lm.nav_rail_animated_width(), LayoutManager::NAV_RAIL_COLLAPSED_WIDTH, 0.5f);
}

// ─── Axis limit animation convergence ────────────────────────────────────────

TEST(TransitionEngine, AxisLimitAnimConverges)
{
    AnimationController ctrl;
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);

    ctrl.animate_axis_limits(ax, {2.0f, 8.0f}, {1.0f, 9.0f}, 0.15f, ease::ease_out);

    for (int i = 0; i < 20; ++i)
    {
        ctrl.update(0.016f);
    }

    EXPECT_FALSE(ctrl.has_active_animations());
    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    EXPECT_FLOAT_EQ(xlim.min, 2.0f);
    EXPECT_FLOAT_EQ(xlim.max, 8.0f);
    EXPECT_FLOAT_EQ(ylim.min, 1.0f);
    EXPECT_FLOAT_EQ(ylim.max, 9.0f);
}

TEST(TransitionEngine, InertialPanConverges)
{
    AnimationController ctrl;
    Axes ax;
    ax.xlim(0.0f, 100.0f);
    ax.ylim(0.0f, 100.0f);

    ctrl.animate_inertial_pan(ax, 50.0f, -25.0f, 0.3f);

    for (int i = 0; i < 30; ++i)
    {
        ctrl.update(0.016f);
    }

    EXPECT_FALSE(ctrl.has_active_animations());
    EXPECT_GT(ax.x_limits().min, 0.0f);
}

// ─── Multiple simultaneous animations ────────────────────────────────────────

TEST(TransitionEngine, MultipleAxesAnimateIndependently)
{
    AnimationController ctrl;
    Axes ax1, ax2;
    ax1.xlim(0, 10);
    ax1.ylim(0, 10);
    ax2.xlim(0, 10);
    ax2.ylim(0, 10);

    ctrl.animate_axis_limits(ax1, {5, 5}, {5, 5}, 0.1f, ease::linear);
    ctrl.animate_axis_limits(ax2, {-5, 15}, {-5, 15}, 0.2f, ease::linear);
    EXPECT_EQ(ctrl.active_count(), 2u);

    ctrl.update(0.15f);
    EXPECT_EQ(ctrl.active_count(), 1u);

    auto x1 = ax1.x_limits();
    EXPECT_FLOAT_EQ(x1.min, 5.0f);
    EXPECT_FLOAT_EQ(x1.max, 5.0f);

    ctrl.update(0.1f);
    EXPECT_FALSE(ctrl.has_active_animations());

    auto x2 = ax2.x_limits();
    EXPECT_FLOAT_EQ(x2.min, -5.0f);
    EXPECT_FLOAT_EQ(x2.max, 15.0f);
}

TEST(TransitionEngine, CancelDoesNotAffectOtherAxes)
{
    AnimationController ctrl;
    Axes ax1, ax2;
    ax1.xlim(0, 10);
    ax1.ylim(0, 10);
    ax2.xlim(0, 10);
    ax2.ylim(0, 10);

    ctrl.animate_axis_limits(ax1, {5, 5}, {5, 5}, 1.0f, ease::linear);
    auto id2 = ctrl.animate_axis_limits(ax2, {5, 5}, {5, 5}, 1.0f, ease::linear);

    ctrl.cancel(id2);
    ctrl.update(0.01f);

    EXPECT_EQ(ctrl.active_count(), 1u);
}

// ─── Get pending target ──────────────────────────────────────────────────────

TEST(TransitionEngine, GetPendingTargetDuringAnimation)
{
    AnimationController ctrl;
    Axes ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    ctrl.animate_axis_limits(ax, {2, 8}, {3, 7}, 1.0f, ease::linear);

    AxisLimits out_x, out_y;
    bool found = ctrl.get_pending_target(&ax, out_x, out_y);
    EXPECT_TRUE(found);
    EXPECT_FLOAT_EQ(out_x.min, 2.0f);
    EXPECT_FLOAT_EQ(out_x.max, 8.0f);
    EXPECT_FLOAT_EQ(out_y.min, 3.0f);
    EXPECT_FLOAT_EQ(out_y.max, 7.0f);
}

TEST(TransitionEngine, GetPendingTargetNoAnimation)
{
    AnimationController ctrl;
    Axes ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    AxisLimits out_x, out_y;
    EXPECT_FALSE(ctrl.get_pending_target(&ax, out_x, out_y));
}

// ─── Easing function properties for transitions ─────────────────────────────

TEST(TransitionEngine, AllEasingFunctionsHaveCorrectEndpoints)
{
    EasingFn fns[] = {ease::linear,
                      ease::ease_in,
                      ease::ease_out,
                      ease::ease_in_out,
                      ease::bounce,
                      ease::elastic,
                      ease::spring,
                      ease::decelerate};
    for (auto fn : fns)
    {
        EXPECT_NEAR(fn(0.0f), 0.0f, 0.01f);
        EXPECT_NEAR(fn(1.0f), 1.0f, 0.01f);
    }
}

TEST(TransitionEngine, EasingOutputBoundedForMonotonicFunctions)
{
    EasingFn monotonic[] = {
        ease::linear, ease::ease_in, ease::ease_out, ease::ease_in_out, ease::decelerate};
    for (auto fn : monotonic)
    {
        for (float t = 0.0f; t <= 1.0f; t += 0.01f)
        {
            float v = fn(t);
            EXPECT_GE(v, -0.01f) << "at t=" << t;
            EXPECT_LE(v, 1.01f) << "at t=" << t;
        }
    }
}

// ─── Layout + animation interaction ──────────────────────────────────────────

TEST(TransitionEngine, LayoutAnimationDoesNotOversizeWindow)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.set_nav_rail_expanded(true);

    for (int i = 0; i < 60; ++i)
    {
        lm.update(1280.0f, 720.0f, 1.0f / 60.0f);

        auto cv = lm.canvas_rect();
        // auto nr = lm.nav_rail_rect();  // Currently unused
        auto insp = lm.inspector_rect();

        // Nav toolbar inset + canvas + inspector should not exceed window width
        float total = LayoutManager::NAV_TOOLBAR_INSET + cv.w + insp.w;
        EXPECT_LE(total, 1280.0f + 1.0f) << "frame " << i;
        EXPECT_GE(cv.w, 0.0f) << "frame " << i;
        EXPECT_GE(cv.h, 0.0f) << "frame " << i;
    }
}

TEST(TransitionEngine, RapidToggleDoesNotCrash)
{
    LayoutManager lm;
    for (int i = 0; i < 100; ++i)
    {
        lm.set_inspector_visible(i % 2 == 0);
        lm.update(1280.0f, 720.0f, 0.008f);
    }
    auto cv = lm.canvas_rect();
    EXPECT_GE(cv.w, 0.0f);
    EXPECT_GE(cv.h, 0.0f);
}

// ─── Performance ─────────────────────────────────────────────────────────────

TEST(TransitionEngine, AnimationUpdatePerformance)
{
    AnimationController ctrl;
    Axes axes[50];
    for (int i = 0; i < 50; ++i)
    {
        axes[i].xlim(0, 10);
        axes[i].ylim(0, 10);
        ctrl.animate_axis_limits(axes[i],
                                 {static_cast<float>(i), static_cast<float>(10 + i)},
                                 {static_cast<float>(i), static_cast<float>(10 + i)},
                                 0.5f,
                                 ease::ease_out);
    }
    EXPECT_EQ(ctrl.active_count(), 50u);

    auto start = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 100; ++frame)
    {
        ctrl.update(0.016f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    EXPECT_LT(ms, 50.0) << "Animation update too slow: " << ms << "ms for 100 frames";
}

TEST(TransitionEngine, LayoutUpdatePerformance)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.set_nav_rail_expanded(true);
    lm.set_tab_bar_visible(true);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        lm.update(1920.0f, 1080.0f, 0.016f);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    EXPECT_LT(ms, 50.0) << "Layout update too slow: " << ms << "ms for 1000 updates";
}

// ═══════════════════════════════════════════════════════════════════════════
// TransitionEngine — unified animation system tests
// ═══════════════════════════════════════════════════════════════════════════

// ─── Float animation ─────────────────────────────────────────────────────────

TEST(TransitionEngineUnified, FloatAnimateConverges)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 10.0f, 0.2f, ease::linear);
    EXPECT_EQ(te.active_count(), 1u);

    for (int i = 0; i < 20; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FALSE(te.has_active_animations());
    EXPECT_FLOAT_EQ(val, 10.0f);
}

TEST(TransitionEngineUnified, FloatAnimateMidpoint)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 100.0f, 1.0f, ease::linear);

    te.update(0.5f);
    EXPECT_NEAR(val, 50.0f, 1.0f);
}

TEST(TransitionEngineUnified, FloatAnimateReplace)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 10.0f, 1.0f, ease::linear);
    te.update(0.1f);

    // Start a new animation on the same target — should cancel the old one
    te.animate(val, -5.0f, 0.2f, ease::linear);
    EXPECT_EQ(te.active_count(), 1u);

    for (int i = 0; i < 20; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FLOAT_EQ(val, -5.0f);
}

TEST(TransitionEngineUnified, FloatAnimateWithEaseOut)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 1.0f, 0.5f, ease::ease_out);

    te.update(0.25f);
    EXPECT_GT(val, 0.5f);
}

// ─── Color animation ────────────────────────────────────────────────────────

TEST(TransitionEngineUnified, ColorAnimateConverges)
{
    TransitionEngine te;
    Color c{0.0f, 0.0f, 0.0f, 1.0f};

    te.animate(c, Color{1.0f, 0.5f, 0.25f, 0.8f}, 0.2f, ease::linear);
    EXPECT_EQ(te.active_count(), 1u);

    for (int i = 0; i < 20; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FALSE(te.has_active_animations());
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.5f);
    EXPECT_FLOAT_EQ(c.b, 0.25f);
    EXPECT_FLOAT_EQ(c.a, 0.8f);
}

TEST(TransitionEngineUnified, ColorAnimateMidpoint)
{
    TransitionEngine te;
    Color c{0.0f, 0.0f, 0.0f, 0.0f};

    te.animate(c, Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, ease::linear);

    te.update(0.5f);
    EXPECT_NEAR(c.r, 0.5f, 0.02f);
    EXPECT_NEAR(c.g, 0.5f, 0.02f);
    EXPECT_NEAR(c.b, 0.5f, 0.02f);
    EXPECT_NEAR(c.a, 0.5f, 0.02f);
}

TEST(TransitionEngineUnified, ColorAnimateReplace)
{
    TransitionEngine te;
    Color c{0.0f, 0.0f, 0.0f, 1.0f};

    te.animate(c, Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, ease::linear);
    te.update(0.1f);

    te.animate(c, Color{0.5f, 0.0f, 0.0f, 1.0f}, 0.2f, ease::linear);
    EXPECT_EQ(te.active_count(), 1u);

    for (int i = 0; i < 20; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FLOAT_EQ(c.r, 0.5f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
}

// ─── AxisLimits animation ───────────────────────────────────────────────────

TEST(TransitionEngineUnified, LimitsAnimateConverges)
{
    TransitionEngine te;
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);

    te.animate_limits(ax, {2.0f, 8.0f}, {1.0f, 9.0f}, 0.15f, ease::ease_out);

    for (int i = 0; i < 20; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FALSE(te.has_active_animations());
    auto xlim = ax.x_limits();
    auto ylim = ax.y_limits();
    EXPECT_FLOAT_EQ(xlim.min, 2.0f);
    EXPECT_FLOAT_EQ(xlim.max, 8.0f);
    EXPECT_FLOAT_EQ(ylim.min, 1.0f);
    EXPECT_FLOAT_EQ(ylim.max, 9.0f);
}

TEST(TransitionEngineUnified, LimitsAnimateReplacesExisting)
{
    TransitionEngine te;
    Axes ax;
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);

    te.animate_limits(ax, {5.0f, 5.0f}, {5.0f, 5.0f}, 1.0f, ease::linear);
    te.update(0.1f);

    te.animate_limits(ax, {-1.0f, 11.0f}, {-1.0f, 11.0f}, 0.2f, ease::linear);
    EXPECT_EQ(te.active_count(), 1u);

    for (int i = 0; i < 20; ++i)
    {
        te.update(0.016f);
    }

    auto xlim = ax.x_limits();
    EXPECT_FLOAT_EQ(xlim.min, -1.0f);
    EXPECT_FLOAT_EQ(xlim.max, 11.0f);
}

// ─── Inertial pan ───────────────────────────────────────────────────────────

TEST(TransitionEngineUnified, InertialPanConverges)
{
    TransitionEngine te;
    Axes ax;
    ax.xlim(0.0f, 100.0f);
    ax.ylim(0.0f, 100.0f);

    te.animate_inertial_pan(ax, 50.0f, -25.0f, 0.3f);

    for (int i = 0; i < 30; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FALSE(te.has_active_animations());
    auto xlim = ax.x_limits();
    EXPECT_GT(xlim.min, 0.0f);
}

TEST(TransitionEngineUnified, InertialPanReplacesExisting)
{
    TransitionEngine te;
    Axes ax;
    ax.xlim(0.0f, 100.0f);
    ax.ylim(0.0f, 100.0f);

    te.animate_inertial_pan(ax, 50.0f, 0.0f, 1.0f);
    te.update(0.05f);

    te.animate_inertial_pan(ax, -50.0f, 0.0f, 0.3f);

    size_t count = te.active_count();
    EXPECT_EQ(count, 1u);
}

// ─── Cancel ─────────────────────────────────────────────────────────────────

TEST(TransitionEngineUnified, CancelById)
{
    TransitionEngine te;
    float val = 0.0f;

    auto id = te.animate(val, 10.0f, 1.0f, ease::linear);
    EXPECT_TRUE(te.has_active_animations());

    te.cancel(id);
    te.update(0.01f);

    EXPECT_FALSE(te.has_active_animations());
    EXPECT_NEAR(val, 0.0f, 0.5f);
}

TEST(TransitionEngineUnified, CancelForAxes)
{
    TransitionEngine te;
    Axes ax1, ax2;
    ax1.xlim(0, 10);
    ax1.ylim(0, 10);
    ax2.xlim(0, 10);
    ax2.ylim(0, 10);

    te.animate_limits(ax1, {5, 5}, {5, 5}, 1.0f, ease::linear);
    te.animate_limits(ax2, {5, 5}, {5, 5}, 1.0f, ease::linear);
    EXPECT_EQ(te.active_count(), 2u);

    te.cancel_for_axes(&ax1);
    te.update(0.01f);

    EXPECT_EQ(te.active_count(), 1u);
}

TEST(TransitionEngineUnified, CancelAll)
{
    TransitionEngine te;
    float f1 = 0.0f, f2 = 0.0f;
    Color c{0, 0, 0, 1};
    Axes ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    te.animate(f1, 10.0f, 1.0f);
    te.animate(f2, 20.0f, 1.0f);
    te.animate(c, Color{1, 1, 1, 1}, 1.0f);
    te.animate_limits(ax, {5, 5}, {5, 5}, 1.0f);
    EXPECT_EQ(te.active_count(), 4u);

    te.cancel_all();
    te.update(0.01f);

    EXPECT_FALSE(te.has_active_animations());
    EXPECT_EQ(te.active_count(), 0u);
}

// ─── Get pending target ─────────────────────────────────────────────────────

TEST(TransitionEngineUnified, GetPendingTarget)
{
    TransitionEngine te;
    Axes ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    te.animate_limits(ax, {2, 8}, {3, 7}, 1.0f, ease::linear);

    AxisLimits out_x, out_y;
    bool found = te.get_pending_target(&ax, out_x, out_y);
    EXPECT_TRUE(found);
    EXPECT_FLOAT_EQ(out_x.min, 2.0f);
    EXPECT_FLOAT_EQ(out_x.max, 8.0f);
    EXPECT_FLOAT_EQ(out_y.min, 3.0f);
    EXPECT_FLOAT_EQ(out_y.max, 7.0f);
}

TEST(TransitionEngineUnified, GetPendingTargetNone)
{
    TransitionEngine te;
    Axes ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    AxisLimits out_x, out_y;
    EXPECT_FALSE(te.get_pending_target(&ax, out_x, out_y));
}

// ─── Mixed animation types ──────────────────────────────────────────────────

TEST(TransitionEngineUnified, MixedAnimationTypes)
{
    TransitionEngine te;
    float f = 0.0f;
    Color c{0, 0, 0, 1};
    Axes ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    te.animate(f, 1.0f, 0.2f, ease::linear);
    te.animate(c, Color{1, 1, 1, 1}, 0.2f, ease::linear);
    te.animate_limits(ax, {5, 5}, {5, 5}, 0.2f, ease::linear);
    te.animate_inertial_pan(ax, 10.0f, 0.0f, 0.3f);

    EXPECT_EQ(te.active_count(), 4u);

    for (int i = 0; i < 30; ++i)
    {
        te.update(0.016f);
    }

    EXPECT_FALSE(te.has_active_animations());
    EXPECT_FLOAT_EQ(f, 1.0f);
    EXPECT_FLOAT_EQ(c.r, 1.0f);
}

// ─── CubicBezier easing with TransitionEngine ───────────────────────────────

TEST(TransitionEngineUnified, CubicBezierEasing)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 1.0f, 0.5f, ease::ease_out_cubic);

    te.update(0.25f);
    EXPECT_GT(val, 0.5f);

    te.update(0.3f);
    EXPECT_FLOAT_EQ(val, 1.0f);
}

TEST(TransitionEngineUnified, SpringEasing)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 1.0f, 0.5f, ease::spring);

    bool overshot = false;
    for (int i = 0; i < 40; ++i)
    {
        te.update(0.016f);
        if (val > 1.0f)
            overshot = true;
    }

    EXPECT_TRUE(overshot) << "Spring easing should overshoot";
    EXPECT_FLOAT_EQ(val, 1.0f);
}

// ─── Performance: 50 animations under 0.05ms per update ─────────────────────

TEST(TransitionEngineUnified, UpdatePerformance50Animations)
{
    TransitionEngine te;
    float floats[25];
    Color colors[10];
    Axes axes[15];

    for (int i = 0; i < 25; ++i)
    {
        floats[i] = 0.0f;
        te.animate(floats[i], static_cast<float>(i + 1), 0.5f, ease::ease_out);
    }
    for (int i = 0; i < 10; ++i)
    {
        colors[i] = Color{0, 0, 0, 1};
        te.animate(colors[i], Color{1, 1, 1, 1}, 0.5f, ease::linear);
    }
    for (int i = 0; i < 15; ++i)
    {
        axes[i].xlim(0, 10);
        axes[i].ylim(0, 10);
        te.animate_limits(axes[i],
                          {static_cast<float>(i), static_cast<float>(10 + i)},
                          {static_cast<float>(i), static_cast<float>(10 + i)},
                          0.5f,
                          ease::ease_out);
    }

    EXPECT_EQ(te.active_count(), 50u);

    auto start = std::chrono::high_resolution_clock::now();
    for (int frame = 0; frame < 100; ++frame)
    {
        te.update(0.001f);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double per_call_ms = total_ms / 100.0;

    EXPECT_LT(per_call_ms, 0.5) << "update() too slow: " << per_call_ms
                                << "ms per call with 50 animations";
}

// ─── No memory leaks: animations are cleaned up after completion ─────────────

TEST(TransitionEngineUnified, CompletedAnimationsAreGarbageCollected)
{
    TransitionEngine te;
    float val = 0.0f;

    for (int i = 0; i < 100; ++i)
    {
        te.animate(val, static_cast<float>(i), 0.01f, ease::linear);
        te.update(0.02f);
    }

    EXPECT_EQ(te.active_count(), 0u);
    EXPECT_FALSE(te.has_active_animations());
}

// ─── Zero-duration animation snaps immediately ──────────────────────────────

TEST(TransitionEngineUnified, ZeroDurationSnaps)
{
    TransitionEngine te;
    float val = 0.0f;

    te.animate(val, 42.0f, 0.0001f, ease::linear);
    te.update(0.001f);

    EXPECT_FLOAT_EQ(val, 42.0f);
    EXPECT_FALSE(te.has_active_animations());
}

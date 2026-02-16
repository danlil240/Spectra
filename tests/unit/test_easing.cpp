#include <cmath>
#include <gtest/gtest.h>
#include <plotix/animator.hpp>

using namespace plotix;

// All easing functions must satisfy: f(0) == 0, f(1) == 1

TEST(Easing, LinearEndpoints)
{
    EXPECT_FLOAT_EQ(ease::linear(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::linear(1.0f), 1.0f);
}

TEST(Easing, LinearMidpoint)
{
    EXPECT_FLOAT_EQ(ease::linear(0.5f), 0.5f);
}

TEST(Easing, EaseInEndpoints)
{
    EXPECT_FLOAT_EQ(ease::ease_in(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::ease_in(1.0f), 1.0f);
}

TEST(Easing, EaseInSlowerStart)
{
    // Cubic ease-in: at t=0.5, value should be 0.125 (0.5^3)
    EXPECT_FLOAT_EQ(ease::ease_in(0.5f), 0.125f);
}

TEST(Easing, EaseOutEndpoints)
{
    EXPECT_FLOAT_EQ(ease::ease_out(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::ease_out(1.0f), 1.0f);
}

TEST(Easing, EaseOutFasterStart)
{
    // Cubic ease-out at t=0.5: 1 - (0.5)^3 = 0.875
    EXPECT_FLOAT_EQ(ease::ease_out(0.5f), 0.875f);
}

TEST(Easing, EaseInOutEndpoints)
{
    EXPECT_FLOAT_EQ(ease::ease_in_out(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::ease_in_out(1.0f), 1.0f);
}

TEST(Easing, EaseInOutMidpoint)
{
    EXPECT_FLOAT_EQ(ease::ease_in_out(0.5f), 0.5f);
}

TEST(Easing, EaseInOutSymmetry)
{
    // ease_in_out should be symmetric: f(t) + f(1-t) ≈ 1
    for (float t = 0.0f; t <= 1.0f; t += 0.1f)
    {
        float sum = ease::ease_in_out(t) + ease::ease_in_out(1.0f - t);
        EXPECT_NEAR(sum, 1.0f, 1e-5f) << "at t=" << t;
    }
}

TEST(Easing, BounceEndpoints)
{
    EXPECT_NEAR(ease::bounce(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(ease::bounce(1.0f), 1.0f, 1e-6f);
}

TEST(Easing, BounceMonotonicallyBounded)
{
    // Bounce should stay in [0, 1] range
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        float v = ease::bounce(t);
        EXPECT_GE(v, -0.01f) << "at t=" << t;
        EXPECT_LE(v, 1.01f) << "at t=" << t;
    }
}

TEST(Easing, ElasticEndpoints)
{
    EXPECT_FLOAT_EQ(ease::elastic(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::elastic(1.0f), 1.0f);
}

TEST(Easing, ElasticOscillates)
{
    // Elastic ease-out should overshoot 1.0 at some point
    bool overshoots = false;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        if (ease::elastic(t) > 1.0f)
        {
            overshoots = true;
            break;
        }
    }
    EXPECT_TRUE(overshoots) << "Elastic easing should overshoot 1.0";
}

// ─── Spring easing ──────────────────────────────────────────────────────────

TEST(Easing, SpringEndpoints)
{
    EXPECT_FLOAT_EQ(ease::spring(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::spring(1.0f), 1.0f);
}

TEST(Easing, SpringOvershoots)
{
    // Damped spring should overshoot 1.0 at some point
    bool overshoots = false;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f)
    {
        if (ease::spring(t) > 1.0f)
        {
            overshoots = true;
            break;
        }
    }
    EXPECT_TRUE(overshoots) << "Spring easing should overshoot 1.0";
}

TEST(Easing, SpringSettlesNearOne)
{
    // At t=0.9, spring should be very close to 1.0
    EXPECT_NEAR(ease::spring(0.9f), 1.0f, 0.05f);
}

// ─── Decelerate easing ──────────────────────────────────────────────────────

TEST(Easing, DecelerateEndpoints)
{
    EXPECT_FLOAT_EQ(ease::decelerate(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::decelerate(1.0f), 1.0f);
}

TEST(Easing, DecelerateFasterStart)
{
    // Quadratic deceleration: at t=0.5, value = 1 - 0.25 = 0.75
    EXPECT_FLOAT_EQ(ease::decelerate(0.5f), 0.75f);
}

TEST(Easing, DecelerateMonotonic)
{
    float prev = 0.0f;
    for (float t = 0.01f; t <= 1.0f; t += 0.01f)
    {
        float v = ease::decelerate(t);
        EXPECT_GE(v, prev) << "at t=" << t;
        prev = v;
    }
}

// ─── CubicBezier easing ────────────────────────────────────────────────────

TEST(Easing, CubicBezierEndpoints)
{
    ease::CubicBezier cb{0.25f, 0.1f, 0.25f, 1.0f};
    EXPECT_NEAR(cb(0.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(cb(1.0f), 1.0f, 1e-5f);
}

TEST(Easing, CubicBezierLinearApprox)
{
    // A linear bezier (0,0,1,1) should approximate linear
    ease::CubicBezier linear_cb{0.0f, 0.0f, 1.0f, 1.0f};
    for (float t = 0.0f; t <= 1.0f; t += 0.1f)
    {
        EXPECT_NEAR(linear_cb(t), t, 0.02f) << "at t=" << t;
    }
}

TEST(Easing, CubicBezierEaseOutPreset)
{
    // ease_out_cubic preset should be fast start, slow end
    float mid = ease::ease_out_cubic(0.5f);
    EXPECT_GT(mid, 0.5f) << "ease-out should be past midpoint at t=0.5";
}

TEST(Easing, CubicBezierMonotonic)
{
    ease::CubicBezier cb{0.25f, 0.1f, 0.25f, 1.0f};
    float prev = 0.0f;
    for (float t = 0.01f; t <= 1.0f; t += 0.01f)
    {
        float v = cb(t);
        EXPECT_GE(v, prev - 0.001f) << "at t=" << t;
        prev = v;
    }
}

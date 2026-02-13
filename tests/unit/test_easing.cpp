#include <gtest/gtest.h>

#include <plotix/animator.hpp>

#include <cmath>

using namespace plotix;

// All easing functions must satisfy: f(0) == 0, f(1) == 1

TEST(Easing, LinearEndpoints) {
    EXPECT_FLOAT_EQ(ease::linear(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::linear(1.0f), 1.0f);
}

TEST(Easing, LinearMidpoint) {
    EXPECT_FLOAT_EQ(ease::linear(0.5f), 0.5f);
}

TEST(Easing, EaseInEndpoints) {
    EXPECT_FLOAT_EQ(ease::ease_in(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::ease_in(1.0f), 1.0f);
}

TEST(Easing, EaseInSlowerStart) {
    // Cubic ease-in: at t=0.5, value should be 0.125 (0.5^3)
    EXPECT_FLOAT_EQ(ease::ease_in(0.5f), 0.125f);
}

TEST(Easing, EaseOutEndpoints) {
    EXPECT_FLOAT_EQ(ease::ease_out(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::ease_out(1.0f), 1.0f);
}

TEST(Easing, EaseOutFasterStart) {
    // Cubic ease-out at t=0.5: 1 - (0.5)^3 = 0.875
    EXPECT_FLOAT_EQ(ease::ease_out(0.5f), 0.875f);
}

TEST(Easing, EaseInOutEndpoints) {
    EXPECT_FLOAT_EQ(ease::ease_in_out(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::ease_in_out(1.0f), 1.0f);
}

TEST(Easing, EaseInOutMidpoint) {
    EXPECT_FLOAT_EQ(ease::ease_in_out(0.5f), 0.5f);
}

TEST(Easing, EaseInOutSymmetry) {
    // ease_in_out should be symmetric: f(t) + f(1-t) ≈ 1
    for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
        float sum = ease::ease_in_out(t) + ease::ease_in_out(1.0f - t);
        EXPECT_NEAR(sum, 1.0f, 1e-5f) << "at t=" << t;
    }
}

TEST(Easing, BounceEndpoints) {
    EXPECT_NEAR(ease::bounce(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(ease::bounce(1.0f), 1.0f, 1e-6f);
}

TEST(Easing, BounceMonotonicallyBounded) {
    // Bounce should stay in [0, 1] range
    for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
        float v = ease::bounce(t);
        EXPECT_GE(v, -0.01f) << "at t=" << t;
        EXPECT_LE(v, 1.01f)  << "at t=" << t;
    }
}

TEST(Easing, ElasticEndpoints) {
    EXPECT_FLOAT_EQ(ease::elastic(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ease::elastic(1.0f), 1.0f);
}

TEST(Easing, ElasticOscillates) {
    // Elastic ease-out should overshoot 1.0 at some point
    bool overshoots = false;
    for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
        if (ease::elastic(t) > 1.0f) {
            overshoots = true;
            break;
        }
    }
    EXPECT_TRUE(overshoots) << "Elastic easing should overshoot 1.0";
}

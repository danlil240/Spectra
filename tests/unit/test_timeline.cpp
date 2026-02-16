#include <cmath>
#include <gtest/gtest.h>
#include <spectra/timeline.hpp>

using namespace spectra;

TEST(Timeline, EmptyTimeline)
{
    Timeline tl;
    EXPECT_TRUE(tl.empty());
    EXPECT_FLOAT_EQ(tl.duration(), 0.0f);
}

TEST(Timeline, SingleFloatKeyframe)
{
    Timeline tl;
    tl.add(0.0f, 42.0f);

    EXPECT_FALSE(tl.empty());
    EXPECT_FLOAT_EQ(tl.duration(), 0.0f);

    auto val = tl.evaluate(0.0f);
    ASSERT_TRUE(std::holds_alternative<float>(val));
    EXPECT_FLOAT_EQ(std::get<float>(val), 42.0f);
}

TEST(Timeline, TwoFloatKeyframesLinear)
{
    Timeline tl;
    tl.add(0.0f, 0.0f, ease::linear);
    tl.add(1.0f, 10.0f, ease::linear);

    EXPECT_FLOAT_EQ(tl.duration(), 1.0f);

    // At t=0
    auto v0 = tl.evaluate(0.0f);
    ASSERT_TRUE(std::holds_alternative<float>(v0));
    EXPECT_FLOAT_EQ(std::get<float>(v0), 0.0f);

    // At t=0.5 → linear interpolation → 5.0
    auto v05 = tl.evaluate(0.5f);
    ASSERT_TRUE(std::holds_alternative<float>(v05));
    EXPECT_FLOAT_EQ(std::get<float>(v05), 5.0f);

    // At t=1.0
    auto v1 = tl.evaluate(1.0f);
    ASSERT_TRUE(std::holds_alternative<float>(v1));
    EXPECT_FLOAT_EQ(std::get<float>(v1), 10.0f);
}

TEST(Timeline, FloatKeyframeBeforeFirst)
{
    Timeline tl;
    tl.add(1.0f, 100.0f);
    tl.add(2.0f, 200.0f);

    // Before first keyframe → should return first keyframe value
    auto v = tl.evaluate(0.0f);
    ASSERT_TRUE(std::holds_alternative<float>(v));
    EXPECT_FLOAT_EQ(std::get<float>(v), 100.0f);
}

TEST(Timeline, FloatKeyframeAfterLast)
{
    Timeline tl;
    tl.add(0.0f, 10.0f);
    tl.add(1.0f, 20.0f);

    // After last keyframe → should return last keyframe value
    auto v = tl.evaluate(5.0f);
    ASSERT_TRUE(std::holds_alternative<float>(v));
    EXPECT_FLOAT_EQ(std::get<float>(v), 20.0f);
}

TEST(Timeline, ThreeFloatKeyframes)
{
    Timeline tl;
    tl.add(0.0f, 0.0f, ease::linear);
    tl.add(1.0f, 10.0f, ease::linear);
    tl.add(2.0f, 0.0f, ease::linear);

    EXPECT_FLOAT_EQ(tl.duration(), 2.0f);

    auto v05 = tl.evaluate(0.5f);
    ASSERT_TRUE(std::holds_alternative<float>(v05));
    EXPECT_FLOAT_EQ(std::get<float>(v05), 5.0f);

    auto v15 = tl.evaluate(1.5f);
    ASSERT_TRUE(std::holds_alternative<float>(v15));
    EXPECT_FLOAT_EQ(std::get<float>(v15), 5.0f);
}

TEST(Timeline, ColorKeyframesLinear)
{
    Timeline tl;
    tl.add(0.0f, Color{1.0f, 0.0f, 0.0f, 1.0f}, ease::linear);
    tl.add(1.0f, Color{0.0f, 0.0f, 1.0f, 1.0f}, ease::linear);

    // At t=0.5 → midpoint between red and blue
    auto v = tl.evaluate(0.5f);
    ASSERT_TRUE(std::holds_alternative<Color>(v));
    auto c = std::get<Color>(v);
    EXPECT_NEAR(c.r, 0.5f, 1e-5f);
    EXPECT_NEAR(c.g, 0.0f, 1e-5f);
    EXPECT_NEAR(c.b, 0.5f, 1e-5f);
    EXPECT_NEAR(c.a, 1.0f, 1e-5f);
}

TEST(Timeline, EaseInKeyframes)
{
    Timeline tl;
    tl.add(0.0f, 0.0f, ease::ease_in);
    tl.add(1.0f, 1.0f, ease::ease_in);

    // At t=0.5, ease_in (cubic) gives 0.125, so value = lerp(0, 1, 0.125) = 0.125
    auto v = tl.evaluate(0.5f);
    ASSERT_TRUE(std::holds_alternative<float>(v));
    EXPECT_NEAR(std::get<float>(v), 0.125f, 1e-5f);
}

TEST(Timeline, Duration)
{
    Timeline tl;
    tl.add(0.0f, 0.0f);
    tl.add(3.5f, 10.0f);
    tl.add(2.0f, 5.0f);  // out of order — duration should still be max time

    EXPECT_FLOAT_EQ(tl.duration(), 3.5f);
}

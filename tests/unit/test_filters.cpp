#include <cmath>
#include <gtest/gtest.h>
#include <vector>

#include "data/filters.hpp"

using namespace spectra::data;

// --- Moving average tests ---

TEST(MovingAverage, EmptyInput)
{
    auto result = moving_average({}, 5);
    EXPECT_TRUE(result.empty());
}

TEST(MovingAverage, WindowSizeOne)
{
    std::vector<float> v = {1, 2, 3, 4, 5};
    auto result = moving_average(v, 1);
    ASSERT_EQ(result.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_FLOAT_EQ(result[i], v[i]);
}

TEST(MovingAverage, ConstantSignal)
{
    std::vector<float> v(50, 7.0f);
    auto result = moving_average(v, 11);
    ASSERT_EQ(result.size(), 50u);
    for (auto val : result)
        EXPECT_NEAR(val, 7.0f, 1e-5f);
}

TEST(MovingAverage, KnownValues)
{
    // [1, 2, 3, 4, 5] with window=3 (centered)
    // i=0: avg(1,2)       = 1.5   (half=1, lo=0, hi=1)
    // i=1: avg(1,2,3)     = 2.0
    // i=2: avg(2,3,4)     = 3.0
    // i=3: avg(3,4,5)     = 4.0
    // i=4: avg(4,5)        = 4.5   (half=1, lo=3, hi=4)
    std::vector<float> v = {1, 2, 3, 4, 5};
    auto result = moving_average(v, 3);
    ASSERT_EQ(result.size(), 5u);
    EXPECT_NEAR(result[0], 1.5f, 1e-5f);
    EXPECT_NEAR(result[1], 2.0f, 1e-5f);
    EXPECT_NEAR(result[2], 3.0f, 1e-5f);
    EXPECT_NEAR(result[3], 4.0f, 1e-5f);
    EXPECT_NEAR(result[4], 4.5f, 1e-5f);
}

TEST(MovingAverage, SmoothsNoise)
{
    // A noisy signal should have lower variance after smoothing
    std::vector<float> v(200);
    for (std::size_t i = 0; i < 200; ++i)
        v[i] = static_cast<float>(i) + ((i % 2 == 0) ? 5.0f : -5.0f);

    auto smoothed = moving_average(v, 21);

    // Compute variance of original vs smoothed (excluding edges)
    double var_orig = 0, var_smooth = 0;
    const std::size_t start = 20, end = 180;
    for (std::size_t i = start; i < end; ++i)
    {
        double d1 = v[i] - static_cast<float>(i);
        double d2 = smoothed[i] - static_cast<float>(i);
        var_orig += d1 * d1;
        var_smooth += d2 * d2;
    }
    EXPECT_LT(var_smooth, var_orig) << "Smoothed signal should have lower variance";
}

TEST(MovingAverage, OutputSameSize)
{
    std::vector<float> v(100, 1.0f);
    auto result = moving_average(v, 15);
    EXPECT_EQ(result.size(), 100u);
}

// --- Exponential smoothing tests ---

TEST(ExponentialSmoothing, EmptyInput)
{
    auto result = exponential_smoothing({}, 0.5f);
    EXPECT_TRUE(result.empty());
}

TEST(ExponentialSmoothing, AlphaOne)
{
    // alpha=1 means no smoothing: output == input
    std::vector<float> v = {1, 5, 3, 8, 2};
    auto result = exponential_smoothing(v, 1.0f);
    ASSERT_EQ(result.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_FLOAT_EQ(result[i], v[i]);
}

TEST(ExponentialSmoothing, FirstValuePreserved)
{
    std::vector<float> v = {10, 20, 30};
    auto result = exponential_smoothing(v, 0.3f);
    EXPECT_FLOAT_EQ(result[0], 10.0f);
}

TEST(ExponentialSmoothing, KnownRecurrence)
{
    // alpha=0.5: out[0]=1, out[1]=0.5*2+0.5*1=1.5, out[2]=0.5*3+0.5*1.5=2.25
    std::vector<float> v = {1, 2, 3};
    auto result = exponential_smoothing(v, 0.5f);
    ASSERT_EQ(result.size(), 3u);
    EXPECT_FLOAT_EQ(result[0], 1.0f);
    EXPECT_FLOAT_EQ(result[1], 1.5f);
    EXPECT_FLOAT_EQ(result[2], 2.25f);
}

TEST(ExponentialSmoothing, LowAlphaSmooths)
{
    // With very low alpha, output should lag behind input significantly
    std::vector<float> v = {0, 0, 0, 0, 100, 100, 100, 100};
    auto result = exponential_smoothing(v, 0.1f);
    // After the step at index 4, the output should still be well below 100
    EXPECT_LT(result[5], 50.0f);
}

TEST(ExponentialSmoothing, OutputSameSize)
{
    std::vector<float> v(100, 1.0f);
    auto result = exponential_smoothing(v, 0.3f);
    EXPECT_EQ(result.size(), 100u);
}

// --- Gaussian smooth tests ---

TEST(GaussianSmooth, EmptyInput)
{
    auto result = gaussian_smooth({}, 1.0f);
    EXPECT_TRUE(result.empty());
}

TEST(GaussianSmooth, ZeroSigmaReturnsInput)
{
    std::vector<float> v = {1, 2, 3, 4, 5};
    auto result = gaussian_smooth(v, 0.0f);
    ASSERT_EQ(result.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_FLOAT_EQ(result[i], v[i]);
}

TEST(GaussianSmooth, ConstantSignal)
{
    std::vector<float> v(50, 3.0f);
    auto result = gaussian_smooth(v, 5.0f);
    ASSERT_EQ(result.size(), 50u);
    for (auto val : result)
        EXPECT_NEAR(val, 3.0f, 1e-4f);
}

TEST(GaussianSmooth, SmoothsNoise)
{
    std::vector<float> v(200);
    for (std::size_t i = 0; i < 200; ++i)
        v[i] = static_cast<float>(i) + ((i % 2 == 0) ? 5.0f : -5.0f);

    auto smoothed = gaussian_smooth(v, 3.0f);

    double var_orig = 0, var_smooth = 0;
    const std::size_t start = 20, end = 180;
    for (std::size_t i = start; i < end; ++i)
    {
        double d1 = v[i] - static_cast<float>(i);
        double d2 = smoothed[i] - static_cast<float>(i);
        var_orig += d1 * d1;
        var_smooth += d2 * d2;
    }
    EXPECT_LT(var_smooth, var_orig);
}

TEST(GaussianSmooth, OutputSameSize)
{
    std::vector<float> v(100, 1.0f);
    auto result = gaussian_smooth(v, 2.0f);
    EXPECT_EQ(result.size(), 100u);
}

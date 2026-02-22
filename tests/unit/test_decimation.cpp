#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

#include "data/decimation.hpp"

using namespace spectra::data;

// --- LTTB tests ---

TEST(LTTB, EmptyInput)
{
    auto result = lttb({}, {}, 10);
    EXPECT_TRUE(result.empty());
}

TEST(LTTB, TargetLargerThanInput)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 4, 9, 16};
    auto result = lttb(x, y, 100);
    ASSERT_EQ(result.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_FLOAT_EQ(result[i].first, x[i]);
        EXPECT_FLOAT_EQ(result[i].second, y[i]);
    }
}

TEST(LTTB, TargetEqualsInput)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 0};
    auto result = lttb(x, y, 3);
    ASSERT_EQ(result.size(), 3u);
}

TEST(LTTB, PreservesFirstAndLast)
{
    std::vector<float> x(100), y(100);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 100; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.1f);

    auto result = lttb(x, y, 20);
    ASSERT_EQ(result.size(), 20u);
    EXPECT_FLOAT_EQ(result.front().first, x.front());
    EXPECT_FLOAT_EQ(result.front().second, y.front());
    EXPECT_FLOAT_EQ(result.back().first, x.back());
    EXPECT_FLOAT_EQ(result.back().second, y.back());
}

TEST(LTTB, OutputSizeMatchesTarget)
{
    std::vector<float> x(1000), y(1000);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 1000; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.01f);

    auto result = lttb(x, y, 100);
    EXPECT_EQ(result.size(), 100u);
}

TEST(LTTB, PreservesKeyPoints)
{
    // Create a signal with a clear spike — LTTB should preserve it
    std::vector<float> x(100), y(100);
    std::iota(x.begin(), x.end(), 0.0f);
    std::fill(y.begin(), y.end(), 0.0f);
    y[50] = 100.0f;  // big spike

    auto result = lttb(x, y, 20);

    // The spike should be preserved
    bool spike_found = false;
    for (auto& [rx, ry] : result)
    {
        if (ry > 50.0f)
        {
            spike_found = true;
            break;
        }
    }
    EXPECT_TRUE(spike_found) << "LTTB should preserve prominent spike";
}

TEST(LTTB, TargetLessThan3ReturnsAll)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 2, 3, 4};
    auto result = lttb(x, y, 2);
    EXPECT_EQ(result.size(), 5u);  // returns all when target < 3
}

// --- Min-max decimation tests ---

TEST(MinMaxDecimate, EmptyInput)
{
    auto result = min_max_decimate({}, {}, 10);
    EXPECT_TRUE(result.empty());
}

TEST(MinMaxDecimate, ZeroBuckets)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 0};
    auto result = min_max_decimate(x, y, 0);
    EXPECT_TRUE(result.empty());
}

TEST(MinMaxDecimate, SmallInputReturnedUnchanged)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {5, 10, 3};
    auto result = min_max_decimate(x, y, 5);
    ASSERT_EQ(result.size(), 3u);
}

TEST(MinMaxDecimate, PreservesExtremes)
{
    std::vector<float> x(100), y(100);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < 100; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.1f);

    auto result = min_max_decimate(x, y, 10);

    // Find global min and max of y
    float y_min = *std::min_element(y.begin(), y.end());
    float y_max = *std::max_element(y.begin(), y.end());

    // The result should contain values close to the global extremes
    float result_min = std::numeric_limits<float>::max();
    float result_max = -std::numeric_limits<float>::max();
    for (auto& [rx, ry] : result)
    {
        result_min = std::min(result_min, ry);
        result_max = std::max(result_max, ry);
    }
    EXPECT_NEAR(result_min, y_min, 0.1f);
    EXPECT_NEAR(result_max, y_max, 0.1f);
}

TEST(MinMaxDecimate, OutputSizeBounded)
{
    std::vector<float> x(1000), y(1000);
    std::iota(x.begin(), x.end(), 0.0f);
    for (auto& v : y)
        v = 1.0f;

    auto result = min_max_decimate(x, y, 50);
    EXPECT_LE(result.size(), 100u);  // at most 2 * bucket_count
}

// --- Resample uniform tests ---

TEST(ResampleUniform, EmptyInput)
{
    auto result = resample_uniform({}, {}, 10);
    EXPECT_TRUE(result.empty());
}

TEST(ResampleUniform, SinglePoint)
{
    std::vector<float> x = {5.0f};
    std::vector<float> y = {3.0f};
    auto result = resample_uniform(x, y, 1);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_FLOAT_EQ(result[0].first, 5.0f);
    EXPECT_FLOAT_EQ(result[0].second, 3.0f);
}

TEST(ResampleUniform, LinearInterpolation)
{
    // y = 2x on [0, 10]
    std::vector<float> x = {0.0f, 10.0f};
    std::vector<float> y = {0.0f, 20.0f};

    auto result = resample_uniform(x, y, 11);
    ASSERT_EQ(result.size(), 11u);

    for (std::size_t i = 0; i < 11; ++i)
    {
        float expected_x = static_cast<float>(i);
        float expected_y = 2.0f * expected_x;
        EXPECT_NEAR(result[i].first, expected_x, 1e-5f) << "at i=" << i;
        EXPECT_NEAR(result[i].second, expected_y, 1e-4f) << "at i=" << i;
    }
}

TEST(ResampleUniform, PreservesEndpoints)
{
    std::vector<float> x = {1.0f, 3.0f, 7.0f, 10.0f};
    std::vector<float> y = {2.0f, 6.0f, 1.0f, 5.0f};

    auto result = resample_uniform(x, y, 50);
    ASSERT_EQ(result.size(), 50u);
    EXPECT_NEAR(result.front().first, 1.0f, 1e-5f);
    EXPECT_NEAR(result.front().second, 2.0f, 1e-4f);
    EXPECT_NEAR(result.back().first, 10.0f, 1e-5f);
    EXPECT_NEAR(result.back().second, 5.0f, 1e-4f);
}

TEST(ResampleUniform, OutputCountRespected)
{
    std::vector<float> x = {0, 1, 5, 10};
    std::vector<float> y = {0, 1, 5, 10};
    auto result = resample_uniform(x, y, 200);
    EXPECT_EQ(result.size(), 200u);
}

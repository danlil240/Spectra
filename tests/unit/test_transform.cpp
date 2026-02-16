#include <gtest/gtest.h>

// Internal headers — tests link against plotix which includes src/ in its include path
#include <cmath>

#include "core/transform.hpp"

using namespace plotix;

// --- ortho_projection ---

TEST(OrthoProjection, Identity)
{
    // Maps [-1,1] × [-1,1] → [-1,1] × [-1,1] should be identity-like
    auto m = ortho_projection(-1.0f, 1.0f, -1.0f, 1.0f);
    // m[0] = 2/(1-(-1)) = 1, m[5] = 1, m[12] = 0, m[13] = 0
    EXPECT_FLOAT_EQ(m[0], 1.0f);
    EXPECT_FLOAT_EQ(m[5], 1.0f);
    EXPECT_FLOAT_EQ(m[12], 0.0f);
    EXPECT_FLOAT_EQ(m[13], 0.0f);
    EXPECT_FLOAT_EQ(m[15], 1.0f);
}

TEST(OrthoProjection, AsymmetricRange)
{
    // Maps [0, 100] × [0, 200]
    auto m = ortho_projection(0.0f, 100.0f, 0.0f, 200.0f);
    EXPECT_FLOAT_EQ(m[0], 2.0f / 100.0f);  // 0.02
    EXPECT_FLOAT_EQ(m[5], 2.0f / 200.0f);  // 0.01
    EXPECT_FLOAT_EQ(m[12], -1.0f);         // -(100+0)/100
    EXPECT_FLOAT_EQ(m[13], -1.0f);         // -(200+0)/200
}

TEST(OrthoProjection, ZeroRangeFallback)
{
    // When left == right, should not produce NaN/Inf
    auto m = ortho_projection(5.0f, 5.0f, 3.0f, 3.0f);
    EXPECT_FALSE(std::isnan(m[0]));
    EXPECT_FALSE(std::isinf(m[0]));
    EXPECT_FALSE(std::isnan(m[5]));
    EXPECT_FALSE(std::isinf(m[5]));
}

// --- data_to_ndc ---

TEST(DataToNdc, Center)
{
    // Midpoint of [0, 10] × [0, 10] should map to (0, 0)
    auto v = data_to_ndc(5.0f, 5.0f, 0.0f, 10.0f, 0.0f, 10.0f);
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(DataToNdc, MinCorner)
{
    auto v = data_to_ndc(0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 10.0f);
    EXPECT_FLOAT_EQ(v.x, -1.0f);
    EXPECT_FLOAT_EQ(v.y, -1.0f);
}

TEST(DataToNdc, MaxCorner)
{
    auto v = data_to_ndc(10.0f, 10.0f, 0.0f, 10.0f, 0.0f, 10.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 1.0f);
}

TEST(DataToNdc, NegativeRange)
{
    auto v = data_to_ndc(0.0f, 0.0f, -5.0f, 5.0f, -5.0f, 5.0f);
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(DataToNdc, ZeroRangeFallback)
{
    auto v = data_to_ndc(5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f);
    EXPECT_FALSE(std::isnan(v.x));
    EXPECT_FALSE(std::isnan(v.y));
}

// --- ndc_to_screen ---

TEST(NdcToScreen, Center)
{
    Rect vp{100.0f, 200.0f, 800.0f, 600.0f};
    auto v = ndc_to_screen(0.0f, 0.0f, vp);
    // Center of viewport: (100 + 400, 200 + 300)
    EXPECT_FLOAT_EQ(v.x, 500.0f);
    EXPECT_FLOAT_EQ(v.y, 500.0f);
}

TEST(NdcToScreen, BottomLeft)
{
    Rect vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    auto v = ndc_to_screen(-1.0f, -1.0f, vp);
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(NdcToScreen, TopRight)
{
    Rect vp{0.0f, 0.0f, 1000.0f, 1000.0f};
    auto v = ndc_to_screen(1.0f, 1.0f, vp);
    EXPECT_FLOAT_EQ(v.x, 1000.0f);
    EXPECT_FLOAT_EQ(v.y, 1000.0f);
}

// --- data_to_screen (end-to-end) ---

TEST(DataToScreen, KnownMapping)
{
    Rect vp{0.0f, 0.0f, 800.0f, 600.0f};
    // Data (5, 5) in range [0,10]×[0,10] → NDC (0,0) → screen center
    auto v = data_to_screen(5.0f, 5.0f, 0.0f, 10.0f, 0.0f, 10.0f, vp);
    EXPECT_FLOAT_EQ(v.x, 400.0f);
    EXPECT_FLOAT_EQ(v.y, 300.0f);
}

TEST(DataToScreen, MinCorner)
{
    Rect vp{50.0f, 50.0f, 400.0f, 300.0f};
    auto v = data_to_screen(0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 10.0f, vp);
    EXPECT_FLOAT_EQ(v.x, 50.0f);
    EXPECT_FLOAT_EQ(v.y, 50.0f);
}

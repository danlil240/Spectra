// fit_rgba_image(): scaling a captured window frame to an explicitly
// requested export size (R08 — explicit sizes scale instead of cropping).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ui/app/window_png_export.hpp"

using spectra::fit_rgba_image;

namespace
{

std::vector<uint8_t> solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4)
    {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

const uint8_t* at(const std::vector<uint8_t>& px, uint32_t w, uint32_t x, uint32_t y)
{
    return px.data() + (static_cast<size_t>(y) * w + x) * 4;
}

}   // namespace

TEST(FitRgbaImage, SameSizeCopiesExactly)
{
    std::vector<uint8_t> src(4 * 3 * 4);
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<uint8_t>(i * 7);
    std::vector<uint8_t> dst(src.size(), 1);
    fit_rgba_image(src.data(), 4, 3, dst.data(), 4, 3);
    EXPECT_EQ(dst, src);
}

TEST(FitRgbaImage, HalvingAveragesBlocks)
{
    // 4x2 source: left half black, right half white -> 2x1 = black, white.
    std::vector<uint8_t> src = solid(4, 2, 0, 0, 0);
    for (uint32_t y = 0; y < 2; ++y)
        for (uint32_t x = 2; x < 4; ++x)
        {
            auto* p = src.data() + (y * 4 + x) * 4;
            p[0] = p[1] = p[2] = 255;
        }
    std::vector<uint8_t> dst(2 * 1 * 4);
    fit_rgba_image(src.data(), 4, 2, dst.data(), 2, 1);
    EXPECT_EQ(at(dst, 2, 0, 0)[0], 0);
    EXPECT_EQ(at(dst, 2, 1, 0)[0], 255);
    EXPECT_EQ(at(dst, 2, 1, 0)[3], 255);
}

TEST(FitRgbaImage, BottomRowsSurviveDownscale)
{
    // A frame whose only content is a red band in the bottom rows (like an
    // xlabel) keeps that band after being fit to a smaller export.
    const uint32_t       W = 80, H = 68;
    std::vector<uint8_t> src = solid(W, H, 255, 255, 255);
    for (uint32_t y = H - 8; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x)
        {
            auto* p = src.data() + (y * W + x) * 4;
            p[1] = p[2] = 0;
        }
    std::vector<uint8_t> dst(40 * 34 * 4);
    fit_rgba_image(src.data(), W, H, dst.data(), 40, 34);
    EXPECT_EQ(at(dst, 40, 20, 33)[1], 0);    // bottom row still red
    EXPECT_EQ(at(dst, 40, 20, 0)[1], 255);   // top still white
}

TEST(FitRgbaImage, AspectMismatchLetterboxesTransparent)
{
    // 4x2 into 4x4: content scaled 1x into rows 1..2, rows 0 and 3 transparent.
    std::vector<uint8_t> src = solid(4, 2, 10, 20, 30);
    std::vector<uint8_t> dst(4 * 4 * 4, 99);
    fit_rgba_image(src.data(), 4, 2, dst.data(), 4, 4);
    for (uint32_t x = 0; x < 4; ++x)
    {
        EXPECT_EQ(at(dst, 4, x, 0)[3], 0);
        EXPECT_EQ(at(dst, 4, x, 3)[3], 0);
        EXPECT_EQ(at(dst, 4, x, 1)[0], 10);
        EXPECT_EQ(at(dst, 4, x, 2)[2], 30);
        EXPECT_EQ(at(dst, 4, x, 2)[3], 255);
    }
}

TEST(FitRgbaImage, UpscaleKeepsSolidColour)
{
    std::vector<uint8_t> src = solid(3, 2, 40, 80, 120);
    std::vector<uint8_t> dst(9 * 6 * 4);
    fit_rgba_image(src.data(), 3, 2, dst.data(), 9, 6);
    for (uint32_t y = 0; y < 6; ++y)
        for (uint32_t x = 0; x < 9; ++x)
        {
            EXPECT_EQ(at(dst, 9, x, y)[0], 40);
            EXPECT_EQ(at(dst, 9, x, y)[2], 120);
            EXPECT_EQ(at(dst, 9, x, y)[3], 255);
        }
}

TEST(FitRgbaImage, NullOrEmptySourceGivesTransparent)
{
    std::vector<uint8_t> dst(2 * 2 * 4, 7);
    fit_rgba_image(nullptr, 0, 0, dst.data(), 2, 2);
    for (uint8_t v : dst)
        EXPECT_EQ(v, 0);
}

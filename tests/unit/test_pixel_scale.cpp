// pixel_scale.hpp: drawing a logical-pixel layout into a HiDPI framebuffer
// (browser canvases at devicePixelRatio > 1, HiDPI windows).

#include <gtest/gtest.h>

#include "render/pixel_scale.hpp"

using spectra::compute_surface_scale;
using spectra::scale_scissor_rect;

TEST(SurfaceScale, SameSizeIsUnscaled)
{
    auto s = compute_surface_scale(800, 600, 800, 600);
    EXPECT_EQ(s.layout_width, 800u);
    EXPECT_EQ(s.layout_height, 600u);
    EXPECT_FLOAT_EQ(s.pixel_scale, 1.0f);
}

TEST(SurfaceScale, Retina2xLaysOutInWindowCoordinates)
{
    auto s = compute_surface_scale(800, 600, 1600, 1200);
    EXPECT_EQ(s.layout_width, 800u);
    EXPECT_EQ(s.layout_height, 600u);
    EXPECT_FLOAT_EQ(s.pixel_scale, 2.0f);
}

TEST(SurfaceScale, FractionalScaleRoundsLayout)
{
    // Windows at 150%: a 1000x750 CSS canvas has a 1500x1125 backing store.
    auto s = compute_surface_scale(1000, 750, 1500, 1125);
    EXPECT_EQ(s.layout_width, 1000u);
    EXPECT_EQ(s.layout_height, 750u);
    EXPECT_FLOAT_EQ(s.pixel_scale, 1.5f);
}

TEST(SurfaceScale, DegenerateSizesFallBackToFramebuffer)
{
    auto s = compute_surface_scale(0, 0, 640, 480);
    EXPECT_EQ(s.layout_width, 640u);
    EXPECT_EQ(s.layout_height, 480u);
    EXPECT_FLOAT_EQ(s.pixel_scale, 1.0f);

    s = compute_surface_scale(800, 600, 0, 0);
    EXPECT_FLOAT_EQ(s.pixel_scale, 1.0f);
}

TEST(SurfaceScale, SmallerFramebufferIsNotDownscaled)
{
    auto s = compute_surface_scale(800, 600, 400, 300);
    EXPECT_EQ(s.layout_width, 400u);
    EXPECT_EQ(s.layout_height, 300u);
    EXPECT_FLOAT_EQ(s.pixel_scale, 1.0f);
}

TEST(ScissorRect, UnscaledPassesThrough)
{
    auto r = scale_scissor_rect(10, 20, 100, 50, 1.0f, 800, 600);
    EXPECT_EQ(r.x, 10);
    EXPECT_EQ(r.y, 20);
    EXPECT_EQ(r.w, 100u);
    EXPECT_EQ(r.h, 50u);
}

TEST(ScissorRect, ScaledCoversEveryTouchedPixel)
{
    auto r = scale_scissor_rect(3, 5, 7, 9, 1.5f, 1000, 1000);
    EXPECT_EQ(r.x, 4);     // floor(4.5)
    EXPECT_EQ(r.y, 7);     // floor(7.5)
    EXPECT_EQ(r.w, 11u);   // ceil(15) - 4
    EXPECT_EQ(r.h, 14u);   // ceil(21) - 7
}

TEST(ScissorRect, ClippedToRenderTarget)
{
    // A full-figure scissor rounded up past the framebuffer edge must not
    // exceed it: WebGPU rejects out-of-bounds scissors.
    auto r = scale_scissor_rect(0, 0, 667, 500, 1.5f, 1000, 750);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 1000u);
    EXPECT_EQ(r.h, 750u);
}

TEST(ScissorRect, NegativeOriginIsClampedNotShifted)
{
    auto r = scale_scissor_rect(-10, -10, 30, 30, 1.0f, 100, 100);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 20u);
    EXPECT_EQ(r.h, 20u);
}

TEST(ScissorRect, FullyOutsideIsEmpty)
{
    auto r = scale_scissor_rect(900, 0, 50, 50, 1.0f, 800, 600);
    EXPECT_EQ(r.w, 0u);
    EXPECT_EQ(r.h, 50u);
}

TEST(ScissorRect, InvalidScaleTreatedAsOne)
{
    auto r = scale_scissor_rect(1, 2, 3, 4, 0.0f, 100, 100);
    EXPECT_EQ(r.x, 1);
    EXPECT_EQ(r.y, 2);
    EXPECT_EQ(r.w, 3u);
    EXPECT_EQ(r.h, 4u);
}

TEST(LogicalExtent, InverseOfPixelScale)
{
    EXPECT_EQ(spectra::logical_extent(1600, 2.0f), 800u);
    EXPECT_EQ(spectra::logical_extent(1501, 1.5f), 1001u);
    EXPECT_EQ(spectra::logical_extent(800, 1.0f), 800u);
    EXPECT_EQ(spectra::logical_extent(800, 0.0f), 800u);
    EXPECT_EQ(spectra::logical_extent(1, 4.0f), 1u);
}

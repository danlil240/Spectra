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

// ── Qt adapter / HiDPI windows: logical size -> framebuffer -> layout ──────

TEST(FramebufferExtent, ScalesLogicalSizeByDevicePixelRatio)
{
    EXPECT_EQ(spectra::framebuffer_extent(800, 1.0), 800u);
    EXPECT_EQ(spectra::framebuffer_extent(800, 2.0), 1600u);
    EXPECT_EQ(spectra::framebuffer_extent(801, 1.25), 1001u);   // 1001.25 rounds
    EXPECT_EQ(spectra::framebuffer_extent(3, 1.5), 5u);         // 4.5 rounds up
}

TEST(FramebufferExtent, DegenerateInputs)
{
    EXPECT_EQ(spectra::framebuffer_extent(0, 2.0), 0u);
    EXPECT_EQ(spectra::framebuffer_extent(-5, 2.0), 0u);
    EXPECT_EQ(spectra::framebuffer_extent(640, 0.0), 640u);   // invalid ratio -> 1
    EXPECT_EQ(spectra::framebuffer_extent(640, -2.0), 640u);
}

TEST(FramebufferExtent, RoundTripsThroughSurfaceScale)
{
    // A Qt window 640x480 (device-independent) on screens of several scales:
    // the layout is always the window size and the pixel scale the ratio.
    for (double dpr : {1.0, 1.25, 1.5, 2.0, 3.0})
    {
        const uint32_t fw = spectra::framebuffer_extent(640, dpr);
        const uint32_t fh = spectra::framebuffer_extent(480, dpr);
        const auto     s  = compute_surface_scale(640, 480, fw, fh);
        EXPECT_EQ(s.layout_width, 640u) << "dpr " << dpr;
        EXPECT_EQ(s.layout_height, 480u) << "dpr " << dpr;
        EXPECT_NEAR(s.pixel_scale, dpr, 1e-5) << "dpr " << dpr;
    }
}

TEST(WindowToLayout, IdentityWhenLayoutIsWindowSize)
{
    // HiDPI 2x: mouse positions in window units are already layout units —
    // they must NOT be multiplied by the device pixel ratio.
    const auto s = compute_surface_scale(800, 600, 1600, 1200);
    const auto p = spectra::window_to_layout(s, 800, 600, 400.0f, 300.0f);
    EXPECT_FLOAT_EQ(p.x, 400.0f);
    EXPECT_FLOAT_EQ(p.y, 300.0f);
}

TEST(WindowToLayout, ScalesWhenLayoutFellBackToFramebuffer)
{
    // The backend refused a pixel scale: layout is the 1600x1200 framebuffer.
    const spectra::SurfaceScale s{1600, 1200, 1.0f};
    const auto                  p = spectra::window_to_layout(s, 800, 600, 100.0f, 50.0f);
    EXPECT_FLOAT_EQ(p.x, 200.0f);
    EXPECT_FLOAT_EQ(p.y, 100.0f);
}

TEST(WindowToLayout, DegenerateWindowPassesThrough)
{
    const auto s = compute_surface_scale(800, 600, 1600, 1200);
    const auto p = spectra::window_to_layout(s, 0, 0, 12.0f, 34.0f);
    EXPECT_FLOAT_EQ(p.x, 12.0f);
    EXPECT_FLOAT_EQ(p.y, 34.0f);
    const auto q = spectra::window_to_layout(spectra::SurfaceScale{}, 800, 600, 12.0f, 34.0f);
    EXPECT_FLOAT_EQ(q.x, 12.0f);
    EXPECT_FLOAT_EQ(q.y, 34.0f);
}

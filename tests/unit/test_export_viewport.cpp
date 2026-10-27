// compute_export_viewport(): layout size and pixel scale of a headless PNG export.

#include <gtest/gtest.h>

#include "render/renderer.hpp"

using spectra::compute_export_viewport;

TEST(ExportViewport, SameSizeIsUnscaled)
{
    auto ev = compute_export_viewport(1280, 800, 1280, 800);
    EXPECT_EQ(ev.layout_width, 1280u);
    EXPECT_EQ(ev.layout_height, 800u);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 1.0f);
}

TEST(ExportViewport, DoubleSizeKeepsLayoutAndScalesByTwo)
{
    auto ev = compute_export_viewport(1280, 800, 2560, 1600);
    EXPECT_EQ(ev.layout_width, 1280u);
    EXPECT_EQ(ev.layout_height, 800u);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 2.0f);
}

TEST(ExportViewport, FractionalScale)
{
    auto ev = compute_export_viewport(1000, 500, 1500, 750);
    EXPECT_EQ(ev.layout_width, 1000u);
    EXPECT_EQ(ev.layout_height, 500u);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 1.5f);
}

TEST(ExportViewport, SmallerExportIsReLaidOutUnscaled)
{
    auto ev = compute_export_viewport(1280, 800, 640, 400);
    EXPECT_EQ(ev.layout_width, 640u);
    EXPECT_EQ(ev.layout_height, 400u);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 1.0f);
}

TEST(ExportViewport, DifferentAspectUsesSmallerRatio)
{
    // 2x wide, 1.5x tall: scale 1.5, the layout gets the extra width.
    auto ev = compute_export_viewport(800, 600, 1600, 900);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 1.5f);
    EXPECT_EQ(ev.layout_width, 1067u);
    EXPECT_EQ(ev.layout_height, 600u);

    // Wider but not taller: no upscaling, plain re-layout.
    ev = compute_export_viewport(800, 600, 1600, 600);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 1.0f);
    EXPECT_EQ(ev.layout_width, 1600u);
    EXPECT_EQ(ev.layout_height, 600u);
}

TEST(ExportViewport, DegenerateSizesAreUnscaled)
{
    auto ev = compute_export_viewport(0, 0, 640, 480);
    EXPECT_EQ(ev.layout_width, 640u);
    EXPECT_EQ(ev.layout_height, 480u);
    EXPECT_FLOAT_EQ(ev.pixel_scale, 1.0f);
}

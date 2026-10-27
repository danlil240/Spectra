// GPU pixel tests: per-shape colours of a single batched ShapeSeries3D reach
// the framebuffer (audit finding S19).

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "render/backend.hpp"

using namespace spectra;

namespace
{

constexpr uint32_t kW = 240;
constexpr uint32_t kH = 240;

struct HueCounts
{
    int red   = 0;
    int green = 0;
    int blue  = 0;
    // Mean (r - max(g, b)) over red-dominant pixels: drops when red blends
    // with a neutral background.
    double red_saturation = 0.0;
};

// Count strongly saturated pixels per dominant channel.  Lighting scales the
// brightness, but a lit red surface stays red-dominant.
HueCounts count_hues(const std::vector<uint8_t>& px)
{
    HueCounts c;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
    {
        const int r = px[i + 0];
        const int g = px[i + 1];
        const int b = px[i + 2];
        if (r > 60 && r > 2 * g && r > 2 * b)
        {
            ++c.red;
            c.red_saturation += r - std::max(g, b);
        }
        else if (g > 60 && g > 2 * r && g > 2 * b)
            ++c.green;
        else if (b > 60 && b > 2 * r && b > 2 * g)
            ++c.blue;
    }
    if (c.red > 0)
        c.red_saturation /= c.red;
    return c;
}

class Shape3DColorTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
    }
    void TearDown() override { app_.reset(); }

    HueCounts render_and_count()
    {
        app_->run();
        std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
        auto*                backend = app_->backend();
        EXPECT_NE(backend, nullptr);
        if (!backend || !backend->readback_framebuffer(px.data(), kW, kH))
        {
            ADD_FAILURE() << "readback failed";
            return {};
        }
        return count_hues(px);
    }

    std::unique_ptr<App> app_;
};

constexpr int kMinPixels = 200;

}   // namespace

TEST_F(Shape3DColorTest, BatchedSeriesRendersEachShapeColor)
{
    auto& fig = app_->figure({.width = kW, .height = kH});
    auto& ax  = fig.subplot3d(1, 1, 1);
    auto& sh  = ax.shapes3d();
    sh.color(colors::blue);
    sh.box(-1.5f, 0, 0, 0.6f, 0.6f, 0.6f).shape_color(colors::red);
    sh.sphere(1.5f, 0, 0, 0.6f).shape_color(colors::green);
    ax.auto_fit();

    HueCounts c = render_and_count();
    EXPECT_GT(c.red, kMinPixels) << "red box missing";
    EXPECT_GT(c.green, kMinPixels) << "green sphere missing";
    EXPECT_LT(c.blue, kMinPixels / 4) << "no shape should fall back to the series colour";
}

TEST_F(Shape3DColorTest, UnspecifiedShapeInheritsSeriesColor)
{
    auto& fig = app_->figure({.width = kW, .height = kH});
    auto& ax  = fig.subplot3d(1, 1, 1);
    auto& sh  = ax.shapes3d();
    sh.color(colors::blue);
    sh.box(-1.5f, 0, 0, 0.6f, 0.6f, 0.6f).shape_color(colors::red);
    sh.sphere(1.5f, 0, 0, 0.6f);   // no shape_color: series colour
    ax.auto_fit();

    HueCounts c = render_and_count();
    EXPECT_GT(c.red, kMinPixels);
    EXPECT_GT(c.blue, kMinPixels) << "uncoloured sphere should use the series colour";
    EXPECT_LT(c.green, kMinPixels / 4);
}

TEST_F(Shape3DColorTest, ShapeColorChangeAfterFirstFrameUpdates)
{
    auto& fig = app_->figure({.width = kW, .height = kH});
    auto& ax  = fig.subplot3d(1, 1, 1);
    auto& sh  = ax.shapes3d();
    sh.color(colors::blue);
    sh.box(0, 0, 0, 0.8f, 0.8f, 0.8f).shape_color(colors::red);
    ax.auto_fit();

    HueCounts before = render_and_count();
    EXPECT_GT(before.red, kMinPixels);
    EXPECT_LT(before.green, kMinPixels / 4);

    sh.shape_color(colors::green);   // applies to the last (only) shape
    HueCounts after = render_and_count();
    EXPECT_GT(after.green, kMinPixels) << "colour change not reflected after first frame";
    EXPECT_LT(after.red, kMinPixels / 4);
}

TEST_F(Shape3DColorTest, TranslucentShapeBlendsOverBackground)
{
    auto& fig = app_->figure({.width = kW, .height = kH});
    auto& ax  = fig.subplot3d(1, 1, 1);
    auto& sh  = ax.shapes3d();
    sh.box(0, 0, 0, 0.8f, 0.8f, 0.8f).shape_color(colors::red);
    ax.auto_fit();
    HueCounts opaque = render_and_count();

    sh.shape_color(Color{1.0f, 0.0f, 0.0f, 0.35f});
    HueCounts translucent = render_and_count();
    // The per-shape alpha must reach the blend path: the box still reads as red
    // but noticeably less saturated than the opaque render.
    ASSERT_GT(opaque.red, kMinPixels);
    ASSERT_GT(translucent.red, kMinPixels / 4);
    EXPECT_LT(translucent.red_saturation, opaque.red_saturation * 0.8);
}

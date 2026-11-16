// GPU pixel tests for the colour vision deficiency preview (view only): the
// theme canvas clear and a figure's background override are simulated like
// the series and ink drawn on them, so the whole plot surface is previewed.

#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "render/backend.hpp"
#include "ui/theme/theme.hpp"

using namespace spectra;

namespace
{

constexpr uint32_t kW = 320;
constexpr uint32_t kH = 240;

struct Rgb
{
    int r = 0, g = 0, b = 0;
};

class CvdPreviewGpuTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
    }
    void TearDown() override { app_.reset(); }

    // The App registers its theme manager as the current one.
    ui::ThemeManager& theme() { return ui::ThemeManager::instance(); }

    std::vector<uint8_t> render()
    {
        app_->run();
        std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
        auto*                backend = app_->backend();
        if (!backend || !backend->readback_framebuffer(px.data(), kW, kH))
            ADD_FAILURE() << "readback failed";
        return px;
    }

    static Rgb at(const std::vector<uint8_t>& px, uint32_t x, uint32_t y)
    {
        const size_t i = (static_cast<size_t>(y) * kW + x) * 4;
        return {px[i + 0], px[i + 1], px[i + 2]};
    }

    static int count_red(const std::vector<uint8_t>& px)
    {
        int n = 0;
        for (size_t i = 0; i + 3 < px.size(); i += 4)
            if (px[i] > 200 && px[i + 1] < 60 && px[i + 2] < 60)
                ++n;
        return n;
    }

    static bool grey(const Rgb& c, int tol = 2)
    {
        return std::abs(c.r - c.g) <= tol && std::abs(c.g - c.b) <= tol;
    }

    std::unique_ptr<App> app_;
};

}   // namespace

TEST_F(CvdPreviewGpuTest, ThemeCanvasClearIsSimulated)
{
    auto& fig = app_->figure({.width = kW, .height = kH});
    fig.subplot(1, 1, 1);
    ASSERT_EQ(theme().current_theme_name(), "night");

    // The night canvas is a clearly blue-tinted near-black.
    const Rgb plain = at(render(), 1, 1);
    EXPECT_GE(plain.b - plain.r, 8) << plain.r << "," << plain.g << "," << plain.b;

    theme().set_cvd_preview(ui::CVDType::Achromatopsia);
    const Rgb sim = at(render(), 1, 1);
    EXPECT_TRUE(grey(sim)) << sim.r << "," << sim.g << "," << sim.b;
}

TEST_F(CvdPreviewGpuTest, FigureBackgroundOverrideIsSimulated)
{
    auto& fig                       = app_->figure({.width = kW, .height = kH});
    fig.style().background          = Color{1.0f, 0.0f, 0.0f, 1.0f};
    fig.style().background_override = true;
    fig.subplot(1, 1, 1);

    ASSERT_GT(count_red(render()), 1000) << "red background override not drawn";

    theme().set_cvd_preview(ui::CVDType::Achromatopsia);
    const std::vector<uint8_t> px = render();
    EXPECT_EQ(count_red(px), 0) << "background override drawn without the preview";
    EXPECT_TRUE(grey(at(px, kW / 2, kH / 2)));
}

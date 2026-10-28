// R08: a windowed PNG export is read back from a later frame than the one it
// was requested in.  When the window grows in between (minimum-size limits
// enforced after the first frame, a user resize, a DPI change), the capture
// used to keep the stale requested extent and copy only the top-left corner of
// the larger frame, dropping its bottom rows (x ticks, xlabel).  A default
// export must take the size of the frame it captures; an explicitly sized one
// must be a scaled (not cropped) image of that frame.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "render/backend.hpp"
#include "stb_image.h"
#include "ui/app/session_runtime.hpp"
#include "ui/app/window_png_export.hpp"
#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #include <GLFW/glfw3.h>
#endif

namespace fs = std::filesystem;
using namespace spectra;

namespace
{

struct Image
{
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> rgba;
};

bool load(const fs::path& p, Image& img)
{
    int      ch   = 0;
    stbi_uc* data = stbi_load(p.string().c_str(), &img.w, &img.h, &ch, 4);
    if (!data)
        return false;
    img.rgba.assign(data, data + static_cast<size_t>(img.w) * img.h * 4);
    stbi_image_free(data);
    return true;
}

// Fraction of pixels in rows [y0, y1) whose RGB differs noticeably.
double diff_fraction(const Image& a, const Image& b, int y0, int y1)
{
    size_t differing = 0;
    size_t n         = 0;
    for (int y = y0; y < y1; ++y)
    {
        for (int x = 0; x < a.w; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * a.w + x) * 4;
            const int    d = std::abs(a.rgba[i + 0] - b.rgba[i + 0])
                          + std::abs(a.rgba[i + 1] - b.rgba[i + 1])
                          + std::abs(a.rgba[i + 2] - b.rgba[i + 2]);
            if (d > 48)
                ++differing;
            ++n;
        }
    }
    return n ? static_cast<double>(differing) / static_cast<double>(n) : 1.0;
}

bool has_display()
{
    const char* x = std::getenv("DISPLAY");
    const char* w = std::getenv("WAYLAND_DISPLAY");
    return (x && *x) || (w && *w);
}

class WindowExportExtent : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        if (!has_display())
            GTEST_SKIP() << "No display (run under xvfb-run)";
        dir_ =
            fs::temp_directory_path()
            / ("spectra_window_export_"
               + std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);

        app_ = std::make_unique<App>(
            AppConfig{.headless = false, .socket_path = "", .theme = "light"});
        // 800x600 is smaller than the window's minimum height.
        fig_                        = &app_->figure({.width = 800, .height = 600});
        auto&                    ax = fig_->subplot(1, 1, 1);
        const std::vector<float> x{0.0f, 1.0f, 2.0f};
        const std::vector<float> y{0.0f, 1.0f, 0.0f};
        ax.line(x, y).label("line");
        ax.xlim(0.0f, 2.0f);
        ax.ylim(0.0f, 1.0f);
        ax.xlabel("X AXIS LABEL MUST BE VISIBLE");
        ax.ylabel("Y axis");
        ax.title("Windowed PNG extent");

        app_->init_runtime();
        if (!app_->backend() || app_->backend()->swapchain_width() == 0 || !app_->session())
            GTEST_SKIP() << "No windowed Vulkan backend";
    }

    void TearDown() override
    {
        if (app_)
            app_->shutdown_runtime();
        app_.reset();
        if (!dir_.empty())
            fs::remove_all(dir_);
    }

    void steps(int n)
    {
        for (int i = 0; i < n; ++i)
        {
            app_->session()->redraw_tracker().mark_dirty("window export test");
            app_->step();
        }
    }

    uint32_t fb_w() { return app_->backend()->swapchain_width(); }
    uint32_t fb_h() { return app_->backend()->swapchain_height(); }

    fs::path             dir_;
    std::unique_ptr<App> app_;
    Figure*              fig_ = nullptr;
};

TEST_F(WindowExportExtent, EarlyDefaultExportMatchesSettledFrame)
{
    const fs::path early   = dir_ / "early.png";
    const fs::path settled = dir_ / "settled.png";

    // Requested before the first frame, i.e. before the window settles.
    ASSERT_TRUE(fig_->save_png(early.string()));
    steps(10);
    ASSERT_TRUE(fs::exists(early));

    ASSERT_TRUE(fig_->save_png(settled.string()));
    steps(10);
    ASSERT_TRUE(fs::exists(settled));

    Image a, b;
    ASSERT_TRUE(load(early, a));
    ASSERT_TRUE(load(settled, b));

    // Both have the size of the frame that was rendered, not the 800x600 the
    // figure had when the early request was made.
    EXPECT_EQ(static_cast<uint32_t>(b.w), fb_w());
    EXPECT_EQ(static_cast<uint32_t>(b.h), fb_h());
    ASSERT_EQ(a.w, b.w);
    ASSERT_EQ(a.h, b.h);
    EXPECT_GT(a.h, 600) << "window minimum height no longer exceeds the figure height";

    // The bottom of the frame (x ticks, xlabel, status bar) is present in the
    // early export exactly as in the settled one.
    const int bottom = std::max(0, a.h - 120);
    EXPECT_LT(diff_fraction(a, b, bottom, a.h), 0.02);
    // And the image as a whole matches (small transient chrome differences only).
    EXPECT_LT(diff_fraction(a, b, 0, a.h), 0.05);
}

TEST_F(WindowExportExtent, ExplicitSizeScalesInsteadOfCropping)
{
    const fs::path explicit_png = dir_ / "explicit.png";
    const fs::path settled      = dir_ / "settled.png";

    ASSERT_TRUE(fig_->save_png(explicit_png.string(), 400, 300));
    steps(10);
    ASSERT_TRUE(fig_->save_png(settled.string()));
    steps(10);

    Image e, s;
    ASSERT_TRUE(load(explicit_png, e));
    ASSERT_TRUE(load(settled, s));
    ASSERT_EQ(e.w, 400);
    ASSERT_EQ(e.h, 300);

    // The explicit export is the whole frame scaled to fit, not its top-left
    // 400x300 corner.
    Image expected;
    expected.w = 400;
    expected.h = 300;
    expected.rgba.resize(static_cast<size_t>(400) * 300 * 4);
    fit_rgba_image(s.rgba.data(),
                   static_cast<uint32_t>(s.w),
                   static_cast<uint32_t>(s.h),
                   expected.rgba.data(),
                   400,
                   300);
    EXPECT_LT(diff_fraction(e, expected, 0, 300), 0.05);
    EXPECT_LT(diff_fraction(e, expected, 240, 300), 0.05);
}

TEST_F(WindowExportExtent, SvgIsWrittenWhileTheWindowIsOpen)
{
    steps(2);
    const fs::path svg = dir_ / "live.svg";
    ASSERT_TRUE(fig_->save_svg(svg.string()));
    steps(1);
    // Written during the frame, not deferred to shutdown_runtime().
    EXPECT_TRUE(fs::exists(svg));
    EXPECT_TRUE(fig_->export_req_.svg_path.empty());
    steps(2);   // the window keeps rendering normally afterwards
}

#ifdef SPECTRA_USE_GLFW
TEST_F(WindowExportExtent, ResizeBetweenRequestAndCaptureUsesCapturedFrame)
{
    steps(10);   // settle
    auto* ui = app_->ui_context();
    ASSERT_NE(ui, nullptr);
    auto* win = static_cast<GLFWwindow*>(ui->glfw_window);
    ASSERT_NE(win, nullptr);

    const fs::path resized = dir_ / "resized.png";
    ASSERT_TRUE(fig_->save_png(resized.string()));
    steps(1);   // schedules the capture for the next frame
    glfwSetWindowSize(win, 960, 760);
    steps(10);

    Image img;
    ASSERT_TRUE(load(resized, img));
    // The capture ran in a frame rendered at the new size: its image has
    // that size instead of the size current when the export was scheduled.
    EXPECT_EQ(static_cast<uint32_t>(img.w), fb_w());
    EXPECT_EQ(static_cast<uint32_t>(img.h), fb_h());
    if (fb_w() == 960 && fb_h() == 760)
    {
        EXPECT_EQ(img.w, 960);
        EXPECT_EQ(img.h, 760);
    }
}
#endif

}   // namespace

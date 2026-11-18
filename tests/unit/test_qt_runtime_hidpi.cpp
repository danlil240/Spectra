// Qt adapter HiDPI: with devicePixelRatio 2 the swapchain must be sized in framebuffer pixels
// while the figure and UI are laid out in the window's device-independent pixels, and mouse
// positions (window units) must map to layout units unchanged.  The figure used to be laid out
// in framebuffer pixels and drawn with devicePixelRatio again, so the scale applied twice.
//
// Runs with QT_SCALE_FACTOR=2 (set in main before QGuiApplication) to simulate a 2x screen.
// Needs a Qt platform with Vulkan support (xcb under xvfb-run) and a Vulkan ICD (lavapipe
// in CI); skips when Vulkan or a window surface is unavailable.

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include "adapters/qt/qt_runtime.hpp"
#include "render/pixel_scale.hpp"

namespace
{

void pump_until(const std::function<bool()>& done, int timeout_ms = 3000)
{
    const auto start = std::chrono::steady_clock::now();
    while (!done()
           && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms))
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

TEST(QtRuntimeHiDpi, LaysOutInLogicalPixelsAndDrawsAtFramebufferResolution)
{
    spectra::adapters::qt::QtRuntime rt;
    if (!rt.init())
        GTEST_SKIP() << "QtRuntime::init() failed (no Vulkan-capable Qt platform/ICD)";

    QWindow window;
    window.setSurfaceType(QSurface::VulkanSurface);
    window.resize(400, 300);
    window.show();
    pump_until([&] { return window.isExposed(); });
    if (!window.isExposed())
        GTEST_SKIP() << "window never exposed";

    const double dpr = window.devicePixelRatio();
    ASSERT_DOUBLE_EQ(dpr, 2.0) << "QT_SCALE_FACTOR=2 not applied";

    const uint32_t fb_w = spectra::framebuffer_extent(window.width(), dpr);
    const uint32_t fb_h = spectra::framebuffer_extent(window.height(), dpr);
    ASSERT_TRUE(rt.attach_window(&window, fb_w, fb_h));

    spectra::Figure    fig;
    std::vector<float> x{0.0f, 1.0f, 2.0f}, y{0.0f, 1.0f, 0.0f};
    fig.subplot(1, 1, 1).line(x, y);

    bool rendered = false;
    for (int i = 0; i < 10 && !rendered; ++i)
    {
        rendered = rt.render_window(&window, fig);
        QCoreApplication::processEvents();
    }
    ASSERT_TRUE(rendered);

    // Layout in device-independent pixels (the window size), drawn 2x.
    EXPECT_EQ(fig.width(), static_cast<uint32_t>(window.width()));
    EXPECT_EQ(fig.height(), static_cast<uint32_t>(window.height()));
    const spectra::SurfaceScale s = rt.surface_scale(&window);
    EXPECT_EQ(s.layout_width, static_cast<uint32_t>(window.width()));
    EXPECT_EQ(s.layout_height, static_cast<uint32_t>(window.height()));
    EXPECT_FLOAT_EQ(s.pixel_scale, 2.0f);

    // Mouse positions from Qt events are already in layout units.
    const QPointF p = rt.map_to_layout(&window, QPointF(123.5, 45.0));
    EXPECT_DOUBLE_EQ(p.x(), 123.5);
    EXPECT_DOUBLE_EQ(p.y(), 45.0);

    // Release the platform surface while the runtime's QVulkanInstance is alive.
    rt.detach_window(&window);
    window.destroy();
    rt.shutdown();
}

}   // namespace

int main(int argc, char** argv)
{
    // QGuiApplication aborts when the xcb platform has no display: report a ctest skip.
    const char* platform = std::getenv("QT_QPA_PLATFORM");
    const bool  wants_x  = !platform || std::string_view(platform) == "xcb";
    if (wants_x && !std::getenv("DISPLAY"))
    {
        std::puts("SKIP: no DISPLAY for the xcb Qt platform");
        return 77;
    }
    // Simulate a 2x HiDPI screen; must be set before QGuiApplication exists.
    setenv("QT_SCALE_FACTOR", "2", 1);
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

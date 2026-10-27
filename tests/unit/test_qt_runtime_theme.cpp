// Qt adapter startup theme: QtRuntime::init() must apply its requested theme before the
// first frame.  It used to call set_theme("dark") on an empty ThemeManager (a silent no-op),
// so ImGui init later fell back to "night".
//
// Needs a Qt platform with Vulkan support (xcb under xvfb-run) and a Vulkan ICD (lavapipe
// in CI); skips when QtRuntime::init() cannot create its Vulkan objects.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <QtGui/QGuiApplication>

#include "adapters/qt/qt_runtime.hpp"
#include "ui/theme/theme.hpp"

namespace
{

TEST(QtRuntimeTheme, InitAppliesDefaultThemeBeforeFirstFrame)
{
    spectra::adapters::qt::QtRuntime rt;
    if (!rt.init())
        GTEST_SKIP() << "QtRuntime::init() failed (no Vulkan-capable Qt platform/ICD)";

    EXPECT_EQ(rt.theme_manager().current_theme_name(),
              spectra::adapters::qt::QtRuntime::DEFAULT_THEME);
    EXPECT_EQ(spectra::ui::ThemeManager::instance().current_theme_name(),
              spectra::adapters::qt::QtRuntime::DEFAULT_THEME);

    // ImGuiIntegration::init() calls ensure_initialized() when the first window attaches;
    // it must not replace the theme init() chose.
    rt.theme_manager().ensure_initialized();
    EXPECT_EQ(rt.theme_manager().current_theme_name(),
              spectra::adapters::qt::QtRuntime::DEFAULT_THEME);

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
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

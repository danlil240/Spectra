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

// Issue #110: native Qt chrome (status bar) must follow the theme, including later
// theme switches made in the canvas UI.
TEST(QtRuntimeTheme, ThemeCallbackDeliversPaletteAndFollowsChanges)
{
    spectra::adapters::qt::QtRuntime rt;
    int                              calls = 0;
    QPalette                         last;
    // Registered before init(): delivered as soon as init() has applied the theme.
    rt.set_theme_changed_callback(
        [&](const QPalette& p)
        {
            ++calls;
            last = p;
        });
    EXPECT_EQ(calls, 0);
    if (!rt.init())
        GTEST_SKIP() << "QtRuntime::init() failed (no Vulkan-capable Qt platform/ICD)";

    ASSERT_EQ(calls, 1);
    EXPECT_EQ(last.color(QPalette::Window), rt.theme_palette().color(QPalette::Window));
    EXPECT_LT(last.color(QPalette::Window).lightnessF(), 0.25);   // dark default

    // No change, no call.
    EXPECT_FALSE(rt.poll_theme_change());
    EXPECT_EQ(calls, 1);

    rt.theme_manager().set_theme("light");
    EXPECT_TRUE(rt.poll_theme_change());
    EXPECT_EQ(calls, 2);
    EXPECT_GT(last.color(QPalette::Window).lightnessF(), 0.75);

    // Re-registering delivers the current palette straight away.
    int second = 0;
    rt.set_theme_changed_callback([&](const QPalette&) { ++second; });
    EXPECT_EQ(second, 1);
    rt.set_theme_changed_callback(nullptr);
    rt.theme_manager().set_theme("dark");
    EXPECT_FALSE(rt.poll_theme_change());

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

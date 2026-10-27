// test_example_plugins.cpp — Loads the shipped example plug-ins from examples/plugins/
// through PluginManager and checks that each one actually registers its feature.
//
// Regression test for plug-ins that returned success while registering nothing,
// because their version guards tested api_version_minor on its own (>= 1/2/3) and
// a v2.0 host reports minor 0.

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "io/export_registry.hpp"
#include "math/data_transform.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/overlay/overlay_registry.hpp"
#include "ui/workspace/plugin_api.hpp"

#ifndef SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH
    #define SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH ""
#endif
#ifndef SPECTRA_EXAMPLE_PLUGIN_OVERLAY_CROSSHAIR_PATH
    #define SPECTRA_EXAMPLE_PLUGIN_OVERLAY_CROSSHAIR_PATH ""
#endif
#ifndef SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH
    #define SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH ""
#endif

using namespace spectra;

namespace
{

class ExamplePluginTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        manager_.set_command_registry(&commands_);
        manager_.set_shortcut_manager(&shortcuts_);
        manager_.set_undo_manager(&undo_);
        manager_.set_transform_registry(&transforms_);
        manager_.set_overlay_registry(&overlays_);
        manager_.set_export_format_registry(&exports_);
    }

    void TearDown() override { manager_.unload_all(); }

    // Loads the plug-in; false when the build did not produce it or loading failed.
    bool load(const std::string& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
            return false;
        return manager_.load_plugin(path);
    }

    CommandRegistry      commands_;
    ShortcutManager      shortcuts_;
    UndoManager          undo_;
    TransformRegistry    transforms_;
    OverlayRegistry      overlays_;
    ExportFormatRegistry exports_;
    PluginManager        manager_;
};

}   // namespace

TEST_F(ExamplePluginTest, TransformSmoothRegistersMovingAverage)
{
    const std::string path = SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH;
    ASSERT_FALSE(path.empty()) << "transform_smooth plug-in path not configured";
    ASSERT_TRUE(load(path)) << path;

    DataTransform t;
    ASSERT_TRUE(transforms_.get_transform("Moving Average (5-pt)", t))
        << "plug-in loaded but did not register its transform";

    // Constant input is a fixed point of any moving average.
    std::vector<float> x_in = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    std::vector<float> y_in(x_in.size(), 2.5f);
    std::vector<float> x_out, y_out;
    t.apply_y(x_in, y_in, x_out, y_out);
    ASSERT_EQ(y_out.size(), y_in.size());
    for (float v : y_out)
        EXPECT_FLOAT_EQ(v, 2.5f);
}

TEST_F(ExamplePluginTest, OverlayCrosshairRegistersOverlay)
{
    const std::string path = SPECTRA_EXAMPLE_PLUGIN_OVERLAY_CROSSHAIR_PATH;
    ASSERT_FALSE(path.empty()) << "overlay_crosshair plug-in path not configured";
    ASSERT_TRUE(load(path)) << path;

    const auto names = overlays_.overlay_names();
    ASSERT_EQ(names.size(), 1u) << "plug-in loaded but did not register its overlay";
    EXPECT_EQ(names[0], "Crosshair");
}

TEST_F(ExamplePluginTest, ExportCsvRegistersCsvFormat)
{
    const std::string path = SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH;
    ASSERT_FALSE(path.empty()) << "export_csv plug-in path not configured";
    ASSERT_TRUE(load(path)) << path;

    const auto formats = exports_.available_formats();
    ASSERT_EQ(formats.size(), 1u) << "plug-in loaded but did not register its export format";
    EXPECT_EQ(formats[0].name, "CSV Data");
    EXPECT_EQ(formats[0].extension, "csv");
}

TEST_F(ExamplePluginTest, AllThreeLoadTogether)
{
    ASSERT_TRUE(load(SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH));
    ASSERT_TRUE(load(SPECTRA_EXAMPLE_PLUGIN_OVERLAY_CROSSHAIR_PATH));
    ASSERT_TRUE(load(SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH));
    EXPECT_EQ(manager_.plugins().size(), 3u);

    DataTransform t;
    EXPECT_TRUE(transforms_.get_transform("Moving Average (5-pt)", t));
    EXPECT_EQ(overlays_.count(), 1u);
    EXPECT_EQ(exports_.available_formats().size(), 1u);
}

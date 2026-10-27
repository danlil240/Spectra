// test_example_plugins.cpp — Loads the shipped example plug-ins from examples/plugins/
// through PluginManager and checks that each one actually registers its feature.
//
// Regression test for plug-ins that returned success while registering nothing,
// because their version guards tested api_version_minor on its own (>= 1/2/3) and
// a v2.0 host reports minor 0.  Also checks that unloading removes, and disabling
// deactivates, what the examples registered (they never unregister themselves).

#include <filesystem>
#include <memory>
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
        manager_.set_transform_registry(transforms_.get());
        manager_.set_overlay_registry(overlays_.get());
        manager_.set_export_format_registry(exports_.get());
    }

    // Unload with the registries still attached: PluginManager must remove the
    // plug-ins' transforms, overlays and export formats itself before closing the
    // libraries (the shipped examples do not unregister anything in shutdown).
    void TearDown() override { manager_.unload_all(); }

    // Loads the plug-in; false when the build did not produce it or loading failed.
    bool load(const std::string& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
            return false;
        return manager_.load_plugin(path);
    }

    CommandRegistry                       commands_;
    ShortcutManager                       shortcuts_;
    UndoManager                           undo_;
    std::unique_ptr<TransformRegistry>    transforms_ = std::make_unique<TransformRegistry>();
    std::unique_ptr<OverlayRegistry>      overlays_   = std::make_unique<OverlayRegistry>();
    std::unique_ptr<ExportFormatRegistry> exports_    = std::make_unique<ExportFormatRegistry>();
    PluginManager                         manager_;
};

}   // namespace

TEST_F(ExamplePluginTest, TransformSmoothRegistersMovingAverage)
{
    const std::string path = SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH;
    ASSERT_FALSE(path.empty()) << "transform_smooth plug-in path not configured";
    ASSERT_TRUE(load(path)) << path;

    DataTransform t;
    ASSERT_TRUE(transforms_->get_transform("Moving Average (5-pt)", t))
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

    const auto names = overlays_->overlay_names();
    ASSERT_EQ(names.size(), 1u) << "plug-in loaded but did not register its overlay";
    EXPECT_EQ(names[0], "Crosshair");
}

TEST_F(ExamplePluginTest, ExportCsvRegistersCsvFormat)
{
    const std::string path = SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH;
    ASSERT_FALSE(path.empty()) << "export_csv plug-in path not configured";
    ASSERT_TRUE(load(path)) << path;

    const auto formats = exports_->available_formats();
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
    EXPECT_TRUE(transforms_->get_transform("Moving Average (5-pt)", t));
    EXPECT_EQ(overlays_->count(), 1u);
    EXPECT_EQ(exports_->available_formats().size(), 1u);
}

namespace
{

constexpr const char* kSmoothName    = "Transform: Moving Average";
constexpr const char* kCrosshairName = "Overlay: Crosshair";
constexpr const char* kCsvName       = "Export: CSV Data";
constexpr const char* kSmoothXform   = "Moving Average (5-pt)";

bool has_transform(const TransformRegistry& reg, const std::string& name)
{
    for (const auto& n : reg.available_transforms())
    {
        if (n == name)
            return true;
    }
    return false;
}

}   // namespace

TEST_F(ExamplePluginTest, UnloadRemovesShippedRegistrationsAndReloads)
{
    const std::vector<std::string> paths = {SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH,
                                            SPECTRA_EXAMPLE_PLUGIN_OVERLAY_CROSSHAIR_PATH,
                                            SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH};
    for (int round = 0; round < 2; ++round)
    {
        SCOPED_TRACE(round);
        for (const auto& path : paths)
            ASSERT_TRUE(load(path)) << path;
        ASSERT_EQ(manager_.plugin_count(), 3u);

        const void* smooth    = manager_.find_plugin(kSmoothName)->owner;
        const void* crosshair = manager_.find_plugin(kCrosshairName)->owner;
        const void* csv       = manager_.find_plugin(kCsvName)->owner;
        ASSERT_NE(smooth, nullptr);
        ASSERT_NE(crosshair, nullptr);
        ASSERT_NE(csv, nullptr);
        EXPECT_EQ(transforms_->count_owned(smooth), 1u);
        EXPECT_EQ(overlays_->count_owned(crosshair), 1u);
        EXPECT_EQ(exports_->count_owned(csv), 1u);

        ASSERT_TRUE(manager_.unload_plugin(kSmoothName));
        EXPECT_EQ(transforms_->count_owned(smooth), 0u);
        EXPECT_FALSE(has_transform(*transforms_, kSmoothXform));
        DataTransform t;
        EXPECT_FALSE(transforms_->get_transform(kSmoothXform, t));

        ASSERT_TRUE(manager_.unload_plugin(kCrosshairName));
        EXPECT_EQ(overlays_->count_owned(crosshair), 0u);
        EXPECT_EQ(overlays_->count(), 0u);
        OverlayDrawContext ctx{};
        overlays_->draw_all(ctx);   // must not reach the unloaded library

        ASSERT_TRUE(manager_.unload_plugin(kCsvName));
        EXPECT_EQ(exports_->count_owned(csv), 0u);
        EXPECT_EQ(exports_->count(), 0u);
        EXPECT_FALSE(exports_->export_figure("CSV Data", "{}", nullptr, 0, 0, "unused.csv"));

        EXPECT_EQ(manager_.plugin_count(), 0u);
    }
}

TEST_F(ExamplePluginTest, DisableGatesShippedExtensions)
{
    ASSERT_TRUE(load(SPECTRA_EXAMPLE_PLUGIN_TRANSFORM_SMOOTH_PATH));
    ASSERT_TRUE(load(SPECTRA_EXAMPLE_PLUGIN_OVERLAY_CROSSHAIR_PATH));
    ASSERT_TRUE(load(SPECTRA_EXAMPLE_PLUGIN_EXPORT_CSV_PATH));

    const auto out_dir = std::filesystem::temp_directory_path() / "spectra_example_plugin_gate";
    std::filesystem::create_directories(out_dir);
    const std::string csv_path = (out_dir / "gate.csv").string();
    const std::string json     = R"({"axes":[]})";

    for (const char* name : {kSmoothName, kCrosshairName, kCsvName})
        manager_.set_plugin_enabled(name, false);

    DataTransform t;
    EXPECT_FALSE(has_transform(*transforms_, kSmoothXform));
    EXPECT_FALSE(transforms_->get_transform(kSmoothXform, t));
    EXPECT_TRUE(exports_->available_formats().empty());
    std::filesystem::remove(csv_path);
    EXPECT_FALSE(exports_->export_figure("CSV Data", json, nullptr, 0, 0, csv_path));
    EXPECT_FALSE(std::filesystem::exists(csv_path));
    // The disabled overlay stays registered (it is removed on unload) but is skipped.
    EXPECT_EQ(overlays_->count(), 1u);

    for (const char* name : {kSmoothName, kCrosshairName, kCsvName})
        manager_.set_plugin_enabled(name, true);

    EXPECT_TRUE(has_transform(*transforms_, kSmoothXform));
    EXPECT_TRUE(transforms_->get_transform(kSmoothXform, t));
    ASSERT_EQ(exports_->available_formats().size(), 1u);
    EXPECT_TRUE(exports_->export_figure("CSV Data", json, nullptr, 0, 0, csv_path));
    EXPECT_TRUE(std::filesystem::exists(csv_path));
    std::filesystem::remove_all(out_dir);
}

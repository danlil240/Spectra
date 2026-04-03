#include <filesystem>
#include <fstream>
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

using namespace spectra;

#ifndef SPECTRA_MOCK_INTEGRATION_PLUGIN_PATH
    #define SPECTRA_MOCK_INTEGRATION_PLUGIN_PATH ""
#endif

namespace
{

bool contains_name(const std::vector<std::string>& values, const std::string& name)
{
    for (const auto& value : values)
    {
        if (value == name)
            return true;
    }
    return false;
}

bool has_export_format(const ExportFormatRegistry& reg, const std::string& name)
{
    const auto formats = reg.available_formats();
    for (const auto& format : formats)
    {
        if (format.name == name)
            return true;
    }
    return false;
}

}   // namespace

class PluginIntegrationTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        plugin_path_ = SPECTRA_MOCK_INTEGRATION_PLUGIN_PATH;
        if (plugin_path_.empty() || !std::filesystem::exists(plugin_path_))
        {
            GTEST_SKIP() << "Mock integration plugin not found at: " << plugin_path_;
        }

        mgr_.set_command_registry(&cmd_reg_);
        mgr_.set_shortcut_manager(&shortcut_mgr_);
        mgr_.set_undo_manager(&undo_mgr_);
        mgr_.set_transform_registry(&transform_reg_);
        mgr_.set_overlay_registry(&overlay_reg_);
        mgr_.set_export_format_registry(&export_reg_);
    }

    void TearDown() override { mgr_.unload_all(); }

    std::string          plugin_path_;
    CommandRegistry      cmd_reg_;
    ShortcutManager      shortcut_mgr_;
    UndoManager          undo_mgr_;
    TransformRegistry    transform_reg_;
    OverlayRegistry      overlay_reg_;
    ExportFormatRegistry export_reg_;
    PluginManager        mgr_;
};

TEST_F(PluginIntegrationTest, LoadUnloadReloadRegistersAndCleansAllRegistries)
{
    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    ASSERT_EQ(mgr_.plugin_count(), 1u);

    DataTransform transform;
    EXPECT_TRUE(transform_reg_.get_transform("IntegrationShift", transform));
    EXPECT_TRUE(contains_name(overlay_reg_.overlay_names(), "IntegrationOverlay"));
    EXPECT_TRUE(has_export_format(export_reg_, "IntegrationExport"));

    const auto tmp_path =
        (std::filesystem::temp_directory_path() / "spectra_plugin_integration.ix").string();
    EXPECT_TRUE(export_reg_.export_figure("IntegrationExport", "{}", nullptr, 0, 0, tmp_path));

    std::ifstream f(tmp_path);
    ASSERT_TRUE(f.is_open());
    std::string line;
    std::getline(f, line);
    EXPECT_EQ(line, "integration-export,2");
    f.close();
    std::filesystem::remove(tmp_path);

    ASSERT_TRUE(mgr_.unload_plugin("MockIntegrationPlugin"));
    EXPECT_FALSE(transform_reg_.get_transform("IntegrationShift", transform));
    EXPECT_FALSE(contains_name(overlay_reg_.overlay_names(), "IntegrationOverlay"));
    EXPECT_FALSE(has_export_format(export_reg_, "IntegrationExport"));

    ASSERT_TRUE(mgr_.load_plugin(plugin_path_));
    EXPECT_TRUE(transform_reg_.get_transform("IntegrationShift", transform));
    EXPECT_TRUE(contains_name(overlay_reg_.overlay_names(), "IntegrationOverlay"));
    EXPECT_TRUE(has_export_format(export_reg_, "IntegrationExport"));
}

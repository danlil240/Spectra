// test_display_registry.cpp — Phase 1 display foundation tests.

#include <gtest/gtest.h>

#include "display/display_registry.hpp"
#include "display/grid_display.hpp"
#include "display/image_display.hpp"
#include "display/laserscan_display.hpp"
#include "display/marker_display.hpp"
#include "display/path_display.hpp"
#include "display/pointcloud_display.hpp"
#include "display/pose_display.hpp"
#include "display/robot_model_display.hpp"
#include "display/tf_display.hpp"
#include "scene/scene_manager.hpp"

using namespace spectra::adapters::ros2;

TEST(DisplayRegistry, RegisterAndCreateGridDisplay)
{
    DisplayRegistry registry;
    ASSERT_TRUE(registry.register_display<GridDisplay>());

    auto display = registry.create("grid");
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(display->type_id(), "grid");
    EXPECT_EQ(registry.list_types().size(), 1u);
}

TEST(DisplayRegistry, DuplicateRegistrationRejected)
{
    DisplayRegistry registry;
    EXPECT_TRUE(registry.register_display<GridDisplay>());
    EXPECT_FALSE(registry.register_display<GridDisplay>());
}

TEST(DisplayRegistry, RegistersPhaseTwoDisplays)
{
    DisplayRegistry registry;
    EXPECT_TRUE(registry.register_display<TfDisplay>());
    EXPECT_TRUE(registry.register_display<MarkerDisplay>());
    EXPECT_TRUE(registry.register_display<PointCloudDisplay>());
    EXPECT_TRUE(registry.register_display<LaserScanDisplay>());
    EXPECT_NE(registry.create("tf"), nullptr);
    EXPECT_NE(registry.create("marker"), nullptr);
    EXPECT_NE(registry.create("pointcloud"), nullptr);
    EXPECT_NE(registry.create("laserscan"), nullptr);
}

TEST(DisplayRegistry, RegistersPhaseFourDisplays)
{
    DisplayRegistry registry;
    EXPECT_TRUE(registry.register_display<ImageDisplay>());
    EXPECT_TRUE(registry.register_display<PathDisplay>());
    EXPECT_TRUE(registry.register_display<PoseDisplay>());
    EXPECT_NE(registry.create("image"), nullptr);
    EXPECT_NE(registry.create("path"), nullptr);
    EXPECT_NE(registry.create("pose"), nullptr);
}

TEST(DisplayRegistry, RegistersRobotModelDisplay)
{
    DisplayRegistry registry;
    EXPECT_TRUE(registry.register_display<RobotModelDisplay>());
    auto display = registry.create("robot_model");
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(display->display_name(), "Robot Model");
}

TEST(GridDisplay, ConfigBlobRoundTrip)
{
    GridDisplay display;
    display.deserialize_config_blob("cell_size=0.500;cell_count=42;plane=xy");

    EXPECT_FLOAT_EQ(display.cell_size(), 0.5f);
    EXPECT_EQ(display.cell_count(), 42);
    EXPECT_EQ(display.plane(), "xy");
    EXPECT_NE(display.serialize_config_blob().find("cell_count=42"), std::string::npos);
}

TEST(GridDisplay, SubmitRenderablesAddsSceneEntity)
{
    GridDisplay display;
    SceneManager scene;

    display.submit_renderables(scene);
    ASSERT_EQ(scene.entity_count(), 1u);
    EXPECT_EQ(scene.entities()[0].type, "grid");
}

TEST(SceneManager, ClearRemovesEntities)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "Grid"});
    scene.add_entity(SceneEntity{.type = "marker", .label = "Marker"});
    ASSERT_EQ(scene.entity_count(), 2u);
    scene.clear();
    EXPECT_EQ(scene.entity_count(), 0u);
}

TEST(SceneManager, SelectionPersistsByLabelAcrossRebuild)
{
    SceneManager scene;
    scene.add_entity(SceneEntity{.type = "grid", .label = "Grid"});
    scene.add_entity(SceneEntity{.type = "marker", .label = "Marker"});
    scene.set_selected_index(1u);

    scene.clear();
    scene.add_entity(SceneEntity{.type = "grid", .label = "Grid"});
    scene.add_entity(SceneEntity{.type = "marker", .label = "Marker"});

    ASSERT_TRUE(scene.selected_index().has_value());
    EXPECT_EQ(*scene.selected_index(), 1u);
    ASSERT_NE(scene.selected_entity(), nullptr);
    EXPECT_EQ(scene.selected_entity()->label, "Marker");
}

// --- Phase 6 expanded coverage ---

TEST(DisplayRegistry, CreateUnknownTypeReturnsNull)
{
    DisplayRegistry registry;
    registry.register_display<GridDisplay>();
    EXPECT_EQ(registry.create("nonexistent"), nullptr);
}

TEST(DisplayRegistry, ListTypesSortedAlphabetically)
{
    DisplayRegistry registry;
    registry.register_display<TfDisplay>();
    registry.register_display<GridDisplay>();
    registry.register_display<MarkerDisplay>();

    const auto types = registry.list_types();
    ASSERT_EQ(types.size(), 3u);
    // Should be sorted: grid, marker, tf
    EXPECT_EQ(types[0].type_id, "grid");
    EXPECT_EQ(types[1].type_id, "marker");
    EXPECT_EQ(types[2].type_id, "tf");
}

TEST(DisplayRegistry, CompatibleMessageTypes)
{
    DisplayRegistry registry;
    registry.register_display<PointCloudDisplay>();

    const auto types = registry.list_types();
    ASSERT_EQ(types.size(), 1u);
    ASSERT_FALSE(types[0].compatible_types.empty());
    EXPECT_EQ(types[0].compatible_types[0], "sensor_msgs/msg/PointCloud2");
}

TEST(DisplayRegistry, AllNineDisplaysRegisterable)
{
    DisplayRegistry registry;
    EXPECT_TRUE(registry.register_display<GridDisplay>());
    EXPECT_TRUE(registry.register_display<TfDisplay>());
    EXPECT_TRUE(registry.register_display<MarkerDisplay>());
    EXPECT_TRUE(registry.register_display<PointCloudDisplay>());
    EXPECT_TRUE(registry.register_display<LaserScanDisplay>());
    EXPECT_TRUE(registry.register_display<ImageDisplay>());
    EXPECT_TRUE(registry.register_display<PathDisplay>());
    EXPECT_TRUE(registry.register_display<PoseDisplay>());
    EXPECT_TRUE(registry.register_display<RobotModelDisplay>());
    EXPECT_EQ(registry.list_types().size(), 9u);
}

TEST(DisplayRegistry, CreatedDisplayHasCorrectMetadata)
{
    DisplayRegistry registry;
    registry.register_display<PointCloudDisplay>();
    auto display = registry.create("pointcloud");
    ASSERT_NE(display, nullptr);
    EXPECT_EQ(display->type_id(), "pointcloud");
    EXPECT_EQ(display->display_name(), "PointCloud2");
    EXPECT_FALSE(display->icon().empty());
}

TEST(DisplayRegistry, GridDisplayDefaultValues)
{
    GridDisplay grid;
    EXPECT_FLOAT_EQ(grid.cell_size(), 1.0f);
    EXPECT_EQ(grid.cell_count(), 20);
    EXPECT_EQ(grid.plane(), "xz");
}

TEST(GridDisplay, ConfigBlobEmptyStringNoOp)
{
    GridDisplay display;
    display.deserialize_config_blob("");
    EXPECT_FLOAT_EQ(display.cell_size(), 1.0f);
    EXPECT_EQ(display.cell_count(), 20);
}

// Phase 5 robot model display tests — URDF collision shapes into scene entities.

#include <gtest/gtest.h>

#include "display/robot_model_display.hpp"
#include "scene/scene_manager.hpp"

using namespace spectra::adapters::ros2;

TEST(RobotModelDisplay, ConfigBlobRoundTrip)
{
    RobotModelDisplay display;
    display.deserialize_config_blob("parameter=my_robot_description;show_collision_shapes=0");

    const std::string blob = display.serialize_config_blob();
    EXPECT_NE(blob.find("parameter=my_robot_description"), std::string::npos);
    EXPECT_NE(blob.find("show_collision_shapes=0"), std::string::npos);
}

TEST(RobotModelDisplay, ParsesUrdfAndSubmitsCollisionEntities)
{
    RobotModelDisplay display;
    DisplayContext context;
    SceneManager scene;

    display.on_enable(context);
    display.set_robot_description_xml_for_test(R"(
<robot name="demo">
  <link name="base">
    <collision name="base_box">
      <origin xyz="1 0 0"/>
      <geometry><box size="1 2 3"/></geometry>
    </collision>
    <collision>
      <geometry><sphere radius="0.5"/></geometry>
    </collision>
  </link>
  <link name="arm">
    <collision>
      <geometry><cylinder radius="0.25" length="2.0"/></geometry>
    </collision>
  </link>
</robot>)");

    display.on_update(0.016f);
    ASSERT_EQ(display.collision_count(), 3u);
    display.submit_renderables(scene);

    ASSERT_EQ(scene.entity_count(), 3u);
    EXPECT_EQ(scene.entities()[0].type, "robot_box");
    EXPECT_EQ(scene.entities()[0].frame_id, "base");
    EXPECT_EQ(scene.entities()[0].display_name, "Robot Model");
    EXPECT_DOUBLE_EQ(scene.entities()[0].transform.translation.x, 1.0);
    EXPECT_EQ(scene.entities()[1].type, "robot_sphere");
    EXPECT_EQ(scene.entities()[2].type, "robot_cylinder");
}

TEST(RobotModelDisplay, InvalidUrdfSetsErrorState)
{
    RobotModelDisplay display;
    DisplayContext context;
    SceneManager scene;

    display.on_enable(context);
    display.set_robot_description_xml_for_test("<robot><link></robot>");
    display.on_update(0.016f);
    EXPECT_EQ(display.status(), DisplayStatus::Error);
    EXPECT_TRUE(display.robot_description().links.empty());
    display.submit_renderables(scene);
    EXPECT_EQ(scene.entity_count(), 0u);
}

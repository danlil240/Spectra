// Phase 5 URDF parser tests — minimal collision-geometry robot descriptions.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "urdf/urdf_parser.hpp"

using namespace spectra::adapters::ros2;

namespace
{
constexpr double kEps = 1e-6;
}

TEST(UrdfParser, ParsesBoxCylinderSphereCollisions)
{
    const std::string xml = R"(
<robot name="test_bot">
  <link name="base_link">
    <collision name="base_box">
      <origin xyz="1 2 3" rpy="0 0 1.57079632679"/>
      <geometry><box size="1 2 3"/></geometry>
    </collision>
    <collision>
      <geometry><cylinder radius="0.25" length="1.5"/></geometry>
    </collision>
  </link>
  <link name="tool_link">
    <collision>
      <geometry><sphere radius="0.125"/></geometry>
    </collision>
  </link>
</robot>)";

    const UrdfParseResult result = UrdfParser::parse_string(xml);
    ASSERT_TRUE(result) << result.error;
    ASSERT_EQ(result.robot.links.size(), 2u);

    const UrdfLink* base_link = result.robot.find_link("base_link");
    ASSERT_NE(base_link, nullptr);
    ASSERT_EQ(base_link->collisions.size(), 2u);
    EXPECT_EQ(base_link->collisions[0].geometry.type, UrdfGeometryType::Box);
    EXPECT_NEAR(base_link->collisions[0].geometry.box_size.x, 1.0, kEps);
    EXPECT_NEAR(base_link->collisions[0].geometry.box_size.y, 2.0, kEps);
    EXPECT_NEAR(base_link->collisions[0].geometry.box_size.z, 3.0, kEps);
    EXPECT_NEAR(base_link->collisions[0].origin.translation.x, 1.0, kEps);
    EXPECT_NEAR(base_link->collisions[0].origin.translation.y, 2.0, kEps);
    EXPECT_NEAR(base_link->collisions[0].origin.translation.z, 3.0, kEps);

    EXPECT_EQ(base_link->collisions[1].geometry.type, UrdfGeometryType::Cylinder);
    EXPECT_NEAR(base_link->collisions[1].geometry.radius, 0.25, kEps);
    EXPECT_NEAR(base_link->collisions[1].geometry.length, 1.5, kEps);

    const UrdfLink* tool_link = result.robot.find_link("tool_link");
    ASSERT_NE(tool_link, nullptr);
    ASSERT_EQ(tool_link->collisions.size(), 1u);
    EXPECT_EQ(tool_link->collisions[0].geometry.type, UrdfGeometryType::Sphere);
    EXPECT_NEAR(tool_link->collisions[0].geometry.radius, 0.125, kEps);
}

TEST(UrdfParser, ParsesJointTypesAxisAndLimits)
{
    const std::string xml = R"(
<robot name="joint_bot">
  <link name="base"/>
  <link name="arm"/>
  <link name="slider"/>
  <link name="tip"/>
  <joint name="arm_joint" type="revolute">
    <parent link="base"/>
    <child link="arm"/>
    <origin xyz="0 0 1" rpy="0 0 0"/>
    <axis xyz="0 1 0"/>
    <limit lower="-1.57" upper="1.57" effort="12" velocity="3"/>
  </joint>
  <joint name="slider_joint" type="prismatic">
    <parent link="arm"/>
    <child link="slider"/>
    <axis xyz="1 0 0"/>
    <limit lower="0.0" upper="0.4" effort="5" velocity="1"/>
  </joint>
  <joint name="tip_joint" type="fixed">
    <parent link="slider"/>
    <child link="tip"/>
  </joint>
</robot>)";

    const UrdfParseResult result = UrdfParser::parse_string(xml);
    ASSERT_TRUE(result) << result.error;
    ASSERT_EQ(result.robot.joints.size(), 3u);

    const UrdfJoint* arm_joint = result.robot.find_joint("arm_joint");
    ASSERT_NE(arm_joint, nullptr);
    EXPECT_EQ(arm_joint->type, UrdfJointType::Revolute);
    EXPECT_EQ(arm_joint->parent_link, "base");
    EXPECT_EQ(arm_joint->child_link, "arm");
    EXPECT_NEAR(arm_joint->origin.translation.z, 1.0, kEps);
    EXPECT_NEAR(arm_joint->axis.x, 0.0, kEps);
    EXPECT_NEAR(arm_joint->axis.y, 1.0, kEps);
    EXPECT_TRUE(arm_joint->limits.has_position_limits);
    EXPECT_NEAR(arm_joint->limits.lower, -1.57, kEps);
    EXPECT_NEAR(arm_joint->limits.upper, 1.57, kEps);
    EXPECT_NEAR(arm_joint->limits.effort, 12.0, kEps);
    EXPECT_NEAR(arm_joint->limits.velocity, 3.0, kEps);

    const UrdfJoint* slider_joint = result.robot.find_joint("slider_joint");
    ASSERT_NE(slider_joint, nullptr);
    EXPECT_EQ(slider_joint->type, UrdfJointType::Prismatic);
    EXPECT_NEAR(slider_joint->axis.x, 1.0, kEps);

    const UrdfJoint* tip_joint = result.robot.find_joint("tip_joint");
    ASSERT_NE(tip_joint, nullptr);
    EXPECT_EQ(tip_joint->type, UrdfJointType::Fixed);
    EXPECT_FALSE(tip_joint->limits.has_position_limits);
}

TEST(UrdfParser, UnsupportedMeshBecomesWarning)
{
    const std::string xml = R"(
<robot name="mesh_bot">
  <link name="base">
    <collision>
      <geometry><mesh filename="package://robot/base.stl"/></geometry>
    </collision>
  </link>
</robot>)";

    const UrdfParseResult result = UrdfParser::parse_string(xml);
    ASSERT_TRUE(result) << result.error;
    ASSERT_EQ(result.robot.links.size(), 1u);
    EXPECT_TRUE(result.robot.links[0].collisions.empty());
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_NE(result.warnings[0].find("unsupported <mesh>"), std::string::npos);
}

TEST(UrdfParser, InvalidAxisFallsBackToDefaultAndWarns)
{
    const std::string xml = R"(
<robot name="axis_bot">
  <link name="base"/>
  <link name="arm"/>
  <joint name="bad_axis" type="continuous">
    <parent link="base"/>
    <child link="arm"/>
    <axis xyz="oops"/>
  </joint>
</robot>)";

    const UrdfParseResult result = UrdfParser::parse_string(xml);
    ASSERT_TRUE(result) << result.error;
    ASSERT_EQ(result.robot.joints.size(), 1u);
    EXPECT_NEAR(result.robot.joints[0].axis.x, 1.0, kEps);
    EXPECT_NEAR(result.robot.joints[0].axis.y, 0.0, kEps);
    EXPECT_NEAR(result.robot.joints[0].axis.z, 0.0, kEps);
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_NE(result.warnings[0].find("invalid axis"), std::string::npos);
}

TEST(UrdfParser, MissingRobotRootFails)
{
    const UrdfParseResult result = UrdfParser::parse_string("<link name=\"base\"/>");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("<robot>"), std::string::npos);
}

TEST(UrdfParser, MissingJointParentOrChildFails)
{
    const std::string xml = R"(
<robot name="broken_bot">
  <link name="base"/>
  <link name="arm"/>
  <joint name="bad_joint" type="fixed">
    <parent link="base"/>
  </joint>
</robot>)";

    const UrdfParseResult result = UrdfParser::parse_string(xml);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("missing <parent> or <child>"), std::string::npos);
}

TEST(UrdfParser, ParseFileReadsFromDisk)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "spectra_test_robot.urdf";
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << R"(<robot name="file_bot"><link name="base"/></robot>)";
    }

    const UrdfParseResult result = UrdfParser::parse_file(path.string());
    std::filesystem::remove(path);

    ASSERT_TRUE(result) << result.error;
    EXPECT_EQ(result.robot.name, "file_bot");
    ASSERT_EQ(result.robot.links.size(), 1u);
    EXPECT_EQ(result.robot.links[0].name, "base");
}

#include <gtest/gtest.h>

#include "resources/ros_package_resolver.hpp"

namespace spectra::adapters::ros2
{
namespace
{

TEST(RosPackageResolverTest, FileUriAbsolutePath)
{
    const auto result = resolve_ros_resource_uri("file:///etc/hosts");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.path, "/etc/hosts");
}

TEST(RosPackageResolverTest, PlainPathRelativeFailsWhenMissing)
{
    const auto result = resolve_ros_resource_uri("/tmp/spectra_ros_missing_mesh.stl");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

}   // namespace
}   // namespace spectra::adapters::ros2

#include <gtest/gtest.h>

#include "tools/measure_tool.hpp"

namespace spectra::adapters::ros2
{
namespace
{

TEST(MeasureToolTest, DistanceBetweenTwoPoints)
{
    MeasureTool tool;
    tool.set_active(true);
    tool.click_point({0.0, 0.0, 0.0});
    tool.click_point({3.0, 4.0, 0.0});
    EXPECT_TRUE(tool.has_result());
    EXPECT_NEAR(tool.distance(), 5.0, 1e-6);
}

TEST(MeasureToolTest, ResetClearsResult)
{
    MeasureTool tool;
    tool.set_active(true);
    tool.click_point({0.0, 0.0, 0.0});
    tool.click_point({1.0, 0.0, 0.0});
    tool.reset();
    EXPECT_FALSE(tool.has_result());
}

}   // namespace
}   // namespace spectra::adapters::ros2

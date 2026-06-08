#include <gtest/gtest.h>

#include "ros_session.hpp"

using namespace spectra::adapters::ros2;

TEST(RosSessionMerge, SkipsDuplicateSubscriptions)
{
    RosSession base;
    base.subscriptions.push_back(
        {.topic = "/cmd_vel", .field_path = "linear.x", .subplot_slot = 1});

    RosSession incoming;
    incoming.subscriptions.push_back(
        {.topic = "/cmd_vel", .field_path = "linear.x", .subplot_slot = 1});
    incoming.subscriptions.push_back(
        {.topic = "/cmd_vel", .field_path = "linear.y", .subplot_slot = 1});

    const RosSession merged = merge_sessions(base, incoming);
    ASSERT_EQ(merged.subscriptions.size(), 2u);
    EXPECT_EQ(merged.subscriptions[1].field_path, "linear.y");
}

TEST(RosSessionMerge, KeepsBaseLayoutPanels)
{
    RosSession base;
    base.panels.plot_area  = true;
    base.panels.topic_echo = false;
    base.time_window_s     = 45.0;

    RosSession incoming;
    incoming.panels.plot_area  = false;
    incoming.panels.topic_echo = true;
    incoming.time_window_s     = 10.0;

    const RosSession merged = merge_sessions(base, incoming);
    EXPECT_TRUE(merged.panels.plot_area);
    EXPECT_FALSE(merged.panels.topic_echo);
    EXPECT_DOUBLE_EQ(merged.time_window_s, 45.0);
}

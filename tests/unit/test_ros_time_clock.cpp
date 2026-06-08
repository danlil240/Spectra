#include <gtest/gtest.h>

#include "ros_time_clock.hpp"

using namespace spectra::adapters::ros2;

TEST(RosTimeClock, LiveModeDefaults)
{
    RosTimeClock clock;
    EXPECT_FALSE(clock.is_bag_mode());
    EXPECT_DOUBLE_EQ(clock.plot_now_sec(), 0.0);
    EXPECT_FALSE(clock.is_playing);
}

TEST(RosTimeClock, BagModeTracksPlayhead)
{
    RosTimeClock clock;
    clock.enter_bag_mode(120.0, 1'000'000'000LL);
    EXPECT_TRUE(clock.is_bag_mode());
    EXPECT_DOUBLE_EQ(clock.bag_duration_sec, 120.0);

    clock.update_bag_transport(42.5, true, 2.0);
    EXPECT_DOUBLE_EQ(clock.playhead_sec, 42.5);
    EXPECT_DOUBLE_EQ(clock.plot_now_sec(), 42.5);
    EXPECT_TRUE(clock.is_playing);
    EXPECT_DOUBLE_EQ(clock.rate, 2.0);
}

TEST(RosTimeClock, EnterLiveResetsBagState)
{
    RosTimeClock clock;
    clock.enter_bag_mode(10.0, 0);
    clock.update_bag_transport(5.0, true, 1.0);
    clock.enter_live_mode();
    EXPECT_FALSE(clock.is_bag_mode());
    EXPECT_DOUBLE_EQ(clock.playhead_sec, 0.0);
    EXPECT_FALSE(clock.is_playing);
}

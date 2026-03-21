#include <gtest/gtest.h>

#include "ui/app/animation_tick_gate.hpp"

#include <chrono>

using namespace spectra;

TEST(AnimationTickGate, UnscheduledGateFallsBackToIdleTimeout)
{
    AnimationTickGate           gate;
    AnimationTickGate::TimePoint now{};

    EXPECT_TRUE(gate.should_tick(now));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(now, 0.1), 0.1);
}

TEST(AnimationTickGate, ScheduledGateWaitsUntilNextFrameDeadline)
{
    AnimationTickGate           gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 60.0f);

    auto half_frame = start + std::chrono::milliseconds(10);
    EXPECT_FALSE(gate.should_tick(half_frame));
    EXPECT_NEAR(gate.wait_timeout_seconds(half_frame, 0.1), 1.0 / 60.0 - 0.010, 1e-4);
}

TEST(AnimationTickGate, ExpiredDeadlineTicksImmediately)
{
    AnimationTickGate           gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 60.0f);

    auto overdue = start + std::chrono::milliseconds(20);
    EXPECT_TRUE(gate.should_tick(overdue));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(overdue, 0.1), 0.0);
}

TEST(AnimationTickGate, InvalidFpsClearsSchedule)
{
    AnimationTickGate           gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 60.0f);
    gate.schedule_next(start, 0.0f);

    EXPECT_TRUE(gate.should_tick(start));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(start, 0.1), 0.1);
}

TEST(AnimationTickGate, AccumulatesSkippedDtUntilConsumed)
{
    AnimationTickGate gate;

    gate.accumulate_dt(0.004f);
    gate.accumulate_dt(0.006f);

    EXPECT_FLOAT_EQ(gate.accumulated_dt(), 0.010f);
    EXPECT_FLOAT_EQ(gate.consume_accumulated_dt(), 0.010f);
    EXPECT_FLOAT_EQ(gate.accumulated_dt(), 0.0f);
}

TEST(AnimationTickGate, ClearResetsPendingDtAndSchedule)
{
    AnimationTickGate           gate;
    AnimationTickGate::TimePoint start{};

    gate.accumulate_dt(0.02f);
    gate.schedule_next(start, 60.0f);
    gate.clear();

    EXPECT_TRUE(gate.should_tick(start));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(start, 0.1), 0.1);
    EXPECT_FLOAT_EQ(gate.accumulated_dt(), 0.0f);
}

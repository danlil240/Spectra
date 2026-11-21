#include <gtest/gtest.h>

#include "ui/app/animation_tick_gate.hpp"

#include <chrono>

using namespace spectra;

TEST(AnimationTickGate, UnscheduledGateFallsBackToIdleTimeout)
{
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint now{};

    EXPECT_TRUE(gate.should_tick(now));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(now, 0.1), 0.1);
}

TEST(AnimationTickGate, ScheduledGateWaitsUntilNextFrameDeadline)
{
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 60.0f);

    auto half_frame = start + std::chrono::milliseconds(10);
    EXPECT_FALSE(gate.should_tick(half_frame));
    // Sleeps until the tick window opens, a quarter period before the deadline.
    EXPECT_NEAR(gate.wait_timeout_seconds(half_frame, 0.1), 0.75 / 60.0 - 0.010, 1e-4);
}

TEST(AnimationTickGate, ExpiredDeadlineTicksImmediately)
{
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 60.0f);

    auto overdue = start + std::chrono::milliseconds(20);
    EXPECT_TRUE(gate.should_tick(overdue));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(overdue, 0.1), 0.0);
}

TEST(AnimationTickGate, InvalidFpsClearsSchedule)
{
    AnimationTickGate            gate;
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
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint start{};

    gate.accumulate_dt(0.02f);
    gate.schedule_next(start, 60.0f);
    gate.clear();

    EXPECT_TRUE(gate.should_tick(start));
    EXPECT_DOUBLE_EQ(gate.wait_timeout_seconds(start, 0.1), 0.1);
    EXPECT_FLOAT_EQ(gate.accumulated_dt(), 0.0f);
}

TEST(AnimationTickGate, FrameJustBeforeTheDeadlineStillTicks)
{
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 175.0f);

    // A 174 Hz vsync frame lands 0.04 ms short of the 175 fps deadline.
    auto vsync = start + std::chrono::microseconds(5747 - 40);
    EXPECT_TRUE(gate.should_tick(vsync));
}

TEST(AnimationTickGate, CadenceDoesNotDriftWithFrameTime)
{
    using std::chrono::microseconds;
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint t{};
    gate.schedule_next(t, 175.0f);

    // Frames on a 174 Hz display, each scheduling the next tick after it has
    // rendered: every frame must tick, as the animation asks for >= refresh.
    int ticks = 0;
    for (int i = 1; i <= 174; ++i)
    {
        t = AnimationTickGate::TimePoint{} + microseconds(i * 5747);
        if (gate.should_tick(t))
        {
            ++ticks;
            gate.schedule_next(t + microseconds(1500), 175.0f);
        }
    }
    EXPECT_EQ(ticks, 174);
}

TEST(AnimationTickGate, LowerFpsKeepsItsAverageRateOnAFastDisplay)
{
    using std::chrono::microseconds;
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint t{};
    gate.schedule_next(t, 60.0f);

    int ticks = 0;
    for (int i = 1; i <= 174; ++i)   // one second at 174 Hz
    {
        t = AnimationTickGate::TimePoint{} + microseconds(i * 5747);
        if (gate.should_tick(t))
        {
            ++ticks;
            gate.schedule_next(t, 60.0f);
        }
    }
    EXPECT_NEAR(ticks, 60, 2);
}

TEST(AnimationTickGate, StallDoesNotCauseABurstOfCatchUpTicks)
{
    using std::chrono::milliseconds;
    AnimationTickGate            gate;
    AnimationTickGate::TimePoint start{};
    gate.schedule_next(start, 60.0f);

    // A 200 ms stall: the next frame ticks once, then the 60 fps cadence resumes.
    auto late = start + milliseconds(200);
    ASSERT_TRUE(gate.should_tick(late));
    gate.schedule_next(late, 60.0f);
    auto next_frame = late + milliseconds(6);
    ASSERT_TRUE(gate.should_tick(next_frame));
    gate.schedule_next(next_frame, 60.0f);
    EXPECT_FALSE(gate.should_tick(next_frame + milliseconds(6)));
    EXPECT_TRUE(gate.should_tick(next_frame + milliseconds(17)));
}

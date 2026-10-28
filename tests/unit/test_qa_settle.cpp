// test_qa_settle.cpp — QA agent bounded wait-until-stable loop (tests/qa/qa_settle.hpp).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "../qa/qa_settle.hpp"

using namespace spectra::qa;

namespace
{
// A fake frame loop: each step() advances a clock by `dt` and one animated
// value toward a target the way LayoutManager::smooth_toward does.
struct FakeLoop
{
    double   t      = 0.0;
    double   dt     = 1.0 / 60.0;
    float    value  = 230.0f;   // expanded nav rail
    float    target = 72.0f;    // collapsed nav rail
    uint64_t steps  = 0;
    bool     stop   = false;

    bool step()
    {
        if (stop)
            return false;
        ++steps;
        t += dt;
        const float diff = target - value;
        if (std::abs(diff) < 0.5f)
            value = target;
        else
            value += diff * std::min(1.0f, 12.0f * static_cast<float>(dt));
        return true;
    }
};
}   // namespace

TEST(QaSettle, WaitsForAnimatedValueInsteadOfFixedFrames)
{
    FakeLoop loop;
    auto     r = wait_until_stable([&] { return loop.step(); },
                               [&] { return loop.value == loop.target; },
                               [&] { return loop.t; });
    EXPECT_TRUE(r.settled);
    EXPECT_FALSE(r.aborted);
    EXPECT_EQ(loop.value, loop.target);
    // A fixed 20-frame wait would have been too short for this animation.
    EXPECT_GT(r.frames, 20u);
    EXPECT_EQ(r.frames, loop.steps);
}

TEST(QaSettle, PredicateMustHoldForStableFrames)
{
    int  frame = 0;
    auto r     = wait_until_stable([&] { return ++frame, true; },
                               [&] { return frame == 3 || frame >= 10; },   // flickers once
                               [&] { return frame * 0.01; },
                               SettleBudget{.stable_frames = 3});
    EXPECT_TRUE(r.settled);
    EXPECT_EQ(r.frames, 12u);   // frames 10, 11, 12 are the stable streak
}

TEST(QaSettle, TimesOutOnlyAfterBothFrameAndWallBudgets)
{
    // Fast frames: the frame budget is spent long before the wall budget.
    double t    = 0.0;
    auto   fast = wait_until_stable([&] { return t += 0.001, true; },
                                  [] { return false; },
                                  [&] { return t; },
                                  SettleBudget{.max_frames = 10, .max_seconds = 1.0});
    EXPECT_FALSE(fast.settled);
    EXPECT_FALSE(fast.aborted);
    EXPECT_GE(fast.seconds, 1.0);
    EXPECT_GE(fast.frames, 999u);

    // Slow frames: the wall budget is spent long before the frame budget.
    t         = 0.0;
    auto slow = wait_until_stable([&] { return t += 0.5, true; },
                                  [] { return false; },
                                  [&] { return t; },
                                  SettleBudget{.max_frames = 10, .max_seconds = 1.0});
    EXPECT_FALSE(slow.settled);
    EXPECT_EQ(slow.frames, 10u);
}

TEST(QaSettle, HardFrameCapStopsAStalledClock)
{
    auto r = wait_until_stable([] { return true; },
                               [] { return false; },
                               [] { return 0.0; },
                               SettleBudget{.max_frames = 5, .hard_max_frames = 50});
    EXPECT_FALSE(r.settled);
    EXPECT_EQ(r.frames, 50u);
}

TEST(QaSettle, StepAbortStopsTheWait)
{
    FakeLoop loop;
    auto     r = wait_until_stable(
        [&]
        {
            loop.stop = loop.steps >= 4;
            return loop.step();
        },
        [] { return false; },
        [&] { return loop.t; });
    EXPECT_TRUE(r.aborted);
    EXPECT_FALSE(r.settled);
    EXPECT_EQ(r.frames, 4u);
}

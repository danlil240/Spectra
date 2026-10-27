#include <gtest/gtest.h>

#include "ui/shell/fps_badge.hpp"

#include <chrono>

using spectra::ui::FpsBadgeState;
using namespace std::chrono_literals;

namespace
{

// Feeds frames every `step` from `start` for `duration`; returns the last frame time.
FpsBadgeState::TimePoint feed(FpsBadgeState&                 badge,
                              FpsBadgeState::TimePoint       start,
                              FpsBadgeState::Clock::duration step,
                              FpsBadgeState::Clock::duration duration)
{
    auto t = start;
    for (; t <= start + duration; t += step)
        badge.on_frame(t);
    return t - step;
}

constexpr auto k60Hz = std::chrono::microseconds(16667);

}   // namespace

TEST(FpsBadgeState, StartsIdle)
{
    FpsBadgeState badge;
    EXPECT_FALSE(badge.is_live());
    EXPECT_EQ(badge.label(), "Idle");
    EXPECT_FALSE(badge.needs_settle_frame(FpsBadgeState::TimePoint{}));
}

TEST(FpsBadgeState, SparseOnDemandFramesStayIdle)
{
    // Rendering on demand: one frame every couple of seconds (the case that
    // used to read "0 fps").
    FpsBadgeState            badge;
    FpsBadgeState::TimePoint t{};
    for (int i = 0; i < 10; ++i)
    {
        badge.on_frame(t);
        t += 2s;
    }
    EXPECT_FALSE(badge.is_live());
    EXPECT_EQ(badge.label(), "Idle");
    EXPECT_DOUBLE_EQ(badge.fps(), 0.0);
}

TEST(FpsBadgeState, ShortInputBurstsDoNotFlicker)
{
    // A click/hover renders a few frames back to back, then sleeps.  The badge
    // must not switch to live for such bursts.
    FpsBadgeState            badge;
    FpsBadgeState::TimePoint t{};
    for (int burst = 0; burst < 20; ++burst)
    {
        t = feed(badge, t, k60Hz, 100ms);
        EXPECT_FALSE(badge.is_live()) << "burst " << burst;
        t += 700ms;
    }
    EXPECT_EQ(badge.label(), "Idle");
}

TEST(FpsBadgeState, SustainedRenderingShowsFps)
{
    FpsBadgeState            badge;
    FpsBadgeState::TimePoint t{};
    t = feed(badge, t, k60Hz, 300ms);
    EXPECT_FALSE(badge.is_live());   // not yet sustained
    t = feed(badge, t + k60Hz, k60Hz, 1s);
    EXPECT_TRUE(badge.is_live());
    EXPECT_NEAR(badge.fps(), 60.0, 1.0);
    EXPECT_EQ(badge.label(), "60 fps");
    EXPECT_FALSE(badge.needs_settle_frame(t));
}

TEST(FpsBadgeState, LowRateAnimationCountsAsContinuous)
{
    FpsBadgeState badge;
    feed(badge, FpsBadgeState::TimePoint{}, 100ms, 2s);   // 10 fps animation
    EXPECT_TRUE(badge.is_live());
    EXPECT_EQ(badge.label(), "10 fps");
}

TEST(FpsBadgeState, HoldsLiveAcrossShortPauses)
{
    FpsBadgeState badge;
    auto          t = feed(badge, FpsBadgeState::TimePoint{}, k60Hz, 1s);
    ASSERT_TRUE(badge.is_live());

    // A pause shorter than the hold, then rendering resumes: stays live.
    t += 600ms;
    badge.on_frame(t);
    EXPECT_TRUE(badge.is_live());
    feed(badge, t + k60Hz, k60Hz, 200ms);
    EXPECT_TRUE(badge.is_live());
}

TEST(FpsBadgeState, SettlesToIdleAfterHold)
{
    FpsBadgeState badge;
    auto          last = feed(badge, FpsBadgeState::TimePoint{}, k60Hz, 1s);
    ASSERT_TRUE(badge.is_live());

    // Rendering stops.  Within the hold nothing needs to happen.
    EXPECT_FALSE(badge.needs_settle_frame(last + 500ms));

    // After the hold the badge is stale and asks for one frame.
    const auto later = last + 1200ms;
    EXPECT_TRUE(badge.needs_settle_frame(later));

    // That frame switches it to Idle, and no further frames are requested.
    badge.on_frame(later);
    EXPECT_FALSE(badge.is_live());
    EXPECT_EQ(badge.label(), "Idle");
    EXPECT_FALSE(badge.needs_settle_frame(later + 10s));
}

TEST(FpsBadgeState, SettleFrameAndGraceFramesDoNotReenterLive)
{
    // The settle frame wakes the loop, which renders a few grace frames.  They
    // must not restart the live state (no self-sustaining redraw loop).
    FpsBadgeState badge;
    auto          last = feed(badge, FpsBadgeState::TimePoint{}, k60Hz, 1s);
    ASSERT_TRUE(badge.is_live());
    const auto settle = last + 1100ms;
    badge.on_frame(settle);
    feed(badge, settle + k60Hz, k60Hz, 50ms);
    EXPECT_FALSE(badge.is_live());
    EXPECT_FALSE(badge.needs_settle_frame(settle + 5s));
}

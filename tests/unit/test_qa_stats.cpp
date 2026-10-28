// test_qa_stats.cpp — QA agent memory / frame-time heuristics (tests/qa/qa_stats.hpp).

#include <gtest/gtest.h>

#include "../qa/qa_stats.hpp"

using namespace spectra::qa;

namespace
{
constexpr int64_t MB = 1024 * 1024;
}

TEST(QaRetention, PlateauAfterWarmupIsNotALeak)
{
    // Measured multi_window_churn shape: +29MB first repetition, then flat.
    const auto v = judge_retention(156 * MB, {185 * MB, 187 * MB, 187 * MB, 188 * MB}, 2 * MB);
    EXPECT_TRUE(v.judged);
    EXPECT_FALSE(v.sustained_growth);
    EXPECT_EQ(v.warmup_bytes, 29 * MB);
    EXPECT_EQ(v.per_rep_bytes, 1 * MB);
}

TEST(QaRetention, SteadyGrowthIsALeak)
{
    const auto v = judge_retention(100 * MB, {120 * MB, 125 * MB, 130 * MB, 135 * MB}, 2 * MB);
    EXPECT_TRUE(v.sustained_growth);
    EXPECT_EQ(v.per_rep_bytes, 5 * MB);
    EXPECT_EQ(v.late_per_rep_bytes, 5 * MB);
}

TEST(QaRetention, GrowthThatStopsIsAPlateau)
{
    // Grows for two repetitions then settles: second half is flat.
    const auto v =
        judge_retention(100 * MB, {120 * MB, 130 * MB, 140 * MB, 140 * MB, 140 * MB}, 2 * MB);
    EXPECT_GT(v.per_rep_bytes, 2 * MB);
    EXPECT_EQ(v.late_per_rep_bytes, 0);
    EXPECT_FALSE(v.sustained_growth);
}

TEST(QaRetention, FewerThanThreeRepetitionsAreNotJudged)
{
    const auto v = judge_retention(100 * MB, {124 * MB}, 2 * MB);
    EXPECT_FALSE(v.judged);
    EXPECT_FALSE(v.sustained_growth);
    EXPECT_EQ(v.warmup_bytes, 24 * MB);
}

TEST(QaRssFloor, TransientPeakDoesNotCount)
{
    // initial 190MB; five windows open for one sample push RSS to 292MB.
    RssFloorMonitor m(5);
    const size_t    initial = 190 * MB;
    for (size_t rss : {200 * MB, 292 * MB, 210 * MB, 205 * MB, 206 * MB})
        m.add(rss);
    EXPECT_TRUE(m.full());
    EXPECT_FALSE(m.sustained_above(initial, 100 * MB));
}

TEST(QaRssFloor, SustainedGrowthCounts)
{
    RssFloorMonitor m(3);
    const size_t    initial = 190 * MB;
    m.add(300 * MB);
    m.add(305 * MB);
    EXPECT_FALSE(m.sustained_above(initial, 100 * MB)) << "window not full yet";
    m.add(310 * MB);
    EXPECT_TRUE(m.sustained_above(initial, 100 * MB));
    EXPECT_EQ(m.floor(), 300 * MB);
}

TEST(QaSpikes, StartupAndFirstUseFramesAreWarmup)
{
    SpikeDetector d;
    // Startup window.
    EXPECT_FALSE(d.is_spike(10, 120.0f, 20.0f));
    // A first-use event at frame 279 covers 279..282.
    d.declare_warmup(279);
    EXPECT_FALSE(d.is_spike(279, 240.0f, 25.0f));
    EXPECT_FALSE(d.is_spike(282, 130.0f, 25.0f));
    EXPECT_EQ(d.warmup_spikes(), 3u);
    // Steady state afterwards is flagged.
    EXPECT_TRUE(d.is_spike(283, 130.0f, 25.0f));
}

TEST(QaSpikes, SmallOrRelativeFastFramesAreNotSpikes)
{
    SpikeDetector d;
    EXPECT_FALSE(d.is_spike(100, 30.0f, 5.0f)) << "below the 33ms floor";
    EXPECT_FALSE(d.is_spike(100, 60.0f, 25.0f)) << "below 3x average";
    EXPECT_TRUE(d.is_spike(100, 80.0f, 25.0f));
    EXPECT_EQ(d.warmup_spikes(), 0u);
}

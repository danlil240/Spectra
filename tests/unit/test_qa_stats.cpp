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

TEST(QaSlope, LeastSquaresFitIsExactOnALine)
{
    const auto fit = least_squares_fit({10.0, 12.0, 14.0, 16.0, 18.0});
    EXPECT_EQ(fit.samples, 5u);
    EXPECT_DOUBLE_EQ(fit.slope, 2.0);
    EXPECT_DOUBLE_EQ(fit.intercept, 10.0);
}

TEST(QaSlope, LeastSquaresFitDegenerateInputs)
{
    EXPECT_DOUBLE_EQ(least_squares_fit({}).slope, 0.0);
    EXPECT_DOUBLE_EQ(least_squares_fit({5.0}).slope, 0.0);
    EXPECT_DOUBLE_EQ(least_squares_fit({5.0}).intercept, 5.0);
    EXPECT_DOUBLE_EQ(least_squares_fit({3.0, 3.0, 3.0}).slope, 0.0);
}

TEST(QaSlope, LeastSquaresIsRobustToOneOutlier)
{
    // Endpoint difference would say 4MB/iter; the fit sees a mostly flat line.
    const auto fit = least_squares_fit({100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 104.0});
    EXPECT_LT(fit.slope, 1.0);
    EXPECT_GT(fit.slope, 0.0);
}

TEST(QaSlope, WarmupIsExcludedFromTheSlope)
{
    // Measured series_mixing shape: +27MB warm-up, then flat.
    const auto v = judge_rss_slope(155 * MB,
                                   {182 * MB, 182 * MB, 182 * MB, 182 * MB, 182 * MB, 182 * MB},
                                   1,
                                   0.5);
    EXPECT_TRUE(v.judged);
    EXPECT_FALSE(v.exceeds);
    EXPECT_EQ(v.measured, 5u);
    EXPECT_DOUBLE_EQ(v.slope_mb_per_iter, 0.0);
    EXPECT_DOUBLE_EQ(v.warmup_mb, 27.0);
}

TEST(QaSlope, SteadyGrowthFailsTheGate)
{
    // Measured stress_docking shape before the fix: ~1MB per iteration.
    const auto v =
        judge_rss_slope(159 * MB, {191 * MB, 192 * MB, 193 * MB, 194 * MB, 195 * MB}, 1, 0.5);
    EXPECT_TRUE(v.judged);
    EXPECT_TRUE(v.exceeds);
    EXPECT_DOUBLE_EQ(v.slope_mb_per_iter, 1.0);
}

TEST(QaSlope, ThresholdIsConfigurable)
{
    const std::vector<int64_t> rss = {191 * MB, 192 * MB, 193 * MB, 194 * MB, 195 * MB};
    EXPECT_FALSE(judge_rss_slope(159 * MB, rss, 1, 1.5).exceeds);
    EXPECT_TRUE(judge_rss_slope(159 * MB, rss, 1, 0.25).exceeds);
}

TEST(QaSlope, TooFewMeasuredIterationsAreNotJudged)
{
    // Three iterations, one warm-up: two measured samples < 3.
    const auto v = judge_rss_slope(100 * MB, {120 * MB, 130 * MB, 140 * MB}, 1, 0.5);
    EXPECT_FALSE(v.judged);
    EXPECT_FALSE(v.exceeds);
    EXPECT_EQ(v.measured, 2u);
    EXPECT_DOUBLE_EQ(v.slope_mb_per_iter, 10.0);

    EXPECT_FALSE(judge_rss_slope(100 * MB, {120 * MB}, 1, 0.5).judged);
    EXPECT_FALSE(judge_rss_slope(100 * MB, {}, 1, 0.5).judged);
}

TEST(QaSlope, WarmupIterationsAreConfigurable)
{
    // Two warm-up iterations hide a two-step warm-up from the gate.
    const std::vector<int64_t> rss = {120 * MB, 140 * MB, 142 * MB, 142 * MB, 142 * MB};
    EXPECT_TRUE(judge_rss_slope(100 * MB, rss, 1, 0.5).exceeds);
    const auto v = judge_rss_slope(100 * MB, rss, 2, 0.5);
    EXPECT_TRUE(v.judged);
    EXPECT_FALSE(v.exceeds);
    EXPECT_DOUBLE_EQ(v.warmup_mb, 42.0);
}

TEST(QaSlope, TheilSenIgnoresASingleSpike)
{
    // Measured heap shape: flat with one transient +1.5MB sample at the end,
    // which would move a least-squares fit by ~0.16MB/iter.
    const std::vector<double> y = {85.6, 85.6, 85.6, 85.6, 85.6, 85.6, 87.1};
    EXPECT_GT(least_squares_fit(y).slope, 0.15);
    EXPECT_DOUBLE_EQ(theil_sen_slope(y), 0.0);
}

TEST(QaSlope, TheilSenFollowsSteadyGrowth)
{
    EXPECT_NEAR(theil_sen_slope({1.0, 1.2, 1.4, 1.6, 1.8}), 0.2, 1e-12);
    EXPECT_NEAR(theil_sen_slope({1.0, 1.2, 9.0, 1.6, 1.8}), 0.2, 1e-12);
    EXPECT_DOUBLE_EQ(theil_sen_slope({}), 0.0);
    EXPECT_DOUBLE_EQ(theil_sen_slope({3.0}), 0.0);
    EXPECT_DOUBLE_EQ(theil_sen_slope({1.0, 3.0}), 2.0);
}

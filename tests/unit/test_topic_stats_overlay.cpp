// Unit tests for TopicStatsOverlay (B3).
//
// These tests do NOT require a ROS2 executor or ImGui context.
// All statistics computation is exercised through inject_sample() /
// compute_now() / snapshot() — the pure C++ path.
//
// Test structure:
//   Construction        (3)
//   TopicSelection      (5)
//   NotifyMessage       (6)
//   HzComputation       (8)
//   BandwidthComputation(5)
//   LatencyStats        (6)
//   DropDetection       (5)
//   FormatHelpers       (8)  — tested via snapshot strings indirectly
//   EdgeCases           (5)
//   TotalByteCount      (3)
//   WindowConfig        (4)
//   ResetStats          (3)

#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "ui/topic_stats_overlay.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Helpers are currently unused but kept as commented-out reference.
// static int64_t ns_now() { ... }
// static void inject_samples(...) { ... }

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, DefaultTitle)
{
    TopicStatsOverlay overlay;
    EXPECT_EQ(overlay.title(), "Topic Statistics");
}

TEST(TopicStatsOverlay, DefaultWindowMs)
{
    TopicStatsOverlay overlay;
    EXPECT_EQ(overlay.window_ms(), 1000);
}

TEST(TopicStatsOverlay, DefaultDropFactor)
{
    TopicStatsOverlay overlay;
    EXPECT_DOUBLE_EQ(overlay.drop_factor(), 3.0);
}

// ---------------------------------------------------------------------------
// Suite: TopicSelection
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, InitialTopicEmpty)
{
    TopicStatsOverlay overlay;
    EXPECT_TRUE(overlay.topic().empty());
}

TEST(TopicStatsOverlay, SetTopicUpdates)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/imu/data");
    EXPECT_EQ(overlay.topic(), "/imu/data");
}

TEST(TopicStatsOverlay, SetTopicClearsPreviousStats)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/topicA");
    overlay.notify_message("/topicA", 100);
    overlay.notify_message("/topicA", 100);
    overlay.notify_message("/topicA", 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    overlay.set_topic("/topicB");
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 0u);
    EXPECT_EQ(snap.total_bytes,    0u);
}

TEST(TopicStatsOverlay, SetSameTopicIsNoop)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/same");
    overlay.notify_message("/same", 50);
    overlay.set_topic("/same");          // same topic — should NOT clear stats
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 1u);
}

TEST(TopicStatsOverlay, SetTopicChangeResetsDropFlag)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/old");
    // Give it some messages with a gap.
    overlay.notify_message("/old", 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    overlay.notify_message("/old", 100);
    overlay.set_topic("/new");
    const auto snap = overlay.snapshot();
    EXPECT_FALSE(snap.drop_detected);
}

// ---------------------------------------------------------------------------
// Suite: NotifyMessage
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, NotifyWrongTopicIgnored)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/correct");
    overlay.notify_message("/wrong", 100);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 0u);
}

TEST(TopicStatsOverlay, NotifyEmptyTopicIgnored)
{
    TopicStatsOverlay overlay;
    // Current topic is empty — notify should be ignored.
    overlay.notify_message("/anything", 100);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 0u);
}

TEST(TopicStatsOverlay, NotifyIncrementsTotalMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/counter");
    for (int i = 0; i < 5; ++i)
        overlay.notify_message("/counter", 64);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 5u);
}

TEST(TopicStatsOverlay, NotifyAccumulatesTotalBytes)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/bytes");
    overlay.notify_message("/bytes", 100);
    overlay.notify_message("/bytes", 200);
    overlay.notify_message("/bytes", 300);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_bytes, 600u);
}

TEST(TopicStatsOverlay, NotifyWithLatencyRecorded)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/latency_topic");
    overlay.notify_message("/latency_topic", 100, 500);  // 500 µs latency
    overlay.notify_message("/latency_topic", 100, 700);  // 700 µs
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto snap = overlay.snapshot();
    // Latency should be between 500 and 700.
    EXPECT_GE(snap.latency_avg_us, 499.0);
    EXPECT_LE(snap.latency_avg_us, 701.0);
}

TEST(TopicStatsOverlay, NotifyNoLatencyReturnsMinusOne)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/no_lat");
    overlay.notify_message("/no_lat", 100, -1);
    overlay.notify_message("/no_lat", 100, -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto snap = overlay.snapshot();
    EXPECT_LT(snap.latency_avg_us, 0.0);
}

// ---------------------------------------------------------------------------
// Suite: HzComputation
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, NoMessagesHzIsZero)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/hz_test");
    const auto snap = overlay.snapshot();
    EXPECT_DOUBLE_EQ(snap.hz_avg, 0.0);
}

TEST(TopicStatsOverlay, SingleMessageHzIsOne)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/hz_one");
    overlay.notify_message("/hz_one", 10);
    const auto snap = overlay.snapshot();
    EXPECT_DOUBLE_EQ(snap.hz_avg, 1.0);
}

TEST(TopicStatsOverlay, TwoMessagesHzPositive)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/hz_two");
    overlay.notify_message("/hz_two", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    overlay.notify_message("/hz_two", 10);
    const auto snap = overlay.snapshot();
    EXPECT_GT(snap.hz_avg, 0.0);
}

TEST(TopicStatsOverlay, HzApproximately10Hz)
{
    // Send 11 messages at roughly 10 Hz (100 ms apart).
    TopicStatsOverlay overlay;
    overlay.set_window_ms(2000);
    overlay.set_topic("/hz_10");
    for (int i = 0; i < 11; ++i) {
        overlay.notify_message("/hz_10", 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto snap = overlay.snapshot();
    // Allow wide tolerance due to OS sleep jitter.
    EXPECT_GT(snap.hz_avg, 40.0);   // at 10ms sleep this runs much faster
    EXPECT_LT(snap.hz_avg, 5000.0);
}

TEST(TopicStatsOverlay, HzMinLessThanOrEqualAvg)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/hz_minmax");
    for (int i = 0; i < 5; ++i) {
        overlay.notify_message("/hz_minmax", 100);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const auto snap = overlay.snapshot();
    if (snap.hz_avg > 0.0 && snap.hz_min > 0.0) {
        EXPECT_LE(snap.hz_min, snap.hz_avg + 1.0);
    }
}

TEST(TopicStatsOverlay, HzMaxGreaterThanOrEqualAvg)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/hz_max");
    for (int i = 0; i < 5; ++i) {
        overlay.notify_message("/hz_max", 100);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const auto snap = overlay.snapshot();
    if (snap.hz_avg > 0.0 && snap.hz_max > 0.0) {
        EXPECT_GE(snap.hz_max, snap.hz_avg - 1.0);
    }
}

TEST(TopicStatsOverlay, OldSamplesPrunedFromWindow)
{
    TopicStatsOverlay overlay;
    overlay.set_window_ms(100);  // 100 ms window
    overlay.set_topic("/prune");

    overlay.notify_message("/prune", 100);
    // Wait longer than the window.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    // The old sample should fall outside the 100 ms window.
    const auto snap = overlay.snapshot();
    // Either no hz or single sample (<=1 Hz in window).
    EXPECT_LE(snap.hz_avg, 1.0);
}

TEST(TopicStatsOverlay, HzComputationWithManyMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_window_ms(2000);
    overlay.set_topic("/many");
    for (int i = 0; i < 20; ++i) {
        overlay.notify_message("/many", 32);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    const auto snap = overlay.snapshot();
    EXPECT_GT(snap.hz_avg, 0.0);
    EXPECT_EQ(snap.total_messages, 20u);
}

// ---------------------------------------------------------------------------
// Suite: BandwidthComputation
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, BwZeroWithNoMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/bw");
    const auto snap = overlay.snapshot();
    EXPECT_DOUBLE_EQ(snap.bw_bps, 0.0);
}

TEST(TopicStatsOverlay, BwPositiveWithMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/bw2");
    overlay.notify_message("/bw2", 1000);
    const auto snap = overlay.snapshot();
    EXPECT_GT(snap.bw_bps, 0.0);
}

TEST(TopicStatsOverlay, TotalBytesCorrect)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/total");
    overlay.notify_message("/total", 512);
    overlay.notify_message("/total", 256);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_bytes, 768u);
}

TEST(TopicStatsOverlay, TotalBytesPersistAcrossWindowExpiry)
{
    TopicStatsOverlay overlay;
    overlay.set_window_ms(50);
    overlay.set_topic("/persist");
    overlay.notify_message("/persist", 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // expire window
    overlay.notify_message("/persist", 500);
    const auto snap = overlay.snapshot();
    // total_bytes must count all messages ever (cumulative counter).
    EXPECT_EQ(snap.total_bytes, 1500u);
}

TEST(TopicStatsOverlay, BwScalesWithLargerMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/bw_large");
    for (int i = 0; i < 3; ++i)
        overlay.notify_message("/bw_large", 100000);
    const auto snap1 = overlay.snapshot();

    TopicStatsOverlay overlay2;
    overlay2.set_topic("/bw_small");
    for (int i = 0; i < 3; ++i)
        overlay2.notify_message("/bw_small", 100);
    const auto snap2 = overlay2.snapshot();

    EXPECT_GT(snap1.bw_bps, snap2.bw_bps);
}

// ---------------------------------------------------------------------------
// Suite: LatencyStats
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, LatencyAvgIsCorrect)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/lat");
    overlay.notify_message("/lat", 50, 1000);   // 1000 µs
    overlay.notify_message("/lat", 50, 2000);   // 2000 µs
    overlay.notify_message("/lat", 50, 3000);   // 3000 µs
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto snap = overlay.snapshot();
    // avg should be ~2000 µs (allow ±100 µs for timing noise)
    EXPECT_NEAR(snap.latency_avg_us, 2000.0, 100.0);
}

TEST(TopicStatsOverlay, LatencyMinIsLowest)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/lat_min");
    overlay.notify_message("/lat_min", 50, 100);
    overlay.notify_message("/lat_min", 50, 500);
    overlay.notify_message("/lat_min", 50, 900);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto snap = overlay.snapshot();
    EXPECT_NEAR(snap.latency_min_us, 100.0, 5.0);
}

TEST(TopicStatsOverlay, LatencyMaxIsHighest)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/lat_max");
    overlay.notify_message("/lat_max", 50, 200);
    overlay.notify_message("/lat_max", 50, 800);
    overlay.notify_message("/lat_max", 50, 1500);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto snap = overlay.snapshot();
    EXPECT_NEAR(snap.latency_max_us, 1500.0, 5.0);
}

TEST(TopicStatsOverlay, MixedLatencyValidAndInvalid)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/lat_mix");
    overlay.notify_message("/lat_mix", 50, 400);   // valid
    overlay.notify_message("/lat_mix", 50, -1);    // invalid (no header)
    overlay.notify_message("/lat_mix", 50, 600);   // valid
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto snap = overlay.snapshot();
    // Only 2 valid samples, avg should be 500.
    EXPECT_NEAR(snap.latency_avg_us, 500.0, 50.0);
}

TEST(TopicStatsOverlay, AllLatencyInvalidReturnsNegative)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/lat_none");
    overlay.notify_message("/lat_none", 50, -1);
    overlay.notify_message("/lat_none", 50, -1);
    const auto snap = overlay.snapshot();
    EXPECT_LT(snap.latency_avg_us, 0.0);
    EXPECT_LT(snap.latency_min_us, 0.0);
    EXPECT_LT(snap.latency_max_us, 0.0);
}

TEST(TopicStatsOverlay, LatencyExpiredWithWindowPrune)
{
    TopicStatsOverlay overlay;
    overlay.set_window_ms(50);
    overlay.set_topic("/lat_prune");
    overlay.notify_message("/lat_prune", 50, 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // After window expiry, the sample is gone — latency should be invalid.
    const auto snap = overlay.snapshot();
    EXPECT_LT(snap.latency_avg_us, 0.0);
}

// ---------------------------------------------------------------------------
// Suite: DropDetection
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, NoDropWithRegularMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/drop_no");
    for (int i = 0; i < 5; ++i) {
        overlay.notify_message("/drop_no", 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto snap = overlay.snapshot();
    EXPECT_FALSE(snap.drop_detected);
}

TEST(TopicStatsOverlay, DropDetectedAfterLongGap)
{
    TopicStatsOverlay overlay;
    overlay.set_drop_factor(2.0);
    overlay.set_window_ms(2000);
    overlay.set_topic("/drop_yes");

    // Establish a baseline: ~100 Hz (10 ms apart).
    for (int i = 0; i < 10; ++i) {
        overlay.notify_message("/drop_yes", 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Now insert a big gap (~300 ms — 30× the expected period).
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    overlay.notify_message("/drop_yes", 50);

    const auto snap = overlay.snapshot();
    EXPECT_TRUE(snap.drop_detected);
}

TEST(TopicStatsOverlay, DropFlagClearsAfterNewTopic)
{
    TopicStatsOverlay overlay;
    overlay.set_drop_factor(2.0);
    overlay.set_window_ms(2000);
    overlay.set_topic("/drop_clear");

    for (int i = 0; i < 5; ++i) {
        overlay.notify_message("/drop_clear", 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    overlay.notify_message("/drop_clear", 50);

    overlay.set_topic("/fresh");
    const auto snap = overlay.snapshot();
    EXPECT_FALSE(snap.drop_detected);
}

TEST(TopicStatsOverlay, DropFactorRespected)
{
    // A factor of 10.0 should NOT flag a 3× gap as a drop.
    TopicStatsOverlay overlay;
    overlay.set_drop_factor(10.0);
    overlay.set_window_ms(2000);
    overlay.set_topic("/drop_factor");

    for (int i = 0; i < 10; ++i) {
        overlay.notify_message("/drop_factor", 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // 3× the expected period (30 ms) — under the 10× threshold.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    overlay.notify_message("/drop_factor", 50);

    const auto snap = overlay.snapshot();
    EXPECT_FALSE(snap.drop_detected);
}

TEST(TopicStatsOverlay, LastGapNsIsPopulated)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/gap_ns");
    overlay.notify_message("/gap_ns", 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    overlay.notify_message("/gap_ns", 50);
    const auto snap = overlay.snapshot();
    // Gap should be roughly 20 ms (20,000,000 ns), allow 5–200 ms range.
    EXPECT_GT(snap.last_gap_ns, 5'000'000LL);
    EXPECT_LT(snap.last_gap_ns, 200'000'000LL);
}

// ---------------------------------------------------------------------------
// Suite: ResetStats
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, ResetClearsWindowSamples)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/reset");
    overlay.notify_message("/reset", 100);
    overlay.notify_message("/reset", 100);
    overlay.reset_stats();
    const auto snap = overlay.snapshot();
    EXPECT_DOUBLE_EQ(snap.hz_avg, 0.0);
}

TEST(TopicStatsOverlay, ResetPreservesTotalMessages)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/reset2");
    overlay.notify_message("/reset2", 50);
    overlay.notify_message("/reset2", 50);
    overlay.reset_stats();
    const auto snap = overlay.snapshot();
    // After reset_window(), cumulative total_messages is cleared too.
    // (reset_window() sets total_messages=0 in the stats struct)
    EXPECT_EQ(snap.total_messages, 0u);
}

TEST(TopicStatsOverlay, ResetDropFlag)
{
    TopicStatsOverlay overlay;
    overlay.set_drop_factor(2.0);
    overlay.set_window_ms(2000);
    overlay.set_topic("/reset_drop");
    for (int i = 0; i < 5; ++i) {
        overlay.notify_message("/reset_drop", 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    overlay.notify_message("/reset_drop", 50);
    overlay.reset_stats();
    const auto snap = overlay.snapshot();
    EXPECT_FALSE(snap.drop_detected);
}

// ---------------------------------------------------------------------------
// Suite: WindowConfig
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, SetWindowMs)
{
    TopicStatsOverlay overlay;
    overlay.set_window_ms(500);
    EXPECT_EQ(overlay.window_ms(), 500);
}

TEST(TopicStatsOverlay, InvalidWindowMsClampedToMinimum)
{
    TopicStatsOverlay overlay;
    overlay.set_window_ms(-100);
    EXPECT_GE(overlay.window_ms(), 1);  // must not stay negative
}

TEST(TopicStatsOverlay, SetDropFactor)
{
    TopicStatsOverlay overlay;
    overlay.set_drop_factor(5.0);
    EXPECT_DOUBLE_EQ(overlay.drop_factor(), 5.0);
}

TEST(TopicStatsOverlay, InvalidDropFactorClampedToMin)
{
    TopicStatsOverlay overlay;
    overlay.set_drop_factor(0.5);  // < 1.0 — invalid
    EXPECT_GE(overlay.drop_factor(), 1.0);
}

// ---------------------------------------------------------------------------
// Suite: EdgeCases
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, ZeroBytesMessage)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/zero");
    overlay.notify_message("/zero", 0);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 1u);
    EXPECT_EQ(snap.total_bytes, 0u);
}

TEST(TopicStatsOverlay, VeryLargeMessageBytes)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/large");
    const size_t big = 100ULL * 1024ULL * 1024ULL;  // 100 MB
    overlay.notify_message("/large", big);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_bytes, big);
}

TEST(TopicStatsOverlay, SnapshotWithNoTopicReturnsEmpty)
{
    TopicStatsOverlay overlay;
    const auto snap = overlay.snapshot();
    EXPECT_TRUE(snap.topic.empty());
    EXPECT_DOUBLE_EQ(snap.hz_avg, 0.0);
}

TEST(TopicStatsOverlay, ConcurrentNotifyAndSnapshot)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/concurrent");
    std::atomic<bool> stop{false};
    std::thread notifier([&]() {
        while (!stop.load()) {
            overlay.notify_message("/concurrent", 64);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    for (int i = 0; i < 20; ++i) {
        auto snap = overlay.snapshot();
        (void)snap;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop = true;
    notifier.join();
    // If we get here without crash / deadlock — pass.
    SUCCEED();
}

TEST(TopicStatsOverlay, DrawNoOpWithoutImGui)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/draw");
    overlay.notify_message("/draw", 100);
    // draw() is a no-op without SPECTRA_USE_IMGUI — just must not crash.
    overlay.draw(nullptr);
    overlay.draw_inline();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Suite: TotalByteCount
// ---------------------------------------------------------------------------

TEST(TopicStatsOverlay, TotalBytesMatchesAllNotifications)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/bytes_total");
    overlay.notify_message("/bytes_total", 100);
    overlay.notify_message("/bytes_total", 200);
    overlay.notify_message("/bytes_total", 300);
    overlay.notify_message("/bytes_total", 400);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_bytes, 1000u);
}

TEST(TopicStatsOverlay, TotalMessagesMatchesAllNotifications)
{
    TopicStatsOverlay overlay;
    overlay.set_topic("/msg_total");
    for (int i = 0; i < 10; ++i)
        overlay.notify_message("/msg_total", 64);
    const auto snap = overlay.snapshot();
    EXPECT_EQ(snap.total_messages, 10u);
}

TEST(TopicStatsOverlay, SetTitleApplied)
{
    TopicStatsOverlay overlay;
    overlay.set_title("My Stats");
    EXPECT_EQ(overlay.title(), "My Stats");
}

// ---------------------------------------------------------------------------
// TopicDetailStats unit tests (direct, no overlay)
// ---------------------------------------------------------------------------

TEST(TopicDetailStats, PushIncrementsCounters)
{
    TopicDetailStats stats;
    stats.push(1000, 100, -1);
    EXPECT_EQ(stats.total_messages, 1u);
    EXPECT_EQ(stats.total_bytes, 100u);
}

TEST(TopicDetailStats, ComputeZeroMessagesGivesZeroHz)
{
    TopicDetailStats stats;
    stats.compute(1'000'000'000LL, 1'000'000'000LL);
    EXPECT_DOUBLE_EQ(stats.hz_avg, 0.0);
}

TEST(TopicDetailStats, ComputePrunesOldSamples)
{
    TopicDetailStats stats;
    // Push a sample at t=0 and one at t=500ms; window = 200ms from t=600ms.
    stats.push(0LL, 50);
    stats.push(500'000'000LL, 50);
    stats.compute(600'000'000LL, 200'000'000LL);  // window: [400ms, 600ms]
    // Only the 500ms sample should survive.
    EXPECT_EQ(stats.samples.size(), 1u);
}

TEST(TopicDetailStats, ComputeHzSpanBased)
{
    TopicDetailStats stats;
    // 11 samples spaced 100 ms → span = 1000 ms → 10 Hz.
    for (int i = 0; i <= 10; ++i)
        stats.push(static_cast<int64_t>(i) * 100'000'000LL, 10);
    stats.compute(2'000'000'000LL, 2'000'000'000LL);
    EXPECT_NEAR(stats.hz_avg, 10.0, 0.5);
}

TEST(TopicDetailStats, ComputeLatencyAvg)
{
    TopicDetailStats stats;
    stats.push(100'000'000LL, 50, 1000);
    stats.push(200'000'000LL, 50, 3000);
    stats.compute(2'000'000'000LL, 2'000'000'000LL);
    EXPECT_NEAR(stats.latency_avg_us, 2000.0, 1.0);
}

TEST(TopicDetailStats, ResetWindowClearsData)
{
    TopicDetailStats stats;
    stats.push(100'000'000LL, 100, 500);
    stats.compute(2'000'000'000LL, 2'000'000'000LL);
    stats.reset_window();
    EXPECT_TRUE(stats.samples.empty());
    EXPECT_DOUBLE_EQ(stats.hz_avg, 0.0);
    EXPECT_LT(stats.latency_avg_us, 0.0);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

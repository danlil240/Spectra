// test_tf_buffer.cpp — Unit tests for the reusable TfBuffer foundation.

#include <gtest/gtest.h>

#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
TransformStamp make_ts(const std::string& parent,
                       const std::string& child,
                       double             tx        = 0.0,
                       double             ty        = 0.0,
                       double             tz        = 0.0,
                       double             qx        = 0.0,
                       double             qy        = 0.0,
                       double             qz        = 0.0,
                       double             qw        = 1.0,
                       bool               is_static = false,
                       uint64_t           recv_ns   = 0)
{
    TransformStamp ts;
    ts.parent_frame = parent;
    ts.child_frame  = child;
    ts.tx           = tx;
    ts.ty           = ty;
    ts.tz           = tz;
    ts.qx           = qx;
    ts.qy           = qy;
    ts.qz           = qz;
    ts.qw           = qw;
    ts.is_static    = is_static;
    ts.recv_ns      = recv_ns;
    return ts;
}
}   // namespace

TEST(TfBuffer, StartsEmpty)
{
    TfBuffer buffer;
    EXPECT_EQ(buffer.frame_count(), 0u);
    EXPECT_TRUE(buffer.all_frames().empty());
}

TEST(TfBuffer, InjectAddsParentAndChild)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 2.0, 3.0, 0, 0, 0, 1, false, 1'000));
    EXPECT_EQ(buffer.frame_count(), 2u);
    EXPECT_TRUE(buffer.has_frame("map"));
    EXPECT_TRUE(buffer.has_frame("base_link"));
}

TEST(TfBuffer, LookupUsesLatestByDefault)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 3.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    const auto result = buffer.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_DOUBLE_EQ(result.tx, 3.0);
}

TEST(TfBuffer, LookupInterpolatesTranslationAtRequestedTime)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 10.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    const auto result = buffer.lookup_transform("map", "base_link", 150);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 5.0, 1e-6);
}

TEST(TfBuffer, CanTransformFailsOutsideHistoryRange)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    EXPECT_FALSE(buffer.can_transform("map", "base_link", 50));
    EXPECT_FALSE(buffer.can_transform("map", "base_link", 250));
    EXPECT_TRUE(buffer.can_transform("map", "base_link", 150));
}

TEST(TfBuffer, ChainedLookupComposesTransforms)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "odom", 1.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("odom", "base_link", 2.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("map", "base_link", 100);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 3.0, 1e-6);
}

TEST(TfBuffer, SnapshotIncludesTreeStats)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "map", 0.0, 0.0, 0.0, 0, 0, 0, 1, true, 100));
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    const auto snapshot = buffer.snapshot();
    EXPECT_EQ(snapshot.total_frames, 3u);
    EXPECT_EQ(snapshot.static_frames, 1u);
    EXPECT_EQ(snapshot.dynamic_frames, 2u);
    EXPECT_NE(std::find(snapshot.roots.begin(), snapshot.roots.end(), "world"),
              snapshot.roots.end());
}

TEST(TfBuffer, ClearDropsAllFrames)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link"));
    buffer.clear();
    EXPECT_EQ(buffer.frame_count(), 0u);
    EXPECT_FALSE(buffer.has_frame("map"));
}

// --- Phase 6 expanded coverage ---

TEST(TfBuffer, SelfTransformReturnsIdentity)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 2.0, 3.0, 0, 0, 0, 1, false, 100));
    const auto result = buffer.lookup_transform("base_link", "base_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_DOUBLE_EQ(result.tx, 0.0);
    EXPECT_DOUBLE_EQ(result.ty, 0.0);
    EXPECT_DOUBLE_EQ(result.tz, 0.0);
    EXPECT_DOUBLE_EQ(result.qw, 1.0);
}

TEST(TfBuffer, InverseLookupChildToParent)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 5.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("base_link", "map", 100);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, -5.0, 1e-5);
}

TEST(TfBuffer, CrossTreeCommonAncestorLookup)
{
    TfBuffer buffer;
    //     world
    //    /      \
    //  arm    sensor
    buffer.inject_transform(make_ts("world", "arm", 1.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("world", "sensor", 0.0, 3.0, 0.0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("arm", "sensor", 100);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, -1.0, 1e-5);
    EXPECT_NEAR(result.ty, 3.0, 1e-5);
}

TEST(TfBuffer, DisconnectedTreesReturnError)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("tree_a", "node_a", 1.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("tree_b", "node_b", 2.0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("node_a", "node_b", 100);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(TfBuffer, UnknownSourceFrameReturnError)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("unknown_frame", "base_link");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unknown_frame"), std::string::npos);
}

TEST(TfBuffer, UnknownTargetFrameReturnError)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("map", "unknown_frame");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unknown_frame"), std::string::npos);
}

TEST(TfBuffer, EmptyFrameIdRejected)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "", 1.0));
    EXPECT_EQ(buffer.frame_count(), 0u);

    buffer.inject_transform(make_ts("", "base_link", 1.0));
    EXPECT_EQ(buffer.frame_count(), 0u);
}

TEST(TfBuffer, SameParentAndChildRejected)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "map", 1.0, 0, 0, 0, 0, 0, 1, false, 100));
    EXPECT_EQ(buffer.frame_count(), 0u);
}

TEST(TfBuffer, LeadingSlashNormalization)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("/map", "/base_link", 3.0, 0, 0, 0, 0, 0, 1, false, 100));

    EXPECT_TRUE(buffer.has_frame("map"));
    EXPECT_TRUE(buffer.has_frame("base_link"));
    EXPECT_TRUE(buffer.has_frame("/map"));

    const auto result = buffer.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 3.0, 1e-6);
}

TEST(TfBuffer, StaticTransformOverwritesPreviousHistory)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "static_link", 1.0, 0.0, 0.0, 0, 0, 0, 1, true, 100));
    buffer.inject_transform(make_ts("world", "static_link", 5.0, 0.0, 0.0, 0, 0, 0, 1, true, 200));

    const auto result = buffer.lookup_transform("world", "static_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 5.0, 1e-6);

    // Static transforms should not interpolate — history has only one entry
    const auto result_at_100 = buffer.lookup_transform("world", "static_link", 100);
    ASSERT_TRUE(result_at_100.ok) << result_at_100.error;
    EXPECT_NEAR(result_at_100.tx, 5.0, 1e-6);
}

TEST(TfBuffer, InterpolatesAtQuarterWay)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 8.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    const auto result = buffer.lookup_transform("map", "base_link", 125);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 2.0, 1e-5);
}

TEST(TfBuffer, InterpolatesAtExactTimestamp)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 10.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    const auto result = buffer.lookup_transform("map", "base_link", 200);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 10.0, 1e-6);
}

TEST(TfBuffer, InterpolatesThreeAxisTranslation)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base", 10.0, 20.0, 30.0, 0, 0, 0, 1, false, 200));

    const auto r = buffer.lookup_transform("map", "base", 150);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_NEAR(r.tx, 5.0, 1e-5);
    EXPECT_NEAR(r.ty, 10.0, 1e-5);
    EXPECT_NEAR(r.tz, 15.0, 1e-5);
}

TEST(TfBuffer, LookupBeforeHistoryFails)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    EXPECT_FALSE(buffer.can_transform("map", "base_link", 50));
    const auto result = buffer.lookup_transform("map", "base_link", 50);
    EXPECT_FALSE(result.ok);
}

TEST(TfBuffer, LookupAfterHistoryFails)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0.0, 0.0, 0, 0, 0, 1, false, 200));

    EXPECT_FALSE(buffer.can_transform("map", "base_link", 300));
}

TEST(TfBuffer, CachePruningDropsOldHistory)
{
    TfBuffer buffer;
    buffer.set_cache_duration_s(1.0);   // 1 second cache

    // Inject 3 samples: at t=1s, t=2s, t=20s
    buffer.inject_transform(
        make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 1'000'000'000));
    buffer.inject_transform(
        make_ts("map", "base_link", 2.0, 0, 0, 0, 0, 0, 1, false, 2'000'000'000));
    buffer.inject_transform(
        make_ts("map", "base_link", 3.0, 0, 0, 0, 0, 0, 1, false, 20'000'000'000));

    // Latest should be the t=20s entry
    const auto result = buffer.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 3.0, 1e-6);

    // After pruning, only the entries within 1s of newest (20s) survive.
    // t=1s and t=2s entries should be pruned, leaving only [19s..20s] window.
    // Interpolation between pruned timestamps should fail since those entries are gone.
    // With a single surviving entry, can_transform returns true (by design — 1 entry always works),
    // but we can verify pruned entries no longer participate in interpolation.
    const auto at_1s = buffer.lookup_transform("map", "base_link", 1'000'000'000);
    const auto at_2s = buffer.lookup_transform("map", "base_link", 2'000'000'000);
    // Both return the sole remaining entry (tx=3.0) since only 1 sample survives
    if (at_1s.ok)
        EXPECT_NEAR(at_1s.tx, 3.0, 1e-6);
    if (at_2s.ok)
        EXPECT_NEAR(at_2s.tx, 3.0, 1e-6);

    // Verify cache_duration_s reflects the setting
    EXPECT_DOUBLE_EQ(buffer.cache_duration_s(), 1.0);
}

TEST(TfBuffer, StaleThresholdGetterSetter)
{
    TfBuffer buffer;
    EXPECT_EQ(buffer.stale_threshold_ms(), 500u);
    buffer.set_stale_threshold_ms(1000);
    EXPECT_EQ(buffer.stale_threshold_ms(), 1000u);
}

TEST(TfBuffer, HzWindowGetterSetter)
{
    TfBuffer buffer;
    EXPECT_EQ(buffer.hz_window_ms(), 1000u);
    buffer.set_hz_window_ms(2000);
    EXPECT_EQ(buffer.hz_window_ms(), 2000u);
}

TEST(TfBuffer, CacheDurationGetterSetter)
{
    TfBuffer buffer;
    EXPECT_DOUBLE_EQ(buffer.cache_duration_s(), 10.0);
    buffer.set_cache_duration_s(5.0);
    EXPECT_DOUBLE_EQ(buffer.cache_duration_s(), 5.0);
}

TEST(TfBuffer, EulerAnglesIdentity)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 0.0, 0.0, 0.0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.roll_deg, 0.0, 1e-6);
    EXPECT_NEAR(result.pitch_deg, 0.0, 1e-6);
    EXPECT_NEAR(result.yaw_deg, 0.0, 1e-6);
}

TEST(TfBuffer, AllFramesSorted)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "odom", 0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("odom", "base_link", 0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto frames = buffer.all_frames();
    ASSERT_EQ(frames.size(), 3u);
    // Should be sorted: base_link, odom, world
    EXPECT_EQ(frames[0], "base_link");
    EXPECT_EQ(frames[1], "odom");
    EXPECT_EQ(frames[2], "world");
}

TEST(TfBuffer, SnapshotSortedFrameList)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "odom", 0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("odom", "base_link", 0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto snapshot = buffer.snapshot();
    ASSERT_EQ(snapshot.frames.size(), 3u);
    EXPECT_EQ(snapshot.frames[0].frame_id, "base_link");
    EXPECT_EQ(snapshot.frames[1].frame_id, "odom");
    EXPECT_EQ(snapshot.frames[2].frame_id, "world");
}

TEST(TfBuffer, SnapshotChildrenMap)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "odom", 0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("odom", "base_link", 0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto snapshot = buffer.snapshot();
    ASSERT_NE(snapshot.children.find("world"), snapshot.children.end());
    EXPECT_EQ(snapshot.children.at("world").size(), 1u);
    EXPECT_EQ(snapshot.children.at("world")[0], "odom");
    ASSERT_NE(snapshot.children.find("odom"), snapshot.children.end());
    EXPECT_EQ(snapshot.children.at("odom").size(), 1u);
    EXPECT_EQ(snapshot.children.at("odom")[0], "base_link");
}

TEST(TfBuffer, SnapshotSingleRoot)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "odom", 0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("odom", "base_link", 0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto snapshot = buffer.snapshot();
    ASSERT_EQ(snapshot.roots.size(), 1u);
    EXPECT_EQ(snapshot.roots[0], "world");
}

TEST(TfBuffer, SnapshotMultipleRoots)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("tree_a", "node_a", 0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("tree_b", "node_b", 0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto snapshot = buffer.snapshot();
    EXPECT_GE(snapshot.roots.size(), 2u);
}

TEST(TfBuffer, SnapshotStaticMixedCounts)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "odom", 0, 0, 0, 0, 0, 0, 1, true, 100));
    buffer.inject_transform(make_ts("odom", "base_link", 0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("odom", "camera", 0, 0, 0, 0, 0, 0, 1, true, 100));

    const auto snapshot = buffer.snapshot();
    EXPECT_EQ(snapshot.total_frames, 4u);
    EXPECT_EQ(snapshot.static_frames, 2u);
}

TEST(TfBuffer, CanTransformSameFrame)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 100));

    EXPECT_TRUE(buffer.can_transform("base_link", "base_link"));
    EXPECT_TRUE(buffer.can_transform("map", "map"));
}

TEST(TfBuffer, LongChainLookup)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "a", 1.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("a", "b", 2.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("b", "c", 3.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("c", "d", 4.0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("world", "d", 100);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 10.0, 1e-5);
}

TEST(TfBuffer, LongChainInverseLookup)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("world", "a", 1.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("a", "b", 2.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("b", "c", 3.0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto result = buffer.lookup_transform("c", "world", 100);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, -6.0, 1e-5);
}

TEST(TfBuffer, LookupWithZeroTimeUsesLatest)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 100));
    buffer.inject_transform(make_ts("map", "base_link", 7.0, 0, 0, 0, 0, 0, 1, false, 200));

    const auto result = buffer.lookup_transform("map", "base_link", 0);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 7.0, 1e-6);
}

TEST(TfBuffer, EmptySourceOrTargetReturnError)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 100));

    const auto r1 = buffer.lookup_transform("", "base_link");
    EXPECT_FALSE(r1.ok);

    const auto r2 = buffer.lookup_transform("map", "");
    EXPECT_FALSE(r2.ok);
}

TEST(TfBuffer, HasFrameReturnsFalseForUnknown)
{
    TfBuffer buffer;
    EXPECT_FALSE(buffer.has_frame("nonexistent"));
}

TEST(TfBuffer, MultipleUpdatesKeepLatest)
{
    TfBuffer buffer;
    for (uint64_t i = 1; i <= 10; ++i)
    {
        buffer.inject_transform(
            make_ts("map", "base_link", static_cast<double>(i), 0, 0, 0, 0, 0, 1, false, i * 100));
    }

    const auto result = buffer.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_NEAR(result.tx, 10.0, 1e-6);
    EXPECT_EQ(buffer.frame_count(), 2u);
}

TEST(TfBuffer, ClearThenReinject)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link", 1.0, 0, 0, 0, 0, 0, 1, false, 100));
    EXPECT_EQ(buffer.frame_count(), 2u);

    buffer.clear();
    EXPECT_EQ(buffer.frame_count(), 0u);

    buffer.inject_transform(make_ts("world", "odom", 2.0, 0, 0, 0, 0, 0, 1, false, 200));
    EXPECT_EQ(buffer.frame_count(), 2u);
    EXPECT_FALSE(buffer.has_frame("map"));
    EXPECT_TRUE(buffer.has_frame("world"));
}

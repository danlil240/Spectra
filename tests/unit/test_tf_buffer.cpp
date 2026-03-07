// test_tf_buffer.cpp — Unit tests for the reusable TfBuffer foundation.

#include <gtest/gtest.h>

#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

namespace
{
TransformStamp make_ts(const std::string& parent,
                       const std::string& child,
                       double tx = 0.0,
                       double ty = 0.0,
                       double tz = 0.0,
                       double qx = 0.0,
                       double qy = 0.0,
                       double qz = 0.0,
                       double qw = 1.0,
                       bool is_static = false,
                       uint64_t recv_ns = 0)
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
    EXPECT_NE(std::find(snapshot.roots.begin(), snapshot.roots.end(), "world"), snapshot.roots.end());
}

TEST(TfBuffer, ClearDropsAllFrames)
{
    TfBuffer buffer;
    buffer.inject_transform(make_ts("map", "base_link"));
    buffer.clear();
    EXPECT_EQ(buffer.frame_count(), 0u);
    EXPECT_FALSE(buffer.has_frame("map"));
}

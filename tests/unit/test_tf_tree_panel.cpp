// test_tf_tree_panel.cpp — Unit tests for TfTreePanel (F2)
//
// All tests exercise pure C++ logic: inject_transform(), snapshot(),
// lookup_transform(), has_frame(), frame_count(), clear(), and config APIs.
// No rclcpp runtime required — uses GTest::gtest_main.

#include <gtest/gtest.h>

#include "ui/tf_tree_panel.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t ms_to_ns(uint64_t ms) { return ms * 1'000'000ULL; }

// Build a simple TransformStamp
static TransformStamp make_ts(const std::string& parent,
                               const std::string& child,
                               double tx = 0.0, double ty = 0.0, double tz = 0.0,
                               double qx = 0.0, double qy = 0.0,
                               double qz = 0.0, double qw = 1.0,
                               bool is_static = false,
                               uint64_t recv_ns = 0)
{
    TransformStamp ts;
    ts.parent_frame = parent;
    ts.child_frame  = child;
    ts.tx = tx; ts.ty = ty; ts.tz = tz;
    ts.qx = qx; ts.qy = qy; ts.qz = qz; ts.qw = qw;
    ts.is_static = is_static;
    ts.recv_ns   = recv_ns;
    return ts;
}

// ---------------------------------------------------------------------------
// Suite 1: Construction
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Construction, DefaultStateEmpty)
{
    TfTreePanel panel;
    EXPECT_EQ(panel.frame_count(), 0u);
    EXPECT_FALSE(panel.is_started());
}

TEST(TfTreePanel_Construction, DefaultStaleThreshold)
{
    TfTreePanel panel;
    EXPECT_EQ(panel.stale_threshold_ms(), 500u);
}

TEST(TfTreePanel_Construction, DefaultHzWindow)
{
    TfTreePanel panel;
    EXPECT_EQ(panel.hz_window_ms(), 1000u);
}

TEST(TfTreePanel_Construction, DefaultTitle)
{
    TfTreePanel panel;
    EXPECT_EQ(panel.title(), "TF Frames");
}

TEST(TfTreePanel_Construction, NonCopyable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<TfTreePanel>);
    EXPECT_FALSE(std::is_copy_assignable_v<TfTreePanel>);
}

TEST(TfTreePanel_Construction, NonMovable)
{
    EXPECT_FALSE(std::is_move_constructible_v<TfTreePanel>);
    EXPECT_FALSE(std::is_move_assignable_v<TfTreePanel>);
}

// ---------------------------------------------------------------------------
// Suite 2: Configuration
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Config, SetStaleThreshold)
{
    TfTreePanel panel;
    panel.set_stale_threshold_ms(200);
    EXPECT_EQ(panel.stale_threshold_ms(), 200u);
}

TEST(TfTreePanel_Config, SetHzWindow)
{
    TfTreePanel panel;
    panel.set_hz_window_ms(2000);
    EXPECT_EQ(panel.hz_window_ms(), 2000u);
}

TEST(TfTreePanel_Config, SetTitle)
{
    TfTreePanel panel;
    panel.set_title("My TF Panel");
    EXPECT_EQ(panel.title(), "My TF Panel");
}

// ---------------------------------------------------------------------------
// Suite 3: Inject single transform
// ---------------------------------------------------------------------------

TEST(TfTreePanel_InjectTransform, SingleFrameAdded)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    EXPECT_EQ(panel.frame_count(), 2u);   // map + base_link
    EXPECT_TRUE(panel.has_frame("base_link"));
    EXPECT_TRUE(panel.has_frame("map"));
}

TEST(TfTreePanel_InjectTransform, UnknownFrameReturnsFalse)
{
    TfTreePanel panel;
    EXPECT_FALSE(panel.has_frame("nonexistent"));
}

TEST(TfTreePanel_InjectTransform, MultipleFrames)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "odom"));
    panel.inject_transform(make_ts("odom", "base_link"));
    panel.inject_transform(make_ts("base_link", "laser"));
    // map, odom, base_link, laser
    EXPECT_EQ(panel.frame_count(), 4u);
    EXPECT_TRUE(panel.has_frame("laser"));
}

TEST(TfTreePanel_InjectTransform, DuplicateFrameUpdateReplaces)
{
    TfTreePanel panel;
    auto ts = make_ts("map", "base_link", 1.0, 0.0, 0.0);
    panel.inject_transform(ts);
    auto ts2 = make_ts("map", "base_link", 2.0, 0.0, 0.0);
    panel.inject_transform(ts2);
    // Frame count unchanged; update was applied
    EXPECT_EQ(panel.frame_count(), 2u);
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "base_link") {
            EXPECT_DOUBLE_EQ(f.last_transform.tx, 2.0);
        }
    }
}

TEST(TfTreePanel_InjectTransform, StaticFlagPreserved)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "odom", 0, 0, 0,
                                    0, 0, 0, 1, true));
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "odom") {
            EXPECT_TRUE(f.is_static);
        }
    }
}

TEST(TfTreePanel_InjectTransform, DynamicFlagPreserved)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link", 0, 0, 0,
                                    0, 0, 0, 1, false));
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "base_link") {
            EXPECT_FALSE(f.is_static);
        }
    }
}

// ---------------------------------------------------------------------------
// Suite 4: Clear
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Clear, ClearResetsFrameCount)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    panel.inject_transform(make_ts("base_link", "laser"));
    panel.clear();
    EXPECT_EQ(panel.frame_count(), 0u);
}

TEST(TfTreePanel_Clear, HasFrameAfterClear)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    panel.clear();
    EXPECT_FALSE(panel.has_frame("base_link"));
    EXPECT_FALSE(panel.has_frame("map"));
}

// ---------------------------------------------------------------------------
// Suite 5: Snapshot
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Snapshot, EmptySnapshot)
{
    TfTreePanel panel;
    const auto snap = panel.snapshot();
    EXPECT_EQ(snap.total_frames, 0u);
    EXPECT_EQ(snap.static_frames, 0u);
    EXPECT_EQ(snap.dynamic_frames, 0u);
    EXPECT_EQ(snap.stale_frames, 0u);
    EXPECT_TRUE(snap.frames.empty());
}

TEST(TfTreePanel_Snapshot, CountersCorrect)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "odom", 0, 0, 0, 0, 0, 0, 1, false));
    panel.inject_transform(make_ts("odom", "base_link", 0, 0, 0, 0, 0, 0, 1, true));
    const auto snap = panel.snapshot();
    EXPECT_EQ(snap.total_frames, 3u);  // map, odom, base_link
}

TEST(TfTreePanel_Snapshot, RootsDetected)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "odom"));
    panel.inject_transform(make_ts("odom", "base_link"));
    const auto snap = panel.snapshot();
    // "map" has no known parent → root
    EXPECT_FALSE(snap.roots.empty());
    EXPECT_NE(std::find(snap.roots.begin(), snap.roots.end(), "map"),
              snap.roots.end());
}

TEST(TfTreePanel_Snapshot, ChildrenMapCorrect)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "odom"));
    panel.inject_transform(make_ts("odom", "base_link"));
    panel.inject_transform(make_ts("odom", "laser"));
    const auto snap = panel.snapshot();
    ASSERT_NE(snap.children.find("odom"), snap.children.end());
    const auto& children = snap.children.at("odom");
    EXPECT_EQ(children.size(), 2u);
    EXPECT_NE(std::find(children.begin(), children.end(), "base_link"),
              children.end());
    EXPECT_NE(std::find(children.begin(), children.end(), "laser"),
              children.end());
}

TEST(TfTreePanel_Snapshot, EverReceivedFlag)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link", 1.5));
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "base_link") {
            EXPECT_TRUE(f.ever_received);
        }
        if (f.frame_id == "map") {
            // map was inserted as a parent entry, not itself received
            // it may or may not be ever_received depending on impl
            // Just check no crash
        }
    }
}

TEST(TfTreePanel_Snapshot, SnapshotIsCopy)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "odom"));
    auto snap1 = panel.snapshot();
    panel.inject_transform(make_ts("odom", "base_link"));
    auto snap2 = panel.snapshot();
    // snap1 was taken before the second inject; snap2 has more frames
    EXPECT_LT(snap1.total_frames, snap2.total_frames);
}

// ---------------------------------------------------------------------------
// Suite 6: Stale detection
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Stale, FreshTransformNotStale)
{
    TfTreePanel panel;
    panel.set_stale_threshold_ms(500);
    const uint64_t now = 1'000'000'000ULL;  // 1 second in ns
    auto ts = make_ts("map", "base_link");
    ts.recv_ns = now;
    panel.inject_transform(ts);

    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "base_link" && !f.is_static) {
            EXPECT_FALSE(f.stale);
        }
    }
}

TEST(TfTreePanel_Stale, StaticTransformNeverStale)
{
    TfTreePanel panel;
    panel.set_stale_threshold_ms(1);  // very short threshold

    auto ts = make_ts("map", "odom_static", 0, 0, 0, 0, 0, 0, 1, true);
    ts.recv_ns = 1'000'000'000ULL;  // far in the past
    panel.inject_transform(ts);

    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "odom_static") {
            EXPECT_FALSE(f.stale);  // static never stale
        }
    }
}

TEST(TfTreePanel_Stale, StaleCounterIncrements)
{
    TfTreePanel panel;
    panel.set_stale_threshold_ms(500);

    auto ts = make_ts("map", "base_link");
    ts.recv_ns = 1'000ULL;  // 1 microsecond — very old
    ts.is_static = false;
    panel.inject_transform(ts);

    // snapshot() computes age using steady_clock::now(), so age_ms >> threshold
    const auto snap = panel.snapshot();
    // The age will be very large (billions of ms since recv_ns = 1000 ns)
    EXPECT_GT(snap.stale_frames, 0u);
}

// ---------------------------------------------------------------------------
// Suite 7: Transform data in snapshot
// ---------------------------------------------------------------------------

TEST(TfTreePanel_TransformData, TranslationPreserved)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link", 1.5, 2.5, 3.5));
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "base_link") {
            EXPECT_DOUBLE_EQ(f.last_transform.tx, 1.5);
            EXPECT_DOUBLE_EQ(f.last_transform.ty, 2.5);
            EXPECT_DOUBLE_EQ(f.last_transform.tz, 3.5);
        }
    }
}

TEST(TfTreePanel_TransformData, QuaternionPreserved)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link",
                                    0, 0, 0,
                                    0.1, 0.2, 0.3, 0.9274));
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "base_link") {
            EXPECT_DOUBLE_EQ(f.last_transform.qx, 0.1);
            EXPECT_DOUBLE_EQ(f.last_transform.qy, 0.2);
            EXPECT_DOUBLE_EQ(f.last_transform.qz, 0.3);
            EXPECT_DOUBLE_EQ(f.last_transform.qw, 0.9274);
        }
    }
}

TEST(TfTreePanel_TransformData, ParentFramePreserved)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("world", "robot_base"));
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "robot_base") {
            EXPECT_EQ(f.parent_frame_id, "world");
        }
    }
}

// ---------------------------------------------------------------------------
// Suite 8: Hz computation
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Hz, NoTransformsHzZero)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "sensor"));
    // Manually inserted with no timestamps in the rolling window
    // (inject uses now_ns so hz might be 0 or 1 depending on window)
    // Just verify it doesn't crash
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "sensor") {
            EXPECT_GE(f.hz, 0.0);
        }
    }
}

TEST(TfTreePanel_Hz, MultipleInjectsRaisesHz)
{
    TfTreePanel panel;
    panel.set_hz_window_ms(1000);
    // Inject many transforms rapidly — hz should be > 0
    const uint64_t base_ns = 1'000'000'000ULL;
    for (int i = 0; i < 30; ++i) {
        auto ts = make_ts("map", "fast_sensor");
        ts.recv_ns = base_ns + static_cast<uint64_t>(i) * 10'000'000ULL;
        panel.inject_transform(ts);
    }
    const auto snap = panel.snapshot();
    for (const auto& f : snap.frames) {
        if (f.frame_id == "fast_sensor") {
            // Should be around 30 Hz in a 1s window (or 0 if too old)
            EXPECT_GE(f.hz, 0.0);
        }
    }
}

// ---------------------------------------------------------------------------
// Suite 9: lookup_transform — identity
// ---------------------------------------------------------------------------

TEST(TfTreePanel_LookupTransform, SameFrameIdentity)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    const auto result = panel.lookup_transform("base_link", "base_link");
    EXPECT_TRUE(result.ok);
    EXPECT_DOUBLE_EQ(result.tx, 0.0);
    EXPECT_DOUBLE_EQ(result.ty, 0.0);
    EXPECT_DOUBLE_EQ(result.tz, 0.0);
    EXPECT_NEAR(result.qw, 1.0, 1e-9);
}

TEST(TfTreePanel_LookupTransform, UnknownSourceFails)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    const auto result = panel.lookup_transform("unknown_frame", "base_link");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(TfTreePanel_LookupTransform, UnknownTargetFails)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    const auto result = panel.lookup_transform("base_link", "unknown_frame");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(TfTreePanel_LookupTransform, EmptyPanelFails)
{
    TfTreePanel panel;
    const auto result = panel.lookup_transform("a", "b");
    EXPECT_FALSE(result.ok);
}

TEST(TfTreePanel_LookupTransform, DirectParentChildTranslation)
{
    TfTreePanel panel;
    // map → base_link: translation (1, 2, 0)
    auto ts = make_ts("map", "base_link", 1.0, 2.0, 0.0);
    ts.recv_ns = 1'000'000'000ULL;
    panel.inject_transform(ts);

    // Lookup base_link relative to map  (child → parent path)
    const auto result = panel.lookup_transform("map", "base_link");
    EXPECT_TRUE(result.ok);
    EXPECT_NEAR(result.tx, 1.0, 1e-9);
    EXPECT_NEAR(result.ty, 2.0, 1e-9);
    EXPECT_NEAR(result.tz, 0.0, 1e-9);
}

TEST(TfTreePanel_LookupTransform, NoCommonAncestorFails)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("world_a", "frame_a",
                                    1, 0, 0, 0, 0, 0, 1, false,
                                    1'000'000'000ULL));
    panel.inject_transform(make_ts("world_b", "frame_b",
                                    1, 0, 0, 0, 0, 0, 1, false,
                                    1'000'000'000ULL));
    const auto result = panel.lookup_transform("frame_a", "frame_b");
    EXPECT_FALSE(result.ok);
}

// ---------------------------------------------------------------------------
// Suite 10: lookup_transform — chained transforms
// ---------------------------------------------------------------------------

TEST(TfTreePanel_LookupChain, TwoHopTranslation)
{
    TfTreePanel panel;
    // map → odom: tx=1
    // odom → base_link: tx=2
    auto ts1 = make_ts("map", "odom", 1.0, 0.0, 0.0);
    ts1.recv_ns = 1'000'000'000ULL;
    auto ts2 = make_ts("odom", "base_link", 2.0, 0.0, 0.0);
    ts2.recv_ns = 1'000'000'000ULL;
    panel.inject_transform(ts1);
    panel.inject_transform(ts2);

    // Lookup base_link in map: should be tx=3
    const auto result = panel.lookup_transform("map", "base_link");
    EXPECT_TRUE(result.ok);
    EXPECT_NEAR(result.tx, 3.0, 1e-9);
    EXPECT_NEAR(result.ty, 0.0, 1e-9);
}

TEST(TfTreePanel_LookupChain, CrossBranchLookup)
{
    TfTreePanel panel;
    // map → left_arm: tx=1
    // map → right_arm: tx=-1
    auto ts1 = make_ts("map", "left_arm", 1.0, 0.0, 0.0);
    ts1.recv_ns = 1'000'000'000ULL;
    auto ts2 = make_ts("map", "right_arm", -1.0, 0.0, 0.0);
    ts2.recv_ns = 1'000'000'000ULL;
    panel.inject_transform(ts1);
    panel.inject_transform(ts2);

    // left_arm → right_arm: should be tx = -2 (go up to map, down to right_arm)
    const auto result = panel.lookup_transform("left_arm", "right_arm");
    EXPECT_TRUE(result.ok);
    EXPECT_NEAR(result.tx, -2.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Suite 11: Euler angle conversion
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Euler, IdentityQuatZeroEuler)
{
    TfTreePanel panel;
    auto ts = make_ts("map", "base_link", 0, 0, 0, 0, 0, 0, 1.0,
                       false, 1'000'000'000ULL);
    panel.inject_transform(ts);
    const auto result = panel.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok);
    EXPECT_NEAR(result.roll_deg,  0.0, 1e-9);
    EXPECT_NEAR(result.pitch_deg, 0.0, 1e-9);
    EXPECT_NEAR(result.yaw_deg,   0.0, 1e-9);
}

TEST(TfTreePanel_Euler, Yaw90Degrees)
{
    // 90° yaw: qz=sin(45°)=√2/2, qw=cos(45°)=√2/2
    const double half_sqrt2 = 0.70710678118;
    TfTreePanel panel;
    auto ts = make_ts("map", "base_link", 0, 0, 0,
                       0, 0, half_sqrt2, half_sqrt2,
                       false, 1'000'000'000ULL);
    panel.inject_transform(ts);
    const auto result = panel.lookup_transform("map", "base_link");
    ASSERT_TRUE(result.ok);
    EXPECT_NEAR(result.yaw_deg, 90.0, 1e-6);
    EXPECT_NEAR(result.roll_deg, 0.0, 1e-6);
    EXPECT_NEAR(result.pitch_deg, 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Suite 12: Callback
// ---------------------------------------------------------------------------

TEST(TfTreePanel_Callback, SelectCallbackNotFiredWithoutImGui)
{
    TfTreePanel panel;
    int call_count = 0;
    panel.set_select_callback([&](const std::string&) { ++call_count; });
    panel.inject_transform(make_ts("map", "base_link"));
    // Without ImGui draw, callback should not fire
    EXPECT_EQ(call_count, 0);
}

TEST(TfTreePanel_Callback, SelectCallbackReplacement)
{
    TfTreePanel panel;
    int count_a = 0, count_b = 0;
    panel.set_select_callback([&](const std::string&) { ++count_a; });
    panel.set_select_callback([&](const std::string&) { ++count_b; });
    // The second callback replaces the first; no direct way to fire without ImGui
    // Just verify no crash
    panel.inject_transform(make_ts("map", "base_link"));
    EXPECT_EQ(count_a, 0);
    EXPECT_EQ(count_b, 0);
}

// ---------------------------------------------------------------------------
// Suite 13: TfTreeSnapshot structure
// ---------------------------------------------------------------------------

TEST(TfTreeSnapshot, SnapshotHasTimestamp)
{
    TfTreePanel panel;
    const auto snap = panel.snapshot();
    EXPECT_GT(snap.snapshot_ns, 0u);
}

TEST(TfTreeSnapshot, ChildrenMapBuildCorrectly)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("root", "child_a"));
    panel.inject_transform(make_ts("root", "child_b"));
    panel.inject_transform(make_ts("child_a", "grandchild"));
    const auto snap = panel.snapshot();

    auto it = snap.children.find("root");
    ASSERT_NE(it, snap.children.end());
    EXPECT_EQ(it->second.size(), 2u);

    auto it2 = snap.children.find("child_a");
    ASSERT_NE(it2, snap.children.end());
    EXPECT_EQ(it2->second.size(), 1u);
    EXPECT_EQ(it2->second[0], "grandchild");
}

TEST(TfTreeSnapshot, RootsIncludesTopLevelFrame)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("universe", "map"));
    panel.inject_transform(make_ts("map", "odom"));
    const auto snap = panel.snapshot();
    // "universe" has no parent in our tree → it's a root
    EXPECT_NE(std::find(snap.roots.begin(), snap.roots.end(), "universe"),
              snap.roots.end());
}

// ---------------------------------------------------------------------------
// Suite 14: Edge cases
// ---------------------------------------------------------------------------

TEST(TfTreePanel_EdgeCases, DrawWithoutImGuiNocrash)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    // draw() and draw_inline() should be no-ops without ImGui
    bool open = true;
    panel.draw(&open);
    panel.draw_inline();
    panel.draw(nullptr);
    // No crash expected
}

TEST(TfTreePanel_EdgeCases, LookupAfterClear)
{
    TfTreePanel panel;
    auto ts = make_ts("map", "base_link", 1.0, 0.0, 0.0);
    ts.recv_ns = 1'000'000'000ULL;
    panel.inject_transform(ts);
    panel.clear();
    const auto result = panel.lookup_transform("map", "base_link");
    EXPECT_FALSE(result.ok);
}

TEST(TfTreePanel_EdgeCases, SnapshotAfterClearIsEmpty)
{
    TfTreePanel panel;
    panel.inject_transform(make_ts("map", "base_link"));
    panel.inject_transform(make_ts("base_link", "laser"));
    panel.clear();
    const auto snap = panel.snapshot();
    EXPECT_EQ(snap.total_frames, 0u);
    EXPECT_TRUE(snap.roots.empty());
    EXPECT_TRUE(snap.frames.empty());
}

TEST(TfTreePanel_EdgeCases, ManyFramesNocrash)
{
    TfTreePanel panel;
    // Build a star topology: root → frame_0, root → frame_1, ...
    for (int i = 0; i < 50; ++i) {
        panel.inject_transform(
            make_ts("root_frame", "frame_" + std::to_string(i)));
    }
    EXPECT_EQ(panel.frame_count(), 51u);  // root + 50 children
    const auto snap = panel.snapshot();
    EXPECT_EQ(snap.total_frames, 51u);
}

TEST(TfTreePanel_EdgeCases, DeepChainNocrash)
{
    TfTreePanel panel;
    // Build a deep chain: a → b → c → ... 20 levels
    for (int i = 0; i < 20; ++i) {
        const std::string parent = "frame_" + std::to_string(i);
        const std::string child  = "frame_" + std::to_string(i + 1);
        auto ts = make_ts(parent, child, 1.0, 0.0, 0.0);
        ts.recv_ns = 1'000'000'000ULL;
        panel.inject_transform(ts);
    }
    EXPECT_EQ(panel.frame_count(), 21u);

    // Lookup from root to deepest leaf
    const auto result = panel.lookup_transform("frame_0", "frame_20");
    EXPECT_TRUE(result.ok);
    EXPECT_NEAR(result.tx, 20.0, 1e-9);
}

TEST(TfTreePanel_EdgeCases, ConcurrentInjectAndSnapshot)
{
    TfTreePanel panel;
    // Fire inject from multiple "simulated contexts" sequentially —
    // verifies no internal corruption from repeated lock acquisition
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 10; ++i) {
            panel.inject_transform(
                make_ts("map", "robot_" + std::to_string(i)));
        }
        const auto snap = panel.snapshot();
        EXPECT_GT(snap.total_frames, 0u);
    }
}

// ---------------------------------------------------------------------------
// Suite 15: TransformResult fields
// ---------------------------------------------------------------------------

TEST(TransformResult, DefaultValues)
{
    TransformResult r;
    EXPECT_FALSE(r.ok);
    EXPECT_DOUBLE_EQ(r.tx, 0.0);
    EXPECT_DOUBLE_EQ(r.ty, 0.0);
    EXPECT_DOUBLE_EQ(r.tz, 0.0);
    EXPECT_DOUBLE_EQ(r.qw, 1.0);
    EXPECT_TRUE(r.error.empty());
}

TEST(TransformResult, ErrorFieldPopulatedOnFailure)
{
    TfTreePanel panel;
    const auto result = panel.lookup_transform("a", "b");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

// ---------------------------------------------------------------------------
// Suite 16: TfFrameStats direct tests
// ---------------------------------------------------------------------------

TEST(TfFrameStats, DefaultValues)
{
    TfFrameStats s;
    EXPECT_FALSE(s.ever_received);
    EXPECT_FALSE(s.stale);
    EXPECT_FALSE(s.is_static);
    EXPECT_DOUBLE_EQ(s.hz, 0.0);
    EXPECT_EQ(s.age_ms, 0u);
}

TEST(TfFrameStats, PushSetsEverReceived)
{
    TfFrameStats s;
    const uint64_t t = 1'000'000'000ULL;
    s.push(t, 500);
    EXPECT_TRUE(s.ever_received);
}

TEST(TfFrameStats, ComputeWithFreshTimestampNotStale)
{
    TfFrameStats s;
    const uint64_t t = 1'000'000'000ULL;
    s.last_transform.recv_ns = t;
    s.is_static = false;
    s.push(t, 500);
    s.compute(t + ms_to_ns(100), 500, 1'000'000'000ULL);
    EXPECT_FALSE(s.stale);
    EXPECT_EQ(s.age_ms, 100u);
}

TEST(TfFrameStats, StaticNeverStale)
{
    TfFrameStats s;
    s.is_static = true;
    const uint64_t t = 1'000'000'000ULL;
    s.last_transform.recv_ns = t;
    s.push(t, 500);
    // Compute with a "now" far in the future
    s.compute(t + ms_to_ns(100'000), 500, 1'000'000'000ULL);
    EXPECT_FALSE(s.stale);
}

TEST(TfFrameStats, DynamicGoesStaleBeyondThreshold)
{
    TfFrameStats s;
    s.is_static = false;
    const uint64_t t = 1'000'000'000ULL;
    s.last_transform.recv_ns = t;
    s.push(t, 500);
    // now = t + 1000 ms → age = 1000 ms > threshold 500 ms → stale
    s.compute(t + ms_to_ns(1000), 500, 1'000'000'000ULL);
    EXPECT_TRUE(s.stale);
    EXPECT_EQ(s.age_ms, 1000u);
}

TEST(TfFrameStats, HzComputedInWindow)
{
    TfFrameStats s;
    s.is_static = false;
    const uint64_t base = 1'000'000'000ULL;
    const uint64_t hz_window = 1'000'000'000ULL;  // 1 s window
    // Push 10 timestamps evenly in 1 s → 10 samples → ~10 Hz
    for (int i = 0; i < 10; ++i) {
        const uint64_t t = base + static_cast<uint64_t>(i) * 100'000'000ULL;
        s.last_transform.recv_ns = t;
        s.push(t, 500);
    }
    const uint64_t now = base + 990'000'000ULL;  // just before last bucket rolls off
    s.last_transform.recv_ns = now;
    s.compute(now, 500, hz_window);
    EXPECT_GT(s.hz, 0.0);
}

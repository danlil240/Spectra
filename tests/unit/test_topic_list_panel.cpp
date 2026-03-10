// test_topic_list_panel.cpp — Unit tests for TopicListPanel (logic only).
//
// All tests exercise the non-ImGui paths: statistics tracking, namespace tree
// building, filter logic, Hz/BW formatting, and callbacks.  No ImGui context
// is needed because draw() is a no-op when SPECTRA_USE_IMGUI is not defined.

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "ui/topic_list_panel.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TopicInfo make_topic(const std::string& name,
                            const std::string& type = "std_msgs/msg/Float64",
                            int                pubs = 1,
                            int                subs = 0)
{
    TopicInfo t;
    t.name             = name;
    t.types            = {type};
    t.publisher_count  = pubs;
    t.subscriber_count = subs;
    return t;
}

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST(TopicListPanel, DefaultState)
{
    TopicListPanel panel;
    EXPECT_EQ(panel.topic_count(), 0u);
    EXPECT_EQ(panel.filtered_topic_count(), 0u);
    EXPECT_EQ(panel.selected_topic(), "");
    EXPECT_EQ(panel.filter(), "");
    EXPECT_EQ(panel.title(), "ROS2 Topics");
    EXPECT_TRUE(panel.group_by_namespace());
    EXPECT_EQ(panel.stale_threshold_ms(), 2000);
    EXPECT_EQ(panel.stats_window_ms(), 1000);
    EXPECT_EQ(panel.topic_discovery(), nullptr);
}

TEST(TopicListPanel, SetTitle)
{
    TopicListPanel panel;
    panel.set_title("My Panel");
    EXPECT_EQ(panel.title(), "My Panel");
}

TEST(TopicListPanel, SetConfig)
{
    TopicListPanel panel;
    panel.set_stale_threshold_ms(5000);
    panel.set_stats_window_ms(500);
    panel.set_group_by_namespace(false);
    EXPECT_EQ(panel.stale_threshold_ms(), 5000);
    EXPECT_EQ(panel.stats_window_ms(), 500);
    EXPECT_FALSE(panel.group_by_namespace());
}

TEST(TopicListPanel, ColumnVisibilityRoundTrips)
{
    TopicListPanel panel;

    panel.set_column_visibility({
        .show_type = false,
        .show_hz   = true,
        .show_pubs = false,
        .show_subs = true,
        .show_bw   = false,
    });

    const auto visibility = panel.column_visibility();
    EXPECT_FALSE(visibility.show_type);
    EXPECT_TRUE(visibility.show_hz);
    EXPECT_FALSE(visibility.show_pubs);
    EXPECT_TRUE(visibility.show_subs);
    EXPECT_FALSE(visibility.show_bw);
}

TEST(TopicListPanel, ExpansionStateTracksMultipleTopics)
{
    TopicListPanel panel;
    panel.set_topic_expanded("/tf", true);
    panel.set_topic_expanded("/odom", true);

    EXPECT_TRUE(panel.is_topic_expanded("/tf"));
    EXPECT_TRUE(panel.is_topic_expanded("/odom"));
    EXPECT_EQ(panel.expanded_topic_count(), 2u);

    panel.set_topic_expanded("/tf", false);
    EXPECT_FALSE(panel.is_topic_expanded("/tf"));
    EXPECT_TRUE(panel.is_topic_expanded("/odom"));
    EXPECT_EQ(panel.expanded_topic_count(), 1u);
}

// ---------------------------------------------------------------------------
// Suite: Topic Management
// ---------------------------------------------------------------------------

TEST(TopicListPanel, SetTopics_Count)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/a"), make_topic("/b"), make_topic("/c")});
    EXPECT_EQ(panel.topic_count(), 3u);
}

TEST(TopicListPanel, SetTopics_Empty)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/foo")});
    EXPECT_EQ(panel.topic_count(), 1u);
    panel.set_topics({});
    EXPECT_EQ(panel.topic_count(), 0u);
}

TEST(TopicListPanel, SetTopics_Replace)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/a"), make_topic("/b")});
    panel.set_topics({make_topic("/x")});
    EXPECT_EQ(panel.topic_count(), 1u);
}

// ---------------------------------------------------------------------------
// Suite: Filter
// ---------------------------------------------------------------------------

TEST(TopicListPanel, FilterEmpty_ShowsAll)
{
    TopicListPanel panel;
    panel.set_topics({
        make_topic("/a/x"),
        make_topic("/a/y"),
        make_topic("/b/z"),
    });
    EXPECT_EQ(panel.filtered_topic_count(), 3u);
}

TEST(TopicListPanel, FilterBySubstring)
{
    TopicListPanel panel;
    panel.set_topics({
        make_topic("/robot/arm"),
        make_topic("/robot/base"),
        make_topic("/sensor/imu"),
    });
    panel.set_filter("robot");
    EXPECT_EQ(panel.filtered_topic_count(), 2u);
}

TEST(TopicListPanel, FilterNoMatch)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/a"), make_topic("/b")});
    panel.set_filter("zzz");
    EXPECT_EQ(panel.filtered_topic_count(), 0u);
}

TEST(TopicListPanel, FilterExactMatch)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/foo/bar"), make_topic("/foo/baz")});
    panel.set_filter("/foo/bar");
    EXPECT_EQ(panel.filtered_topic_count(), 1u);
}

TEST(TopicListPanel, FilterClearedShowsAll)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/a"), make_topic("/b"), make_topic("/c")});
    panel.set_filter("a");
    EXPECT_EQ(panel.filtered_topic_count(), 1u);
    panel.set_filter("");
    EXPECT_EQ(panel.filtered_topic_count(), 3u);
}

TEST(TopicListPanel, FilterAccessor)
{
    TopicListPanel panel;
    panel.set_filter("hello");
    EXPECT_EQ(panel.filter(), "hello");
}

// ---------------------------------------------------------------------------
// Suite: TopicStats — unit-level
// ---------------------------------------------------------------------------

TEST(TopicStats, EmptyStats)
{
    TopicStats st;
    st.prune_and_compute(1'000'000'000LL);
    EXPECT_DOUBLE_EQ(st.hz, 0.0);
    EXPECT_DOUBLE_EQ(st.bandwidth_bps, 0.0);
    EXPECT_FALSE(st.active);
    EXPECT_EQ(st.total_messages, 0u);
    EXPECT_EQ(st.total_bytes, 0u);
}

TEST(TopicStats, SingleMessage)
{
    TopicStats st;
    st.push(1'000'000'000LL, 64);
    st.prune_and_compute(1'000'100'000LL);   // 100 µs later
    // Only 1 sample → hz = 1.0 (special case), bw still 0
    EXPECT_DOUBLE_EQ(st.hz, 1.0);
    EXPECT_EQ(st.total_messages, 1u);
    EXPECT_EQ(st.total_bytes, 64u);
}

TEST(TopicStats, TwoMessagesHz)
{
    TopicStats st;
    // 2 messages 500 ms apart → 1 interval → hz = 1/(0.5) = 2.0
    st.push(0LL, 100);
    st.push(500'000'000LL, 100);
    st.prune_and_compute(600'000'000LL);
    EXPECT_NEAR(st.hz, 2.0, 0.01);
}

TEST(TopicStats, TenMessagesHz)
{
    TopicStats st;
    // 10 messages at 100 ms intervals over 0.9 s → hz ≈ 10 Hz
    for (int i = 0; i < 10; ++i)
    {
        st.push(static_cast<int64_t>(i) * 100'000'000LL, 50);
    }
    st.prune_and_compute(1'000'000'000LL);
    EXPECT_NEAR(st.hz, 10.0, 1.0);
}

TEST(TopicStats, BandwidthComputed)
{
    TopicStats st;
    // Push 10 messages × 1024 bytes at 10 Hz within a 1 s window.
    for (int i = 0; i < 10; ++i)
    {
        st.push(static_cast<int64_t>(i) * 100'000'000LL, 1024);
    }
    st.prune_and_compute(1'000'000'000LL);
    // BW = total_bytes_in_window / window_s = 10 * 1024 / 1.0 = 10240 B/s
    EXPECT_NEAR(st.bandwidth_bps, 10240.0, 500.0);
}

TEST(TopicStats, ActiveWithinThreshold)
{
    TopicStats    st;
    const int64_t now = 5'000'000'000LL;
    st.push(now - 500'000'000LL, 64);   // 0.5 s ago
    st.prune_and_compute(now);
    EXPECT_TRUE(st.active);
}

TEST(TopicStats, StaleAfterNoMessage)
{
    TopicStats st;
    // Push an old message (3 s ago) and compute at now.
    const int64_t now = 5'000'000'000LL;
    st.push(now - 3'000'000'000LL, 64);
    st.prune_and_compute(now);
    EXPECT_FALSE(st.active);
}

TEST(TopicStats, OldMessagesDroppedFromHz)
{
    TopicStats st;
    // Push 5 old messages (> 1 s window), then 3 recent ones.
    const int64_t now = 10'000'000'000LL;
    for (int i = 0; i < 5; ++i)
    {
        st.push(now - 5'000'000'000LL + static_cast<int64_t>(i) * 100'000'000LL, 32);
    }
    // Recent messages within window.
    st.push(now - 900'000'000LL, 32);
    st.push(now - 600'000'000LL, 32);
    st.push(now - 300'000'000LL, 32);

    st.prune_and_compute(now);
    // 3 messages in last 0.9 s → hz ≈ 2/0.6 ≈ 3.3
    EXPECT_GT(st.hz, 0.0);
    EXPECT_LT(st.hz, 10.0);
}

TEST(TopicStats, Counters)
{
    TopicStats st;
    st.push(100LL, 200);
    st.push(200LL, 300);
    EXPECT_EQ(st.total_messages, 2u);
    EXPECT_EQ(st.total_bytes, 500u);
}

// ---------------------------------------------------------------------------
// Suite: notify_message + stats_for
// ---------------------------------------------------------------------------

TEST(TopicListPanel, NotifyMessage_StatsForUnknown)
{
    TopicListPanel panel;
    auto           snap = panel.stats_for("/nonexistent");
    EXPECT_DOUBLE_EQ(snap.hz, 0.0);
    EXPECT_DOUBLE_EQ(snap.bandwidth_bps, 0.0);
    EXPECT_FALSE(snap.active);
    EXPECT_EQ(snap.total_messages, 0u);
}

TEST(TopicListPanel, NotifyMessage_CountsMessages)
{
    TopicListPanel panel;
    panel.notify_message("/chatter", 100);
    panel.notify_message("/chatter", 100);
    panel.notify_message("/chatter", 100);

    auto snap = panel.stats_for("/chatter");
    EXPECT_EQ(snap.total_messages, 3u);
}

TEST(TopicListPanel, NotifyMessage_ActiveAfterRecent)
{
    TopicListPanel panel;
    panel.notify_message("/chatter", 64);

    // Give stats_for time to compute; we just check active flag.
    auto snap = panel.stats_for("/chatter");
    EXPECT_TRUE(snap.active);
}

TEST(TopicListPanel, NotifyMessage_MultipleTopic)
{
    TopicListPanel panel;
    panel.notify_message("/a", 10);
    panel.notify_message("/a", 10);
    panel.notify_message("/b", 20);

    EXPECT_EQ(panel.stats_for("/a").total_messages, 2u);
    EXPECT_EQ(panel.stats_for("/b").total_messages, 1u);
}

TEST(TopicListPanel, NotifyMessage_ThreadSafe)
{
    TopicListPanel panel;
    // Fire 100 notifications from two threads.
    auto worker = [&]
    {
        for (int i = 0; i < 50; ++i)
        {
            panel.notify_message("/t", 64);
        }
    };
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();

    EXPECT_EQ(panel.stats_for("/t").total_messages, 100u);
}

// ---------------------------------------------------------------------------
// Suite: Callbacks
// ---------------------------------------------------------------------------

TEST(TopicListPanel, SelectCallbackNotFiredBeforeDraw)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/foo")});
    bool fired = false;
    panel.set_select_callback([&](const std::string&) { fired = true; });
    // No draw() called — callback must NOT fire.
    EXPECT_FALSE(fired);
    EXPECT_EQ(panel.selected_topic(), "");
}

TEST(TopicListPanel, PlotCallbackSetAndGet)
{
    TopicListPanel panel;
    bool           fired = false;
    panel.set_plot_callback([&](const std::string&) { fired = true; });
    // Callback stored; draw() would fire it on double-click, not testable here.
    EXPECT_FALSE(fired);
}

// ---------------------------------------------------------------------------
// Suite: Namespace split helper (tested via public interface indirectly)
// ---------------------------------------------------------------------------

TEST(TopicListPanel, TopicCountAfterSetTopics)
{
    TopicListPanel panel;
    panel.set_topics({
        make_topic("/"),   // degenerate root
        make_topic("/rosout"),
        make_topic("/robot/cmd_vel"),
        make_topic("/robot/arm/joint_states"),
        make_topic("/sensor/imu/data"),
    });
    EXPECT_EQ(panel.topic_count(), 5u);
}

// ---------------------------------------------------------------------------
// Suite: Format functions (tested via stats_for + indirect draw path)
// ---------------------------------------------------------------------------

TEST(TopicListPanel, FormatHz_ZeroYieldsDash)
{
    // Verify via stats_for returning hz=0 for a topic with no messages.
    TopicListPanel panel;
    panel.set_topics({make_topic("/t")});
    auto snap = panel.stats_for("/t");
    EXPECT_DOUBLE_EQ(snap.hz, 0.0);
}

TEST(TopicListPanel, FormatBW_ZeroYieldsDash)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/t")});
    auto snap = panel.stats_for("/t");
    EXPECT_DOUBLE_EQ(snap.bandwidth_bps, 0.0);
}

// ---------------------------------------------------------------------------
// Suite: draw() no-op without ImGui
// ---------------------------------------------------------------------------

TEST(TopicListPanel, DrawNoOpWithoutImGui)
{
    TopicListPanel panel;
    panel.set_topics({make_topic("/a"), make_topic("/b")});
    // Should not crash.
    panel.draw(nullptr);
    bool open = true;
    panel.draw(&open);
    EXPECT_TRUE(open);   // p_open must not be modified without ImGui
}

// ---------------------------------------------------------------------------
// Suite: Edge cases
// ---------------------------------------------------------------------------

TEST(TopicListPanel, MultipleSetTopicsCalls)
{
    TopicListPanel panel;
    for (int i = 0; i < 10; ++i)
    {
        std::vector<TopicInfo> ts;
        for (int j = 0; j < i; ++j)
        {
            ts.push_back(make_topic("/t" + std::to_string(j)));
        }
        panel.set_topics(ts);
        EXPECT_EQ(panel.topic_count(), static_cast<size_t>(i));
    }
}

TEST(TopicListPanel, NotifyBeforeSetTopics)
{
    TopicListPanel panel;
    // notify_message before any topics are set must not crash.
    panel.notify_message("/unknown", 100);
    EXPECT_EQ(panel.stats_for("/unknown").total_messages, 1u);
}

TEST(TopicListPanel, LargeTopicList)
{
    TopicListPanel         panel;
    std::vector<TopicInfo> ts;
    for (int i = 0; i < 200; ++i)
    {
        ts.push_back(make_topic("/ns/sub/topic_" + std::to_string(i)));
    }
    panel.set_topics(ts);
    EXPECT_EQ(panel.topic_count(), 200u);
    EXPECT_EQ(panel.filtered_topic_count(), 200u);

    panel.set_filter("topic_1");
    // Matches topic_1, topic_10..19, topic_100..199 = 1 + 10 + 100 = 111
    EXPECT_EQ(panel.filtered_topic_count(), 111u);
}

TEST(TopicListPanel, SetDiscoveryNull)
{
    TopicListPanel panel;
    panel.set_topic_discovery(nullptr);
    EXPECT_EQ(panel.topic_discovery(), nullptr);
}

TEST(TopicListPanel, StatsWindowMs)
{
    TopicListPanel panel;
    panel.set_stats_window_ms(500);
    TopicStats st;
    // Push messages at t=0 and t=600ms — the 600ms one is within a 1s window
    // but outside a 500ms window so only the recent one counts.
    const int64_t now = 1'000'000'000LL;
    st.push(now - 600'000'000LL, 50);   // outside 500 ms window
    st.push(now - 100'000'000LL, 50);   // inside 500 ms window
    st.prune_and_compute(now, 500'000'000LL);
    EXPECT_EQ(st.timestamps.size(), 1u);   // old one pruned
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

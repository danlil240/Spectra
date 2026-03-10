// test_log_viewer.cpp — Unit tests for RosLogViewer (F5) backend and
// LogViewerPanel (pure C++ logic, no ROS2 runtime, no ImGui, no Vulkan).
//
// Tests are structured so that all suites can run with GTest::gtest_main
// (no RclcppEnvironment needed — RosLogViewer is constructed with a null
// node and exercised via inject()).

#include <gtest/gtest.h>

#include "ros_log_viewer.hpp"
#include "ui/log_viewer_panel.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace ros2 = spectra::adapters::ros2;
using ros2::LogEntry;
using ros2::LogFilter;
using ros2::LogSeverity;
using ros2::RosLogViewer;
using ros2::LogViewerPanel;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static LogEntry make(uint64_t           seq,
                     LogSeverity        sev,
                     const std::string& node,
                     const std::string& msg,
                     double             wall = 0.0)
{
    return RosLogViewer::make_entry(seq, static_cast<uint8_t>(sev), 0, 0, wall, node, msg);
}

static std::unique_ptr<RosLogViewer> make_viewer(size_t cap = 100)
{
    auto v = std::make_unique<RosLogViewer>(nullptr);
    v->set_capacity(cap);
    return v;
}

// ---------------------------------------------------------------------------
// Suite 1: SeverityHelpers
// ---------------------------------------------------------------------------

TEST(SeverityHelpers, FromRclKnownLevels)
{
    EXPECT_EQ(ros2::severity_from_rcl(10), LogSeverity::Debug);
    EXPECT_EQ(ros2::severity_from_rcl(20), LogSeverity::Info);
    EXPECT_EQ(ros2::severity_from_rcl(30), LogSeverity::Warn);
    EXPECT_EQ(ros2::severity_from_rcl(40), LogSeverity::Error);
    EXPECT_EQ(ros2::severity_from_rcl(50), LogSeverity::Fatal);
}

TEST(SeverityHelpers, FromRclUnsetAndRoundDown)
{
    EXPECT_EQ(ros2::severity_from_rcl(0), LogSeverity::Unset);
    EXPECT_EQ(ros2::severity_from_rcl(5), LogSeverity::Unset);
    EXPECT_EQ(ros2::severity_from_rcl(15), LogSeverity::Debug);
    EXPECT_EQ(ros2::severity_from_rcl(25), LogSeverity::Info);
    EXPECT_EQ(ros2::severity_from_rcl(45), LogSeverity::Error);
    EXPECT_EQ(ros2::severity_from_rcl(99), LogSeverity::Fatal);
}

TEST(SeverityHelpers, NamesCorrect)
{
    EXPECT_STREQ(ros2::severity_name(LogSeverity::Debug), "DEBUG");
    EXPECT_STREQ(ros2::severity_name(LogSeverity::Info), "INFO");
    EXPECT_STREQ(ros2::severity_name(LogSeverity::Warn), "WARN");
    EXPECT_STREQ(ros2::severity_name(LogSeverity::Error), "ERROR");
    EXPECT_STREQ(ros2::severity_name(LogSeverity::Fatal), "FATAL");
    EXPECT_STREQ(ros2::severity_name(LogSeverity::Unset), "UNSET");
}

TEST(SeverityHelpers, CharsCorrect)
{
    EXPECT_EQ(ros2::severity_char(LogSeverity::Debug), 'D');
    EXPECT_EQ(ros2::severity_char(LogSeverity::Info), 'I');
    EXPECT_EQ(ros2::severity_char(LogSeverity::Warn), 'W');
    EXPECT_EQ(ros2::severity_char(LogSeverity::Error), 'E');
    EXPECT_EQ(ros2::severity_char(LogSeverity::Fatal), 'F');
    EXPECT_EQ(ros2::severity_char(LogSeverity::Unset), '?');
}

// ---------------------------------------------------------------------------
// Suite 2: MakeEntry
// ---------------------------------------------------------------------------

TEST(MakeEntry, FieldsPopulated)
{
    auto e = RosLogViewer::make_entry(7,
                                      30,
                                      100,
                                      500,
                                      1.5,
                                      "/my_node",
                                      "hello world",
                                      "foo.cpp",
                                      "bar()",
                                      42u);
    EXPECT_EQ(e.seq, 7u);
    EXPECT_EQ(e.severity, LogSeverity::Warn);
    EXPECT_EQ(e.stamp_ns, 100LL * 1'000'000'000LL + 500LL);
    EXPECT_DOUBLE_EQ(e.wall_time_s, 1.5);
    EXPECT_EQ(e.node, "/my_node");
    EXPECT_EQ(e.message, "hello world");
    EXPECT_EQ(e.file, "foo.cpp");
    EXPECT_EQ(e.function, "bar()");
    EXPECT_EQ(e.line, 42u);
}

TEST(MakeEntry, DefaultOptionals)
{
    auto e = RosLogViewer::make_entry(0, 20, 0, 0, 0.0, "n", "m");
    EXPECT_TRUE(e.file.empty());
    EXPECT_TRUE(e.function.empty());
    EXPECT_EQ(e.line, 0u);
}

// ---------------------------------------------------------------------------
// Suite 3: FormatHelpers
// ---------------------------------------------------------------------------

TEST(FormatHelpers, FormatStampZero)
{
    EXPECT_EQ(RosLogViewer::format_stamp(0), "0.000000000");
}

TEST(FormatHelpers, FormatStampOneSecond)
{
    EXPECT_EQ(RosLogViewer::format_stamp(1'000'000'000LL), "1.000000000");
}

TEST(FormatHelpers, FormatStampNonZeroNsec)
{
    const std::string s = RosLogViewer::format_stamp(1'500'000'007LL);
    EXPECT_EQ(s, "1.500000007");
}

TEST(FormatHelpers, FormatWallTimeZero)
{
    EXPECT_EQ(RosLogViewer::format_wall_time(0.0), "00:00:00.000");
}

TEST(FormatHelpers, FormatWallTimeOneHour)
{
    EXPECT_EQ(RosLogViewer::format_wall_time(3600.0), "01:00:00.000");
}

TEST(FormatHelpers, FormatWallTimeMilliseconds)
{
    EXPECT_EQ(RosLogViewer::format_wall_time(1.5), "00:00:01.500");
}

// ---------------------------------------------------------------------------
// Suite 4: RingBuffer — capacity and circular wrap
// ---------------------------------------------------------------------------

TEST(RingBuffer, DefaultCapacity)
{
    RosLogViewer v(nullptr);
    EXPECT_EQ(v.capacity(), RosLogViewer::DEFAULT_CAPACITY);
}

TEST(RingBuffer, SetCapacityClamp)
{
    RosLogViewer v(nullptr);
    v.set_capacity(0);   // below MIN → clamped to MIN (1)
    EXPECT_EQ(v.capacity(), RosLogViewer::MIN_CAPACITY);
    v.set_capacity(200'000);   // above MAX → clamped to MAX
    EXPECT_EQ(v.capacity(), RosLogViewer::MAX_CAPACITY);
    v.set_capacity(500);
    EXPECT_EQ(v.capacity(), 500u);
}

TEST(RingBuffer, InjectAndCount)
{
    auto v = make_viewer(10);
    for (int i = 0; i < 5; ++i)
        v->inject(make(i, LogSeverity::Info, "n", "msg"));
    EXPECT_EQ(v->entry_count(), 5u);
}

TEST(RingBuffer, CircularWrapOldestDropped)
{
    auto v = make_viewer(3);
    for (int i = 0; i < 5; ++i)
        v->inject(make(i, LogSeverity::Info, "n", std::to_string(i)));
    EXPECT_EQ(v->entry_count(), 3u);
    auto snap = v->snapshot();
    EXPECT_EQ(snap[0].message, "2");
    EXPECT_EQ(snap[1].message, "3");
    EXPECT_EQ(snap[2].message, "4");
}

TEST(RingBuffer, ClearEmpty)
{
    auto v = make_viewer(10);
    v->inject(make(0, LogSeverity::Info, "n", "msg"));
    v->clear();
    EXPECT_EQ(v->entry_count(), 0u);
    EXPECT_EQ(v->snapshot().size(), 0u);
}

TEST(RingBuffer, TotalReceivedCountsAll)
{
    auto v = make_viewer(5);
    for (int i = 0; i < 20; ++i)
        v->inject(make(i, LogSeverity::Info, "n", "msg"));
    EXPECT_EQ(v->total_received(), 20u);
    EXPECT_EQ(v->entry_count(), 5u);
}

TEST(RingBuffer, SetCapacitySmallerPreservesNewest)
{
    auto v = make_viewer(10);
    for (int i = 0; i < 8; ++i)
        v->inject(make(i, LogSeverity::Info, "n", std::to_string(i)));
    v->set_capacity(4);
    EXPECT_EQ(v->entry_count(), 4u);
    auto snap = v->snapshot();
    EXPECT_EQ(snap[0].message, "4");
    EXPECT_EQ(snap[3].message, "7");
}

TEST(RingBuffer, SnapshotOrderOldestFirst)
{
    auto v = make_viewer(10);
    v->inject(make(0, LogSeverity::Info, "n", "first"));
    v->inject(make(1, LogSeverity::Info, "n", "second"));
    v->inject(make(2, LogSeverity::Info, "n", "third"));
    auto snap = v->snapshot();
    ASSERT_EQ(snap.size(), 3u);
    EXPECT_EQ(snap[0].message, "first");
    EXPECT_EQ(snap[2].message, "third");
}

// ---------------------------------------------------------------------------
// Suite 5: PauseResume
// ---------------------------------------------------------------------------

TEST(PauseResume, DefaultNotPaused)
{
    auto v = make_viewer();
    EXPECT_FALSE(v->is_paused());
}

TEST(PauseResume, PauseBlocksInject)
{
    auto v = make_viewer();
    v->pause();
    EXPECT_TRUE(v->is_paused());
    v->inject(make(0, LogSeverity::Info, "n", "ignored"));
    EXPECT_EQ(v->entry_count(), 0u);
}

TEST(PauseResume, ResumeAllowsInject)
{
    auto v = make_viewer();
    v->pause();
    v->resume();
    EXPECT_FALSE(v->is_paused());
    v->inject(make(0, LogSeverity::Info, "n", "ok"));
    EXPECT_EQ(v->entry_count(), 1u);
}

// ---------------------------------------------------------------------------
// Suite 6: LogFilter — severity gate
// ---------------------------------------------------------------------------

TEST(LogFilterSeverity, PassesAtExactLevel)
{
    LogFilter f;
    f.min_severity = LogSeverity::Warn;
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Warn, "n", "m")));
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Error, "n", "m")));
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Fatal, "n", "m")));
}

TEST(LogFilterSeverity, BlocksBelowLevel)
{
    LogFilter f;
    f.min_severity = LogSeverity::Warn;
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Debug, "n", "m")));
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Info, "n", "m")));
}

TEST(LogFilterSeverity, UnsetPassesAll)
{
    LogFilter f;
    f.min_severity = LogSeverity::Unset;
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Debug, "n", "m")));
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Fatal, "n", "m")));
}

// ---------------------------------------------------------------------------
// Suite 7: LogFilter — node filter
// ---------------------------------------------------------------------------

TEST(LogFilterNode, EmptyPassesAll)
{
    LogFilter f;
    f.node_filter = "";
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "/my_node", "m")));
}

TEST(LogFilterNode, SubstringMatch)
{
    LogFilter f;
    f.node_filter = "camera";
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "/camera_driver", "m")));
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Info, "/lidar_node", "m")));
}

TEST(LogFilterNode, CaseInsensitive)
{
    LogFilter f;
    f.node_filter = "Camera";
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "/CAMERA_DRIVER", "m")));
}

TEST(CiContains, EmptyNeedle)
{
    EXPECT_TRUE(ros2::ci_contains("anything", ""));
}

TEST(CiContains, MatchCaseInsensitive)
{
    EXPECT_TRUE(ros2::ci_contains("Hello World", "world"));
    EXPECT_TRUE(ros2::ci_contains("hello world", "HELLO"));
    EXPECT_FALSE(ros2::ci_contains("hello", "bye"));
}

// ---------------------------------------------------------------------------
// Suite 8: LogFilter — regex
// ---------------------------------------------------------------------------

TEST(LogFilterRegex, EmptyPassesAll)
{
    LogFilter f;
    f.message_regex_str = "";
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "n", "any message")));
}

TEST(LogFilterRegex, SimpleMatch)
{
    LogFilter f;
    f.message_regex_str = "error.*code";
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "n", "error in code 42")));
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Info, "n", "all good")));
}

TEST(LogFilterRegex, CaseInsensitiveByDefault)
{
    LogFilter f;
    f.message_regex_str = "ERROR";
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "n", "error occurred")));
}

TEST(LogFilterRegex, InvalidRegexPassesAll)
{
    LogFilter f;
    f.message_regex_str = "[invalid(";
    // invalid regex → pass all
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Info, "n", "any message")));
    EXPECT_FALSE(f.regex_error.empty());
}

TEST(LogFilterRegex, CompileSuccess)
{
    LogFilter f;
    f.message_regex_str = "hello";
    std::regex re;
    EXPECT_TRUE(f.compile_regex(re));
    EXPECT_TRUE(f.regex_error.empty());
}

TEST(LogFilterRegex, CompileFailure)
{
    LogFilter f;
    f.message_regex_str = "(unclosed";
    std::regex re;
    EXPECT_FALSE(f.compile_regex(re));
    EXPECT_FALSE(f.regex_error.empty());
}

// ---------------------------------------------------------------------------
// Suite 9: Combined filter AND logic
// ---------------------------------------------------------------------------

TEST(LogFilterCombined, SeverityAndNodeAndRegexAllMustPass)
{
    LogFilter f;
    f.min_severity      = LogSeverity::Warn;
    f.node_filter       = "cam";
    f.message_regex_str = "lost";

    // Passes all three
    EXPECT_TRUE(f.passes(make(0, LogSeverity::Error, "/camera_node", "frame lost")));
    // Fails severity
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Info, "/camera_node", "frame lost")));
    // Fails node
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Error, "/lidar_node", "frame lost")));
    // Fails regex
    EXPECT_FALSE(f.passes(make(0, LogSeverity::Error, "/camera_node", "all fine")));
}

// ---------------------------------------------------------------------------
// Suite 10: RosLogViewer filter integration
// ---------------------------------------------------------------------------

TEST(ViewerFilter, FilteredSnapshotRespectsSeverity)
{
    auto v = make_viewer(20);
    for (int i = 0; i < 5; ++i)
        v->inject(make(i, LogSeverity::Debug, "n", "d"));
    for (int i = 5; i < 10; ++i)
        v->inject(make(i, LogSeverity::Info, "n", "i"));
    v->set_min_severity(LogSeverity::Info);
    auto snap = v->filtered_snapshot();
    EXPECT_EQ(snap.size(), 5u);
    for (const auto& e : snap)
        EXPECT_EQ(e.severity, LogSeverity::Info);
}

TEST(ViewerFilter, FilteredSnapshotNodeFilter)
{
    auto v = make_viewer(20);
    v->inject(make(0, LogSeverity::Info, "/camera", "msg"));
    v->inject(make(1, LogSeverity::Info, "/lidar", "msg"));
    v->inject(make(2, LogSeverity::Info, "/camera", "msg"));
    v->set_node_filter("/camera");
    auto snap = v->filtered_snapshot();
    EXPECT_EQ(snap.size(), 2u);
    for (const auto& e : snap)
        EXPECT_EQ(e.node, "/camera");
}

TEST(ViewerFilter, FilteredSnapshotRegex)
{
    auto v = make_viewer(20);
    v->inject(make(0, LogSeverity::Info, "n", "connection established"));
    v->inject(make(1, LogSeverity::Info, "n", "connection lost"));
    v->inject(make(2, LogSeverity::Info, "n", "connection established"));
    v->set_message_regex("connection.*established");
    auto snap = v->filtered_snapshot();
    EXPECT_EQ(snap.size(), 2u);
    for (const auto& e : snap)
        EXPECT_EQ(e.message, "connection established");
}

TEST(ViewerFilter, ForEachFilteredCounts)
{
    auto v = make_viewer(20);
    v->inject(make(0, LogSeverity::Warn, "n", "warn1"));
    v->inject(make(1, LogSeverity::Info, "n", "info1"));
    v->inject(make(2, LogSeverity::Error, "n", "err1"));
    v->set_min_severity(LogSeverity::Warn);
    int count = 0;
    v->for_each_filtered([&](const LogEntry&) { ++count; });
    EXPECT_EQ(count, 2);
}

TEST(ViewerFilter, SetFilterReplacesAll)
{
    auto v = make_viewer(20);
    v->inject(make(0, LogSeverity::Debug, "/cam", "lost"));
    v->inject(make(1, LogSeverity::Warn, "/lidar", "ok"));

    LogFilter f;
    f.min_severity      = LogSeverity::Warn;
    f.node_filter       = "lidar";
    f.message_regex_str = "";
    v->set_filter(f);

    auto fs = v->filtered_snapshot();
    EXPECT_EQ(fs.size(), 1u);
    EXPECT_EQ(fs[0].node, "/lidar");
}

// ---------------------------------------------------------------------------
// Suite 11: SeverityCounts
// ---------------------------------------------------------------------------

TEST(SeverityCounts, EmptyAllZero)
{
    auto v    = make_viewer();
    auto cnts = v->severity_counts();
    for (auto c : cnts)
        EXPECT_EQ(c, 0u);
}

TEST(SeverityCounts, MixedSeverities)
{
    auto v = make_viewer(20);
    v->inject(make(0, LogSeverity::Debug, "n", "m"));
    v->inject(make(1, LogSeverity::Debug, "n", "m"));
    v->inject(make(2, LogSeverity::Info, "n", "m"));
    v->inject(make(3, LogSeverity::Warn, "n", "m"));
    v->inject(make(4, LogSeverity::Error, "n", "m"));
    auto cnts = v->severity_counts();
    // Index = severity / 10: Unset=0, Debug=1, Info=2, Warn=3, Error=4, Fatal=5
    EXPECT_EQ(cnts[0], 0u);   // Unset
    EXPECT_EQ(cnts[1], 2u);   // Debug
    EXPECT_EQ(cnts[2], 1u);   // Info
    EXPECT_EQ(cnts[3], 1u);   // Warn
    EXPECT_EQ(cnts[4], 1u);   // Error
    EXPECT_EQ(cnts[5], 0u);   // Fatal
}

TEST(SeverityCounts, AfterClearAllZero)
{
    auto v = make_viewer(10);
    v->inject(make(0, LogSeverity::Error, "n", "m"));
    v->clear();
    auto cnts = v->severity_counts();
    for (auto c : cnts)
        EXPECT_EQ(c, 0u);
}

// ---------------------------------------------------------------------------
// Suite 12: Thread safety — concurrent inject + snapshot
// ---------------------------------------------------------------------------

TEST(ThreadSafety, ConcurrentInjectAndSnapshot)
{
    auto v = make_viewer(1000);

    constexpr int N = 500;
    std::thread   writer(
        [&]()
        {
            for (int i = 0; i < N; ++i)
                v->inject(make(i, LogSeverity::Info, "n", "msg"));
        });

    // Snapshot while writer is running — must not crash.
    std::vector<LogEntry> snap;
    for (int i = 0; i < 10; ++i)
        snap = v->snapshot();

    writer.join();
    EXPECT_LE(v->entry_count(), 1000u);
}

// ---------------------------------------------------------------------------
// Suite 13: LogViewerPanel — pure logic (no ImGui)
// ---------------------------------------------------------------------------

TEST(LogViewerPanel, ConstructionDefaults)
{
    auto           v = make_viewer(100);
    LogViewerPanel panel(*v);
    EXPECT_FALSE(panel.is_paused());
    EXPECT_TRUE(panel.auto_scroll());
    EXPECT_EQ(panel.selected_row(), -1);
    EXPECT_EQ(panel.visible_count(), 0u);
}

TEST(LogViewerPanel, BuildCopyTextEmpty)
{
    auto           v = make_viewer(100);
    LogViewerPanel panel(*v);
    EXPECT_EQ(panel.build_copy_text({}), "");
}

TEST(LogViewerPanel, BuildCopyTextHasHeader)
{
    auto                  v = make_viewer();
    LogViewerPanel        panel(*v);
    std::vector<LogEntry> entries{make(0, LogSeverity::Fatal, "/n", "boom")};
    const std::string     text = panel.build_copy_text(entries);
    EXPECT_FALSE(text.empty());
    EXPECT_TRUE(text.find("FATAL") != std::string::npos);
    EXPECT_NE(text.find("Severity"), std::string::npos);
    EXPECT_NE(text.find("Message"), std::string::npos);
}

TEST(LogViewerPanel, BuildCopyTextContainsEntries)
{
    auto                  v = make_viewer(10);
    LogViewerPanel        panel(*v);
    std::vector<LogEntry> entries{make(0, LogSeverity::Warn, "/cam", "frame lost"),
                                  make(1, LogSeverity::Error, "/lidar", "timeout")};
    const std::string     text = panel.build_copy_text(entries);
    EXPECT_TRUE(text.find("WARN") != std::string::npos);
    EXPECT_TRUE(text.find("ERROR") != std::string::npos);
    EXPECT_TRUE(text.find("frame lost") != std::string::npos);
    EXPECT_TRUE(text.find("timeout") != std::string::npos);
}

TEST(LogViewerPanel, FormatRowTabSeparated)
{
    auto e   = make(0, LogSeverity::Info, "/my_node", "test message", 3661.5);
    auto row = LogViewerPanel::format_row(e);
    EXPECT_NE(row.find('\t'), std::string::npos);
    EXPECT_NE(row.find("INFO"), std::string::npos);
    EXPECT_NE(row.find("/my_node"), std::string::npos);
    EXPECT_NE(row.find("test message"), std::string::npos);
}

TEST(LogViewerPanel, FormatRowContainsTime)
{
    auto e   = make(0, LogSeverity::Debug, "n", "m", 3661.0);
    auto row = LogViewerPanel::format_row(e);
    // wall_time_s=3661.0 → "01:01:01.000"
    EXPECT_NE(row.find("01:01:01"), std::string::npos);
}

TEST(LogViewerPanel, TitleDefault)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    EXPECT_EQ(panel.title(), "ROS2 Log");
}

TEST(LogViewerPanel, SetTitle)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    panel.set_title("My Log");
    EXPECT_EQ(panel.title(), "My Log");
}

TEST(LogViewerPanel, DisplayHzDefault)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    EXPECT_DOUBLE_EQ(panel.display_hz(), 20.0);
}

TEST(LogViewerPanel, SetDisplayHz)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    panel.set_display_hz(60.0);
    EXPECT_DOUBLE_EQ(panel.display_hz(), 60.0);
}

TEST(LogViewerPanel, MaxDisplayRowsDefault)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    EXPECT_EQ(panel.max_display_rows(), 2000u);
}

TEST(LogViewerPanel, SetMaxDisplayRows)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    panel.set_max_display_rows(500);
    EXPECT_EQ(panel.max_display_rows(), 500u);
}

TEST(LogViewerPanel, SetMaxDisplayRowsZeroClampedToOne)
{
    auto           v = make_viewer();
    LogViewerPanel panel(*v);
    panel.set_max_display_rows(0);
    EXPECT_EQ(panel.max_display_rows(), 1u);
}

// ---------------------------------------------------------------------------
// Suite 14: Edge cases
// ---------------------------------------------------------------------------

TEST(EdgeCases, InjectWithZeroSeqGetsAutoSeq)
{
    auto     v = make_viewer(10);
    LogEntry e;
    e.seq      = 0;
    e.severity = LogSeverity::Info;
    e.node     = "/node";
    e.message  = "msg";
    v->inject(e);
    EXPECT_EQ(v->entry_count(), 1u);
    EXPECT_EQ(v->total_received(), 1u);
    auto snap = v->snapshot();
    EXPECT_EQ(snap.size(), 1u);
    // Default filter (min_severity=Debug) passes Info entries.
    EXPECT_EQ(v->filtered_snapshot().size(), 1u);
}

TEST(EdgeCases, EmptyViewerSnapshot)
{
    auto v = make_viewer();
    EXPECT_TRUE(v->snapshot().empty());
    EXPECT_TRUE(v->filtered_snapshot().empty());
}

TEST(EdgeCases, ForEachOnEmptyNoCrash)
{
    auto v     = make_viewer();
    int  count = 0;
    v->for_each_filtered([&](const LogEntry&) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(EdgeCases, LargeMessageDoesNotCrash)
{
    auto              v = make_viewer(10);
    const std::string big(4096, 'X');
    v->inject(make(0, LogSeverity::Info, "/node", big));
    EXPECT_EQ(v->entry_count(), 1u);
    EXPECT_EQ(v->snapshot()[0].message, big);
}

TEST(EdgeCases, BuildCopyTextSingleEntry)
{
    auto                  v = make_viewer();
    LogViewerPanel        panel(*v);
    std::vector<LogEntry> entries{make(0, LogSeverity::Fatal, "/n", "boom")};
    const std::string     text = panel.build_copy_text(entries);
    EXPECT_NE(text.find("boom"), std::string::npos);
    EXPECT_NE(text.find("FATAL"), std::string::npos);
}

TEST(EdgeCases, FilteredSnapshotEmptyAfterClear)
{
    auto v = make_viewer(10);
    for (int i = 0; i < 5; ++i)
        v->inject(make(i, LogSeverity::Info, "n", "msg"));
    v->clear();
    EXPECT_EQ(v->entry_count(), 0u);
    EXPECT_TRUE(v->filtered_snapshot().empty());
}

TEST(EdgeCases, NullNodeConstructionDoesNotCrash)
{
    // Constructing with nullptr node — subscribe() should be a no-op.
    RosLogViewer v(nullptr);
    EXPECT_FALSE(v.is_subscribed());
    v.subscribe();   // should not crash even with null node_
    EXPECT_FALSE(v.is_subscribed());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
// Uses GTest::gtest_main (no custom main / no RclcppEnvironment).

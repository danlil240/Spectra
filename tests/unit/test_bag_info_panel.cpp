// test_bag_info_panel.cpp — unit tests for BagInfoPanel (D4).
//
// All tests exercise pure C++ logic only — no ImGui context, no BagReader
// disk I/O (requires SPECTRA_ROS2_BAG + a real bag file).
//
// The approach:
//   - BagSummary helper methods (duration_string, format_size, format_time)
//     are fully testable without any bag on disk.
//   - BagInfoPanel lifecycle (open_bag failure path, close_bag, is_bag_path,
//     try_open_file, select_row, plot_row, callbacks) can be tested against
//     BagReader's no-SPECTRA_ROS2_BAG stub (always returns false/empty).
//   - When SPECTRA_ROS2_BAG IS defined this file can be extended with real
//     bag tests; for now all tests stay in the stub path.

#include <gtest/gtest.h>

#include "ui/bag_info_panel.hpp"

using namespace spectra::adapters::ros2;

// ============================================================================
// Suite: BagSummaryDurationString
// ============================================================================

TEST(BagSummaryDurationString, Zero)
{
    BagSummary s;
    s.duration_sec = 0.0;
    EXPECT_EQ(s.duration_string(), "0.000 s");
}

TEST(BagSummaryDurationString, MillisecondsOnly)
{
    BagSummary s;
    s.duration_sec = 0.123;
    // 0 s, 123 ms
    EXPECT_EQ(s.duration_string(), "0.123 s");
}

TEST(BagSummaryDurationString, SecondsOnly)
{
    BagSummary s;
    s.duration_sec = 45.0;
    EXPECT_EQ(s.duration_string(), "45.000 s");
}

TEST(BagSummaryDurationString, MinutesAndSeconds)
{
    BagSummary s;
    s.duration_sec = 2.0 * 60.0 + 7.5;  // 2m 07s
    const std::string r = s.duration_string();
    EXPECT_EQ(r, "2m 07s");
}

TEST(BagSummaryDurationString, HoursMinutesSeconds)
{
    BagSummary s;
    s.duration_sec = 1.0 * 3600.0 + 23.0 * 60.0 + 4.0;
    EXPECT_EQ(s.duration_string(), "1h 23m 04s");
}

TEST(BagSummaryDurationString, NegativeClampedToZero)
{
    BagSummary s;
    s.duration_sec = -5.0;
    EXPECT_EQ(s.duration_string(), "0.000 s");
}

// ============================================================================
// Suite: BagSummaryFormatSize
// ============================================================================

TEST(BagSummaryFormatSize, Zero)
{
    EXPECT_EQ(BagSummary::format_size(0), "—");
}

TEST(BagSummaryFormatSize, Bytes)
{
    EXPECT_EQ(BagSummary::format_size(512), "512 B");
    EXPECT_EQ(BagSummary::format_size(1), "1 B");
    EXPECT_EQ(BagSummary::format_size(1023), "1023 B");
}

TEST(BagSummaryFormatSize, Kilobytes)
{
    const std::string r = BagSummary::format_size(1024);
    EXPECT_EQ(r, "1.0 KB");
}

TEST(BagSummaryFormatSize, LargeKilobytes)
{
    const std::string r = BagSummary::format_size(512 * 1024);
    EXPECT_EQ(r, "512.0 KB");
}

TEST(BagSummaryFormatSize, Megabytes)
{
    const std::string r = BagSummary::format_size(1024ULL * 1024ULL);
    EXPECT_EQ(r, "1.00 MB");
}

TEST(BagSummaryFormatSize, Gigabytes)
{
    const std::string r = BagSummary::format_size(1024ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(r, "1.00 GB");
}

TEST(BagSummaryFormatSize, LargeGigabytes)
{
    const std::string r = BagSummary::format_size(10ULL * 1024ULL * 1024ULL * 1024ULL);
    EXPECT_EQ(r, "10.00 GB");
}

// ============================================================================
// Suite: BagSummaryFormatTime
// ============================================================================

TEST(BagSummaryFormatTime, Zero)
{
    EXPECT_EQ(BagSummary::format_time(0.0), "—");
}

TEST(BagSummaryFormatTime, Negative)
{
    EXPECT_EQ(BagSummary::format_time(-1.0), "—");
}

TEST(BagSummaryFormatTime, Midnight)
{
    // t = 0 seconds past epoch → 00:00:00.000
    // Actual wall-clock wrapping doesn't matter for unit tests; we just
    // verify the format pattern.
    const std::string r = BagSummary::format_time(1.0);
    // Must contain colons and a dot.
    EXPECT_NE(r.find(':'), std::string::npos);
    EXPECT_NE(r.find('.'), std::string::npos);
}

TEST(BagSummaryFormatTime, Milliseconds)
{
    // Feed a value where fractional seconds give 500 ms.
    // We can't know the absolute hour/minute (epoch-dependent), but the
    // millisecond portion must be 500.
    const std::string r = BagSummary::format_time(1000.5);
    EXPECT_TRUE(r.size() >= 11);  // "HH:MM:SS.mmm" = 12 chars
    // Last 3 chars should be "500"
    EXPECT_EQ(r.substr(r.size() - 3), "500");
}

// ============================================================================
// Suite: BagInfoPanelIsBagPath
// ============================================================================

TEST(BagInfoPanelIsBagPath, Db3Extension)
{
    EXPECT_TRUE(BagInfoPanel::is_bag_path("/path/to/my_bag.db3"));
}

TEST(BagInfoPanelIsBagPath, McapExtension)
{
    EXPECT_TRUE(BagInfoPanel::is_bag_path("/path/to/my_bag.mcap"));
}

TEST(BagInfoPanelIsBagPath, Db3UpperCase)
{
    EXPECT_TRUE(BagInfoPanel::is_bag_path("/path/to/MY_BAG.DB3"));
}

TEST(BagInfoPanelIsBagPath, McapUpperCase)
{
    EXPECT_TRUE(BagInfoPanel::is_bag_path("/path/to/MY_BAG.MCAP"));
}

TEST(BagInfoPanelIsBagPath, Db3MixedCase)
{
    EXPECT_TRUE(BagInfoPanel::is_bag_path("/path/to/bag.Db3"));
}

TEST(BagInfoPanelIsBagPath, WrongExtension)
{
    EXPECT_FALSE(BagInfoPanel::is_bag_path("/path/to/bag.bag"));
    EXPECT_FALSE(BagInfoPanel::is_bag_path("/path/to/bag.sqlite3"));
    EXPECT_FALSE(BagInfoPanel::is_bag_path("/path/to/bag.mp4"));
}

TEST(BagInfoPanelIsBagPath, NoExtension)
{
    EXPECT_FALSE(BagInfoPanel::is_bag_path("/path/to/bag"));
    EXPECT_FALSE(BagInfoPanel::is_bag_path(""));
}

TEST(BagInfoPanelIsBagPath, JustExtension)
{
    EXPECT_TRUE(BagInfoPanel::is_bag_path(".db3"));
    EXPECT_TRUE(BagInfoPanel::is_bag_path(".mcap"));
}

// ============================================================================
// Suite: BagInfoPanelConstruction
// ============================================================================

TEST(BagInfoPanelConstruction, DefaultState)
{
    BagInfoPanel panel;
    EXPECT_FALSE(panel.is_open());
    EXPECT_FALSE(panel.summary().is_open);
    EXPECT_EQ(panel.summary().topic_count(), 0u);
    EXPECT_EQ(panel.selected_index(), -1);
    EXPECT_EQ(panel.title(), "Bag Info");
}

TEST(BagInfoPanelConstruction, SetTitle)
{
    BagInfoPanel panel;
    panel.set_title("My Bag");
    EXPECT_EQ(panel.title(), "My Bag");
}

TEST(BagInfoPanelConstruction, NonCopyable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<BagInfoPanel>);
    EXPECT_FALSE(std::is_copy_assignable_v<BagInfoPanel>);
}

TEST(BagInfoPanelConstruction, NonMovable)
{
    EXPECT_FALSE(std::is_move_constructible_v<BagInfoPanel>);
    EXPECT_FALSE(std::is_move_assignable_v<BagInfoPanel>);
}

// ============================================================================
// Suite: BagInfoPanelOpenBag
// (BagReader stub always returns false; exercises failure path)
// ============================================================================

TEST(BagInfoPanelOpenBag, OpenNonExistentBagFails)
{
    BagInfoPanel panel;
    const bool ok = panel.open_bag("/nonexistent/path/bag.db3");
#ifdef SPECTRA_ROS2_BAG
    // With real BagReader this should fail gracefully.
    EXPECT_FALSE(ok);
    EXPECT_FALSE(panel.is_open());
#else
    // Stub BagReader always fails.
    EXPECT_FALSE(ok);
    EXPECT_FALSE(panel.is_open());
#endif
}

TEST(BagInfoPanelOpenBag, FailedOpenSetsLastError)
{
    BagInfoPanel panel;
    panel.open_bag("/does/not/exist.db3");
    // Error should be non-empty (stub sets a "built without SPECTRA_ROS2_BAG" message).
    EXPECT_FALSE(panel.summary().last_error.empty());
}

TEST(BagInfoPanelOpenBag, FailedOpenKeepsClosed)
{
    BagInfoPanel panel;
    panel.open_bag("/does/not/exist.mcap");
    EXPECT_FALSE(panel.is_open());
    EXPECT_EQ(panel.summary().topic_count(), 0u);
    EXPECT_EQ(panel.selected_index(), -1);
}

TEST(BagInfoPanelOpenBag, OpenedCallbackNotFiredOnFailure)
{
    BagInfoPanel panel;
    int calls = 0;
    panel.set_bag_opened_callback([&](const std::string&) { ++calls; });
    panel.open_bag("/nonexistent.db3");
    EXPECT_EQ(calls, 0);
}

// ============================================================================
// Suite: BagInfoPanelCloseBag
// ============================================================================

TEST(BagInfoPanelCloseBag, CloseWhenNotOpenIsNoOp)
{
    BagInfoPanel panel;
    EXPECT_NO_THROW(panel.close_bag());
    EXPECT_FALSE(panel.is_open());
}

TEST(BagInfoPanelCloseBag, CloseResetsSummary)
{
    BagInfoPanel panel;
    // Even if open never succeeded, close should leave summary empty.
    panel.close_bag();
    EXPECT_FALSE(panel.summary().is_open);
    EXPECT_TRUE(panel.summary().path.empty());
    EXPECT_EQ(panel.summary().message_count, 0u);
}

TEST(BagInfoPanelCloseBag, CloseResetsSelectedIndex)
{
    BagInfoPanel panel;
    // Force a (failed) open then close.
    panel.open_bag("/fake.db3");
    panel.close_bag();
    EXPECT_EQ(panel.selected_index(), -1);
}

// ============================================================================
// Suite: BagInfoPanelTryOpenFile
// ============================================================================

TEST(BagInfoPanelTryOpenFile, WrongExtensionReturnsFalse)
{
    BagInfoPanel panel;
    EXPECT_FALSE(panel.try_open_file("/path/to/video.mp4"));
    EXPECT_FALSE(panel.is_open());
}

TEST(BagInfoPanelTryOpenFile, Db3ExtensionAttempsOpen)
{
    BagInfoPanel panel;
    // Returns false because file doesn't exist, but it DID attempt open
    // (stub returns false regardless).
    const bool result = panel.try_open_file("/nonexistent.db3");
    EXPECT_FALSE(result);  // stub always fails
}

TEST(BagInfoPanelTryOpenFile, McapExtensionAttemptsOpen)
{
    BagInfoPanel panel;
    const bool result = panel.try_open_file("/nonexistent.mcap");
    EXPECT_FALSE(result);  // stub always fails
}

TEST(BagInfoPanelTryOpenFile, EmptyPathReturnsFalse)
{
    BagInfoPanel panel;
    EXPECT_FALSE(panel.try_open_file(""));
}

// ============================================================================
// Suite: BagInfoPanelSelectRow
// ============================================================================

TEST(BagInfoPanelSelectRow, SelectOnEmptyTopicsNoOp)
{
    BagInfoPanel panel;
    // No topics — selecting any index should be safe.
    panel.select_row(0);
    EXPECT_EQ(panel.selected_index(), -1);
}

TEST(BagInfoPanelSelectRow, SelectNegativeIndexNoOp)
{
    BagInfoPanel panel;
    panel.select_row(-1);
    EXPECT_EQ(panel.selected_index(), -1);
}

TEST(BagInfoPanelSelectRow, SelectOutOfRangeNoOp)
{
    BagInfoPanel panel;
    panel.select_row(100);
    EXPECT_EQ(panel.selected_index(), -1);
}

TEST(BagInfoPanelSelectRow, CallbackNotFiredOnEmptyTopics)
{
    BagInfoPanel panel;
    int calls = 0;
    panel.set_topic_select_callback([&](const std::string&, const std::string&) { ++calls; });
    panel.select_row(0);
    EXPECT_EQ(calls, 0);
}

// ============================================================================
// Suite: BagInfoPanelPlotRow
// ============================================================================

TEST(BagInfoPanelPlotRow, PlotOnEmptyTopicsNoOp)
{
    BagInfoPanel panel;
    EXPECT_NO_THROW(panel.plot_row(0));
}

TEST(BagInfoPanelPlotRow, PlotCallbackNotFiredOnEmptyTopics)
{
    BagInfoPanel panel;
    int calls = 0;
    panel.set_topic_plot_callback([&](const std::string&, const std::string&) { ++calls; });
    panel.plot_row(0);
    EXPECT_EQ(calls, 0);
}

// ============================================================================
// Suite: BagInfoPanelCallbacks
// ============================================================================

TEST(BagInfoPanelCallbacks, SetAndGetSelectCallback)
{
    BagInfoPanel panel;
    bool fired = false;
    panel.set_topic_select_callback([&](const std::string&, const std::string&) { fired = true; });
    EXPECT_TRUE(static_cast<bool>(panel.topic_select_callback()));
}

TEST(BagInfoPanelCallbacks, SetAndGetPlotCallback)
{
    BagInfoPanel panel;
    panel.set_topic_plot_callback([](const std::string&, const std::string&) {});
    EXPECT_TRUE(static_cast<bool>(panel.topic_plot_callback()));
}

TEST(BagInfoPanelCallbacks, SetAndGetOpenedCallback)
{
    BagInfoPanel panel;
    panel.set_bag_opened_callback([](const std::string&) {});
    EXPECT_TRUE(static_cast<bool>(panel.bag_opened_callback()));
}

TEST(BagInfoPanelCallbacks, NullCallbacksAreDefault)
{
    BagInfoPanel panel;
    EXPECT_FALSE(static_cast<bool>(panel.topic_select_callback()));
    EXPECT_FALSE(static_cast<bool>(panel.topic_plot_callback()));
    EXPECT_FALSE(static_cast<bool>(panel.bag_opened_callback()));
}

// ============================================================================
// Suite: BagInfoPanelRefreshSummary
// ============================================================================

TEST(BagInfoPanelRefreshSummary, RefreshWhenNotOpenGivesEmptySummary)
{
    BagInfoPanel panel;
    panel.refresh_summary();
    EXPECT_FALSE(panel.summary().is_open);
    EXPECT_EQ(panel.summary().topic_count(), 0u);
    EXPECT_EQ(panel.summary().message_count, 0u);
}

TEST(BagInfoPanelRefreshSummary, TopicsAccessorMatchesSummary)
{
    BagInfoPanel panel;
    EXPECT_EQ(&panel.topics(), &panel.summary().topics);
}

// ============================================================================
// Suite: BagInfoPanelDrawNoOp (without ImGui context)
// ============================================================================

TEST(BagInfoPanelDrawNoOp, DrawWithoutImGuiDoesNotCrash)
{
    BagInfoPanel panel;
    // Without SPECTRA_USE_IMGUI or a valid ImGui context these must be no-ops.
    EXPECT_NO_THROW(panel.draw_inline());
}

TEST(BagInfoPanelDrawNoOp, DrawWithPOpenDoesNotCrash)
{
    BagInfoPanel panel;
    bool open = true;
    EXPECT_NO_THROW(panel.draw(&open));
}

// ============================================================================
// Suite: BagSummaryValid
// ============================================================================

TEST(BagSummaryValid, ValidFalseWhenNotOpen)
{
    BagSummary s;
    EXPECT_FALSE(s.valid());
}

TEST(BagSummaryValid, ValidTrueWhenOpen)
{
    BagSummary s;
    s.is_open = true;
    EXPECT_TRUE(s.valid());
}

TEST(BagSummaryValid, TopicCountMatchesVector)
{
    BagSummary s;
    EXPECT_EQ(s.topic_count(), 0u);
    s.topics.push_back({"t1", "std_msgs/msg/Float64", 100});
    EXPECT_EQ(s.topic_count(), 1u);
    s.topics.push_back({"t2", "std_msgs/msg/Int32", 50});
    EXPECT_EQ(s.topic_count(), 2u);
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

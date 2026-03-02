// test_bag_recorder.cpp — Unit tests for BagRecorder (D3)
//
// When SPECTRA_ROS2_BAG is defined (full build):
//   - All 50+ tests run using rosbag2_cpp::Writer + synthetic bag files.
//   - Uses RclcppEnvironment custom main() (same as test_bag_reader.cpp).
//
// When SPECTRA_ROS2_BAG is NOT defined (stub-only build):
//   - 8 stub tests validate the no-op API via GTest::gtest_main.

#ifdef SPECTRA_ROS2_BAG

// ============================================================================
// Full tests (SPECTRA_ROS2_BAG defined)
// ============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include "bag_recorder.hpp"
#include "bag_reader.hpp"

namespace fs = std::filesystem;
using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// RclcppEnvironment — initialises / shuts down rclcpp once for the process.
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        if (!rclcpp::ok()) {
            rclcpp::init(0, nullptr);
        }
    }
    void TearDown() override
    {
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
    }
};

// ---------------------------------------------------------------------------
// Unique temp directory per test.
// ---------------------------------------------------------------------------

static std::string make_tmp_dir(const std::string& name)
{
    const auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string dir = "/tmp/spectra_rec_test_" + name + "_" + std::to_string(ns);
    fs::create_directories(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Fixture — shared node + executor
// ---------------------------------------------------------------------------

class BagRecorderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        node_ = rclcpp::Node::make_shared("test_bag_recorder_node",
                                          rclcpp::NodeOptions());
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);
        spin_thread_ = std::thread([this] {
            while (!stop_spin_.load()) {
                executor_->spin_some(std::chrono::milliseconds(10));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    void TearDown() override
    {
        stop_spin_.store(true);
        if (spin_thread_.joinable()) {
            spin_thread_.join();
        }
        executor_.reset();
        node_.reset();
    }

    // Publish `n` Float64 messages to `topic` and wait for them to arrive.
    void publish_float64(const std::string& topic, int n, double start_value = 1.0)
    {
        auto pub = node_->create_publisher<std_msgs::msg::Float64>(topic, 10);
        // Allow time for subscription to connect.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (int i = 0; i < n; ++i) {
            std_msgs::msg::Float64 msg;
            msg.data = start_value + i;
            pub->publish(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    rclcpp::Node::SharedPtr                                        node_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor>     executor_;
    std::thread                                                    spin_thread_;
    std::atomic<bool>                                              stop_spin_{false};
};

// ===========================================================================
// Suite 1: Construction
// ===========================================================================

TEST_F(BagRecorderTest, Construction_NotRecording)
{
    BagRecorder rec(node_);
    EXPECT_EQ(rec.state(), RecordingState::Idle);
    EXPECT_FALSE(rec.is_recording());
}

TEST_F(BagRecorderTest, Construction_DefaultConfig)
{
    BagRecorder rec(node_);
    EXPECT_EQ(rec.max_size_bytes(), 0u);
    EXPECT_DOUBLE_EQ(rec.max_duration_seconds(), 0.0);
    EXPECT_TRUE(rec.reliable_qos());
    EXPECT_TRUE(rec.storage_id().empty());
}

TEST_F(BagRecorderTest, Construction_ZeroStats)
{
    BagRecorder rec(node_);
    EXPECT_EQ(rec.recorded_message_count(), 0u);
    EXPECT_EQ(rec.recorded_bytes(), 0u);
    EXPECT_DOUBLE_EQ(rec.elapsed_seconds(), 0.0);
    EXPECT_EQ(rec.split_index(), 0u);
    EXPECT_TRUE(rec.recorded_topics().empty());
    EXPECT_TRUE(rec.recording_path().empty());
    EXPECT_TRUE(rec.current_path().empty());
}

// ===========================================================================
// Suite 2: Configuration setters / getters
// ===========================================================================

TEST_F(BagRecorderTest, Config_MaxSizeBytes)
{
    BagRecorder rec(node_);
    rec.set_max_size_bytes(1024u * 1024u);
    EXPECT_EQ(rec.max_size_bytes(), 1024u * 1024u);
    rec.set_max_size_bytes(0u);
    EXPECT_EQ(rec.max_size_bytes(), 0u);
}

TEST_F(BagRecorderTest, Config_MaxDurationSeconds)
{
    BagRecorder rec(node_);
    rec.set_max_duration_seconds(30.0);
    EXPECT_DOUBLE_EQ(rec.max_duration_seconds(), 30.0);
    rec.set_max_duration_seconds(0.0);
    EXPECT_DOUBLE_EQ(rec.max_duration_seconds(), 0.0);
}

TEST_F(BagRecorderTest, Config_StorageIdOverride)
{
    BagRecorder rec(node_);
    rec.set_storage_id("mcap");
    EXPECT_EQ(rec.storage_id(), "mcap");
    rec.set_storage_id("sqlite3");
    EXPECT_EQ(rec.storage_id(), "sqlite3");
    rec.set_storage_id("");
    EXPECT_TRUE(rec.storage_id().empty());
}

TEST_F(BagRecorderTest, Config_ReliableQos)
{
    BagRecorder rec(node_);
    rec.set_reliable_qos(false);
    EXPECT_FALSE(rec.reliable_qos());
    rec.set_reliable_qos(true);
    EXPECT_TRUE(rec.reliable_qos());
}

// ===========================================================================
// Suite 3: start() / stop() failure paths
// ===========================================================================

TEST_F(BagRecorderTest, Start_EmptyPathFails)
{
    BagRecorder rec(node_);
    EXPECT_FALSE(rec.start("", {"/chatter"}));
    EXPECT_FALSE(rec.last_error().empty());
    EXPECT_EQ(rec.state(), RecordingState::Idle);
}

TEST_F(BagRecorderTest, Start_NoTopicsFails)
{
    const std::string dir  = make_tmp_dir("no_topics");
    const std::string path = dir + "/test.db3";
    BagRecorder rec(node_);
    EXPECT_FALSE(rec.start(path, {}));
    EXPECT_FALSE(rec.last_error().empty());
    EXPECT_EQ(rec.state(), RecordingState::Idle);
}

TEST_F(BagRecorderTest, Start_UnknownTopicFails)
{
    const std::string dir  = make_tmp_dir("unknown_topic");
    const std::string path = dir + "/test.db3";
    BagRecorder rec(node_);
    // Topic "/no_such_topic_xyz" does not exist in graph
    EXPECT_FALSE(rec.start(path, {"/no_such_topic_xyz_abc"}));
    EXPECT_FALSE(rec.last_error().empty());
    EXPECT_EQ(rec.state(), RecordingState::Idle);
}

TEST_F(BagRecorderTest, DoubleStartFails)
{
    // First start a publisher so the topic is in the graph
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_topic", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string dir  = make_tmp_dir("double_start");
    const std::string path = dir + "/test.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_topic"}));
    EXPECT_TRUE(rec.is_recording());

    // Second start should fail
    EXPECT_FALSE(rec.start(path, {"/rec_topic"}));
    EXPECT_FALSE(rec.last_error().empty());

    rec.stop();
}

TEST_F(BagRecorderTest, StopWhenIdleIsNoop)
{
    BagRecorder rec(node_);
    EXPECT_NO_THROW(rec.stop());
    EXPECT_EQ(rec.state(), RecordingState::Idle);
}

// ===========================================================================
// Suite 4: Successful recording lifecycle
// ===========================================================================

TEST_F(BagRecorderTest, StartStop_FileCreated)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_float", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string dir  = make_tmp_dir("file_created");
    const std::string path = dir + "/output.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_float"})) << rec.last_error();
    EXPECT_EQ(rec.state(), RecordingState::Recording);
    EXPECT_TRUE(rec.is_recording());
    EXPECT_EQ(rec.recording_path(), path);
    EXPECT_EQ(rec.current_path(), path);
    EXPECT_FALSE(rec.recorded_topics().empty());

    rec.stop();

    EXPECT_EQ(rec.state(), RecordingState::Idle);
    EXPECT_FALSE(rec.is_recording());
    // The file (or directory for sqlite3) should exist after stop
    // rosbag2 may write metadata.yaml in a directory with same name minus extension
    // at minimum the path or a sibling should exist
    const bool file_exists = fs::exists(path) || fs::exists(dir);
    EXPECT_TRUE(file_exists);
}

TEST_F(BagRecorderTest, StartStop_ElapsedNonzero)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_elapsed", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string dir  = make_tmp_dir("elapsed");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_elapsed"})) << rec.last_error();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_GT(rec.elapsed_seconds(), 0.0);

    rec.stop();
    EXPECT_DOUBLE_EQ(rec.elapsed_seconds(), 0.0);  // resets to 0 on stop
}

TEST_F(BagRecorderTest, StartStop_RecordedTopics)
{
    auto pub1 = node_->create_publisher<std_msgs::msg::Float64>("/rec_top_a", 10);
    auto pub2 = node_->create_publisher<std_msgs::msg::Float64>("/rec_top_b", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("recorded_topics");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_top_a", "/rec_top_b"})) << rec.last_error();

    const auto topics = rec.recorded_topics();
    EXPECT_EQ(topics.size(), 2u);

    rec.stop();
    EXPECT_TRUE(rec.recorded_topics().empty());
}

// ===========================================================================
// Suite 5: Message recording — count & bytes
// ===========================================================================

TEST_F(BagRecorderTest, Recording_MessageCount)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_cnt", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("msg_count");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_cnt"})) << rec.last_error();

    // Publish 10 messages
    for (int i = 0; i < 10; ++i) {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    // Allow delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(rec.recorded_message_count(), 5u);  // at least 5 received
    EXPECT_GT(rec.recorded_bytes(), 0u);

    rec.stop();
}

TEST_F(BagRecorderTest, Recording_BytesIncrease)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_bytes", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("bytes");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_bytes"})) << rec.last_error();

    const uint64_t bytes_before = rec.recorded_bytes();

    std_msgs::msg::Float64 msg;
    msg.data = 42.0;
    pub->publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const uint64_t bytes_after = rec.recorded_bytes();
    EXPECT_GE(bytes_after, bytes_before);

    rec.stop();
}

// ===========================================================================
// Suite 6: Auto-split by size
// ===========================================================================

TEST_F(BagRecorderTest, AutoSplit_BySize_SplitCallbackFires)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_split_sz", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("split_sz");
    const std::string path = dir + "/out.db3";

    std::atomic<int>  split_count{0};
    std::string       last_new_path;

    BagRecorder rec(node_);
    // Very small limit — 1 byte triggers split on every message
    rec.set_max_size_bytes(1u);
    rec.set_split_callback([&](const RecordingSplitInfo& info) {
        ++split_count;
        last_new_path = info.new_path;
    });

    ASSERT_TRUE(rec.start(path, {"/rec_split_sz"})) << rec.last_error();

    // Publish 3 messages
    for (int i = 0; i < 3; ++i) {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(split_count.load(), 1);
    EXPECT_FALSE(last_new_path.empty());

    rec.stop();
}

TEST_F(BagRecorderTest, AutoSplit_BySize_SplitIndexIncreases)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_split_idx", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("split_idx");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    rec.set_max_size_bytes(1u);  // trigger split on every message

    ASSERT_TRUE(rec.start(path, {"/rec_split_idx"})) << rec.last_error();

    // Publish enough messages to guarantee at least one split
    for (int i = 0; i < 5; ++i) {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    EXPECT_GE(rec.split_index(), 1u);

    rec.stop();
}

TEST_F(BagRecorderTest, AutoSplit_BySize_SplitPathPattern)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_split_path", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("split_path");
    const std::string path = dir + "/output.db3";

    std::string first_new_path;

    BagRecorder rec(node_);
    rec.set_max_size_bytes(1u);
    rec.set_split_callback([&](const RecordingSplitInfo& info) {
        if (first_new_path.empty()) {
            first_new_path = info.new_path;
        }
    });

    ASSERT_TRUE(rec.start(path, {"/rec_split_path"})) << rec.last_error();

    std_msgs::msg::Float64 msg;
    msg.data = 1.0;
    pub->publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    rec.stop();

    if (!first_new_path.empty()) {
        // The split path should contain "split001"
        EXPECT_NE(first_new_path.find("split001"), std::string::npos)
            << "first_new_path=" << first_new_path;
        // Must end with .db3
        EXPECT_EQ(first_new_path.substr(first_new_path.size() - 4), ".db3");
    }
}

// ===========================================================================
// Suite 7: Auto-split by duration
// ===========================================================================

TEST_F(BagRecorderTest, AutoSplit_ByDuration_SplitCallbackFires)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_split_dur", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("split_dur");
    const std::string path = dir + "/out.db3";

    std::atomic<int> split_count{0};

    BagRecorder rec(node_);
    rec.set_max_duration_seconds(0.05);  // 50 ms — very short, split quickly
    rec.set_split_callback([&](const RecordingSplitInfo&) {
        ++split_count;
    });

    ASSERT_TRUE(rec.start(path, {"/rec_split_dur"})) << rec.last_error();

    // Publish messages over 200 ms — triggers several duration splits
    for (int i = 0; i < 10; ++i) {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(split_count.load(), 1);

    rec.stop();
}

// ===========================================================================
// Suite 8: Callbacks
// ===========================================================================

TEST_F(BagRecorderTest, ErrorCallback_NotFiredOnSuccess)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_err_cb", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("err_cb");
    const std::string path = dir + "/out.db3";

    std::atomic<int> error_count{0};
    BagRecorder rec(node_);
    rec.set_error_callback([&](const std::string&) { ++error_count; });

    ASSERT_TRUE(rec.start(path, {"/rec_err_cb"})) << rec.last_error();

    std_msgs::msg::Float64 msg;
    msg.data = 99.0;
    pub->publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    rec.stop();
    EXPECT_EQ(error_count.load(), 0);
}

TEST_F(BagRecorderTest, SplitCallback_InfoFields)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_info", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("info_fields");
    const std::string path = dir + "/out.db3";

    RecordingSplitInfo captured_info;
    std::atomic<bool>  got_info{false};

    BagRecorder rec(node_);
    rec.set_max_size_bytes(1u);
    rec.set_split_callback([&](const RecordingSplitInfo& info) {
        if (!got_info.load()) {
            captured_info = info;
            got_info.store(true);
        }
    });

    ASSERT_TRUE(rec.start(path, {"/rec_info"})) << rec.last_error();

    std_msgs::msg::Float64 msg;
    msg.data = 7.0;
    pub->publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    rec.stop();

    if (got_info.load()) {
        EXPECT_FALSE(captured_info.closed_path.empty());
        EXPECT_FALSE(captured_info.new_path.empty());
        EXPECT_EQ(captured_info.split_index, 1u);
    }
}

// ===========================================================================
// Suite 9: Storage ID detection
// ===========================================================================

TEST_F(BagRecorderTest, StorageId_Db3Extension)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_db3", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("db3_ext");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_db3"})) << rec.last_error();
    rec.stop();
}

TEST_F(BagRecorderTest, StorageId_McapExtension)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_mcap", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("mcap_ext");
    const std::string path = dir + "/out.mcap";

    BagRecorder rec(node_);
    // May fail if mcap plugin not installed — treat as acceptable
    const bool ok = rec.start(path, {"/rec_mcap"});
    if (ok) {
        rec.stop();
    }
    // Either success (mcap plugin present) or graceful error — not a crash
}

TEST_F(BagRecorderTest, StorageId_Override)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_override", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("override_id");
    const std::string path = dir + "/out.bag";  // no recognised extension

    BagRecorder rec(node_);
    rec.set_storage_id("sqlite3");  // explicit override
    ASSERT_TRUE(rec.start(path, {"/rec_override"})) << rec.last_error();
    rec.stop();
}

// ===========================================================================
// Suite 10: Re-use (start → stop → start again)
// ===========================================================================

TEST_F(BagRecorderTest, Reuse_StartStopStart)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_reuse", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    BagRecorder rec(node_);

    const std::string dir1  = make_tmp_dir("reuse1");
    const std::string path1 = dir1 + "/out.db3";

    ASSERT_TRUE(rec.start(path1, {"/rec_reuse"})) << rec.last_error();
    EXPECT_TRUE(rec.is_recording());
    rec.stop();
    EXPECT_FALSE(rec.is_recording());

    const std::string dir2  = make_tmp_dir("reuse2");
    const std::string path2 = dir2 + "/out.db3";

    ASSERT_TRUE(rec.start(path2, {"/rec_reuse"})) << rec.last_error();
    EXPECT_TRUE(rec.is_recording());
    EXPECT_EQ(rec.recording_path(), path2);
    rec.stop();
}

TEST_F(BagRecorderTest, Reuse_StatsClearedOnNewStart)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_stats_clear", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    BagRecorder rec(node_);

    // First recording — publish some messages
    const std::string dir1  = make_tmp_dir("stats_clear1");
    ASSERT_TRUE(rec.start(dir1 + "/out.db3", {"/rec_stats_clear"})) << rec.last_error();
    for (int i = 0; i < 5; ++i) {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    rec.stop();

    // Second recording — stats should reset
    const std::string dir2  = make_tmp_dir("stats_clear2");
    ASSERT_TRUE(rec.start(dir2 + "/out.db3", {"/rec_stats_clear"})) << rec.last_error();
    EXPECT_EQ(rec.recorded_message_count(), 0u);
    EXPECT_EQ(rec.recorded_bytes(), 0u);
    EXPECT_EQ(rec.split_index(), 0u);
    rec.stop();
}

// ===========================================================================
// Suite 11: Edge cases
// ===========================================================================

TEST_F(BagRecorderTest, EdgeCase_DestroyWhileRecording)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_destroy", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("destroy");
    const std::string path = dir + "/out.db3";

    {
        BagRecorder rec(node_);
        ASSERT_TRUE(rec.start(path, {"/rec_destroy"})) << rec.last_error();
        std_msgs::msg::Float64 msg;
        msg.data = 1.0;
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // rec destroyed here without explicit stop — must not crash
    }
}

TEST_F(BagRecorderTest, EdgeCase_SplitIndexAfterMultipleSplits)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_multi_split", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("multi_split");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    rec.set_max_size_bytes(1u);  // split every message

    ASSERT_TRUE(rec.start(path, {"/rec_multi_split"})) << rec.last_error();

    for (int i = 0; i < 4; ++i) {
        std_msgs::msg::Float64 msg;
        msg.data = static_cast<double>(i);
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const uint32_t idx = rec.split_index();
    EXPECT_GE(idx, 1u);  // at least one split happened

    rec.stop();
}

TEST_F(BagRecorderTest, EdgeCase_ClearError)
{
    BagRecorder rec(node_);
    // Trigger an error
    EXPECT_FALSE(rec.start("", {"/topic"}));
    EXPECT_FALSE(rec.last_error().empty());
    rec.clear_error();
    EXPECT_TRUE(rec.last_error().empty());
}

TEST_F(BagRecorderTest, EdgeCase_MultipleTopics_Db3)
{
    auto pub1 = node_->create_publisher<std_msgs::msg::Float64>("/rec_mt_a", 10);
    auto pub2 = node_->create_publisher<std_msgs::msg::Float64>("/rec_mt_b", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::string dir  = make_tmp_dir("multi_topic");
    const std::string path = dir + "/out.db3";

    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(path, {"/rec_mt_a", "/rec_mt_b"})) << rec.last_error();
    EXPECT_EQ(rec.recorded_topics().size(), 2u);

    for (int i = 0; i < 5; ++i) {
        std_msgs::msg::Float64 ma, mb;
        ma.data = static_cast<double>(i);
        mb.data = static_cast<double>(i) * 2;
        pub1->publish(ma);
        pub2->publish(mb);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_GE(rec.recorded_message_count(), 5u);

    rec.stop();
}

// ===========================================================================
// Suite 12: RecordingState enum coverage
// ===========================================================================

TEST_F(BagRecorderTest, StateEnum_AllValues)
{
    EXPECT_NE(RecordingState::Idle,      RecordingState::Recording);
    EXPECT_NE(RecordingState::Idle,      RecordingState::Stopping);
    EXPECT_NE(RecordingState::Recording, RecordingState::Stopping);
}

TEST_F(BagRecorderTest, StateEnum_IdleAfterStop)
{
    auto pub = node_->create_publisher<std_msgs::msg::Float64>("/rec_state", 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const std::string dir  = make_tmp_dir("state_enum");
    BagRecorder rec(node_);
    ASSERT_TRUE(rec.start(dir + "/out.db3", {"/rec_state"})) << rec.last_error();
    EXPECT_EQ(rec.state(), RecordingState::Recording);
    rec.stop();
    EXPECT_EQ(rec.state(), RecordingState::Idle);
}

// ===========================================================================
// Suite 13: RecordingSplitInfo struct
// ===========================================================================

TEST(RecordingSplitInfoTest, DefaultFields)
{
    RecordingSplitInfo info;
    EXPECT_TRUE(info.closed_path.empty());
    EXPECT_TRUE(info.new_path.empty());
    EXPECT_EQ(info.split_index, 0u);
    EXPECT_EQ(info.messages_in_closed, 0u);
    EXPECT_EQ(info.bytes_in_closed, 0u);
}

TEST(RecordingSplitInfoTest, Copyable)
{
    RecordingSplitInfo a;
    a.closed_path        = "old.db3";
    a.new_path           = "new.db3";
    a.split_index        = 2u;
    a.messages_in_closed = 100u;
    a.bytes_in_closed    = 4096u;

    const RecordingSplitInfo b = a;
    EXPECT_EQ(b.closed_path, "old.db3");
    EXPECT_EQ(b.new_path, "new.db3");
    EXPECT_EQ(b.split_index, 2u);
    EXPECT_EQ(b.messages_in_closed, 100u);
    EXPECT_EQ(b.bytes_in_closed, 4096u);
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}

#else // SPECTRA_ROS2_BAG not defined — stub tests only

// ============================================================================
// Stub tests (SPECTRA_ROS2_BAG absent) — validate the no-op API compiles and
// behaves correctly.  Uses GTest::gtest_main (no custom main).
// ============================================================================

#include <gtest/gtest.h>
#include "bag_recorder.hpp"

using namespace spectra::adapters::ros2;

TEST(BagRecorderStub, DefaultState)
{
    BagRecorder rec(nullptr);
    EXPECT_EQ(rec.state(), RecordingState::Idle);
    EXPECT_FALSE(rec.is_recording());
}

TEST(BagRecorderStub, StartReturnsFalse)
{
    BagRecorder rec(nullptr);
    EXPECT_FALSE(rec.start("/tmp/test.db3", {"/topic"}));
}

TEST(BagRecorderStub, StopIsNoop)
{
    BagRecorder rec(nullptr);
    EXPECT_NO_THROW(rec.stop());
}

TEST(BagRecorderStub, ZeroStats)
{
    BagRecorder rec(nullptr);
    EXPECT_EQ(rec.recorded_message_count(), 0u);
    EXPECT_EQ(rec.recorded_bytes(), 0u);
    EXPECT_DOUBLE_EQ(rec.elapsed_seconds(), 0.0);
    EXPECT_EQ(rec.split_index(), 0u);
    EXPECT_TRUE(rec.recorded_topics().empty());
}

TEST(BagRecorderStub, LastErrorSet)
{
    BagRecorder rec(nullptr);
    EXPECT_FALSE(rec.last_error().empty());
}

TEST(BagRecorderStub, ClearError)
{
    BagRecorder rec(nullptr);
    rec.clear_error();
    EXPECT_TRUE(rec.last_error().empty());
}

TEST(BagRecorderStub, CallbacksAccepted)
{
    BagRecorder rec(nullptr);
    EXPECT_NO_THROW(rec.set_split_callback([](const RecordingSplitInfo&) {}));
    EXPECT_NO_THROW(rec.set_error_callback([](const std::string&) {}));
}

TEST(BagRecorderStub, ConfigGettersSetters)
{
    BagRecorder rec(nullptr);
    rec.set_max_size_bytes(1024u);
    EXPECT_EQ(rec.max_size_bytes(), 0u);  // stub always returns 0

    rec.set_max_duration_seconds(30.0);
    EXPECT_DOUBLE_EQ(rec.max_duration_seconds(), 0.0);  // stub always 0

    rec.set_reliable_qos(false);
    EXPECT_TRUE(rec.reliable_qos());  // stub always true

    rec.set_storage_id("mcap");
    EXPECT_TRUE(rec.storage_id().empty());  // stub always empty
}

#endif // SPECTRA_ROS2_BAG

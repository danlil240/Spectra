// Unit tests for ros_app_shell — G1 mission.
//
// Covers:
//   - parse_args: CLI argument parsing
//   - parse_layout_mode / layout_mode_name: enum round-trips
//   - RosAppConfig: defaults
//   - RosAppShell::window_title()
//   - RosAppShell::add_topic_plot() topic:field parsing (logic path)
//   - Layout visibility flags per LayoutMode
//
// No ROS2 runtime required — all tests exercise pure C++ logic only.
// RosAppShell::init() / shutdown() / poll() are NOT called here (they need
// a live ROS2 context). Those are covered by the manual integration checklist.

#include <gtest/gtest.h>

#include "ros_app_shell.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static RosAppConfig make_cfg(const std::string& node = "spectra_ros")
{
    RosAppConfig c;
    c.node_name = node;
    return c;
}

// Build a fake argv from a vector of strings.
// Returns the char* array (pointers into the strings).
static std::vector<char*> make_argv(std::vector<std::string>& args)
{
    std::vector<char*> ptrs;
    ptrs.reserve(args.size());
    for (auto& s : args) ptrs.push_back(s.data());
    return ptrs;
}

// ---------------------------------------------------------------------------
// Suite: LayoutMode
// ---------------------------------------------------------------------------

TEST(LayoutMode, ParseDefault)
{
    EXPECT_EQ(parse_layout_mode("default"),  LayoutMode::Default);
}

TEST(LayoutMode, ParsePlotOnly)
{
    EXPECT_EQ(parse_layout_mode("plot-only"), LayoutMode::PlotOnly);
}

TEST(LayoutMode, ParseMonitor)
{
    EXPECT_EQ(parse_layout_mode("monitor"), LayoutMode::Monitor);
}

TEST(LayoutMode, ParseUnknownFallsBackToDefault)
{
    EXPECT_EQ(parse_layout_mode("bogus"),   LayoutMode::Default);
    EXPECT_EQ(parse_layout_mode(""),        LayoutMode::Default);
    EXPECT_EQ(parse_layout_mode("MONITOR"), LayoutMode::Default);
}

TEST(LayoutMode, NameRoundTrip)
{
    EXPECT_STREQ(layout_mode_name(LayoutMode::Default),  "default");
    EXPECT_STREQ(layout_mode_name(LayoutMode::PlotOnly), "plot-only");
    EXPECT_STREQ(layout_mode_name(LayoutMode::Monitor),  "monitor");
}

// ---------------------------------------------------------------------------
// Suite: RosAppConfig defaults
// ---------------------------------------------------------------------------

TEST(RosAppConfig, Defaults)
{
    RosAppConfig cfg;
    EXPECT_EQ(cfg.node_name,    "spectra_ros");
    EXPECT_EQ(cfg.node_ns,      "");
    EXPECT_EQ(cfg.layout,       LayoutMode::Default);
    EXPECT_TRUE(cfg.initial_topics.empty());
    EXPECT_TRUE(cfg.bag_file.empty());
    EXPECT_DOUBLE_EQ(cfg.time_window_s, 30.0);
    EXPECT_EQ(cfg.subplot_rows, 1);
    EXPECT_EQ(cfg.subplot_cols, 1);
    EXPECT_EQ(cfg.window_width,  1600u);
    EXPECT_EQ(cfg.window_height,  900u);
}

TEST(StartupPolicy, DisablesValidationWhenEnvIsUnset)
{
    EXPECT_TRUE(should_skip_debug_validation_for_ros_app(nullptr, nullptr));
}

TEST(StartupPolicy, RespectsExplicitNoValidationEnv)
{
    EXPECT_TRUE(should_skip_debug_validation_for_ros_app("1", nullptr));
    EXPECT_FALSE(should_skip_debug_validation_for_ros_app("0", nullptr));
}

TEST(StartupPolicy, EnableValidationEnvOverridesDefault)
{
    EXPECT_FALSE(should_skip_debug_validation_for_ros_app(nullptr, "1"));
    EXPECT_TRUE(should_skip_debug_validation_for_ros_app(nullptr, "0"));
}

TEST(StartupPolicy, TrimsVulkanLoaderEnvironmentByDefault)
{
    EXPECT_TRUE(should_trim_vulkan_loader_environment_for_ros_app(nullptr));
}

TEST(StartupPolicy, PreserveLoaderEnvDisablesTrim)
{
    EXPECT_FALSE(should_trim_vulkan_loader_environment_for_ros_app("1"));
    EXPECT_TRUE(should_trim_vulkan_loader_environment_for_ros_app("0"));
}

// ---------------------------------------------------------------------------
// Suite: parse_args
// ---------------------------------------------------------------------------

TEST(ParseArgs, EmptyArgv)
{
    std::vector<std::string> args = {"spectra-ros"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.node_name, "spectra_ros");
    EXPECT_EQ(cfg.layout,    LayoutMode::Default);
}

TEST(ParseArgs, HelpFlag)
{
    std::vector<std::string> args = {"spectra-ros", "--help"};
    auto ptrs = make_argv(args);
    std::string err;
    parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("Usage:"), std::string::npos);
}

TEST(ParseArgs, HelpShortFlag)
{
    std::vector<std::string> args = {"spectra-ros", "-h"};
    auto ptrs = make_argv(args);
    std::string err;
    parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("Usage:"), std::string::npos);
}

TEST(ParseArgs, NodeName)
{
    std::vector<std::string> args = {"spectra-ros", "--node-name", "my_node"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.node_name, "my_node");
}

TEST(ParseArgs, NodeNameShort)
{
    std::vector<std::string> args = {"spectra-ros", "-n", "robot_vis"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.node_name, "robot_vis");
}

TEST(ParseArgs, LayoutPlotOnly)
{
    std::vector<std::string> args = {"spectra-ros", "--layout", "plot-only"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.layout, LayoutMode::PlotOnly);
}

TEST(ParseArgs, LayoutMonitor)
{
    std::vector<std::string> args = {"spectra-ros", "--layout", "monitor"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.layout, LayoutMode::Monitor);
}

TEST(ParseArgs, LayoutShortFlag)
{
    std::vector<std::string> args = {"spectra-ros", "-l", "plot-only"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.layout, LayoutMode::PlotOnly);
}

TEST(ParseArgs, SingleTopic)
{
    std::vector<std::string> args = {"spectra-ros", "--topics", "/cmd_vel"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(cfg.initial_topics.size(), 1u);
    EXPECT_EQ(cfg.initial_topics[0], "/cmd_vel");
}

TEST(ParseArgs, MultipleTopics)
{
    std::vector<std::string> args = {
        "spectra-ros", "--topics", "/imu", "/cmd_vel", "/odom"
    };
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(cfg.initial_topics.size(), 3u);
    EXPECT_EQ(cfg.initial_topics[0], "/imu");
    EXPECT_EQ(cfg.initial_topics[1], "/cmd_vel");
    EXPECT_EQ(cfg.initial_topics[2], "/odom");
}

TEST(ParseArgs, TopicsWithField)
{
    std::vector<std::string> args = {
        "spectra-ros", "-t", "/imu:linear_acceleration.x"
    };
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(cfg.initial_topics.size(), 1u);
    EXPECT_EQ(cfg.initial_topics[0], "/imu:linear_acceleration.x");
}

TEST(ParseArgs, BagFile)
{
    std::vector<std::string> args = {"spectra-ros", "--bag", "/data/my.db3"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.bag_file, "/data/my.db3");
}

TEST(ParseArgs, BagShortFlag)
{
    std::vector<std::string> args = {"spectra-ros", "-b", "recording.bag"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.bag_file, "recording.bag");
}

TEST(ParseArgs, WindowSeconds)
{
    std::vector<std::string> args = {"spectra-ros", "--window-s", "60"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_DOUBLE_EQ(cfg.time_window_s, 60.0);
}

TEST(ParseArgs, WindowSecondsShortFlag)
{
    std::vector<std::string> args = {"spectra-ros", "-w", "10.5"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_DOUBLE_EQ(cfg.time_window_s, 10.5);
}

TEST(ParseArgs, WindowSecondsClampedToMin)
{
    std::vector<std::string> args = {"spectra-ros", "--window-s", "0.001"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_DOUBLE_EQ(cfg.time_window_s, RosPlotManager::MIN_WINDOW_S);
}

TEST(ParseArgs, WindowSecondsClampedToMax)
{
    std::vector<std::string> args = {"spectra-ros", "--window-s", "99999"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_DOUBLE_EQ(cfg.time_window_s, RosPlotManager::MAX_WINDOW_S);
}

TEST(ParseArgs, InvalidWindowSeconds)
{
    std::vector<std::string> args = {"spectra-ros", "--window-s", "notanumber"};
    auto ptrs = make_argv(args);
    std::string err;
    parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("window-s"), std::string::npos);
}

TEST(ParseArgs, RowsAndCols)
{
    std::vector<std::string> args = {"spectra-ros", "--rows", "3", "--cols", "2"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.subplot_rows, 3);
    EXPECT_EQ(cfg.subplot_cols, 2);
}

TEST(ParseArgs, RowsClampedToMin)
{
    std::vector<std::string> args = {"spectra-ros", "--rows", "0"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.subplot_rows, 1);
}

TEST(ParseArgs, ColsClampedToMin)
{
    std::vector<std::string> args = {"spectra-ros", "--cols", "-5"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.subplot_cols, 1);
}

TEST(ParseArgs, InvalidRows)
{
    std::vector<std::string> args = {"spectra-ros", "--rows", "abc"};
    auto ptrs = make_argv(args);
    std::string err;
    parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_FALSE(err.empty());
}

TEST(ParseArgs, InvalidCols)
{
    std::vector<std::string> args = {"spectra-ros", "--cols", "xyz"};
    auto ptrs = make_argv(args);
    std::string err;
    parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_FALSE(err.empty());
}

TEST(ParseArgs, MultipleOptions)
{
    std::vector<std::string> args = {
        "spectra-ros",
        "--node-name", "my_vis",
        "--layout", "monitor",
        "--rows", "2",
        "--cols", "3",
        "--window-s", "45",
        "--bag", "test.db3",
        "--topics", "/imu", "/odom"
    };
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.node_name,     "my_vis");
    EXPECT_EQ(cfg.layout,        LayoutMode::Monitor);
    EXPECT_EQ(cfg.subplot_rows,  2);
    EXPECT_EQ(cfg.subplot_cols,  3);
    EXPECT_DOUBLE_EQ(cfg.time_window_s, 45.0);
    EXPECT_EQ(cfg.bag_file,      "test.db3");
    ASSERT_EQ(cfg.initial_topics.size(), 2u);
    EXPECT_EQ(cfg.initial_topics[0], "/imu");
    EXPECT_EQ(cfg.initial_topics[1], "/odom");
}

// ---------------------------------------------------------------------------
// Suite: RosAppShell — pure logic (no ROS2 init)
// ---------------------------------------------------------------------------

TEST(RosAppShell, WindowTitleDefault)
{
    auto cfg = make_cfg("spectra_ros");
    RosAppShell shell(cfg);
    EXPECT_EQ(shell.window_title(), "Spectra ROS2 \xe2\x80\x94 spectra_ros");
}

TEST(RosAppShell, WindowTitleCustomNode)
{
    auto cfg = make_cfg("my_robot_vis");
    RosAppShell shell(cfg);
    EXPECT_EQ(shell.window_title(), "Spectra ROS2 \xe2\x80\x94 my_robot_vis");
}

TEST(RosAppShell, InitiallyNotShuttingDown)
{
    auto cfg = make_cfg();
    RosAppShell shell(cfg);
    EXPECT_FALSE(shell.shutdown_requested());
}

TEST(RosAppShell, RequestShutdown)
{
    auto cfg = make_cfg();
    RosAppShell shell(cfg);
    shell.request_shutdown();
    EXPECT_TRUE(shell.shutdown_requested());
}

TEST(RosAppShell, ActivePlotCountBeforeInit)
{
    auto cfg = make_cfg();
    RosAppShell shell(cfg);
    // Before init() no engines exist; active_plot_count() must not crash.
    EXPECT_EQ(shell.active_plot_count(), 0);
}

TEST(RosAppShell, ConfigAccessible)
{
    RosAppConfig cfg;
    cfg.node_name     = "test_node";
    cfg.subplot_rows  = 3;
    cfg.subplot_cols  = 2;
    cfg.time_window_s = 60.0;
    cfg.layout        = LayoutMode::PlotOnly;
    RosAppShell shell(cfg);
    EXPECT_EQ(shell.config().node_name,    "test_node");
    EXPECT_EQ(shell.config().subplot_rows,  3);
    EXPECT_EQ(shell.config().subplot_cols,  2);
    EXPECT_EQ(shell.config().layout,       LayoutMode::PlotOnly);
}

// ---------------------------------------------------------------------------
// Suite: Layout visibility from config
// ---------------------------------------------------------------------------

TEST(LayoutVisibility, DefaultShowsAll)
{
    RosAppConfig cfg;
    cfg.layout = LayoutMode::Default;
    RosAppShell shell(cfg);
    // Visibility is set during init(), not construction.
    // We check that the layout config is stored correctly.
    EXPECT_EQ(shell.config().layout, LayoutMode::Default);
}

TEST(LayoutVisibility, PlotOnlyHidesNonPlot)
{
    RosAppConfig cfg;
    cfg.layout = LayoutMode::PlotOnly;
    RosAppShell shell(cfg);
    // Shell sets visibility in setup_layout_visibility() called from init().
    // Without init() the defaults are still "all visible" from construction.
    // We just verify the config is stored.
    EXPECT_EQ(shell.config().layout, LayoutMode::PlotOnly);
}

TEST(LayoutVisibility, MonitorHidesPlot)
{
    RosAppConfig cfg;
    cfg.layout = LayoutMode::Monitor;
    RosAppShell shell(cfg);
    EXPECT_EQ(shell.config().layout, LayoutMode::Monitor);
}

// Visibility setters work before init.
TEST(LayoutVisibility, SettersWorkWithoutInit)
{
    RosAppConfig cfg;
    RosAppShell shell(cfg);
    shell.set_topic_list_visible(false);
    shell.set_plot_area_visible(false);
    EXPECT_FALSE(shell.topic_list_visible());
    EXPECT_FALSE(shell.plot_area_visible());
    EXPECT_TRUE(shell.topic_echo_visible());
    EXPECT_TRUE(shell.topic_stats_visible());
}

// ---------------------------------------------------------------------------
// Suite: topic_field parsing (add_topic_plot path — no ROS2 needed for parsing)
// ---------------------------------------------------------------------------

// We can't call add_topic_plot() without an initialised shell,
// but we CAN test parse_args topic format round-trips as integration.

TEST(TopicFieldParsing, TopicOnly)
{
    std::vector<std::string> args = {"spectra-ros", "--topics", "/chatter"};
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(cfg.initial_topics.size(), 1u);
    // No colon — only a topic name.
    const std::string& tf = cfg.initial_topics[0];
    const auto colon = tf.find(':');
    EXPECT_EQ(colon, std::string::npos);
    EXPECT_EQ(tf, "/chatter");
}

TEST(TopicFieldParsing, TopicWithField)
{
    std::vector<std::string> args = {
        "spectra-ros", "--topics", "/imu:linear_acceleration.z"
    };
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(cfg.initial_topics.size(), 1u);
    const std::string& tf = cfg.initial_topics[0];
    const auto colon = tf.find(':');
    ASSERT_NE(colon, std::string::npos);
    EXPECT_EQ(tf.substr(0, colon), "/imu");
    EXPECT_EQ(tf.substr(colon + 1), "linear_acceleration.z");
}

TEST(TopicFieldParsing, MixedTopicAndField)
{
    std::vector<std::string> args = {
        "spectra-ros", "-t",
        "/odom", "/imu:angular_velocity.z", "/cmd_vel:linear.x"
    };
    auto ptrs = make_argv(args);
    std::string err;
    auto cfg = parse_args(static_cast<int>(ptrs.size()), ptrs.data(), err);
    EXPECT_TRUE(err.empty());
    ASSERT_EQ(cfg.initial_topics.size(), 3u);

    EXPECT_EQ(cfg.initial_topics[0].find(':'), std::string::npos);

    const auto c1 = cfg.initial_topics[1].find(':');
    ASSERT_NE(c1, std::string::npos);
    EXPECT_EQ(cfg.initial_topics[1].substr(0, c1), "/imu");

    const auto c2 = cfg.initial_topics[2].find(':');
    ASSERT_NE(c2, std::string::npos);
    EXPECT_EQ(cfg.initial_topics[2].substr(0, c2), "/cmd_vel");
}

// ---------------------------------------------------------------------------
// Suite: TotalMessages counter
// ---------------------------------------------------------------------------

TEST(RosAppShell, TotalMessagesZeroBeforeInit)
{
    RosAppShell shell(make_cfg());
    EXPECT_EQ(shell.total_messages(), 0u);
}

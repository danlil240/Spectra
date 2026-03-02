// Unit tests for Ros2Bridge — ROS2 node lifecycle wrapper.
//
// These tests only compile and run when SPECTRA_USE_ROS2 is ON and a ROS2
// workspace is sourced.  They are registered in tests/CMakeLists.txt inside
// the if(SPECTRA_USE_ROS2) block.
//
// NOTE: rclcpp::init / rclcpp::shutdown may only be called once per process
// in older ROS2 versions.  We use a single shared initialisation via
// RclcppEnvironment so all tests share one rclcpp lifecycle.

#include "ros2_bridge.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

using namespace spectra::adapters::ros2;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Test environment: init rclcpp once for the whole test binary.
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);
    }
    void TearDown() override
    {
        if (rclcpp::ok())
            rclcpp::shutdown();
    }
};

// Registered in main below.

// ---------------------------------------------------------------------------
// Fixture: each test gets a fresh Ros2Bridge but shares the single rclcpp
// context that RclcppEnvironment started.
// ---------------------------------------------------------------------------

class Ros2BridgeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure rclcpp is running for this test (it might have been shut down
        // by a previous test that called bridge.shutdown()).
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);
    }
    void TearDown() override
    {
        // If the bridge is still alive, shut it down cleanly.
        bridge_.shutdown();
    }

    Ros2Bridge bridge_;
};

// ---------------------------------------------------------------------------
// Suite: Construction
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, InitialStateIsUninitialized)
{
    EXPECT_EQ(bridge_.state(), BridgeState::Uninitialized);
    EXPECT_FALSE(bridge_.is_ok());
    EXPECT_EQ(bridge_.node(), nullptr);
    EXPECT_EQ(bridge_.executor(), nullptr);
}

// ---------------------------------------------------------------------------
// Suite: Init
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, InitReturnsTrue)
{
    EXPECT_TRUE(bridge_.init("test_node_init"));
    EXPECT_EQ(bridge_.state(), BridgeState::Initialized);
}

TEST_F(Ros2BridgeTest, InitCreatesNode)
{
    bridge_.init("test_node_create");
    ASSERT_NE(bridge_.node(), nullptr);
    EXPECT_EQ(bridge_.node()->get_name(), std::string("test_node_create"));
}

TEST_F(Ros2BridgeTest, InitCreatesExecutor)
{
    bridge_.init("test_node_exec");
    EXPECT_NE(bridge_.executor(), nullptr);
}

TEST_F(Ros2BridgeTest, InitStoresNodeName)
{
    bridge_.init("my_node");
    EXPECT_EQ(bridge_.node_name(), std::string("my_node"));
}

TEST_F(Ros2BridgeTest, InitIdempotentSameName)
{
    EXPECT_TRUE(bridge_.init("idem_node"));
    EXPECT_TRUE(bridge_.init("idem_node"));   // second call is idempotent
    EXPECT_EQ(bridge_.state(), BridgeState::Initialized);
}

TEST_F(Ros2BridgeTest, InitReturnsFalseOnDifferentName)
{
    bridge_.init("name_a");
    EXPECT_FALSE(bridge_.init("name_b"));   // different name → false
    EXPECT_EQ(bridge_.node_name(), std::string("name_a"));   // unchanged
}

// ---------------------------------------------------------------------------
// Suite: StartSpin
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, StartSpinReturnsFalseWhenUninitialized)
{
    EXPECT_FALSE(bridge_.start_spin());
}

TEST_F(Ros2BridgeTest, StartSpinReturnsTrueAfterInit)
{
    bridge_.init("spin_ok_node");
    EXPECT_TRUE(bridge_.start_spin());
}

TEST_F(Ros2BridgeTest, StartSpinTransitionsToSpinning)
{
    bridge_.init("spinning_node");
    bridge_.start_spin();

    // Give the thread a moment to start and set state.
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (bridge_.state() != BridgeState::Spinning &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_EQ(bridge_.state(), BridgeState::Spinning);
    EXPECT_TRUE(bridge_.is_ok());
}

TEST_F(Ros2BridgeTest, StartSpinReturnsFalseWhenAlreadySpinning)
{
    bridge_.init("double_spin_node");
    bridge_.start_spin();

    // Wait until spinning.
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (bridge_.state() != BridgeState::Spinning &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_FALSE(bridge_.start_spin());   // already spinning
}

// ---------------------------------------------------------------------------
// Suite: Shutdown
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, ShutdownFromUninitializedIsNoOp)
{
    EXPECT_NO_THROW(bridge_.shutdown());
    EXPECT_EQ(bridge_.state(), BridgeState::Uninitialized);
}

TEST_F(Ros2BridgeTest, ShutdownFromInitialized)
{
    bridge_.init("shutdown_init_node");
    bridge_.shutdown();
    EXPECT_EQ(bridge_.state(), BridgeState::Stopped);
    EXPECT_EQ(bridge_.node(), nullptr);
    EXPECT_EQ(bridge_.executor(), nullptr);
}

TEST_F(Ros2BridgeTest, ShutdownFromSpinning)
{
    bridge_.init("shutdown_spin_node");
    bridge_.start_spin();

    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (bridge_.state() != BridgeState::Spinning &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(5ms);
    }

    bridge_.shutdown();
    EXPECT_EQ(bridge_.state(), BridgeState::Stopped);
}

TEST_F(Ros2BridgeTest, ShutdownIsIdempotent)
{
    bridge_.init("idempotent_shutdown");
    bridge_.shutdown();
    EXPECT_NO_THROW(bridge_.shutdown());   // second shutdown is no-op
    EXPECT_EQ(bridge_.state(), BridgeState::Stopped);
}

TEST_F(Ros2BridgeTest, NodeNullAfterShutdown)
{
    bridge_.init("null_after_shutdown");
    bridge_.start_spin();
    bridge_.shutdown();
    EXPECT_EQ(bridge_.node(), nullptr);
}

TEST_F(Ros2BridgeTest, ExecutorNullAfterShutdown)
{
    bridge_.init("exec_null_after_shutdown");
    bridge_.shutdown();
    EXPECT_EQ(bridge_.executor(), nullptr);
}

// ---------------------------------------------------------------------------
// Suite: Full init/shutdown cycle
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, FullInitShutdownCycle)
{
    // init → start_spin → wait for Spinning → shutdown → Stopped
    EXPECT_TRUE(bridge_.init("cycle_node"));
    EXPECT_EQ(bridge_.state(), BridgeState::Initialized);

    EXPECT_TRUE(bridge_.start_spin());

    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (bridge_.state() != BridgeState::Spinning &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(bridge_.state(), BridgeState::Spinning);
    EXPECT_TRUE(bridge_.is_ok());
    ASSERT_NE(bridge_.node(), nullptr);
    EXPECT_EQ(bridge_.node()->get_name(), std::string("cycle_node"));

    bridge_.shutdown();
    EXPECT_EQ(bridge_.state(), BridgeState::Stopped);
    EXPECT_FALSE(bridge_.is_ok());
    EXPECT_EQ(bridge_.node(), nullptr);
}

// ---------------------------------------------------------------------------
// Suite: State callback
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, StateCallbackFiredOnInitialized)
{
    std::atomic<int>   count{0};
    BridgeState        last{BridgeState::Uninitialized};
    bridge_.set_state_callback([&](BridgeState s) {
        last = s;
        ++count;
    });

    bridge_.init("cb_init_node");
    EXPECT_GE(count.load(), 1);
    EXPECT_EQ(last, BridgeState::Initialized);
}

TEST_F(Ros2BridgeTest, StateCallbackFiredOnSpinning)
{
    std::atomic<bool>  got_spinning{false};
    bridge_.set_state_callback([&](BridgeState s) {
        if (s == BridgeState::Spinning)
            got_spinning.store(true);
    });

    bridge_.init("cb_spin_node");
    bridge_.start_spin();

    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (!got_spinning.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);

    EXPECT_TRUE(got_spinning.load());
}

TEST_F(Ros2BridgeTest, StateCallbackFiredOnStopped)
{
    std::atomic<bool> got_stopped{false};
    bridge_.set_state_callback([&](BridgeState s) {
        if (s == BridgeState::Stopped)
            got_stopped.store(true);
    });

    bridge_.init("cb_stop_node");
    bridge_.shutdown();
    EXPECT_TRUE(got_stopped.load());
}

// ---------------------------------------------------------------------------
// Suite: Destructor
// ---------------------------------------------------------------------------

TEST_F(Ros2BridgeTest, DestructorShutsDownCleanly)
{
    // Allocate on heap so we control destruction timing.
    {
        Ros2Bridge b;
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);
        b.init("dtor_node");
        b.start_spin();

        const auto deadline = std::chrono::steady_clock::now() + 500ms;
        while (b.state() != BridgeState::Spinning &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
        }
        // b goes out of scope → destructor calls shutdown() → should not crash
    }
    SUCCEED();   // if we get here without hanging, destructor is clean
}

// ---------------------------------------------------------------------------
// main — register the shared RclcppEnvironment
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}

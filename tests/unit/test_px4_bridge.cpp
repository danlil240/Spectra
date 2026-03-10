// Unit tests for Px4Bridge — state machine and channel management.
//
// Tests the bridge lifecycle, TelemetryChannel ring buffer, and state
// transitions.  No real network or MAVLink device needed.

#include <gtest/gtest.h>

#include "px4_bridge.hpp"

#include <string>
#include <vector>

using namespace spectra::adapters::px4;

// ---------------------------------------------------------------------------
// TelemetryChannel tests
// ---------------------------------------------------------------------------

TEST(TelemetryChannelTest, PushAndSnapshot)
{
    TelemetryChannel ch;
    ch.capacity = 5;

    TelemetryMessage msg;
    msg.name         = "ATTITUDE";
    msg.timestamp_us = 1000;
    msg.fields.push_back({"roll", 0.1});

    ch.push(msg);
    EXPECT_EQ(ch.count, 1u);

    auto snap = ch.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].name, "ATTITUDE");
}

TEST(TelemetryChannelTest, RingBufferWrap)
{
    TelemetryChannel ch;
    ch.capacity = 3;

    for (int i = 0; i < 5; ++i)
    {
        TelemetryMessage msg;
        msg.name         = "MSG";
        msg.timestamp_us = static_cast<uint64_t>(i) * 1000;
        msg.fields.push_back({"val", static_cast<double>(i)});
        ch.push(msg);
    }

    EXPECT_EQ(ch.count, 5u);

    auto snap = ch.snapshot();
    ASSERT_EQ(snap.size(), 3u);   // capacity = 3

    // Should contain the last 3 messages (indices 2, 3, 4).
    EXPECT_EQ(snap[0].timestamp_us, 2000u);
    EXPECT_EQ(snap[1].timestamp_us, 3000u);
    EXPECT_EQ(snap[2].timestamp_us, 4000u);
}

TEST(TelemetryChannelTest, Latest)
{
    TelemetryChannel ch;
    ch.capacity = 10;

    TelemetryMessage msg1;
    msg1.name         = "HB";
    msg1.timestamp_us = 100;
    ch.push(msg1);

    TelemetryMessage msg2;
    msg2.name         = "HB";
    msg2.timestamp_us = 200;
    ch.push(msg2);

    auto latest = ch.latest();
    EXPECT_EQ(latest.timestamp_us, 200u);
}

TEST(TelemetryChannelTest, LatestEmpty)
{
    TelemetryChannel ch;
    auto             latest = ch.latest();
    EXPECT_TRUE(latest.name.empty());
}

// ---------------------------------------------------------------------------
// Px4Bridge state machine tests
// ---------------------------------------------------------------------------

TEST(Px4BridgeTest, DefaultState)
{
    Px4Bridge bridge;
    EXPECT_EQ(bridge.state(), BridgeState::Disconnected);
    EXPECT_FALSE(bridge.is_connected());
    EXPECT_FALSE(bridge.is_receiving());
    EXPECT_EQ(bridge.total_messages(), 0u);
}

TEST(Px4BridgeTest, InitSetsEndpoint)
{
    Px4Bridge bridge;
    EXPECT_TRUE(bridge.init("192.168.1.1", 14550));
    EXPECT_EQ(bridge.host(), "192.168.1.1");
    EXPECT_EQ(bridge.port(), 14550);
}

TEST(Px4BridgeTest, ChannelNamesEmpty)
{
    Px4Bridge bridge;
    auto      names = bridge.channel_names();
    EXPECT_TRUE(names.empty());
}

TEST(Px4BridgeTest, ChannelLatestNotFound)
{
    Px4Bridge bridge;
    auto      latest = bridge.channel_latest("nonexistent");
    EXPECT_FALSE(latest.has_value());
}

TEST(Px4BridgeTest, BridgeStateNames)
{
    EXPECT_STREQ(bridge_state_name(BridgeState::Disconnected), "Disconnected");
    EXPECT_STREQ(bridge_state_name(BridgeState::Connecting), "Connecting");
    EXPECT_STREQ(bridge_state_name(BridgeState::Connected), "Connected");
    EXPECT_STREQ(bridge_state_name(BridgeState::Receiving), "Receiving");
    EXPECT_STREQ(bridge_state_name(BridgeState::ShuttingDown), "ShuttingDown");
    EXPECT_STREQ(bridge_state_name(BridgeState::Stopped), "Stopped");
}

TEST(Px4BridgeTest, ShutdownFromDisconnected)
{
    Px4Bridge bridge;
    bridge.shutdown();   // Should be a no-op.
    EXPECT_EQ(bridge.state(), BridgeState::Disconnected);
}

TEST(Px4BridgeTest, MessageRateInitiallyZero)
{
    Px4Bridge bridge;
    EXPECT_DOUBLE_EQ(bridge.message_rate(), 0.0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

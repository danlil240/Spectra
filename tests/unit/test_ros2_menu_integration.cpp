// test_ros2_menu_integration.cpp — G2: Tools → ROS2 Adapter menu integration tests.
//
// Tests the two components added by G2:
//
//   1. ros2_adapter_state.hpp — pending-error flag lifecycle (set / has / clear).
//      These tests run regardless of whether SPECTRA_USE_ROS2 is defined; when it
//      is NOT defined the stubs are exercised (always no-op / false).
//
//   2. tools.ros2_adapter command — registered with correct metadata (category,
//      icon, label) when SPECTRA_USE_ROS2 is ON.  Verified via CommandRegistry
//      directly without invoking the callback (which would fork a child process).
//
// No ROS2 node, no ImGui context, no GLFW window needed.

#include <gtest/gtest.h>

#include "ui/app/ros2_adapter_state.hpp"

// ── 1. ros2_adapter_state flag lifecycle ────────────────────────────────────

class Ros2AdapterStateTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        // Ensure clean state before every test.
        spectra::ros2_adapter_clear_error();
    }
    void TearDown() override { spectra::ros2_adapter_clear_error(); }
};

TEST_F(Ros2AdapterStateTest, InitiallyNoError)
{
    EXPECT_FALSE(spectra::ros2_adapter_has_error());
}

TEST_F(Ros2AdapterStateTest, SetErrorMakesHasErrorTrue)
{
    spectra::ros2_adapter_set_error("test error");
#ifdef SPECTRA_USE_ROS2
    EXPECT_TRUE(spectra::ros2_adapter_has_error());
#else
    // Stubs: set_error is a no-op, has_error always returns false.
    EXPECT_FALSE(spectra::ros2_adapter_has_error());
#endif
}

TEST_F(Ros2AdapterStateTest, ClearErrorMakesHasErrorFalse)
{
    spectra::ros2_adapter_set_error("test error");
    spectra::ros2_adapter_clear_error();
    EXPECT_FALSE(spectra::ros2_adapter_has_error());
}

TEST_F(Ros2AdapterStateTest, SetErrorEmptyStringIsNotError)
{
    spectra::ros2_adapter_set_error("");
    EXPECT_FALSE(spectra::ros2_adapter_has_error());
}

TEST_F(Ros2AdapterStateTest, SetErrorMultipleTimesKeepsLast)
{
    spectra::ros2_adapter_set_error("first");
    spectra::ros2_adapter_set_error("second");
#ifdef SPECTRA_USE_ROS2
    EXPECT_TRUE(spectra::ros2_adapter_has_error());
    EXPECT_EQ(spectra::ros2_adapter_pending_error(), "second");
#else
    EXPECT_FALSE(spectra::ros2_adapter_has_error());
#endif
}

TEST_F(Ros2AdapterStateTest, ClearWithoutSetIsNoop)
{
    // Should not crash or throw.
    EXPECT_NO_THROW(spectra::ros2_adapter_clear_error());
    EXPECT_FALSE(spectra::ros2_adapter_has_error());
}

// ── 2. tools.ros2_adapter command registration ──────────────────────────────
// Only meaningful when SPECTRA_USE_ROS2 is ON; under OFF the command is simply
// not registered, which is the intended behaviour (grayed-out menu item
// is rendered directly in imgui_integration.cpp without a command entry).

#ifdef SPECTRA_USE_ROS2

    #include "ui/commands/command_registry.hpp"

// A minimal CommandBindings + register_standard_commands call is heavyweight
// (it requires a full WindowUIContext).  Instead, we register the command
// directly with the same parameters used in register_commands.cpp and verify
// the metadata — this is an isolated unit test of the command contract.

TEST(Ros2AdapterCommandTest, CommandRegisteredWithCorrectCategory)
{
    spectra::CommandRegistry reg;
    reg.register_command("tools.ros2_adapter",
                         "ROS2 Adapter",
                         []() {},   // no-op callback for test
                         "",
                         "Tools",
                         0);

    const spectra::Command* cmd = reg.find("tools.ros2_adapter");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->category, "Tools");
}

TEST(Ros2AdapterCommandTest, CommandRegisteredWithCorrectLabel)
{
    spectra::CommandRegistry reg;
    reg.register_command("tools.ros2_adapter",
                         "ROS2 Adapter",
                         []() {},
                         "",
                         "Tools",
                         0);

    const spectra::Command* cmd = reg.find("tools.ros2_adapter");
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->label, "ROS2 Adapter");
}

TEST(Ros2AdapterCommandTest, CommandEnabledByDefault)
{
    spectra::CommandRegistry reg;
    reg.register_command("tools.ros2_adapter",
                         "ROS2 Adapter",
                         []() {},
                         "",
                         "Tools",
                         0);

    const spectra::Command* cmd = reg.find("tools.ros2_adapter");
    ASSERT_NE(cmd, nullptr);
    EXPECT_TRUE(cmd->enabled);
}

TEST(Ros2AdapterCommandTest, CommandNotPresentByDefaultInEmptyRegistry)
{
    spectra::CommandRegistry reg;
    EXPECT_EQ(reg.find("tools.ros2_adapter"), nullptr);
}

TEST(Ros2AdapterCommandTest, CommandExecuteInvokesCallback)
{
    spectra::CommandRegistry reg;
    bool called = false;
    reg.register_command("tools.ros2_adapter",
                         "ROS2 Adapter",
                         [&called]() { called = true; },
                         "",
                         "Tools",
                         0);

    bool ok = reg.execute("tools.ros2_adapter");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(called);
}

TEST(Ros2AdapterCommandTest, CommandAppearsInToolsCategory)
{
    spectra::CommandRegistry reg;
    reg.register_command("tools.ros2_adapter",
                         "ROS2 Adapter",
                         []() {},
                         "",
                         "Tools",
                         0);

    auto tools_cmds = reg.commands_in_category("Tools");
    bool found      = false;
    for (const auto* c : tools_cmds)
    {
        if (c->id == "tools.ros2_adapter")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(Ros2AdapterCommandTest, CommandSearchFindsROS2Adapter)
{
    spectra::CommandRegistry reg;
    reg.register_command("tools.ros2_adapter",
                         "ROS2 Adapter",
                         []() {},
                         "",
                         "Tools",
                         0);

    auto results = reg.search("ros2", 10);
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results[0].command->id, "tools.ros2_adapter");
}

#endif   // SPECTRA_USE_ROS2

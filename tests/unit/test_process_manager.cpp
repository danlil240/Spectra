#include <gtest/gtest.h>

#include "daemon/process_manager.hpp"

using namespace spectra::daemon;
using namespace spectra::ipc;

TEST(ProcessManager, DefaultState)
{
    ProcessManager pm;
    EXPECT_EQ(pm.process_count(), 0u);
    EXPECT_TRUE(pm.all_processes().empty());
    EXPECT_TRUE(pm.agent_path().empty());
}

TEST(ProcessManager, SetAgentPath)
{
    ProcessManager pm;
    pm.set_agent_path("/usr/bin/spectra-window");
    EXPECT_EQ(pm.agent_path(), "/usr/bin/spectra-window");
}

TEST(ProcessManager, SpawnFailsWithoutPaths)
{
    ProcessManager pm;
    // No agent_path or socket_path set
    pid_t pid = pm.spawn_agent();
    EXPECT_EQ(pid, -1);
    EXPECT_EQ(pm.process_count(), 0u);
}

TEST(ProcessManager, SpawnFailsWithBadPath)
{
    ProcessManager pm;
    pm.set_agent_path("/nonexistent/spectra-window-fake");
    pm.set_socket_path("/tmp/test.sock");
    pid_t pid = pm.spawn_agent();
    EXPECT_EQ(pid, -1);
    EXPECT_EQ(pm.process_count(), 0u);
}

TEST(ProcessManager, SetWindowId)
{
    ProcessManager pm;
    // Can't set window ID for non-existent process — should not crash
    pm.set_window_id(12345, 42);
    EXPECT_EQ(pm.pid_for_window(42), 0);
}

TEST(ProcessManager, PidForWindowNotFound)
{
    ProcessManager pm;
    EXPECT_EQ(pm.pid_for_window(999), 0);
}

TEST(ProcessManager, RemoveNonexistentProcess)
{
    ProcessManager pm;
    pm.remove_process(12345);   // Should not crash
    EXPECT_EQ(pm.process_count(), 0u);
}

TEST(ProcessManager, ReapFinishedEmpty)
{
    ProcessManager pm;
    auto           reaped = pm.reap_finished();
    EXPECT_TRUE(reaped.empty());
}

#ifdef __linux__

TEST(ProcessManager, SpawnRealProcess)
{
    // Spawn /bin/true — it exits immediately with code 0
    ProcessManager pm;
    pm.set_agent_path("/bin/true");
    pm.set_socket_path("/tmp/test-dummy.sock");

    pid_t pid = pm.spawn_agent();
    EXPECT_GT(pid, 0);
    EXPECT_EQ(pm.process_count(), 1u);

    // Wait for it to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto reaped = pm.reap_finished();
    EXPECT_EQ(reaped.size(), 1u);
    if (!reaped.empty())
    {
        EXPECT_EQ(reaped[0], pid);
    }
    EXPECT_EQ(pm.process_count(), 0u);
}

TEST(ProcessManager, SpawnForWindow)
{
    ProcessManager pm;
    pm.set_agent_path("/bin/true");
    pm.set_socket_path("/tmp/test-dummy.sock");

    pid_t pid = pm.spawn_agent_for_window(42);
    EXPECT_GT(pid, 0);
    EXPECT_EQ(pm.pid_for_window(42), pid);

    // Wait and reap
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pm.reap_finished();
}

TEST(ProcessManager, AllProcesses)
{
    ProcessManager pm;
    pm.set_agent_path("/bin/true");
    pm.set_socket_path("/tmp/test-dummy.sock");

    pm.spawn_agent();
    pm.spawn_agent();
    EXPECT_EQ(pm.process_count(), 2u);

    auto procs = pm.all_processes();
    EXPECT_EQ(procs.size(), 2u);

    // Wait and reap
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pm.reap_finished();
}

#endif   // __linux__

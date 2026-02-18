#pragma once

#include "../ipc/message.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace spectra::daemon
{

// Tracks a spawned agent process.
struct ProcessEntry
{
    pid_t pid = 0;
    ipc::WindowId window_id = ipc::INVALID_WINDOW;
    std::string socket_path;
    bool alive = true;
};

// Manages spawning and tracking of window-agent processes.
// Thread-safe — all public methods lock the internal mutex.
class ProcessManager
{
   public:
    ProcessManager() = default;

    // Set the path to the agent binary.
    void set_agent_path(const std::string& path) { agent_path_ = path; }
    const std::string& agent_path() const { return agent_path_; }

    // Set the socket path that agents should connect to.
    void set_socket_path(const std::string& path) { socket_path_ = path; }

    // Spawn a new agent process. Returns the PID on success, -1 on failure.
    // The agent will be launched with: spectra-window --socket <socket_path>
    pid_t spawn_agent();

    // Spawn an agent and associate it with a window ID (for tracking).
    pid_t spawn_agent_for_window(ipc::WindowId wid);

    // Check if a PID is still alive (via waitpid WNOHANG).
    bool is_alive(pid_t pid) const;

    // Reap any finished child processes. Returns PIDs of reaped processes.
    std::vector<pid_t> reap_finished();

    // Get the number of tracked processes.
    size_t process_count() const;

    // Get all tracked process entries.
    std::vector<ProcessEntry> all_processes() const;

    // Remove a process entry by PID.
    void remove_process(pid_t pid);

    // Associate a window ID with a PID (after handshake).
    void set_window_id(pid_t pid, ipc::WindowId wid);

    // Find PID by window ID. Returns 0 if not found.
    pid_t pid_for_window(ipc::WindowId wid) const;

   private:
    mutable std::mutex mu_;
    std::string agent_path_;
    std::string socket_path_;
    std::unordered_map<pid_t, ProcessEntry> processes_;
};

}  // namespace spectra::daemon

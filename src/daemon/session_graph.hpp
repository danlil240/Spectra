#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ipc/message.hpp"

namespace spectra::daemon
{

// Tracks a connected window-agent process.
struct AgentEntry
{
    ipc::WindowId window_id = ipc::INVALID_WINDOW;
    ipc::ProcessId process_id = 0;
    int connection_fd = -1;
    std::vector<uint64_t> assigned_figures;
    std::chrono::steady_clock::time_point last_heartbeat;
    bool alive = true;
};

// Tracks a figure in the session.
struct FigureEntry
{
    uint64_t figure_id = 0;
    ipc::WindowId assigned_window = ipc::INVALID_WINDOW;
    std::string title;
};

// Session graph: the backend daemon's model of the world.
// Thread-safe — all public methods lock the internal mutex.
class SessionGraph
{
   public:
    SessionGraph() = default;

    // --- Agent management ---

    // Register a new agent. Returns the assigned WindowId.
    ipc::WindowId add_agent(ipc::ProcessId pid, int connection_fd);

    // Remove an agent by window ID. Returns its assigned figures.
    std::vector<uint64_t> remove_agent(ipc::WindowId wid);

    // Update heartbeat timestamp for an agent.
    void heartbeat(ipc::WindowId wid);

    // Returns window IDs of agents whose heartbeat is older than `timeout`.
    std::vector<ipc::WindowId> stale_agents(std::chrono::milliseconds timeout) const;

    // --- Figure management ---

    // Add a figure to the session. Returns the figure ID.
    uint64_t add_figure(const std::string& title = "");

    // Assign a figure to a window agent.
    bool assign_figure(uint64_t figure_id, ipc::WindowId wid);

    // Remove a figure from the session.
    void remove_figure(uint64_t figure_id);

    // Get all figure IDs assigned to a window.
    std::vector<uint64_t> figures_for_window(ipc::WindowId wid) const;

    // --- Queries ---

    // Returns the number of connected agents.
    size_t agent_count() const;

    // Returns the number of figures.
    size_t figure_count() const;

    // Returns true if no agents and no pending work.
    bool is_empty() const;

    // Get agent entry (for logging/debugging). Returns nullptr if not found.
    const AgentEntry* agent(ipc::WindowId wid) const;

    // Get all window IDs.
    std::vector<ipc::WindowId> all_window_ids() const;

    // Get the session ID.
    ipc::SessionId session_id() const { return session_id_; }

   private:
    mutable std::mutex mu_;
    ipc::SessionId session_id_ = 1;  // single session for now
    ipc::WindowId next_window_id_ = 1;
    uint64_t next_figure_id_ = 1;
    std::unordered_map<ipc::WindowId, AgentEntry> agents_;
    std::unordered_map<uint64_t, FigureEntry> figures_;
};

}  // namespace spectra::daemon

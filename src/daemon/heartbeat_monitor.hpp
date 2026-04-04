#pragma once

#include <chrono>

#include "daemon_server.hpp"

namespace spectra::daemon
{

// Periodically checks for stale agents and reaps zombie child processes.
class HeartbeatMonitor
{
   public:
    explicit HeartbeatMonitor(
        std::chrono::milliseconds agent_timeout = std::chrono::milliseconds(15000));

    // Run a single tick: check stale agents and reap processes.
    // Call this from the main event loop on each iteration.
    void tick(DaemonContext& ctx);

   private:
    std::chrono::milliseconds                  agent_timeout_;
    std::chrono::steady_clock::time_point      last_check_;
    static constexpr std::chrono::milliseconds CHECK_INTERVAL{5000};
};

}   // namespace spectra::daemon

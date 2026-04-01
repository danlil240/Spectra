#include "heartbeat_monitor.hpp"

#include <iostream>

namespace spectra::daemon
{

HeartbeatMonitor::HeartbeatMonitor(std::chrono::milliseconds agent_timeout)
    : agent_timeout_(agent_timeout)
    , last_check_(std::chrono::steady_clock::now())
{
}

void HeartbeatMonitor::tick(DaemonContext& ctx)
{
    auto now = std::chrono::steady_clock::now();
    if (now - last_check_ < CHECK_INTERVAL)
        return;
    last_check_ = now;

    // Check for stale agents
    auto stale = ctx.graph.stale_agents(agent_timeout_);
    for (auto wid : stale)
    {
        std::cerr << "[spectra-backend] Stale agent detected: window=" << wid << "\n";

        auto orphaned = ctx.graph.remove_agent(wid);

        // Close the stale agent's connection
        for (auto& c : ctx.clients)
        {
            if (c.window_id == wid && c.conn)
            {
                send_close_window(*c.conn, wid, ctx.graph.session_id(), "heartbeat_timeout");
                c.conn->close();
                break;
            }
        }

        // Notify Python clients about closed figures
        for (auto fid : orphaned)
            notify_python_window_closed(ctx, fid, wid, "heartbeat_timeout");

        // Redistribute orphaned figures
        redistribute_orphaned_figures(ctx, orphaned);
    }

    // Remove closed connections
    auto& clients = ctx.clients;
    clients.erase(std::remove_if(clients.begin(),
                                  clients.end(),
                                  [](const ClientSlot& c) {
                                      return !c.conn || !c.conn->is_open();
                                  }),
                  clients.end());

    // Reap zombie child processes
    auto reaped = ctx.proc_mgr.reap_finished();
    for (auto pid : reaped)
    {
        std::cerr << "[spectra-backend] Reaped child process pid=" << pid << "\n";
        ctx.proc_mgr.remove_process(pid);
    }

    // Check if session is empty and no Python clients connected
    if (ctx.graph.is_empty())
    {
        bool has_python = false;
        for (const auto& c : ctx.clients)
        {
            if (c.conn && c.conn->is_open() && c.client_type == ClientType::PYTHON)
            {
                has_python = true;
                break;
            }
        }
        if (!has_python)
        {
            std::cerr << "[spectra-backend] No agents, no Python clients — shutting down\n";
            ctx.running.store(false, std::memory_order_relaxed);
        }
    }
}

}   // namespace spectra::daemon

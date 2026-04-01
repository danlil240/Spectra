#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if __has_include(<spectra/version.hpp>)
    #include <spectra/version.hpp>
#endif

#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"
#include "agent_message_handler.hpp"
#include "daemon_server.hpp"
#include "heartbeat_monitor.hpp"
#include "python_message_handler.hpp"

#ifdef _WIN32
    #include <process.h>
    #define getpid _getpid
#else
    #include <cerrno>
    #include <poll.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace
{

std::atomic<bool> g_running{true};

void signal_handler(int /*sig*/)
{
    g_running.store(false, std::memory_order_relaxed);
}

#ifndef _WIN32
// Resolve the path to the spectra-window agent binary.
// Looks next to the backend binary first, then falls back to PATH.
std::string resolve_agent_path(const char* argv0)
{
    // Try sibling path: same directory as this binary
    std::string self(argv0);
    auto        slash = self.rfind('/');
    if (slash != std::string::npos)
    {
        std::string dir       = self.substr(0, slash + 1);
        std::string candidate = dir + "spectra-window";
        if (::access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    // Fallback: assume it's on PATH
    return "spectra-window";
}
#endif

}   // namespace

int main(int argc, char* argv[])
{
    using namespace spectra::daemon;
    using spectra::ipc::MessageType;

    // Handle --version and --help before anything else
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0)
        {
#ifdef SPECTRA_VERSION_STRING
            std::cout << "spectra-backend " << SPECTRA_VERSION_STRING << "\n";
#else
            std::cout << "spectra-backend (version unknown)\n";
#endif
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            std::cout << "Usage: spectra-backend [OPTIONS]\n"
                      << "\n"
                      << "Options:\n"
                      << "  --socket <path>  Unix socket path to listen on\n"
                      << "  --version, -v    Print version and exit\n"
                      << "  --help, -h       Show this help\n";
            return 0;
        }
    }

#ifdef _WIN32
    std::cerr << "[spectra-backend] Unix domain socket daemon not supported on Windows\n";
    return 1;
#else
    // Parse optional --socket <path> argument
    std::string socket_path;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (std::string(argv[i]) == "--socket")
        {
            socket_path = argv[i + 1];
            break;
        }
    }
    if (socket_path.empty())
        socket_path = spectra::ipc::default_socket_path();

    std::string agent_path = resolve_agent_path(argv[0]);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    std::cerr << "[spectra-backend] Starting daemon, socket: " << socket_path << "\n";
    std::cerr << "[spectra-backend] Agent binary: " << agent_path << "\n";

    // --- Start UDS listener ---
    spectra::ipc::Server server;
    if (!server.listen(socket_path))
    {
        std::cerr << "[spectra-backend] Failed to listen on " << socket_path << "\n";
        return 1;
    }

    SessionGraph   graph;
    FigureModel    fig_model;
    ProcessManager proc_mgr;
    proc_mgr.set_agent_path(agent_path);
    proc_mgr.set_socket_path(socket_path);

    std::vector<ClientSlot> clients;
    HeartbeatMonitor        heartbeat;

    DaemonContext ctx{graph, fig_model, proc_mgr, clients, g_running};

    bool had_agents = false;

    std::cerr << "[spectra-backend] Listening for connections...\n";

    // --- Poll-based multiplexed event loop ---
    // poll() watches the listen fd + all client fds simultaneously so we never
    // block waiting for a new connection while existing clients have data.
    while (g_running.load(std::memory_order_relaxed))
    {
        // Build pollfd array: [0] = listen socket, [1..N] = client sockets
        std::vector<struct pollfd> pfds;
        pfds.reserve(1 + clients.size());
        pfds.push_back({server.listen_fd(), POLLIN, 0});
        for (auto& c : clients)
            pfds.push_back({c.conn ? c.conn->fd() : -1, POLLIN, 0});

        int poll_ret = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 1);

        if (poll_ret < 0)
        {
            if (errno == EINTR)
                continue;
            std::cerr << "[spectra-backend] poll() error\n";
            break;
        }

        // Accept new connections
        if (pfds[0].revents & POLLIN)
        {
            auto new_conn = server.try_accept();
            if (new_conn)
            {
                std::cerr << "[spectra-backend] New connection (fd=" << new_conn->fd() << ")\n";
                ClientSlot slot;
                slot.conn = std::move(new_conn);
                clients.push_back(std::move(slot));
            }
        }

        // Process messages from all connected clients
        for (auto it = clients.begin(); it != clients.end();)
        {
            if (!it->conn || !it->conn->is_open())
            {
                // Connection closed — remove agent
                if (it->window_id != spectra::ipc::INVALID_WINDOW)
                {
                    auto orphaned = graph.remove_agent(it->window_id);
                    std::cerr << "[spectra-backend] Agent disconnected (window=" << it->window_id
                              << ", orphaned_figures=" << orphaned.size() << ")\n";
                    for (auto fid : orphaned)
                        notify_python_window_closed(ctx, fid, it->window_id, "agent_disconnected");
                }
                it = clients.erase(it);
                continue;
            }

            // Only recv() when poll() says data is ready (index offset by 1 for listen fd)
            size_t pfd_idx  = 1 + static_cast<size_t>(it - clients.begin());
            bool   has_data = (pfd_idx < pfds.size()) && (pfds[pfd_idx].revents & POLLIN);
            if (!has_data)
            {
                ++it;
                continue;
            }

            auto msg_opt = it->conn->recv();
            if (!msg_opt)
            {
                // Connection closed or error
                if (it->is_source_client)
                {
                    std::cerr << "[spectra-backend] Source app disconnected — shutting down\n";
                    for (auto& entry : proc_mgr.all_processes())
                        ::kill(entry.pid, SIGTERM);
                    g_running.store(false, std::memory_order_relaxed);
                    it = clients.erase(it);
                    break;
                }
                if (it->window_id != spectra::ipc::INVALID_WINDOW)
                {
                    auto orphaned = graph.remove_agent(it->window_id);
                    std::cerr << "[spectra-backend] Agent lost (window=" << it->window_id
                              << ", orphaned_figures=" << orphaned.size() << ")\n";
                    for (auto fid : orphaned)
                        notify_python_window_closed(ctx, fid, it->window_id, "agent_lost");
                }
                it = clients.erase(it);
                continue;
            }

            auto&        msg    = *msg_opt;
            HandleResult result = HandleResult::Continue;

            switch (msg.header.type)
            {
                // ─── Agent / app messages ───────────────────────────────────
                case MessageType::HELLO:
                    result = handle_hello(ctx, *it, msg);
                    break;
                case MessageType::EVT_HEARTBEAT:
                    result = handle_heartbeat(ctx, *it, msg);
                    break;
                case MessageType::REQ_CREATE_WINDOW:
                    result = handle_req_create_window(ctx, *it, msg);
                    break;
                case MessageType::REQ_CLOSE_WINDOW:
                    result = handle_req_close_window(ctx, *it, msg);
                    break;
                case MessageType::REQ_DETACH_FIGURE:
                    result = handle_req_detach_figure(ctx, *it, msg);
                    break;
                case MessageType::EVT_WINDOW:
                    result = handle_evt_window(ctx, *it, msg);
                    break;
                case MessageType::EVT_INPUT:
                    result = handle_evt_input(ctx, *it, msg);
                    break;
                case MessageType::STATE_SNAPSHOT:
                    result = handle_state_snapshot(ctx, *it, msg);
                    break;
                case MessageType::STATE_DIFF:
                    result = handle_state_diff(ctx, *it, msg);
                    break;
                case MessageType::ACK_STATE:
                    break;

                // ─── Python request messages ────────────────────────────────
                case MessageType::REQ_CREATE_FIGURE:
                    result = handle_req_create_figure(ctx, *it, msg);
                    break;
                case MessageType::REQ_CREATE_AXES:
                    result = handle_req_create_axes(ctx, *it, msg);
                    break;
                case MessageType::REQ_ADD_SERIES:
                    result = handle_req_add_series(ctx, *it, msg);
                    break;
                case MessageType::REQ_SET_DATA:
                    result = handle_req_set_data(ctx, *it, msg);
                    break;
                case MessageType::REQ_UPDATE_PROPERTY:
                    result = handle_req_update_property(ctx, *it, msg);
                    break;
                case MessageType::REQ_SHOW:
                    result = handle_req_show(ctx, *it, msg);
                    break;
                case MessageType::REQ_APPEND_DATA:
                    result = handle_req_append_data(ctx, *it, msg);
                    break;
                case MessageType::REQ_REMOVE_SERIES:
                    result = handle_req_remove_series(ctx, *it, msg);
                    break;
                case MessageType::REQ_CLOSE_FIGURE:
                    result = handle_req_close_figure(ctx, *it, msg);
                    break;
                case MessageType::REQ_UPDATE_BATCH:
                    result = handle_req_update_batch(ctx, *it, msg);
                    break;
                case MessageType::REQ_DESTROY_FIGURE:
                    result = handle_req_destroy_figure(ctx, *it, msg);
                    break;
                case MessageType::REQ_LIST_FIGURES:
                    result = handle_req_list_figures(ctx, *it, msg);
                    break;
                case MessageType::REQ_RECONNECT:
                    result = handle_req_reconnect(ctx, *it, msg);
                    break;
                case MessageType::REQ_DISCONNECT:
                    result = handle_req_disconnect(ctx, *it, msg);
                    break;
                case MessageType::REQ_GET_SNAPSHOT:
                    result = handle_req_get_snapshot(ctx, *it, msg);
                    break;

                default:
                    std::cerr << "[spectra-backend] Unknown message type 0x" << std::hex
                              << static_cast<uint16_t>(msg.header.type) << std::dec
                              << " from window=" << it->window_id << "\n";
                    break;
            }

            // Handle iterator advancement based on handler result
            switch (result)
            {
                case HandleResult::EraseAndContinue:
                    it = clients.erase(it);
                    break;
                case HandleResult::Shutdown:
                    g_running.store(false, std::memory_order_relaxed);
                    ++it;
                    break;
                case HandleResult::Continue:
                default:
                    ++it;
                    break;
            }
        }

        // --- Heartbeat monitor: stale agents + process reaping ---
        heartbeat.tick(ctx);

        // --- Shutdown rule: exit when no agents remain (after at least one connected) ---
        if (!graph.is_empty())
        {
            had_agents = true;
        }
        else if (had_agents)
        {
            std::cerr << "[spectra-backend] All agents disconnected, shutting down\n";
            g_running.store(false, std::memory_order_relaxed);
        }
    }

    // Cleanup
    for (auto& c : clients)
    {
        if (c.conn)
            c.conn->close();
    }
    server.close();

    std::cerr << "[spectra-backend] Daemon stopped\n";
    return 0;
#endif   // !_WIN32
}

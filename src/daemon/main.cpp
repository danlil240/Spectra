#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if __has_include(<spectra/version.hpp>)
    #include <spectra/version.hpp>
#endif

#include "../ipc/codec.hpp"
#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"
#include "client_router.hpp"
#include "figure_model.hpp"
#include "process_manager.hpp"
#include "session_graph.hpp"

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

// Helper: send CMD_ASSIGN_FIGURES to a specific client.
bool send_assign_figures(spectra::ipc::Connection&    conn,
                         spectra::ipc::WindowId       wid,
                         spectra::ipc::SessionId      sid,
                         const std::vector<uint64_t>& figure_ids,
                         uint64_t                     active_figure_id)
{
    spectra::ipc::CmdAssignFiguresPayload payload;
    payload.window_id        = wid;
    payload.figure_ids       = figure_ids;
    payload.active_figure_id = active_figure_id;

    spectra::ipc::Message msg;
    msg.header.type        = spectra::ipc::MessageType::CMD_ASSIGN_FIGURES;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = spectra::ipc::encode_cmd_assign_figures(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send STATE_SNAPSHOT to a specific client.
bool send_state_snapshot(spectra::ipc::Connection&                 conn,
                         spectra::ipc::WindowId                    wid,
                         spectra::ipc::SessionId                   sid,
                         const spectra::ipc::StateSnapshotPayload& snap)
{
    spectra::ipc::Message msg;
    msg.header.type        = spectra::ipc::MessageType::STATE_SNAPSHOT;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = spectra::ipc::encode_state_snapshot(snap);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send STATE_DIFF to a specific client.
bool send_state_diff(spectra::ipc::Connection&             conn,
                     spectra::ipc::WindowId                wid,
                     spectra::ipc::SessionId               sid,
                     const spectra::ipc::StateDiffPayload& diff)
{
    spectra::ipc::Message msg;
    msg.header.type        = spectra::ipc::MessageType::STATE_DIFF;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = spectra::ipc::encode_state_diff(diff);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send CMD_CLOSE_WINDOW to a specific client.
bool send_close_window(spectra::ipc::Connection& conn,
                       spectra::ipc::WindowId    wid,
                       spectra::ipc::SessionId   sid,
                       const std::string&        reason)
{
    spectra::ipc::CmdCloseWindowPayload payload;
    payload.window_id = wid;
    payload.reason    = reason;

    spectra::ipc::Message msg;
    msg.header.type        = spectra::ipc::MessageType::CMD_CLOSE_WINDOW;
    msg.header.session_id  = sid;
    msg.header.window_id   = wid;
    msg.payload            = spectra::ipc::encode_cmd_close_window(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send a typed response to a Python client.
bool send_python_response(spectra::ipc::Connection&   conn,
                          spectra::ipc::MessageType   type,
                          spectra::ipc::SessionId     sid,
                          spectra::ipc::RequestId     req_id,
                          const std::vector<uint8_t>& payload)
{
    spectra::ipc::Message msg;
    msg.header.type        = type;
    msg.header.session_id  = sid;
    msg.header.request_id  = req_id;
    msg.payload            = payload;
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send RESP_ERR to a client.
bool send_resp_err(spectra::ipc::Connection& conn,
                   spectra::ipc::SessionId   sid,
                   spectra::ipc::RequestId   req_id,
                   uint32_t                  code,
                   const std::string&        message)
{
    spectra::ipc::Message msg;
    msg.header.type        = spectra::ipc::MessageType::RESP_ERR;
    msg.header.session_id  = sid;
    msg.header.request_id  = req_id;
    msg.payload            = spectra::ipc::encode_resp_err({req_id, code, message});
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

}   // namespace

int main(int argc, char* argv[])
{
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

    std::cerr << "[spectra-backend] Starting daemon, socket: " << socket_path << "\n";
    std::cerr << "[spectra-backend] Agent binary: " << agent_path << "\n";

    // --- Start UDS listener ---
    spectra::ipc::Server server;
    if (!server.listen(socket_path))
    {
        std::cerr << "[spectra-backend] Failed to listen on " << socket_path << "\n";
        return 1;
    }

    spectra::daemon::SessionGraph graph;
    spectra::daemon::FigureModel fig_model;
    spectra::daemon::ProcessManager proc_mgr;
    proc_mgr.set_agent_path(agent_path);
    proc_mgr.set_socket_path(socket_path);

    std::cerr << "[spectra-backend] Waiting for app to push figures via STATE_SNAPSHOT...\n";

    // Heartbeat timeout: 3× the heartbeat interval (default 5s → 15s)
    static constexpr auto HEARTBEAT_TIMEOUT = std::chrono::milliseconds(15000);
    // How often to check for stale agents
    static constexpr auto STALE_CHECK_INTERVAL = std::chrono::milliseconds(5000);
    // How often to reap finished child processes
    static constexpr auto REAP_INTERVAL = std::chrono::milliseconds(2000);
    auto last_stale_check = std::chrono::steady_clock::now();
    auto last_reap_check = std::chrono::steady_clock::now();

    // Track active connections (non-owning — Connection objects stored here)
    struct ClientSlot
    {
        std::unique_ptr<spectra::ipc::Connection> conn;
        spectra::ipc::WindowId window_id = spectra::ipc::INVALID_WINDOW;
        bool handshake_done = false;
        bool is_source_client = false;   // true = app pushing figures (not a render agent)
        spectra::daemon::ClientType client_type = spectra::daemon::ClientType::UNKNOWN;
    };
    std::vector<ClientSlot> clients;

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
                clients.push_back({std::move(new_conn), spectra::ipc::INVALID_WINDOW, false});
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
                }
                it = clients.erase(it);
                continue;
            }

            // Only recv() when poll() says data is ready (index offset by 1 for listen fd)
            size_t pfd_idx = 1 + static_cast<size_t>(it - clients.begin());
            bool has_data = (pfd_idx < pfds.size()) && (pfds[pfd_idx].revents & POLLIN);
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
                    // Kill all agent processes
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
                }
                it = clients.erase(it);
                continue;
            }

            auto& msg = *msg_opt;

            switch (msg.header.type)
            {
                case spectra::ipc::MessageType::HELLO:
                {
                    auto hello = spectra::ipc::decode_hello(msg.payload);
                    spectra::daemon::ClientType ctype = spectra::daemon::ClientType::AGENT;
                    if (hello)
                    {
                        ctype = spectra::daemon::classify_client(*hello);
                        std::cerr << "[spectra-backend] HELLO from "
                                  << (ctype == spectra::daemon::ClientType::PYTHON ? "python"
                                      : ctype == spectra::daemon::ClientType::APP ? "app"
                                                                                  : "agent")
                                  << " (build=" << hello->agent_build
                                  << ", client_type=" << hello->client_type << ")\n";
                    }

                    it->client_type = ctype;
                    if (ctype == spectra::daemon::ClientType::APP)
                        it->is_source_client = true;

                    // Python clients and app clients are NOT render agents —
                    // don't add them to the session graph.
                    spectra::ipc::WindowId wid = spectra::ipc::INVALID_WINDOW;
                    if (ctype == spectra::daemon::ClientType::AGENT)
                    {
                        // Try to claim a pre-registered agent slot (created by
                        // STATE_SNAPSHOT or REQ_DETACH_FIGURE handlers).
                        // If none available, register as a brand-new agent.
                        wid = graph.claim_pending_agent(it->conn->fd());
                        if (wid == spectra::ipc::INVALID_WINDOW)
                            wid = graph.add_agent(0, it->conn->fd());
                    }
                    it->window_id = wid;
                    it->handshake_done = true;

                    // Send WELCOME
                    spectra::ipc::WelcomePayload wp;
                    wp.session_id = graph.session_id();
                    wp.window_id = wid;
                    wp.process_id = static_cast<spectra::ipc::ProcessId>(::getpid());
                    wp.heartbeat_ms = 5000;
                    wp.mode = "multiproc";

                    spectra::ipc::Message reply;
                    reply.header.type = spectra::ipc::MessageType::WELCOME;
                    reply.header.session_id = wp.session_id;
                    reply.header.window_id = wid;
                    reply.payload = spectra::ipc::encode_welcome(wp);
                    reply.header.payload_len = static_cast<uint32_t>(reply.payload.size());
                    it->conn->send(reply);

                    // For agents: send figure assignments and state snapshot
                    if (ctype == spectra::daemon::ClientType::AGENT)
                    {
                        auto assigned = graph.figures_for_window(wid);
                        if (!assigned.empty())
                        {
                            send_assign_figures(*it->conn,
                                                wid,
                                                graph.session_id(),
                                                assigned,
                                                assigned[0]);
                        }

                        auto snap = fig_model.snapshot(assigned);
                        send_state_snapshot(*it->conn, wid, graph.session_id(), snap);

                        std::cerr << "[spectra-backend] Assigned window_id=" << wid << " with "
                                  << assigned.size() << " figures\n";
                    }
                    break;
                }

                case spectra::ipc::MessageType::EVT_HEARTBEAT:
                {
                    if (it->window_id != spectra::ipc::INVALID_WINDOW)
                        graph.heartbeat(it->window_id);
                    break;
                }

                case spectra::ipc::MessageType::REQ_CREATE_WINDOW:
                {
                    std::cerr << "[spectra-backend] REQ_CREATE_WINDOW from window=" << it->window_id
                              << "\n";

                    // Spawn a new agent process
                    pid_t pid = proc_mgr.spawn_agent();
                    if (pid > 0)
                    {
                        std::cerr << "[spectra-backend] Spawned new agent pid=" << pid << "\n";
                        // Send RESP_OK to the requesting agent
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.window_id = it->window_id;
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    else
                    {
                        std::cerr << "[spectra-backend] Failed to spawn agent\n";
                        spectra::ipc::Message err_msg;
                        err_msg.header.type = spectra::ipc::MessageType::RESP_ERR;
                        err_msg.header.session_id = graph.session_id();
                        err_msg.header.window_id = it->window_id;
                        err_msg.header.request_id = msg.header.request_id;
                        err_msg.payload = spectra::ipc::encode_resp_err(
                            {msg.header.request_id, 500, "Failed to spawn agent"});
                        err_msg.header.payload_len = static_cast<uint32_t>(err_msg.payload.size());
                        it->conn->send(err_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_CLOSE_WINDOW:
                {
                    auto close_req = spectra::ipc::decode_req_close_window(msg.payload);
                    spectra::ipc::WindowId target_wid = it->window_id;
                    if (close_req && close_req->window_id != spectra::ipc::INVALID_WINDOW)
                        target_wid = close_req->window_id;

                    std::cerr << "[spectra-backend] REQ_CLOSE_WINDOW window=" << target_wid
                              << " reason=" << (close_req ? close_req->reason : "unknown") << "\n";

                    // Remove agent from graph, get orphaned figures
                    auto orphaned = graph.remove_agent(target_wid);

                    // Redistribute orphaned figures to first remaining agent
                    if (!orphaned.empty())
                    {
                        auto remaining = graph.all_window_ids();
                        if (!remaining.empty())
                        {
                            auto target = remaining[0];
                            for (auto fid : orphaned)
                                graph.assign_figure(fid, target);

                            std::cerr << "[spectra-backend] Redistributed " << orphaned.size()
                                      << " figures to window=" << target << "\n";

                            // Notify the receiving agent
                            auto figs = graph.figures_for_window(target);
                            for (auto& c : clients)
                            {
                                if (c.window_id == target && c.conn)
                                {
                                    send_assign_figures(*c.conn,
                                                        target,
                                                        graph.session_id(),
                                                        figs,
                                                        figs.empty() ? 0 : figs[0]);
                                    break;
                                }
                            }
                        }
                        else
                        {
                            std::cerr << "[spectra-backend] No remaining agents for "
                                      << orphaned.size() << " orphaned figures\n";
                        }
                    }

                    // Send CMD_CLOSE_WINDOW to the target agent
                    if (target_wid == it->window_id)
                    {
                        send_close_window(*it->conn, target_wid, graph.session_id(), "close_ack");
                        it->conn->close();
                        it = clients.erase(it);
                        continue;   // skip ++it
                    }
                    else
                    {
                        // Close a different window
                        for (auto& c : clients)
                        {
                            if (c.window_id == target_wid && c.conn)
                            {
                                send_close_window(*c.conn,
                                                  target_wid,
                                                  graph.session_id(),
                                                  "close_ack");
                                c.conn->close();
                                break;
                            }
                        }
                        // Send RESP_OK to requester
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_DETACH_FIGURE:
                {
                    auto detach = spectra::ipc::decode_req_detach_figure(msg.payload);
                    if (!detach)
                        break;

                    std::cerr << "[spectra-backend] REQ_DETACH_FIGURE: figure=" << detach->figure_id
                              << " from window=" << detach->source_window_id << " → new window at ("
                              << detach->screen_x << "," << detach->screen_y << ")\n";

                    // Verify the figure exists
                    if (!fig_model.has_figure(detach->figure_id))
                    {
                        std::cerr << "[spectra-backend] Figure " << detach->figure_id
                                  << " not found, ignoring detach\n";
                        break;
                    }

                    // Remove figure from source agent in session graph
                    graph.unassign_figure(detach->figure_id, detach->source_window_id);

                    // Notify source agent to remove the figure
                    {
                        spectra::ipc::CmdRemoveFigurePayload rm;
                        rm.window_id = detach->source_window_id;
                        rm.figure_id = detach->figure_id;
                        spectra::ipc::Message rm_msg;
                        rm_msg.header.type = spectra::ipc::MessageType::CMD_REMOVE_FIGURE;
                        rm_msg.header.session_id = graph.session_id();
                        rm_msg.header.window_id = detach->source_window_id;
                        rm_msg.payload = spectra::ipc::encode_cmd_remove_figure(rm);
                        rm_msg.header.payload_len = static_cast<uint32_t>(rm_msg.payload.size());
                        for (auto& c : clients)
                        {
                            if (c.window_id == detach->source_window_id && c.conn)
                            {
                                c.conn->send(rm_msg);
                                break;
                            }
                        }
                    }

                    // Spawn a new agent process for the detached figure.
                    // The new agent will connect, do HELLO/WELCOME handshake,
                    // and receive a new window_id. We pre-register an agent
                    // entry so assign_figure works, then the HELLO handler
                    // will match it up.
                    auto new_wid = graph.add_agent(0, -1);
                    graph.assign_figure(detach->figure_id, new_wid);
                    graph.heartbeat(new_wid);

                    std::cerr << "[spectra-backend] Spawning new agent for detached figure, window="
                              << new_wid << "\n";

                    proc_mgr.spawn_agent_for_window(new_wid);

                    // Send RESP_OK to the requesting agent
                    {
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::EVT_WINDOW:
                {
                    // Agent reports window event (e.g. close_requested)
                    std::cerr << "[spectra-backend] EVT_WINDOW from window=" << it->window_id
                              << "\n";

                    if (it->window_id != spectra::ipc::INVALID_WINDOW)
                    {
                        auto orphaned = graph.remove_agent(it->window_id);
                        std::cerr << "[spectra-backend] Agent closed (window=" << it->window_id
                                  << ", orphaned_figures=" << orphaned.size() << ")\n";

                        // Notify Python clients about closed figures
                        for (auto fid : orphaned)
                        {
                            spectra::ipc::EvtWindowClosedPayload evt;
                            evt.figure_id = fid;
                            evt.window_id = it->window_id;
                            evt.reason = "user_close";
                            auto evt_payload = spectra::ipc::encode_evt_window_closed(evt);

                            for (auto& c : clients)
                            {
                                if (c.conn && c.handshake_done
                                    && c.client_type == spectra::daemon::ClientType::PYTHON)
                                {
                                    spectra::ipc::Message evt_msg;
                                    evt_msg.header.type =
                                        spectra::ipc::MessageType::EVT_WINDOW_CLOSED;
                                    evt_msg.header.session_id = graph.session_id();
                                    evt_msg.payload = evt_payload;
                                    evt_msg.header.payload_len =
                                        static_cast<uint32_t>(evt_msg.payload.size());
                                    c.conn->send(evt_msg);
                                }
                            }
                        }

                        // Redistribute orphaned figures
                        if (!orphaned.empty())
                        {
                            auto remaining = graph.all_window_ids();
                            if (!remaining.empty())
                            {
                                auto target = remaining[0];
                                for (auto fid : orphaned)
                                    graph.assign_figure(fid, target);

                                std::cerr << "[spectra-backend] Redistributed " << orphaned.size()
                                          << " figures to window=" << target << "\n";

                                auto figs = graph.figures_for_window(target);
                                for (auto& c : clients)
                                {
                                    if (c.window_id == target && c.conn)
                                    {
                                        send_assign_figures(*c.conn,
                                                            target,
                                                            graph.session_id(),
                                                            figs,
                                                            figs.empty() ? 0 : figs[0]);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    it->conn->close();
                    it = clients.erase(it);
                    continue;   // skip ++it
                }

                case spectra::ipc::MessageType::EVT_INPUT:
                {
                    auto input = spectra::ipc::decode_evt_input(msg.payload);
                    if (!input)
                        break;

                    // All model mutations go through the backend's FigureModel.
                    // The agent sends raw input events; the backend interprets
                    // them and applies the appropriate mutation.
                    spectra::ipc::StateDiffPayload diff;
                    auto base_rev = fig_model.revision();

                    switch (input->input_type)
                    {
                        case spectra::ipc::EvtInputPayload::InputType::SCROLL:
                        {
                            // Scroll → zoom: x,y carry scroll deltas.
                            // Backend computes new axis limits from current
                            // limits + scroll amount.
                            // For now, treat x as x_scroll, y as y_scroll
                            // and apply a zoom factor to the current limits.
                            auto snap = fig_model.snapshot({input->figure_id});
                            if (!snap.figures.empty()
                                && input->axes_index < snap.figures[0].axes.size())
                            {
                                const auto& ax = snap.figures[0].axes[input->axes_index];
                                float zoom = 1.0f - static_cast<float>(input->y) * 0.1f;
                                if (zoom < 0.1f)
                                    zoom = 0.1f;
                                if (zoom > 10.0f)
                                    zoom = 10.0f;
                                float cx = (ax.x_min + ax.x_max) * 0.5f;
                                float cy = (ax.y_min + ax.y_max) * 0.5f;
                                float hw = (ax.x_max - ax.x_min) * 0.5f * zoom;
                                float hh = (ax.y_max - ax.y_min) * 0.5f * zoom;
                                auto op = fig_model.set_axis_limits(input->figure_id,
                                                                    input->axes_index,
                                                                    cx - hw,
                                                                    cx + hw,
                                                                    cy - hh,
                                                                    cy + hh);
                                diff.ops.push_back(op);
                            }
                            break;
                        }

                        case spectra::ipc::EvtInputPayload::InputType::KEY_PRESS:
                        {
                            // Key press → backend interprets shortcuts
                            // 'g' toggles grid, 'h' resets view (home)
                            if (input->key == 'G' || input->key == 'g')
                            {
                                auto snap = fig_model.snapshot({input->figure_id});
                                if (!snap.figures.empty()
                                    && input->axes_index < snap.figures[0].axes.size())
                                {
                                    bool cur = snap.figures[0].axes[input->axes_index].grid_visible;
                                    auto op = fig_model.set_grid_visible(input->figure_id,
                                                                         input->axes_index,
                                                                         !cur);
                                    diff.ops.push_back(op);
                                }
                            }
                            break;
                        }

                        case spectra::ipc::EvtInputPayload::InputType::KEY_RELEASE:
                        case spectra::ipc::EvtInputPayload::InputType::MOUSE_BUTTON:
                        case spectra::ipc::EvtInputPayload::InputType::MOUSE_MOVE:
                            // Reserved for future interaction (pan, selection, etc.)
                            break;
                    }

                    // Broadcast STATE_DIFF to ALL agents (including sender)
                    // so every agent gets the authoritative state.
                    if (!diff.ops.empty())
                    {
                        diff.base_revision = base_rev;
                        diff.new_revision = fig_model.revision();

                        for (auto& c : clients)
                        {
                            if (c.conn && c.handshake_done)
                            {
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                            }
                        }
                    }
                    break;
                }

                case spectra::ipc::MessageType::STATE_SNAPSHOT:
                {
                    // App client pushes its figures to the backend.
                    // Load into fig_model, register in session graph, spawn agent.
                    auto incoming = spectra::ipc::decode_state_snapshot(msg.payload);
                    if (!incoming || incoming->figures.empty())
                    {
                        std::cerr << "[spectra-backend] STATE_SNAPSHOT: empty or decode failed\n";
                        break;
                    }

                    std::cerr << "[spectra-backend] STATE_SNAPSHOT: received "
                              << incoming->figures.size() << " figure(s) from app\n";

                    auto new_ids = fig_model.load_snapshot(*incoming);

                    // Register all figures in session graph
                    for (size_t i = 0; i < new_ids.size(); ++i)
                    {
                        const auto& fig = incoming->figures[i];
                        graph.register_figure(
                            new_ids[i],
                            fig.title.empty() ? "Figure " + std::to_string(i + 1) : fig.title);
                    }

                    // Group figures by window_group and spawn one agent per group.
                    // Figures with the same non-zero window_group share one agent window.
                    // Figures with window_group==0 each get their own agent.
                    std::unordered_map<uint32_t, std::vector<size_t>> groups;
                    std::vector<size_t> ungrouped;
                    for (size_t fi = 0; fi < new_ids.size(); ++fi)
                    {
                        uint32_t wg = incoming->figures[fi].window_group;
                        if (wg != 0)
                            groups[wg].push_back(fi);
                        else
                            ungrouped.push_back(fi);
                    }

                    // Spawn one agent per group
                    for (auto& [wg, fig_indices] : groups)
                    {
                        auto pre_wid = graph.add_agent(0, -1);
                        for (size_t fi : fig_indices)
                        {
                            graph.assign_figure(new_ids[fi], pre_wid);
                        }
                        graph.heartbeat(pre_wid);

                        pid_t pid = proc_mgr.spawn_agent();
                        if (pid <= 0)
                        {
                            std::cerr << "[spectra-backend] Failed to spawn agent for group " << wg
                                      << "\n";
                        }
                        else
                        {
                            std::cerr << "[spectra-backend] Spawned agent pid=" << pid
                                      << " for group " << wg << " with " << fig_indices.size()
                                      << " figure(s)"
                                      << " (pre-assigned window=" << pre_wid << ")\n";
                        }
                    }

                    // Spawn one agent per ungrouped figure
                    for (size_t fi : ungrouped)
                    {
                        auto pre_wid = graph.add_agent(0, -1);
                        graph.assign_figure(new_ids[fi], pre_wid);
                        graph.heartbeat(pre_wid);

                        pid_t pid = proc_mgr.spawn_agent();
                        if (pid <= 0)
                        {
                            std::cerr << "[spectra-backend] Failed to spawn agent for figure "
                                      << new_ids[fi] << "\n";
                        }
                        else
                        {
                            std::cerr << "[spectra-backend] Spawned agent pid=" << pid
                                      << " for figure " << new_ids[fi]
                                      << " (pre-assigned window=" << pre_wid << ")\n";
                        }
                    }
                    break;
                }

                case spectra::ipc::MessageType::STATE_DIFF:
                {
                    // Decode, apply to FigureModel, and forward.
                    auto incoming_diff = spectra::ipc::decode_state_diff(msg.payload);
                    if (!incoming_diff || incoming_diff->ops.empty())
                        break;

                    auto base_rev = fig_model.revision();
                    for (const auto& op : incoming_diff->ops)
                        fig_model.apply_diff_op(op);

                    spectra::ipc::StateDiffPayload fwd_diff;
                    fwd_diff.ops = incoming_diff->ops;
                    fwd_diff.base_revision = base_rev;
                    fwd_diff.new_revision = fig_model.revision();

                    bool from_source = it->is_source_client;
                    for (auto& c : clients)
                    {
                        if (!c.conn || !c.handshake_done)
                            continue;
                        if (from_source)
                        {
                            // App → agents: forward to all render agents
                            if (!c.is_source_client)
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), fwd_diff);
                        }
                        else
                        {
                            // Agent → app: forward to source client
                            // (e.g. knob value changes from UI)
                            if (c.is_source_client)
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), fwd_diff);
                        }
                    }
                    break;
                }

                case spectra::ipc::MessageType::ACK_STATE:
                    break;

                    // ─── Python request handlers ─────────────────────────────────

                case spectra::ipc::MessageType::REQ_CREATE_FIGURE:
                {
                    auto req = spectra::ipc::decode_req_create_figure(msg.payload);
                    if (!req)
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      400,
                                      "Bad REQ_CREATE_FIGURE payload");
                        break;
                    }

                    auto fid = fig_model.create_figure(req->title.empty() ? "Figure" : req->title,
                                                       req->width,
                                                       req->height);
                    graph.register_figure(fid, req->title);

                    std::cerr << "[spectra-backend] Python: created figure " << fid
                              << " title=" << req->title << "\n";

                    spectra::ipc::RespFigureCreatedPayload resp;
                    resp.request_id = msg.header.request_id;
                    resp.figure_id = fid;
                    send_python_response(*it->conn,
                                         spectra::ipc::MessageType::RESP_FIGURE_CREATED,
                                         graph.session_id(),
                                         msg.header.request_id,
                                         spectra::ipc::encode_resp_figure_created(resp));
                    break;
                }

                case spectra::ipc::MessageType::REQ_CREATE_AXES:
                {
                    auto req = spectra::ipc::decode_req_create_axes(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    // Update grid dimensions on the figure model so the
                    // snapshot sent to agents has the correct layout.
                    fig_model.set_grid(req->figure_id, req->grid_rows, req->grid_cols);

                    auto axes_idx =
                        fig_model.add_axes(req->figure_id, 0.0f, 1.0f, 0.0f, 1.0f, req->is_3d);

                    std::cerr << "[spectra-backend] Python: created axes " << axes_idx
                              << (req->is_3d ? " (3D)" : "") << " in figure " << req->figure_id
                              << "\n";

                    // Broadcast ADD_AXES diff to all agents
                    {
                        spectra::ipc::DiffOp add_op;
                        add_op.type = spectra::ipc::DiffOp::Type::ADD_AXES;
                        add_op.figure_id = req->figure_id;
                        add_op.axes_index = axes_idx;
                        add_op.bool_val = req->is_3d;

                        spectra::ipc::StateDiffPayload diff;
                        diff.base_revision = fig_model.revision() - 1;
                        diff.new_revision = fig_model.revision();
                        diff.ops.push_back(add_op);
                        for (auto& c : clients)
                        {
                            if (c.conn && c.handshake_done
                                && c.client_type == spectra::daemon::ClientType::AGENT)
                            {
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                            }
                        }
                    }

                    spectra::ipc::RespAxesCreatedPayload resp;
                    resp.request_id = msg.header.request_id;
                    resp.axes_index = axes_idx;
                    send_python_response(*it->conn,
                                         spectra::ipc::MessageType::RESP_AXES_CREATED,
                                         graph.session_id(),
                                         msg.header.request_id,
                                         spectra::ipc::encode_resp_axes_created(resp));
                    break;
                }

                case spectra::ipc::MessageType::REQ_ADD_SERIES:
                {
                    auto req = spectra::ipc::decode_req_add_series(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    uint32_t series_idx = 0;
                    auto add_op = fig_model.add_series_with_diff(req->figure_id,
                                                                 req->label,
                                                                 req->series_type,
                                                                 req->axes_index,
                                                                 series_idx);

                    std::cerr << "[spectra-backend] Python: added series " << series_idx
                              << " type=" << req->series_type << " in figure " << req->figure_id
                              << "\n";

                    // Broadcast ADD_SERIES diff to all agents rendering this figure
                    {
                        spectra::ipc::StateDiffPayload diff;
                        diff.base_revision = fig_model.revision() - 1;
                        diff.new_revision = fig_model.revision();
                        diff.ops.push_back(add_op);

                        // If the series was created with a label, also send
                        // SET_SERIES_LABEL so the agent picks it up immediately
                        // (ADD_SERIES diff only carries the type, not the name).
                        if (!req->label.empty())
                        {
                            spectra::ipc::DiffOp label_op;
                            label_op.type = spectra::ipc::DiffOp::Type::SET_SERIES_LABEL;
                            label_op.figure_id = req->figure_id;
                            label_op.series_index = series_idx;
                            label_op.str_val = req->label;
                            diff.ops.push_back(label_op);
                        }

                        for (auto& c : clients)
                        {
                            if (c.conn && c.handshake_done
                                && c.client_type == spectra::daemon::ClientType::AGENT)
                            {
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                            }
                        }
                    }

                    spectra::ipc::RespSeriesAddedPayload resp;
                    resp.request_id = msg.header.request_id;
                    resp.series_index = series_idx;
                    send_python_response(*it->conn,
                                         spectra::ipc::MessageType::RESP_SERIES_ADDED,
                                         graph.session_id(),
                                         msg.header.request_id,
                                         spectra::ipc::encode_resp_series_added(resp));
                    break;
                }

                case spectra::ipc::MessageType::REQ_SET_DATA:
                {
                    auto req = spectra::ipc::decode_req_set_data(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    auto op =
                        fig_model.set_series_data(req->figure_id, req->series_index, req->data);

                    // Broadcast diff to all agents rendering this figure
                    spectra::ipc::StateDiffPayload diff;
                    diff.base_revision = fig_model.revision() - 1;
                    diff.new_revision = fig_model.revision();
                    diff.ops.push_back(op);

                    for (auto& c : clients)
                    {
                        if (c.conn && c.handshake_done
                            && c.client_type == spectra::daemon::ClientType::AGENT)
                        {
                            send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                        }
                    }

                    // Send RESP_OK to Python
                    spectra::ipc::Message ok_msg;
                    ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                    ok_msg.header.session_id = graph.session_id();
                    ok_msg.header.request_id = msg.header.request_id;
                    ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                    ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                    it->conn->send(ok_msg);
                    break;
                }

                case spectra::ipc::MessageType::REQ_UPDATE_PROPERTY:
                {
                    auto req = spectra::ipc::decode_req_update_property(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    spectra::ipc::StateDiffPayload diff;
                    auto base_rev = fig_model.revision();

                    // Dispatch property updates to FigureModel
                    if (req->property == "color")
                    {
                        auto op = fig_model.set_series_color(req->figure_id,
                                                             req->series_index,
                                                             req->f1,
                                                             req->f2,
                                                             req->f3,
                                                             req->f4);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "xlim")
                    {
                        float cx0, cx1, cy0, cy1;
                        fig_model
                            .get_axis_limits(req->figure_id, req->axes_index, cx0, cx1, cy0, cy1);
                        auto op = fig_model.set_axis_limits(req->figure_id,
                                                            req->axes_index,
                                                            req->f1,
                                                            req->f2,
                                                            cy0,
                                                            cy1);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "ylim")
                    {
                        float cx0, cx1, cy0, cy1;
                        fig_model
                            .get_axis_limits(req->figure_id, req->axes_index, cx0, cx1, cy0, cy1);
                        auto op = fig_model.set_axis_limits(req->figure_id,
                                                            req->axes_index,
                                                            cx0,
                                                            cx1,
                                                            req->f1,
                                                            req->f2);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "zlim")
                    {
                        auto op = fig_model.set_axis_zlimits(req->figure_id,
                                                             req->axes_index,
                                                             req->f1,
                                                             req->f2);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "title")
                    {
                        auto op = fig_model.set_figure_title(req->figure_id, req->str_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "grid")
                    {
                        auto op = fig_model.set_grid_visible(req->figure_id,
                                                             req->axes_index,
                                                             req->bool_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "visible")
                    {
                        auto op = fig_model.set_series_visible(req->figure_id,
                                                               req->series_index,
                                                               req->bool_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "line_width")
                    {
                        auto op =
                            fig_model.set_line_width(req->figure_id, req->series_index, req->f1);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "marker_size")
                    {
                        auto op =
                            fig_model.set_marker_size(req->figure_id, req->series_index, req->f1);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "opacity")
                    {
                        auto op = fig_model.set_opacity(req->figure_id, req->series_index, req->f1);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "xlabel")
                    {
                        auto op = fig_model.set_axis_xlabel(req->figure_id,
                                                            req->axes_index,
                                                            req->str_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "ylabel")
                    {
                        auto op = fig_model.set_axis_ylabel(req->figure_id,
                                                            req->axes_index,
                                                            req->str_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "axes_title")
                    {
                        auto op =
                            fig_model.set_axis_title(req->figure_id, req->axes_index, req->str_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "label")
                    {
                        auto op = fig_model.set_series_label(req->figure_id,
                                                             req->series_index,
                                                             req->str_val);
                        diff.ops.push_back(op);
                    }
                    else if (req->property == "legend" || req->property == "legend_visible")
                    {
                        // Legend visibility is client-side UI state; acknowledge silently.
                    }
                    else
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      400,
                                      "Unknown property: " + req->property);
                        break;
                    }

                    // Broadcast diff to agents
                    if (!diff.ops.empty())
                    {
                        diff.base_revision = base_rev;
                        diff.new_revision = fig_model.revision();
                        for (auto& c : clients)
                        {
                            if (c.conn && c.handshake_done
                                && c.client_type == spectra::daemon::ClientType::AGENT)
                            {
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                            }
                        }
                    }

                    // Send RESP_OK to Python
                    {
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_SHOW:
                {
                    auto req = spectra::ipc::decode_req_show(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    // If window_id is specified and valid, add figure as tab to existing window
                    if (req->window_id != spectra::ipc::INVALID_WINDOW
                        && graph.agent(req->window_id) != nullptr)
                    {
                        std::cerr << "[spectra-backend] Python: REQ_SHOW figure=" << req->figure_id
                                  << " as tab in window=" << req->window_id << "\n";

                        graph.assign_figure(req->figure_id, req->window_id);

                        // Send CMD_ASSIGN_FIGURES to the target agent with updated figure list
                        // Keep the first figure as active so the first tab stays selected
                        auto assigned = graph.figures_for_window(req->window_id);
                        for (auto& c : clients)
                        {
                            if (c.window_id == req->window_id && c.conn)
                            {
                                send_assign_figures(
                                    *c.conn,
                                    req->window_id,
                                    graph.session_id(),
                                    assigned,
                                    assigned.empty() ? req->figure_id : assigned[0]);

                                // Also send updated state snapshot so agent has the new figure data
                                auto snap = fig_model.snapshot(assigned);
                                send_state_snapshot(*c.conn,
                                                    req->window_id,
                                                    graph.session_id(),
                                                    snap);
                                break;
                            }
                        }

                        // Send RESP_OK with the window_id so Python can track it
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.header.window_id = req->window_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    else
                    {
                        // No target window — spawn a new agent
                        std::cerr << "[spectra-backend] Python: REQ_SHOW figure=" << req->figure_id
                                  << "\n";

                        auto new_wid = graph.add_agent(0, -1);
                        graph.assign_figure(req->figure_id, new_wid);
                        graph.heartbeat(new_wid);

                        pid_t pid = proc_mgr.spawn_agent();
                        if (pid > 0)
                        {
                            std::cerr << "[spectra-backend] Spawned agent pid=" << pid
                                      << " for figure " << req->figure_id << " (window=" << new_wid
                                      << ")\n";

                            spectra::ipc::Message ok_msg;
                            ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                            ok_msg.header.session_id = graph.session_id();
                            ok_msg.header.request_id = msg.header.request_id;
                            ok_msg.header.window_id = new_wid;
                            ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                            ok_msg.header.payload_len =
                                static_cast<uint32_t>(ok_msg.payload.size());
                            it->conn->send(ok_msg);
                        }
                        else
                        {
                            graph.remove_agent(new_wid);
                            send_resp_err(*it->conn,
                                          graph.session_id(),
                                          msg.header.request_id,
                                          500,
                                          "Failed to spawn agent");
                        }
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_APPEND_DATA:
                {
                    auto req = spectra::ipc::decode_req_append_data(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    auto op =
                        fig_model.append_series_data(req->figure_id, req->series_index, req->data);

                    // Broadcast diff to all agents rendering this figure
                    spectra::ipc::StateDiffPayload diff;
                    diff.base_revision = fig_model.revision() - 1;
                    diff.new_revision = fig_model.revision();
                    diff.ops.push_back(op);

                    for (auto& c : clients)
                    {
                        if (c.conn && c.handshake_done
                            && c.client_type == spectra::daemon::ClientType::AGENT)
                        {
                            send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                        }
                    }

                    // Send RESP_OK to Python
                    spectra::ipc::Message ok_msg;
                    ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                    ok_msg.header.session_id = graph.session_id();
                    ok_msg.header.request_id = msg.header.request_id;
                    ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                    ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                    it->conn->send(ok_msg);
                    break;
                }

                case spectra::ipc::MessageType::REQ_REMOVE_SERIES:
                {
                    auto req = spectra::ipc::decode_req_remove_series(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    auto op = fig_model.remove_series(req->figure_id, req->series_index);

                    std::cerr << "[spectra-backend] Python: removed series " << req->series_index
                              << " from figure " << req->figure_id << "\n";

                    // Broadcast diff to all agents
                    spectra::ipc::StateDiffPayload diff;
                    diff.base_revision = fig_model.revision() - 1;
                    diff.new_revision = fig_model.revision();
                    diff.ops.push_back(op);

                    for (auto& c : clients)
                    {
                        if (c.conn && c.handshake_done
                            && c.client_type == spectra::daemon::ClientType::AGENT)
                        {
                            send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                        }
                    }

                    // Send RESP_OK to Python
                    {
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_CLOSE_FIGURE:
                {
                    auto req = spectra::ipc::decode_req_close_figure(msg.payload);
                    if (!req || !fig_model.has_figure(req->figure_id))
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      404,
                                      "Figure not found");
                        break;
                    }

                    std::cerr << "[spectra-backend] Python: REQ_CLOSE_FIGURE figure="
                              << req->figure_id << " (closing window, keeping figure)\n";

                    // Find and close agent windows displaying this figure
                    for (auto wid : graph.all_window_ids())
                    {
                        auto figs = graph.figures_for_window(wid);
                        bool has_fig = false;
                        for (auto fid : figs)
                        {
                            if (fid == req->figure_id)
                            {
                                has_fig = true;
                                break;
                            }
                        }
                        if (!has_fig)
                            continue;

                        for (auto& c : clients)
                        {
                            if (c.window_id == wid && c.conn)
                            {
                                spectra::ipc::Message close_msg;
                                close_msg.header.type = spectra::ipc::MessageType::CMD_CLOSE_WINDOW;
                                close_msg.header.session_id = graph.session_id();
                                close_msg.header.window_id = wid;
                                close_msg.payload = {};
                                close_msg.header.payload_len = 0;
                                c.conn->send(close_msg);
                                break;
                            }
                        }
                    }

                    // Notify Python client
                    {
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_UPDATE_BATCH:
                {
                    auto req = spectra::ipc::decode_req_update_batch(msg.payload);
                    if (!req || req->updates.empty())
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      400,
                                      "Bad REQ_UPDATE_BATCH payload");
                        break;
                    }

                    spectra::ipc::StateDiffPayload diff;
                    auto base_rev = fig_model.revision();

                    for (const auto& upd : req->updates)
                    {
                        if (!fig_model.has_figure(upd.figure_id))
                            continue;

                        if (upd.property == "color")
                        {
                            diff.ops.push_back(fig_model.set_series_color(upd.figure_id,
                                                                          upd.series_index,
                                                                          upd.f1,
                                                                          upd.f2,
                                                                          upd.f3,
                                                                          upd.f4));
                        }
                        else if (upd.property == "xlim")
                        {
                            float cx0, cx1, cy0, cy1;
                            fig_model
                                .get_axis_limits(upd.figure_id, upd.axes_index, cx0, cx1, cy0, cy1);
                            diff.ops.push_back(fig_model.set_axis_limits(upd.figure_id,
                                                                         upd.axes_index,
                                                                         upd.f1,
                                                                         upd.f2,
                                                                         cy0,
                                                                         cy1));
                        }
                        else if (upd.property == "ylim")
                        {
                            float cx0, cx1, cy0, cy1;
                            fig_model
                                .get_axis_limits(upd.figure_id, upd.axes_index, cx0, cx1, cy0, cy1);
                            diff.ops.push_back(fig_model.set_axis_limits(upd.figure_id,
                                                                         upd.axes_index,
                                                                         cx0,
                                                                         cx1,
                                                                         upd.f1,
                                                                         upd.f2));
                        }
                        else if (upd.property == "zlim")
                        {
                            diff.ops.push_back(fig_model.set_axis_zlimits(upd.figure_id,
                                                                          upd.axes_index,
                                                                          upd.f1,
                                                                          upd.f2));
                        }
                        else if (upd.property == "title")
                        {
                            diff.ops.push_back(
                                fig_model.set_figure_title(upd.figure_id, upd.str_val));
                        }
                        else if (upd.property == "grid")
                        {
                            diff.ops.push_back(fig_model.set_grid_visible(upd.figure_id,
                                                                          upd.axes_index,
                                                                          upd.bool_val));
                        }
                        else if (upd.property == "visible")
                        {
                            diff.ops.push_back(fig_model.set_series_visible(upd.figure_id,
                                                                            upd.series_index,
                                                                            upd.bool_val));
                        }
                        else if (upd.property == "line_width")
                        {
                            diff.ops.push_back(
                                fig_model.set_line_width(upd.figure_id, upd.series_index, upd.f1));
                        }
                        else if (upd.property == "marker_size")
                        {
                            diff.ops.push_back(
                                fig_model.set_marker_size(upd.figure_id, upd.series_index, upd.f1));
                        }
                        else if (upd.property == "opacity")
                        {
                            diff.ops.push_back(
                                fig_model.set_opacity(upd.figure_id, upd.series_index, upd.f1));
                        }
                        else if (upd.property == "xlabel")
                        {
                            diff.ops.push_back(fig_model.set_axis_xlabel(upd.figure_id,
                                                                         upd.axes_index,
                                                                         upd.str_val));
                        }
                        else if (upd.property == "ylabel")
                        {
                            diff.ops.push_back(fig_model.set_axis_ylabel(upd.figure_id,
                                                                         upd.axes_index,
                                                                         upd.str_val));
                        }
                        else if (upd.property == "axes_title")
                        {
                            diff.ops.push_back(fig_model.set_axis_title(upd.figure_id,
                                                                        upd.axes_index,
                                                                        upd.str_val));
                        }
                        else if (upd.property == "label")
                        {
                            diff.ops.push_back(fig_model.set_series_label(upd.figure_id,
                                                                          upd.series_index,
                                                                          upd.str_val));
                        }
                        else if (upd.property == "legend")
                        {
                            // legend visibility — stored as grid_visible for now
                            // (or a separate field if added later)
                        }
                    }

                    std::cerr << "[spectra-backend] Python: batch update with "
                              << req->updates.size() << " items, " << diff.ops.size()
                              << " applied\n";

                    // Broadcast diff to agents
                    if (!diff.ops.empty())
                    {
                        diff.base_revision = base_rev;
                        diff.new_revision = fig_model.revision();
                        for (auto& c : clients)
                        {
                            if (c.conn && c.handshake_done
                                && c.client_type == spectra::daemon::ClientType::AGENT)
                            {
                                send_state_diff(*c.conn, c.window_id, graph.session_id(), diff);
                            }
                        }
                    }

                    // Send RESP_OK to Python
                    {
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_DESTROY_FIGURE:
                {
                    auto req = spectra::ipc::decode_req_destroy_figure(msg.payload);
                    if (!req)
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      400,
                                      "Bad payload");
                        break;
                    }

                    std::cerr << "[spectra-backend] Python: REQ_DESTROY_FIGURE figure="
                              << req->figure_id << "\n";

                    fig_model.remove_figure(req->figure_id);
                    graph.remove_figure(req->figure_id);

                    // Send RESP_OK
                    {
                        spectra::ipc::Message ok_msg;
                        ok_msg.header.type = spectra::ipc::MessageType::RESP_OK;
                        ok_msg.header.session_id = graph.session_id();
                        ok_msg.header.request_id = msg.header.request_id;
                        ok_msg.payload = spectra::ipc::encode_resp_ok({msg.header.request_id});
                        ok_msg.header.payload_len = static_cast<uint32_t>(ok_msg.payload.size());
                        it->conn->send(ok_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::REQ_LIST_FIGURES:
                {
                    spectra::ipc::RespFigureListPayload resp;
                    resp.request_id = msg.header.request_id;
                    resp.figure_ids = fig_model.all_figure_ids();

                    send_python_response(*it->conn,
                                         spectra::ipc::MessageType::RESP_FIGURE_LIST,
                                         graph.session_id(),
                                         msg.header.request_id,
                                         spectra::ipc::encode_resp_figure_list(resp));
                    break;
                }

                case spectra::ipc::MessageType::REQ_RECONNECT:
                {
                    auto req = spectra::ipc::decode_req_reconnect(msg.payload);
                    if (!req)
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      400,
                                      "Bad REQ_RECONNECT payload");
                        break;
                    }

                    std::cerr << "[spectra-backend] Python: REQ_RECONNECT session="
                              << req->session_id << "\n";

                    // Verify session ID matches (or accept any if 0)
                    if (req->session_id != 0 && req->session_id != graph.session_id())
                    {
                        send_resp_err(*it->conn,
                                      graph.session_id(),
                                      msg.header.request_id,
                                      409,
                                      "Session ID mismatch");
                        break;
                    }

                    // Send full snapshot so the reconnecting client can rebuild state
                    auto snap = fig_model.snapshot();
                    send_python_response(*it->conn,
                                         spectra::ipc::MessageType::RESP_SNAPSHOT,
                                         graph.session_id(),
                                         msg.header.request_id,
                                         spectra::ipc::encode_state_snapshot(snap));
                    break;
                }

                case spectra::ipc::MessageType::REQ_DISCONNECT:
                {
                    std::cerr << "[spectra-backend] Python client disconnected gracefully\n";
                    it->conn->close();
                    it = clients.erase(it);
                    continue;   // skip ++it
                }

                case spectra::ipc::MessageType::REQ_GET_SNAPSHOT:
                {
                    auto snap = fig_model.snapshot();
                    send_python_response(*it->conn,
                                         spectra::ipc::MessageType::RESP_SNAPSHOT,
                                         graph.session_id(),
                                         msg.header.request_id,
                                         spectra::ipc::encode_state_snapshot(snap));
                    break;
                }

                default:
                    std::cerr << "[spectra-backend] Unknown message type 0x" << std::hex
                              << static_cast<uint16_t>(msg.header.type) << std::dec
                              << " from window=" << it->window_id << "\n";
                    break;
            }

            ++it;
        }

        // --- Stale agent check ---
        auto now = std::chrono::steady_clock::now();
        if (now - last_stale_check >= STALE_CHECK_INTERVAL)
        {
            last_stale_check = now;
            auto stale = graph.stale_agents(HEARTBEAT_TIMEOUT);
            for (auto wid : stale)
            {
                std::cerr << "[spectra-backend] Agent timed out (window=" << wid << ")\n";
                auto orphaned = graph.remove_agent(wid);

                // Redistribute orphaned figures
                if (!orphaned.empty())
                {
                    auto remaining = graph.all_window_ids();
                    if (!remaining.empty())
                    {
                        auto target = remaining[0];
                        for (auto fid : orphaned)
                            graph.assign_figure(fid, target);

                        auto figs = graph.figures_for_window(target);
                        for (auto& c : clients)
                        {
                            if (c.window_id == target && c.conn)
                            {
                                send_assign_figures(*c.conn,
                                                    target,
                                                    graph.session_id(),
                                                    figs,
                                                    figs.empty() ? 0 : figs[0]);
                                break;
                            }
                        }
                    }
                }

                // Close the connection
                for (auto cit = clients.begin(); cit != clients.end(); ++cit)
                {
                    if (cit->window_id == wid)
                    {
                        cit->conn->close();
                        clients.erase(cit);
                        break;
                    }
                }
            }
        }

        // --- Reap finished child processes ---
        if (now - last_reap_check >= REAP_INTERVAL)
        {
            last_reap_check = now;
            proc_mgr.reap_finished();
        }

        // --- Shutdown rule: exit when no agents remain (after at least one connected) ---
        // Note: the source app client stays in `clients` but is never added to `graph`,
        // so we check graph.is_empty() rather than clients.empty().
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

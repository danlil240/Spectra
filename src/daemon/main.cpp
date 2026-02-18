#include "figure_model.hpp"
#include "process_manager.hpp"
#include "session_graph.hpp"

#include "../ipc/codec.hpp"
#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef __linux__
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

// Resolve the path to the spectra-window agent binary.
// Looks next to the backend binary first, then falls back to PATH.
std::string resolve_agent_path(const char* argv0)
{
    // Try sibling path: same directory as this binary
    std::string self(argv0);
    auto slash = self.rfind('/');
    if (slash != std::string::npos)
    {
        std::string dir = self.substr(0, slash + 1);
        std::string candidate = dir + "spectra-window";
        if (::access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    // Fallback: assume it's on PATH
    return "spectra-window";
}

// Helper: send CMD_ASSIGN_FIGURES to a specific client.
bool send_assign_figures(
    spectra::ipc::Connection& conn,
    spectra::ipc::WindowId wid,
    spectra::ipc::SessionId sid,
    const std::vector<uint64_t>& figure_ids,
    uint64_t active_figure_id)
{
    spectra::ipc::CmdAssignFiguresPayload payload;
    payload.window_id = wid;
    payload.figure_ids = figure_ids;
    payload.active_figure_id = active_figure_id;

    spectra::ipc::Message msg;
    msg.header.type = spectra::ipc::MessageType::CMD_ASSIGN_FIGURES;
    msg.header.session_id = sid;
    msg.header.window_id = wid;
    msg.payload = spectra::ipc::encode_cmd_assign_figures(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send STATE_SNAPSHOT to a specific client.
bool send_state_snapshot(
    spectra::ipc::Connection& conn,
    spectra::ipc::WindowId wid,
    spectra::ipc::SessionId sid,
    const spectra::ipc::StateSnapshotPayload& snap)
{
    spectra::ipc::Message msg;
    msg.header.type = spectra::ipc::MessageType::STATE_SNAPSHOT;
    msg.header.session_id = sid;
    msg.header.window_id = wid;
    msg.payload = spectra::ipc::encode_state_snapshot(snap);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send STATE_DIFF to a specific client.
bool send_state_diff(
    spectra::ipc::Connection& conn,
    spectra::ipc::WindowId wid,
    spectra::ipc::SessionId sid,
    const spectra::ipc::StateDiffPayload& diff)
{
    spectra::ipc::Message msg;
    msg.header.type = spectra::ipc::MessageType::STATE_DIFF;
    msg.header.session_id = sid;
    msg.header.window_id = wid;
    msg.payload = spectra::ipc::encode_state_diff(diff);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: send CMD_CLOSE_WINDOW to a specific client.
bool send_close_window(
    spectra::ipc::Connection& conn,
    spectra::ipc::WindowId wid,
    spectra::ipc::SessionId sid,
    const std::string& reason)
{
    spectra::ipc::CmdCloseWindowPayload payload;
    payload.window_id = wid;
    payload.reason = reason;

    spectra::ipc::Message msg;
    msg.header.type = spectra::ipc::MessageType::CMD_CLOSE_WINDOW;
    msg.header.session_id = sid;
    msg.header.window_id = wid;
    msg.payload = spectra::ipc::encode_cmd_close_window(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// Helper: find a ClientSlot by window_id.
struct ClientSlot;
ClientSlot* find_client(std::vector<ClientSlot>& clients, spectra::ipc::WindowId wid);

}  // namespace

int main(int argc, char* argv[])
{
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

    // Create a default figure so the first agent has something to render
    uint64_t default_fig = fig_model.create_figure("Figure 1");
    fig_model.add_axes(default_fig, 0.0f, 10.0f, 0.0f, 10.0f);
    std::cerr << "[spectra-backend] Created default figure id=" << default_fig << "\n";

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
    };
    std::vector<ClientSlot> clients;

    bool had_agents = false;

    std::cerr << "[spectra-backend] Listening for connections...\n";

    // --- Accept loop (non-blocking via short timeout) ---
    // For simplicity, we use a single-threaded poll loop.
    // In production, this would use epoll/kqueue.
    while (g_running.load(std::memory_order_relaxed))
    {
        // Accept new connections (non-blocking: we'll use a short sleep)
        // TODO: Use poll/epoll for proper multiplexing
        auto new_conn = server.accept();  // This blocks — see note below
        if (new_conn)
        {
            std::cerr << "[spectra-backend] New connection (fd=" << new_conn->fd() << ")\n";
            clients.push_back({std::move(new_conn), spectra::ipc::INVALID_WINDOW, false});
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
                    std::cerr << "[spectra-backend] Agent disconnected (window="
                              << it->window_id << ", orphaned_figures="
                              << orphaned.size() << ")\n";
                }
                it = clients.erase(it);
                continue;
            }

            // Try to receive a message (non-blocking would be better)
            auto msg_opt = it->conn->recv();
            if (!msg_opt)
            {
                // Connection closed or error
                if (it->window_id != spectra::ipc::INVALID_WINDOW)
                {
                    auto orphaned = graph.remove_agent(it->window_id);
                    std::cerr << "[spectra-backend] Agent lost (window="
                              << it->window_id << ", orphaned_figures="
                              << orphaned.size() << ")\n";
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
                    if (hello)
                    {
                        std::cerr << "[spectra-backend] HELLO from agent (build="
                                  << hello->agent_build << ")\n";
                    }

                    // Register agent in session graph
                    auto wid = graph.add_agent(0, it->conn->fd());
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

                    // Assign default figures to the new agent
                    auto all_figs = fig_model.all_figure_ids();
                    for (auto fid : all_figs)
                        graph.assign_figure(fid, wid);

                    // Send CMD_ASSIGN_FIGURES
                    auto assigned = graph.figures_for_window(wid);
                    if (!assigned.empty())
                    {
                        send_assign_figures(*it->conn, wid, graph.session_id(),
                                            assigned, assigned[0]);
                    }

                    // Send STATE_SNAPSHOT with full figure data
                    auto snap = fig_model.snapshot(assigned);
                    send_state_snapshot(*it->conn, wid, graph.session_id(), snap);

                    std::cerr << "[spectra-backend] Assigned window_id=" << wid
                              << " with " << assigned.size() << " figures\n";
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
                    std::cerr << "[spectra-backend] REQ_CREATE_WINDOW from window="
                              << it->window_id << "\n";

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

                    std::cerr << "[spectra-backend] REQ_CLOSE_WINDOW window="
                              << target_wid << " reason="
                              << (close_req ? close_req->reason : "unknown") << "\n";

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
                                    send_assign_figures(*c.conn, target,
                                                        graph.session_id(), figs,
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
                        continue;  // skip ++it
                    }
                    else
                    {
                        // Close a different window
                        for (auto& c : clients)
                        {
                            if (c.window_id == target_wid && c.conn)
                            {
                                send_close_window(*c.conn, target_wid,
                                                  graph.session_id(), "close_ack");
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

                case spectra::ipc::MessageType::EVT_WINDOW:
                {
                    // Agent reports window event (e.g. close_requested)
                    std::cerr << "[spectra-backend] EVT_WINDOW from window="
                              << it->window_id << "\n";

                    if (it->window_id != spectra::ipc::INVALID_WINDOW)
                    {
                        auto orphaned = graph.remove_agent(it->window_id);
                        std::cerr << "[spectra-backend] Agent closed (window="
                                  << it->window_id << ", orphaned_figures="
                                  << orphaned.size() << ")\n";

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
                                        send_assign_figures(*c.conn, target,
                                                            graph.session_id(), figs,
                                                            figs.empty() ? 0 : figs[0]);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    it->conn->close();
                    it = clients.erase(it);
                    continue;  // skip ++it
                }

                case spectra::ipc::MessageType::EVT_INPUT:
                {
                    auto input = spectra::ipc::decode_evt_input(msg.payload);
                    if (!input)
                        break;

                    // Apply the input mutation to the authoritative model
                    // For now, we handle axis-limit changes from pan/zoom
                    // The agent sends the desired new limits as an EVT_INPUT
                    // with input_type = SCROLL (pan/zoom result)
                    if (input->input_type == spectra::ipc::EvtInputPayload::InputType::SCROLL)
                    {
                        // x,y carry the new x_min,x_max; figure_id + axes_index identify target
                        // This is a simplified protocol — full implementation would
                        // carry the actual input and let backend compute new limits
                        auto diff_op = fig_model.set_axis_limits(
                            input->figure_id, input->axes_index,
                            static_cast<float>(input->x),
                            static_cast<float>(input->y),
                            0.0f, 0.0f);  // y limits would need additional fields

                        // Broadcast STATE_DIFF to all other agents
                        spectra::ipc::StateDiffPayload diff;
                        diff.base_revision = fig_model.revision() - 1;
                        diff.new_revision = fig_model.revision();
                        diff.ops.push_back(diff_op);

                        for (auto& c : clients)
                        {
                            if (c.window_id != it->window_id && c.conn && c.handshake_done)
                            {
                                send_state_diff(*c.conn, c.window_id,
                                                graph.session_id(), diff);
                            }
                        }
                    }
                    break;
                }

                case spectra::ipc::MessageType::ACK_STATE:
                {
                    // Agent acknowledges a state revision — for now just log
                    auto ack = spectra::ipc::decode_ack_state(msg.payload);
                    if (ack)
                    {
                        std::cerr << "[spectra-backend] ACK_STATE rev="
                                  << ack->revision << " from window="
                                  << it->window_id << "\n";
                    }
                    break;
                }

                default:
                    std::cerr << "[spectra-backend] Unknown message type 0x"
                              << std::hex << static_cast<uint16_t>(msg.header.type)
                              << std::dec << " from window=" << it->window_id << "\n";
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
                                send_assign_figures(*c.conn, target,
                                                    graph.session_id(), figs,
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
        if (!graph.is_empty())
        {
            had_agents = true;
        }
        else if (had_agents && clients.empty())
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
}

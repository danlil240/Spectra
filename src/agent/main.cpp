#include "../ipc/codec.hpp"
#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
    #include <poll.h>
    #include <unistd.h>
#endif

namespace
{

std::atomic<bool> g_running{true};

void signal_handler(int /*sig*/)
{
    g_running.store(false, std::memory_order_relaxed);
}

}  // namespace

int main(int argc, char* argv[])
{
    // Parse --socket <path> argument
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
    {
        std::cerr << "[spectra-window] Error: --socket <path> required\n";
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cerr << "[spectra-window] Connecting to backend: " << socket_path << "\n";

    // --- Connect to backend daemon ---
    auto conn = spectra::ipc::Client::connect(socket_path);
    if (!conn)
    {
        std::cerr << "[spectra-window] Failed to connect to " << socket_path << "\n";
        return 1;
    }

    std::cerr << "[spectra-window] Connected (fd=" << conn->fd() << ")\n";

    // --- Send HELLO ---
    spectra::ipc::HelloPayload hello;
    hello.protocol_major = spectra::ipc::PROTOCOL_MAJOR;
    hello.protocol_minor = spectra::ipc::PROTOCOL_MINOR;
    hello.agent_build = "spectra-window/0.1.0";
    hello.capabilities = 0;

    spectra::ipc::Message hello_msg;
    hello_msg.header.type = spectra::ipc::MessageType::HELLO;
    hello_msg.payload = spectra::ipc::encode_hello(hello);
    hello_msg.header.payload_len = static_cast<uint32_t>(hello_msg.payload.size());

    if (!conn->send(hello_msg))
    {
        std::cerr << "[spectra-window] Failed to send HELLO\n";
        return 1;
    }

    // --- Receive WELCOME ---
    auto welcome_msg = conn->recv();
    if (!welcome_msg || welcome_msg->header.type != spectra::ipc::MessageType::WELCOME)
    {
        std::cerr << "[spectra-window] Did not receive WELCOME\n";
        return 1;
    }

    auto welcome = spectra::ipc::decode_welcome(welcome_msg->payload);
    if (!welcome)
    {
        std::cerr << "[spectra-window] Failed to decode WELCOME\n";
        return 1;
    }

    spectra::ipc::SessionId session_id = welcome->session_id;
    spectra::ipc::WindowId window_id = welcome->window_id;
    uint32_t heartbeat_ms = welcome->heartbeat_ms;

    std::cerr << "[spectra-window] WELCOME: session=" << session_id
              << " window=" << window_id
              << " heartbeat=" << heartbeat_ms << "ms"
              << " mode=" << welcome->mode << "\n";

    // --- Track assigned figures ---
    std::vector<uint64_t> assigned_figures;
    uint64_t active_figure_id = 0;

    // --- Local figure state cache (received from backend via STATE_SNAPSHOT/DIFF) ---
    std::vector<spectra::ipc::SnapshotFigureState> figure_cache;
    spectra::ipc::Revision current_revision = 0;

    // --- Main loop ---
    // In a full implementation, this would also:
    //   1. Initialize GLFW + Vulkan + ImGui (reusing WindowRuntime)
    //   2. Render assigned figures each frame
    //
    // Currently implements: IPC protocol loop with heartbeats, CMD_ASSIGN_FIGURES,
    // CMD_CLOSE_WINDOW, CMD_REMOVE_FIGURE, CMD_SET_ACTIVE handling.

    auto last_heartbeat = std::chrono::steady_clock::now();
    auto heartbeat_interval = std::chrono::milliseconds(heartbeat_ms);

    // Use poll() for non-blocking recv so we can interleave heartbeats
    while (g_running.load(std::memory_order_relaxed))
    {
        // Send heartbeat if interval elapsed
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= heartbeat_interval)
        {
            spectra::ipc::Message hb;
            hb.header.type = spectra::ipc::MessageType::EVT_HEARTBEAT;
            hb.header.session_id = session_id;
            hb.header.window_id = window_id;
            hb.header.payload_len = 0;

            if (!conn->send(hb))
            {
                std::cerr << "[spectra-window] Lost connection to backend\n";
                break;
            }
            last_heartbeat = now;
        }

        // Check for incoming messages from backend (non-blocking via poll)
        bool has_data = false;
#ifdef __linux__
        struct pollfd pfd;
        pfd.fd = conn->fd();
        pfd.events = POLLIN;
        pfd.revents = 0;
        int poll_ret = ::poll(&pfd, 1, 16);  // 16ms timeout (~60fps)
        has_data = (poll_ret > 0 && (pfd.revents & POLLIN));
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
#endif

        if (has_data)
        {
            auto msg_opt = conn->recv();
            if (!msg_opt)
            {
                std::cerr << "[spectra-window] Connection to backend lost\n";
                break;
            }

            auto& msg = *msg_opt;
            switch (msg.header.type)
            {
                case spectra::ipc::MessageType::CMD_ASSIGN_FIGURES:
                {
                    auto payload = spectra::ipc::decode_cmd_assign_figures(msg.payload);
                    if (payload)
                    {
                        assigned_figures = payload->figure_ids;
                        active_figure_id = payload->active_figure_id;
                        std::cerr << "[spectra-window] CMD_ASSIGN_FIGURES: "
                                  << assigned_figures.size() << " figures, active="
                                  << active_figure_id << "\n";
                    }
                    break;
                }

                case spectra::ipc::MessageType::CMD_REMOVE_FIGURE:
                {
                    auto payload = spectra::ipc::decode_cmd_remove_figure(msg.payload);
                    if (payload)
                    {
                        auto it = std::find(assigned_figures.begin(),
                                            assigned_figures.end(),
                                            payload->figure_id);
                        if (it != assigned_figures.end())
                            assigned_figures.erase(it);
                        std::cerr << "[spectra-window] CMD_REMOVE_FIGURE: "
                                  << payload->figure_id << "\n";
                    }
                    break;
                }

                case spectra::ipc::MessageType::CMD_SET_ACTIVE:
                {
                    auto payload = spectra::ipc::decode_cmd_set_active(msg.payload);
                    if (payload)
                    {
                        active_figure_id = payload->figure_id;
                        std::cerr << "[spectra-window] CMD_SET_ACTIVE: "
                                  << active_figure_id << "\n";
                    }
                    break;
                }

                case spectra::ipc::MessageType::CMD_CLOSE_WINDOW:
                {
                    std::cerr << "[spectra-window] CMD_CLOSE_WINDOW received\n";
                    g_running.store(false, std::memory_order_relaxed);
                    break;
                }

                case spectra::ipc::MessageType::STATE_SNAPSHOT:
                {
                    auto snap = spectra::ipc::decode_state_snapshot(msg.payload);
                    if (snap)
                    {
                        figure_cache = snap->figures;
                        current_revision = snap->revision;
                        std::cerr << "[spectra-window] STATE_SNAPSHOT: rev="
                                  << current_revision << " figures="
                                  << figure_cache.size() << "\n";
                        for (const auto& fig : figure_cache)
                        {
                            std::cerr << "  figure id=" << fig.figure_id
                                      << " title=\"" << fig.title << "\""
                                      << " axes=" << fig.axes.size()
                                      << " series=" << fig.series.size() << "\n";
                        }

                        // Send ACK_STATE back to backend
                        spectra::ipc::AckStatePayload ack;
                        ack.revision = current_revision;
                        spectra::ipc::Message ack_msg;
                        ack_msg.header.type = spectra::ipc::MessageType::ACK_STATE;
                        ack_msg.header.session_id = session_id;
                        ack_msg.header.window_id = window_id;
                        ack_msg.payload = spectra::ipc::encode_ack_state(ack);
                        ack_msg.header.payload_len = static_cast<uint32_t>(ack_msg.payload.size());
                        conn->send(ack_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::STATE_DIFF:
                {
                    auto diff = spectra::ipc::decode_state_diff(msg.payload);
                    if (diff)
                    {
                        std::cerr << "[spectra-window] STATE_DIFF: base_rev="
                                  << diff->base_revision << " new_rev="
                                  << diff->new_revision << " ops="
                                  << diff->ops.size() << "\n";

                        // Apply diff ops to local figure cache
                        for (const auto& op : diff->ops)
                        {
                            // Find the target figure in cache
                            for (auto& fig : figure_cache)
                            {
                                if (fig.figure_id != op.figure_id)
                                    continue;

                                switch (op.type)
                                {
                                    case spectra::ipc::DiffOp::Type::SET_AXIS_LIMITS:
                                        if (op.axes_index < fig.axes.size())
                                        {
                                            fig.axes[op.axes_index].x_min = op.f1;
                                            fig.axes[op.axes_index].x_max = op.f2;
                                            fig.axes[op.axes_index].y_min = op.f3;
                                            fig.axes[op.axes_index].y_max = op.f4;
                                        }
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_SERIES_COLOR:
                                        if (op.series_index < fig.series.size())
                                        {
                                            fig.series[op.series_index].color_r = op.f1;
                                            fig.series[op.series_index].color_g = op.f2;
                                            fig.series[op.series_index].color_b = op.f3;
                                            fig.series[op.series_index].color_a = op.f4;
                                        }
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_SERIES_VISIBLE:
                                        if (op.series_index < fig.series.size())
                                            fig.series[op.series_index].visible = op.bool_val;
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_FIGURE_TITLE:
                                        fig.title = op.str_val;
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_GRID_VISIBLE:
                                        if (op.axes_index < fig.axes.size())
                                            fig.axes[op.axes_index].grid_visible = op.bool_val;
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_LINE_WIDTH:
                                        if (op.series_index < fig.series.size())
                                            fig.series[op.series_index].line_width = op.f1;
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_MARKER_SIZE:
                                        if (op.series_index < fig.series.size())
                                            fig.series[op.series_index].marker_size = op.f1;
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_OPACITY:
                                        if (op.series_index < fig.series.size())
                                            fig.series[op.series_index].opacity = op.f1;
                                        break;
                                    case spectra::ipc::DiffOp::Type::SET_SERIES_DATA:
                                        if (op.series_index < fig.series.size())
                                        {
                                            fig.series[op.series_index].data = op.data;
                                            fig.series[op.series_index].point_count =
                                                static_cast<uint32_t>(op.data.size() / 2);
                                        }
                                        break;
                                    default:
                                        break;
                                }
                                break;  // found the figure, stop searching
                            }
                        }

                        current_revision = diff->new_revision;

                        // Send ACK_STATE
                        spectra::ipc::AckStatePayload ack;
                        ack.revision = current_revision;
                        spectra::ipc::Message ack_msg;
                        ack_msg.header.type = spectra::ipc::MessageType::ACK_STATE;
                        ack_msg.header.session_id = session_id;
                        ack_msg.header.window_id = window_id;
                        ack_msg.payload = spectra::ipc::encode_ack_state(ack);
                        ack_msg.header.payload_len = static_cast<uint32_t>(ack_msg.payload.size());
                        conn->send(ack_msg);
                    }
                    break;
                }

                case spectra::ipc::MessageType::RESP_OK:
                case spectra::ipc::MessageType::RESP_ERR:
                    // Response to a request we sent — log and ignore for now
                    break;

                default:
                    std::cerr << "[spectra-window] Unknown message type 0x"
                              << std::hex << static_cast<uint16_t>(msg.header.type)
                              << std::dec << "\n";
                    break;
            }
        }

        // TODO: In full implementation, this is where we'd:
        //   - glfwPollEvents()
        //   - Check for window close → send EVT_WINDOW
        //   - WindowRuntime::update() + render() with assigned_figures
    }

    // --- Clean shutdown: notify backend ---
    std::cerr << "[spectra-window] Shutting down, notifying backend\n";

    spectra::ipc::Message close_msg;
    close_msg.header.type = spectra::ipc::MessageType::EVT_WINDOW;
    close_msg.header.session_id = session_id;
    close_msg.header.window_id = window_id;
    close_msg.header.payload_len = 0;
    conn->send(close_msg);  // best-effort

    conn->close();

    std::cerr << "[spectra-window] Agent stopped\n";
    return 0;
}

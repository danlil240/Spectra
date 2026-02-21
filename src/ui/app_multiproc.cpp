// app_multiproc.cpp — Multi-process run implementation.
//
// Auto-spawns spectra-backend (found next to own binary), connects via IPC,
// pushes all figures, and waits for the agent windows to close.
// Single-terminal UX: just call app.run() — no manual backend startup needed.
//
// This file is only compiled when SPECTRA_RUNTIME_MODE=multiproc (see CMakeLists.txt).

#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../ipc/codec.hpp"
#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef __linux__
    #include <poll.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace spectra
{

// Default socket path used by both the auto-spawned backend and this client.
static constexpr const char* SPECTRA_DEFAULT_SOCK = "/tmp/spectra-auto.sock";

// ─── Find own binary directory ───────────────────────────────────────────────
static std::string self_dir()
{
#ifdef __linux__
    char buf[4096] = {};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
        std::string path(buf, static_cast<size_t>(n));
        auto slash = path.rfind('/');
        if (slash != std::string::npos)
            return path.substr(0, slash + 1);
    }
#endif
    return "";
}

// ─── Spawn spectra-backend and return its PID ────────────────────────────────
static pid_t spawn_backend(const std::string& sock_path)
{
#ifdef __linux__
    std::string dir = self_dir();
    std::string backend_bin = dir + "spectra-backend";
    if (::access(backend_bin.c_str(), X_OK) != 0)
    {
        // Try one directory up (e.g. binary is in build/examples/, backend in build/)
        if (dir.size() > 1)
        {
            auto parent_slash = dir.rfind('/', dir.size() - 2);
            if (parent_slash != std::string::npos)
            {
                std::string parent = dir.substr(0, parent_slash + 1);
                std::string candidate = parent + "spectra-backend";
                if (::access(candidate.c_str(), X_OK) == 0)
                    backend_bin = candidate;
            }
        }
        if (::access(backend_bin.c_str(), X_OK) != 0)
            backend_bin = "spectra-backend";  // fall back to PATH
    }

    pid_t pid = ::fork();
    if (pid == 0)
    {
        // Child: exec the backend
        ::execlp(backend_bin.c_str(), backend_bin.c_str(),
                 "--socket", sock_path.c_str(), nullptr);
        ::_exit(127);
    }
    return pid;
#else
    (void)sock_path;
    return -1;
#endif
}

// ─── Serialize a Figure into a SnapshotFigureState ──────────────────────────
static ipc::SnapshotFigureState figure_to_snapshot(const Figure& fig, uint64_t figure_id)
{
    ipc::SnapshotFigureState snap;
    snap.figure_id = figure_id;
    snap.title = "";
    snap.width = fig.width();
    snap.height = fig.height();
    snap.grid_rows = fig.grid_rows();
    snap.grid_cols = fig.grid_cols();

    for (const auto& ax_ptr : fig.axes())
    {
        if (!ax_ptr) continue;
        const auto& ax = *ax_ptr;
        ipc::SnapshotAxisState sa;
        sa.x_min = ax.x_limits().min;
        sa.x_max = ax.x_limits().max;
        sa.y_min = ax.y_limits().min;
        sa.y_max = ax.y_limits().max;
        sa.grid_visible = ax.grid_enabled();
        sa.x_label = ax.xlabel();
        sa.y_label = ax.ylabel();
        sa.title = ax.title();
        snap.axes.push_back(std::move(sa));

        for (const auto& s_ptr : ax.series())
        {
            if (!s_ptr) continue;
            const auto& s = *s_ptr;
            ipc::SnapshotSeriesState ss;
            ss.name = s.label();
            ss.color_r = s.color().r;
            ss.color_g = s.color().g;
            ss.color_b = s.color().b;
            ss.color_a = s.color().a;
            ss.visible = s.visible();
            ss.opacity = s.opacity();

            if (auto* line = dynamic_cast<const LineSeries*>(&s))
            {
                ss.type = "line";
                ss.line_width = line->width();
                ss.marker_size = s.marker_size();
                auto xd = line->x_data();
                auto yd = line->y_data();
                ss.data.reserve(xd.size() * 2);
                for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                {
                    ss.data.push_back(xd[i]);
                    ss.data.push_back(yd[i]);
                }
                ss.point_count = static_cast<uint32_t>(xd.size());
            }
            else if (auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
            {
                ss.type = "scatter";
                ss.marker_size = scatter->size();
                ss.line_width = 2.0f;
                auto xd = scatter->x_data();
                auto yd = scatter->y_data();
                ss.data.reserve(xd.size() * 2);
                for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                {
                    ss.data.push_back(xd[i]);
                    ss.data.push_back(yd[i]);
                }
                ss.point_count = static_cast<uint32_t>(xd.size());
            }

            snap.series.push_back(std::move(ss));
        }
    }

    return snap;
}

// ─── Send an IPC message helper ─────────────────────────────────────────────
static bool send_msg(ipc::Connection& conn,
                     ipc::MessageType type,
                     ipc::SessionId session_id,
                     ipc::WindowId window_id,
                     std::vector<uint8_t> payload = {})
{
    ipc::Message msg;
    msg.header.type = type;
    msg.header.session_id = session_id;
    msg.header.window_id = window_id;
    msg.payload = std::move(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

void App::run_multiproc()
{
    if (registry_.count() == 0)
    {
        std::cerr << "[spectra] No figures to display\n";
        return;
    }

    // ── Headless mode: render + export locally (no daemon needed) ─────────
    if (config_.headless)
    {
        if (!backend_ || !renderer_)
        {
            std::cerr << "[spectra] Cannot run headless: backend or renderer not initialized\n";
            return;
        }

        for (auto id : registry_.all_ids())
        {
            Figure* fig = registry_.get(id);
            if (!fig) continue;
            fig->compute_layout();

            uint32_t export_w = fig->png_export_width_ > 0 ? fig->png_export_width_ : fig->width();
            uint32_t export_h = fig->png_export_height_ > 0 ? fig->png_export_height_ : fig->height();

            backend_->create_offscreen_framebuffer(export_w, export_h);
            static_cast<VulkanBackend*>(backend_.get())->ensure_pipelines();

            uint32_t orig_w = fig->config_.width;
            uint32_t orig_h = fig->config_.height;
            fig->config_.width = export_w;
            fig->config_.height = export_h;
            fig->compute_layout();

            if (backend_->begin_frame())
            {
                renderer_->render_figure(*fig);
                backend_->end_frame();
            }

            fig->config_.width = orig_w;
            fig->config_.height = orig_h;
            fig->compute_layout();

            if (!fig->png_export_path_.empty())
            {
                std::vector<uint8_t> pixels(static_cast<size_t>(export_w) * export_h * 4);
                if (backend_->readback_framebuffer(pixels.data(), export_w, export_h))
                {
                    if (!ImageExporter::write_png(fig->png_export_path_, pixels.data(), export_w, export_h))
                        std::cerr << "[spectra] Failed to write PNG: " << fig->png_export_path_ << "\n";
                }
                else
                {
                    std::cerr << "[spectra] Failed to readback framebuffer\n";
                }
            }

            if (!fig->svg_export_path_.empty())
            {
                if (!SvgExporter::write_svg(fig->svg_export_path_, *fig))
                    std::cerr << "[spectra] Failed to write SVG: " << fig->svg_export_path_ << "\n";
            }
        }

        if (backend_) backend_->wait_idle();
        return;
    }

    // Use a per-process unique socket so each app run gets its own backend.
    // This prevents stale backends from previous runs accumulating agents.
    const std::string sock = "/tmp/spectra-" + std::to_string(::getpid()) + ".sock";

    std::unique_ptr<ipc::Connection> conn;

    // Always spawn a fresh backend for this process.
    {
        pid_t backend_pid = spawn_backend(sock);
        if (backend_pid <= 0)
        {
            std::cerr << "[spectra] Failed to spawn spectra-backend\n";
            return;
        }

        // Retry connection with backoff (backend needs a moment to start)
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            conn = ipc::Client::connect(sock);
            if (conn && conn->is_open())
                break;
        }

        if (!conn || !conn->is_open())
        {
            std::cerr << "[spectra] Timed out waiting for spectra-backend to start\n";
#ifdef __linux__
            ::kill(backend_pid, SIGTERM);
#endif
            return;
        }
    }

    // Send HELLO
    ipc::HelloPayload hello;
    hello.agent_build = "spectra-app/0.1.0";
    send_msg(*conn, ipc::MessageType::HELLO, 0, 0, ipc::encode_hello(hello));

    // Wait for WELCOME
    ipc::SessionId session_id = 0;
    ipc::WindowId window_id = 0;
    {
        auto welcome_msg = conn->recv();
        if (!welcome_msg || welcome_msg->header.type != ipc::MessageType::WELCOME)
        {
            std::cerr << "[spectra] Did not receive WELCOME from backend\n";
            return;
        }
        auto wp = ipc::decode_welcome(welcome_msg->payload);
        if (!wp)
        {
            std::cerr << "[spectra] Failed to decode WELCOME\n";
            return;
        }
        session_id = wp->session_id;
        window_id = wp->window_id;
    }

    // Drain any initial messages (CMD_ASSIGN_FIGURES, STATE_SNAPSHOT for default figure)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < deadline)
        {
#ifdef __linux__
            struct pollfd pfd;
            pfd.fd = conn->fd();
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (::poll(&pfd, 1, 50) <= 0 || !(pfd.revents & POLLIN))
                break;
#endif
            auto msg_opt = conn->recv();
            if (!msg_opt) break;
        }
    }

    // Serialize and push all figures as a STATE_SNAPSHOT.
    // Assign window_group so the daemon groups sibling figures into one agent.
    ipc::StateSnapshotPayload snap;
    snap.revision = 1;
    snap.session_id = session_id;

    auto window_groups = compute_window_groups();

    // Map registry FigureId → IPC figure_id (starting at 100)
    std::unordered_map<FigureId, uint64_t> reg_to_ipc;
    uint64_t fig_counter = 100;
    for (auto id : registry_.all_ids())
        reg_to_ipc[id] = fig_counter++;

    fig_counter = 100;
    for (auto id : registry_.all_ids())
    {
        Figure* fig = registry_.get(id);
        if (!fig) continue;
        fig->compute_layout();
        auto fig_snap = figure_to_snapshot(*fig, reg_to_ipc[id]);
        fig_snap.title = "Figure " + std::to_string(reg_to_ipc[id] - 99);
        snap.figures.push_back(std::move(fig_snap));
    }

    // Assign window_group: figures in the same group get the same non-zero ID
    for (uint32_t gi = 0; gi < window_groups.size(); ++gi)
    {
        uint32_t group_id = gi + 1;  // 1-based group IDs
        for (auto reg_id : window_groups[gi])
        {
            uint64_t ipc_id = reg_to_ipc[reg_id];
            for (auto& fs : snap.figures)
            {
                if (fs.figure_id == ipc_id)
                {
                    fs.window_group = group_id;
                    break;
                }
            }
        }
    }

    send_msg(*conn, ipc::MessageType::STATE_SNAPSHOT,
             session_id, window_id,
             ipc::encode_state_snapshot(snap));

    // The daemon spawns one agent per figure automatically when it
    // receives the STATE_SNAPSHOT.  No need to send REQ_CREATE_WINDOW.

    bool has_any_animation = false;
    float max_fps = 60.0f;
    for (auto id : registry_.all_ids())
    {
        Figure* fig = registry_.get(id);
        if (fig && fig->has_animation())
        {
            has_any_animation = true;
            if (fig->anim_fps() > max_fps) max_fps = fig->anim_fps();
        }
    }

    std::unique_ptr<FrameScheduler> scheduler;
    if (has_any_animation)
    {
        scheduler = std::make_unique<FrameScheduler>(max_fps);
    }

    // Wait until all agent windows are closed (backend sends CMD_CLOSE_WINDOW or drops connection)
    auto last_heartbeat = std::chrono::steady_clock::now();
    static constexpr auto HEARTBEAT_INTERVAL = std::chrono::milliseconds(5000);
    while (true)
    {
        if (scheduler)
            scheduler->begin_frame();

#ifdef __linux__
        struct pollfd pfd;
        pfd.fd = conn->fd();
        pfd.events = POLLIN;
        
        int timeout_ms = scheduler ? 0 : 1000;
        bool exit_requested = false;
        
        while (true)
        {
            pfd.revents = 0;
            int pr = ::poll(&pfd, 1, timeout_ms);
            if (pr < 0) { exit_requested = true; break; }
            if (pr == 0) break; // timeout
            
            if (pfd.revents & (POLLHUP | POLLERR)) { exit_requested = true; break; }
            if (pfd.revents & POLLIN)
            {
                auto msg = conn->recv();
                if (!msg) { exit_requested = true; break; }
                if (msg->header.type == ipc::MessageType::CMD_CLOSE_WINDOW) { exit_requested = true; break; }
            }
            else {
                break;
            }
            timeout_ms = 0; // only block on the first iteration
        }
        if (exit_requested) break;
#else
        if (!scheduler)
        {
            auto msg = conn->recv();
            if (!msg) break;
            if (msg->header.type == ipc::MessageType::CMD_CLOSE_WINDOW) break;
        }
#endif

        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL)
        {
            if (!send_msg(*conn, ipc::MessageType::EVT_HEARTBEAT,
                          session_id, window_id, {}))
                break;
            last_heartbeat = now;
        }

        if (scheduler)
        {
            Frame frame = scheduler->current_frame();
            ipc::StateDiffPayload diff;
            
            for (auto id : registry_.all_ids())
            {
                Figure* fig = registry_.get(id);
                if (fig && fig->has_animation())
                {
                    if (fig->anim_on_frame_)
                    {
                        fig->anim_on_frame_(frame);
                    }
                }

                if (fig)
                {
                    uint32_t axes_idx = 0;
                    for (const auto& ax_ptr : fig->axes())
                    {
                        if (!ax_ptr) { axes_idx++; continue; }
                        uint32_t series_idx = 0;
                        for (const auto& s_ptr : ax_ptr->series())
                        {
                            if (s_ptr && s_ptr->is_dirty())
                            {
                                ipc::DiffOp op;
                                op.type = ipc::DiffOp::Type::SET_SERIES_DATA;
                                op.figure_id = reg_to_ipc[id];
                                op.axes_index = axes_idx;
                                op.series_index = series_idx;

                                if (auto* line = dynamic_cast<const LineSeries*>(s_ptr.get()))
                                {
                                    auto xd = line->x_data();
                                    auto yd = line->y_data();
                                    op.data.reserve(xd.size() * 2);
                                    for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                                    {
                                        op.data.push_back(xd[i]);
                                        op.data.push_back(yd[i]);
                                    }
                                }
                                else if (auto* scatter = dynamic_cast<const ScatterSeries*>(s_ptr.get()))
                                {
                                    auto xd = scatter->x_data();
                                    auto yd = scatter->y_data();
                                    op.data.reserve(xd.size() * 2);
                                    for (size_t i = 0; i < xd.size() && i < yd.size(); ++i)
                                    {
                                        op.data.push_back(xd[i]);
                                        op.data.push_back(yd[i]);
                                    }
                                }

                                diff.ops.push_back(std::move(op));
                                const_cast<Series*>(s_ptr.get())->clear_dirty();
                            }
                            series_idx++;
                        }
                        axes_idx++;
                    }
                }
            }

            if (!diff.ops.empty())
            {
                send_msg(*conn, ipc::MessageType::STATE_DIFF, session_id, window_id, ipc::encode_state_diff(diff));
            }

            scheduler->end_frame();
        }
    }

    // Notify backend we are done — it will kill all agents and exit
    ipc::ReqCloseWindowPayload close_req;
    close_req.window_id = window_id;
    close_req.reason = "app_exit";
    send_msg(*conn, ipc::MessageType::REQ_CLOSE_WINDOW,
             session_id, window_id,
             ipc::encode_req_close_window(close_req));
    conn->close();
}

}  // namespace spectra

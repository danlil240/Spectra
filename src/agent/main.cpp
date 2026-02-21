// spectra-window — Multi-process window agent.
//
// Uses the EXACT SAME UI stack as the in-process runtime (WindowManager,
// SessionRuntime, WindowRuntime, WindowUIContext, ImGui, full command set).
// Figures are populated from IPC snapshots instead of user code — that is
// the ONLY difference from app_inproc.  One line in CMake controls which
// mode is used.

#include <spectra/animator.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include "../anim/frame_scheduler.hpp"
#include "../ipc/codec.hpp"
#include "../ipc/message.hpp"
#include "../ipc/transport.hpp"
#include "../render/renderer.hpp"
#include "../render/vulkan/vk_backend.hpp"
#include "../ui/command_queue.hpp"
#include "../ui/figure_registry.hpp"
#include "../ui/knob_manager.hpp"
#include "../ui/register_commands.hpp"
#include "../ui/session_runtime.hpp"
#include "../ui/window_runtime.hpp"
#include "../ui/window_ui_context.hpp"

#ifdef SPECTRA_USE_GLFW
    #define GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

    #include "../ui/glfw_adapter.hpp"
    #include "../ui/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>

#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
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

// Helper: check if a series type string is a 3D type.
static bool is_3d_series_type(const std::string& t)
{
    return t == "line3d" || t == "scatter3d" || t == "surface" || t == "mesh";
}

// ─── Build a real Figure from a SnapshotFigureState ──────────────────────────
std::unique_ptr<spectra::Figure> build_figure_from_snapshot(
    const spectra::ipc::SnapshotFigureState& snap,
    uint32_t override_width = 0,
    uint32_t override_height = 0)
{
    spectra::FigureConfig cfg;
    cfg.width = (override_width > 0) ? override_width : snap.width;
    cfg.height = (override_height > 0) ? override_height : snap.height;
    auto fig = std::make_unique<spectra::Figure>(cfg);

    int rows = std::max(snap.grid_rows, int32_t(1));
    int cols = std::max(snap.grid_cols, int32_t(1));

    size_t num_axes = std::max(snap.axes.size(), size_t(1));
    for (size_t i = 0; i < num_axes; ++i)
    {
        bool axes_is_3d = (i < snap.axes.size()) && snap.axes[i].is_3d;

        if (axes_is_3d)
        {
            auto& ax3d = fig->subplot3d(rows, cols, static_cast<int>(i + 1));
            const auto& sa = snap.axes[i];
            ax3d.xlim(sa.x_min, sa.x_max);
            ax3d.ylim(sa.y_min, sa.y_max);
            ax3d.zlim(sa.z_min, sa.z_max);
            ax3d.grid(sa.grid_visible);
            if (!sa.x_label.empty())
                ax3d.xlabel(sa.x_label);
            if (!sa.y_label.empty())
                ax3d.ylabel(sa.y_label);
            if (!sa.title.empty())
                ax3d.title(sa.title);

            // Add 3D series to this axes
            for (const auto& ss : snap.series)
            {
                if (!is_3d_series_type(ss.type))
                    continue;

                // Unpack XYZ stride-3 data
                std::vector<float> xs, ys, zs;
                for (size_t j = 0; j + 2 < ss.data.size(); j += 3)
                {
                    xs.push_back(ss.data[j]);
                    ys.push_back(ss.data[j + 1]);
                    zs.push_back(ss.data[j + 2]);
                }

                if (ss.type == "scatter3d")
                {
                    auto& s = ax3d.scatter3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.size(ss.marker_size);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else if (ss.type == "surface")
                {
                    // SurfaceSeries expects 1D grid vectors (unique sorted X, Y)
                    // plus a rows*cols Z array.  The IPC data is raveled meshgrid
                    // (all X, all Y, all Z each of length rows*cols).
                    // Reconstruct 1D grids by extracting unique sorted values.
                    std::vector<float> ux(xs.begin(), xs.end());
                    std::vector<float> uy(ys.begin(), ys.end());
                    std::sort(ux.begin(), ux.end());
                    ux.erase(std::unique(ux.begin(), ux.end(),
                        [](float a, float b){ return std::abs(a - b) < 1e-6f; }), ux.end());
                    std::sort(uy.begin(), uy.end());
                    uy.erase(std::unique(uy.begin(), uy.end(),
                        [](float a, float b){ return std::abs(a - b) < 1e-6f; }), uy.end());

                    // Reorder Z into row-major (y-row, x-col) order expected by SurfaceSeries
                    size_t ncols = ux.size();
                    size_t nrows = uy.size();
                    std::vector<float> z_grid(nrows * ncols, 0.0f);
                    for (size_t k = 0; k < xs.size(); ++k)
                    {
                        // Find column index for xs[k]
                        auto cit = std::lower_bound(ux.begin(), ux.end(), xs[k] - 1e-6f);
                        size_t ci = static_cast<size_t>(std::distance(ux.begin(), cit));
                        if (ci >= ncols) ci = ncols - 1;
                        // Find row index for ys[k]
                        auto rit = std::lower_bound(uy.begin(), uy.end(), ys[k] - 1e-6f);
                        size_t ri = static_cast<size_t>(std::distance(uy.begin(), rit));
                        if (ri >= nrows) ri = nrows - 1;
                        z_grid[ri * ncols + ci] = zs[k];
                    }

                    auto& s = ax3d.surface(ux, uy, z_grid);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else if (ss.type == "mesh")
                {
                    // mesh expects vertices + indices; for now treat as line3d
                    auto& s = ax3d.line3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else  // "line3d"
                {
                    auto& s = ax3d.line3d(xs, ys, zs);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
            }
        }
        else
        {
            auto& ax = fig->subplot(rows, cols, static_cast<int>(i + 1));
            if (i < snap.axes.size())
            {
                const auto& sa = snap.axes[i];
                ax.xlim(sa.x_min, sa.x_max);
                ax.ylim(sa.y_min, sa.y_max);
                ax.grid(sa.grid_visible);
                if (!sa.x_label.empty())
                    ax.xlabel(sa.x_label);
                if (!sa.y_label.empty())
                    ax.ylabel(sa.y_label);
                if (!sa.title.empty())
                    ax.title(sa.title);
            }

            // Add 2D series to this axes
            for (const auto& ss : snap.series)
            {
                if (is_3d_series_type(ss.type))
                    continue;

                std::vector<float> xs, ys;
                for (size_t j = 0; j + 1 < ss.data.size(); j += 2)
                {
                    xs.push_back(ss.data[j]);
                    ys.push_back(ss.data[j + 1]);
                }
                if (ss.type == "scatter")
                {
                    auto& s = ax.scatter(xs, ys);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.size(ss.marker_size);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
                else
                {
                    auto& s = ax.line(xs, ys);
                    s.color({ss.color_r, ss.color_g, ss.color_b, ss.color_a});
                    s.visible(ss.visible);
                    s.opacity(ss.opacity);
                    s.width(ss.line_width);
                    if (!ss.name.empty())
                        s.label(ss.name);
                }
            }
        }
    }

    return fig;
}

// ─── Apply a DiffOp to a cached SnapshotFigureState ─────────────────────────
void apply_diff_op_to_cache(spectra::ipc::SnapshotFigureState& fig, const spectra::ipc::DiffOp& op)
{
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
                fig.series[op.series_index].point_count = static_cast<uint32_t>(op.data.size() / 2);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes.size())
            {
                fig.axes[op.axes_index].z_min = op.f1;
                fig.axes[op.axes_index].z_max = op.f2;
            }
            break;
        case spectra::ipc::DiffOp::Type::ADD_SERIES:
        {
            spectra::ipc::SnapshotSeriesState s;
            s.type = op.str_val;
            // Grow series list to accommodate the new index
            while (fig.series.size() <= op.series_index)
                fig.series.push_back({});
            fig.series[op.series_index] = std::move(s);
            break;
        }
        case spectra::ipc::DiffOp::Type::ADD_AXES:
        {
            spectra::ipc::SnapshotAxisState ax;
            ax.is_3d = op.bool_val;
            // Grow axes list to accommodate the new index
            while (fig.axes.size() <= op.axes_index)
                fig.axes.push_back({});
            fig.axes[op.axes_index] = std::move(ax);
            break;
        }
        default:
            break;
    }
}

// ─── Apply a DiffOp directly to a live Figure object ─────────────────────────
void apply_diff_op_to_figure(spectra::Figure& fig, const spectra::ipc::DiffOp& op)
{
    switch (op.type)
    {
        case spectra::ipc::DiffOp::Type::SET_AXIS_LIMITS:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                fig.axes_mut()[op.axes_index]->xlim(op.f1, op.f2);
                fig.axes_mut()[op.axes_index]->ylim(op.f3, op.f4);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_GRID_VISIBLE:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                fig.axes_mut()[op.axes_index]->grid(op.bool_val);
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_AXIS_ZLIMITS:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto* ax3d = dynamic_cast<spectra::Axes3D*>(fig.axes_mut()[op.axes_index].get());
                if (ax3d)
                    ax3d->zlim(op.f1, op.f2);
            }
            break;
        case spectra::ipc::DiffOp::Type::ADD_SERIES:
            // Series will be populated by the subsequent SET_SERIES_DATA diff.
            // Add a placeholder so the series_index slot exists in the live figure.
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto* ax = fig.axes_mut()[op.axes_index].get();
                auto* ax3d = dynamic_cast<spectra::Axes3D*>(ax);
                if (ax3d)
                {
                    if (op.str_val == "scatter3d")
                        ax3d->scatter3d({}, {}, {});
                    else if (op.str_val == "surface")
                        ax3d->surface({}, {}, {});
                    else
                        ax3d->line3d({}, {}, {});
                }
                else
                {
                    if (op.str_val == "scatter")
                        ax->scatter({}, {});
                    else
                        ax->line({}, {});
                }
            }
            break;
        case spectra::ipc::DiffOp::Type::SET_SERIES_DATA:
            if (op.axes_index < fig.axes().size() && fig.axes()[op.axes_index])
            {
                auto& series_vec = fig.axes_mut()[op.axes_index]->series_mut();
                if (op.series_index < series_vec.size() && series_vec[op.series_index])
                {
                    // Deinterleave [x0,y0,x1,y1,...] into separate x/y vectors
                    size_t n = op.data.size() / 2;
                    std::vector<float> xv(n), yv(n);
                    for (size_t i = 0; i < n; ++i)
                    {
                        xv[i] = op.data[i * 2];
                        yv[i] = op.data[i * 2 + 1];
                    }
                    auto* s = series_vec[op.series_index].get();
                    if (auto* line = dynamic_cast<spectra::LineSeries*>(s))
                    {
                        line->set_x(xv);
                        line->set_y(yv);
                    }
                    else if (auto* scatter = dynamic_cast<spectra::ScatterSeries*>(s))
                    {
                        scatter->set_x(xv);
                        scatter->set_y(yv);
                    }
                }
            }
            break;
        default:
            break;
    }
}

// ─── Send an IPC message helper ──────────────────────────────────────────────
bool send_ipc(spectra::ipc::Connection& conn,
              spectra::ipc::MessageType type,
              spectra::ipc::SessionId session_id,
              spectra::ipc::WindowId window_id,
              std::vector<uint8_t> payload = {})
{
    spectra::ipc::Message msg;
    msg.header.type = type;
    msg.header.session_id = session_id;
    msg.header.window_id = window_id;
    msg.payload = std::move(payload);
    msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
    return conn.send(msg);
}

// ─── Rebuild FigureRegistry from IPC cache ───────────────────────────────────
// Re-creates Figure objects from snapshot cache and registers them.
// Returns the list of new FigureId values.
std::vector<spectra::FigureId> rebuild_registry_from_cache(
    spectra::FigureRegistry& registry,
    const std::vector<spectra::ipc::SnapshotFigureState>& cache,
    uint32_t width,
    uint32_t height)
{
    // Clear existing figures
    for (auto id : registry.all_ids())
        registry.unregister_figure(id);

    std::vector<spectra::FigureId> ids;
    for (const auto& snap : cache)
    {
        auto fig = build_figure_from_snapshot(snap, width, height);
        auto id = registry.register_figure(std::move(fig));
        ids.push_back(id);
    }
    return ids;
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

    // Initialize logger
    auto& logger = spectra::Logger::instance();
    logger.set_level(spectra::LogLevel::Debug);
    logger.add_sink(spectra::sinks::console_sink());
    try
    {
        std::string log_path = std::filesystem::temp_directory_path() / "spectra_agent.log";
        logger.add_sink(spectra::sinks::file_sink(log_path));
    }
    catch (...)
    {
    }

    std::cerr << "[spectra-window] Connecting to backend: " << socket_path << "\n";

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 1: IPC connection + handshake
    // ═══════════════════════════════════════════════════════════════════════

    auto conn = spectra::ipc::Client::connect(socket_path);
    if (!conn)
    {
        std::cerr << "[spectra-window] Failed to connect to " << socket_path << "\n";
        return 1;
    }

    std::cerr << "[spectra-window] Connected (fd=" << conn->fd() << ")\n";

    // Send HELLO
    spectra::ipc::HelloPayload hello;
    hello.protocol_major = spectra::ipc::PROTOCOL_MAJOR;
    hello.protocol_minor = spectra::ipc::PROTOCOL_MINOR;
    hello.agent_build = "spectra-window/0.1.0";
    hello.capabilities = 0;
    {
        spectra::ipc::Message hello_msg;
        hello_msg.header.type = spectra::ipc::MessageType::HELLO;
        hello_msg.payload = spectra::ipc::encode_hello(hello);
        hello_msg.header.payload_len = static_cast<uint32_t>(hello_msg.payload.size());
        if (!conn->send(hello_msg))
        {
            std::cerr << "[spectra-window] Failed to send HELLO\n";
            return 1;
        }
    }

    // Receive WELCOME
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
    spectra::ipc::WindowId ipc_window_id = welcome->window_id;
    uint32_t heartbeat_ms = welcome->heartbeat_ms;

    std::cerr << "[spectra-window] WELCOME: session=" << session_id << " window=" << ipc_window_id
              << " heartbeat=" << heartbeat_ms << "ms\n";

    // Track IPC state
    std::vector<uint64_t> assigned_figures;
    uint64_t ipc_active_figure_id = 0;
    std::vector<spectra::ipc::SnapshotFigureState> figure_cache;
    std::vector<spectra::ipc::SnapshotKnobState> knob_cache;
    spectra::ipc::Revision current_revision = 0;
    bool cache_dirty = false;

    // Drain initial messages (CMD_ASSIGN_FIGURES + STATE_SNAPSHOT)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool got_snapshot = false;
        while (!got_snapshot && std::chrono::steady_clock::now() < deadline)
        {
#ifdef __linux__
            struct pollfd pfd;
            pfd.fd = conn->fd();
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (::poll(&pfd, 1, 100) <= 0 || !(pfd.revents & POLLIN))
                continue;
#endif
            auto msg_opt = conn->recv();
            if (!msg_opt)
                break;
            auto& msg = *msg_opt;
            if (msg.header.type == spectra::ipc::MessageType::CMD_ASSIGN_FIGURES)
            {
                auto payload = spectra::ipc::decode_cmd_assign_figures(msg.payload);
                if (payload)
                {
                    assigned_figures = payload->figure_ids;
                    ipc_active_figure_id = payload->active_figure_id;
                }
            }
            else if (msg.header.type == spectra::ipc::MessageType::STATE_SNAPSHOT)
            {
                auto snap = spectra::ipc::decode_state_snapshot(msg.payload);
                if (snap)
                {
                    figure_cache = snap->figures;
                    knob_cache = snap->knobs;
                    current_revision = snap->revision;
                    cache_dirty = true;
                    got_snapshot = true;

                    spectra::ipc::AckStatePayload ack;
                    ack.revision = current_revision;
                    send_ipc(*conn,
                             spectra::ipc::MessageType::ACK_STATE,
                             session_id,
                             ipc_window_id,
                             spectra::ipc::encode_ack_state(ack));

                    std::cerr << "[spectra-window] STATE_SNAPSHOT (init): rev=" << current_revision
                              << " figures=" << figure_cache.size() << "\n";
                }
            }
        }
        if (!got_snapshot)
            std::cerr << "[spectra-window] Warning: no STATE_SNAPSHOT received\n";
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 2: Build figures into FigureRegistry
    // ═══════════════════════════════════════════════════════════════════════

    constexpr uint32_t INITIAL_WIDTH = 1280;
    constexpr uint32_t INITIAL_HEIGHT = 720;

    spectra::FigureRegistry registry;
    auto all_ids =
        rebuild_registry_from_cache(registry, figure_cache, INITIAL_WIDTH, INITIAL_HEIGHT);
    cache_dirty = false;

    if (registry.count() == 0)
    {
        std::cerr << "[spectra-window] No figures received from backend, exiting\n";
        conn->close();
        return 0;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 3: Initialize GPU + WindowManager + SessionRuntime
    //          (identical to App::run_inproc)
    // ═══════════════════════════════════════════════════════════════════════

    spectra::FrameState frame_state;
    {
        // Pick the correct initial active figure based on IPC assignment
        spectra::FigureId initial_active = all_ids[0];
        if (ipc_active_figure_id != 0)
        {
            for (size_t i = 0; i < assigned_figures.size() && i < all_ids.size(); ++i)
            {
                if (assigned_figures[i] == ipc_active_figure_id)
                {
                    initial_active = all_ids[i];
                    break;
                }
            }
        }
        frame_state.active_figure_id = initial_active;
    }
    frame_state.active_figure = registry.get(frame_state.active_figure_id);
    spectra::Figure* active_figure = frame_state.active_figure;
    spectra::FigureId& active_figure_id = frame_state.active_figure_id;

    auto backend = std::make_unique<spectra::VulkanBackend>();
    if (!backend->init(false))
    {
        std::cerr << "[spectra-window] Failed to initialize Vulkan backend\n";
        return 1;
    }

    auto renderer_ptr = std::make_unique<spectra::Renderer>(*backend);
    if (!renderer_ptr->init())
    {
        std::cerr << "[spectra-window] Failed to initialize renderer\n";
        return 1;
    }

    spectra::CommandQueue cmd_queue;
    spectra::FrameScheduler scheduler(active_figure->anim_fps());
    // Windowed agent uses VK_PRESENT_MODE_FIFO_KHR (VSync) — don't
    // double-pace with FrameScheduler sleep on top.
    scheduler.set_mode(spectra::FrameScheduler::Mode::VSync);
    spectra::Animator animator;
    spectra::SessionRuntime session(*backend, *renderer_ptr, registry);

    frame_state.has_animation = active_figure->has_animation();

    spectra::WindowUIContext* ui_ctx_ptr = nullptr;

#ifdef SPECTRA_USE_GLFW
    std::unique_ptr<spectra::GlfwAdapter> glfw;
    std::unique_ptr<spectra::WindowManager> window_mgr;

    glfw = std::make_unique<spectra::GlfwAdapter>();
    if (!glfw->init(active_figure->width(), active_figure->height(), "Spectra"))
    {
        std::cerr << "[spectra-window] Failed to create GLFW window\n";
        return 1;
    }

    backend->create_surface(glfw->native_window());
    backend->create_swapchain(active_figure->width(), active_figure->height());

    window_mgr = std::make_unique<spectra::WindowManager>();
    window_mgr->init(
        static_cast<spectra::VulkanBackend*>(backend.get()), &registry, renderer_ptr.get());

    // Set tab drag handlers BEFORE creating windows so all windows get them
    window_mgr->set_tab_detach_handler(
        [&session](
            spectra::FigureId fid, uint32_t w, uint32_t h, const std::string& title, int sx, int sy)
        {
            session.queue_detach({fid, w, h, title, sx, sy});
        });
    window_mgr->set_tab_move_handler(
        [&session](
            spectra::FigureId fid, uint32_t target_wid, int drop_zone, float local_x, float local_y,
            spectra::FigureId target_figure_id)
        {
            session.queue_move({fid, target_wid, drop_zone, local_x, local_y, target_figure_id});
        });

    auto* initial_wctx = window_mgr->create_first_window_with_ui(glfw->native_window(), all_ids);

    if (initial_wctx && initial_wctx->ui_ctx)
    {
        ui_ctx_ptr = initial_wctx->ui_ctx.get();

        // Set tab titles from snapshot cache (so tabs show "Figure 1", "Figure 2", etc.
        // instead of FigureId-based defaults like "Figure 2", "Figure 3", "Figure 4")
        if (ui_ctx_ptr->fig_mgr)
        {
            for (size_t fi = 0; fi < all_ids.size() && fi < figure_cache.size(); ++fi)
            {
                if (!figure_cache[fi].title.empty())
                    ui_ctx_ptr->fig_mgr->set_title(all_ids[fi], figure_cache[fi].title);
            }
            // Switch to the correct initial active figure
            ui_ctx_ptr->fig_mgr->switch_to(frame_state.active_figure_id);
        }

        // Sync WindowContext active figure
        initial_wctx->active_figure_id = frame_state.active_figure_id;

        // Reconstruct knobs from IPC cache into the window's KnobManager
        if (!knob_cache.empty())
        {
            auto& km = ui_ctx_ptr->knob_manager;
            for (const auto& ks : knob_cache)
            {
                switch (ks.type)
                {
                    case 0:  // Float
                        km.add_float(ks.name, ks.value, ks.min_val, ks.max_val, ks.step);
                        break;
                    case 1:  // Int
                        km.add_int(ks.name,
                                   static_cast<int>(ks.value),
                                   static_cast<int>(ks.min_val),
                                   static_cast<int>(ks.max_val));
                        break;
                    case 2:  // Bool
                        km.add_bool(ks.name, ks.value >= 0.5f);
                        break;
                    case 3:  // Choice
                        km.add_choice(ks.name, ks.choices, static_cast<int>(ks.value));
                        break;
                }
            }
        }
    }
#endif

    // Headless fallback
    std::unique_ptr<spectra::WindowUIContext> headless_ui_ctx;
    if (!ui_ctx_ptr)
    {
        headless_ui_ctx = std::make_unique<spectra::WindowUIContext>();
        headless_ui_ctx->fig_mgr_owned = std::make_unique<spectra::FigureManager>(registry);
        headless_ui_ctx->fig_mgr = headless_ui_ctx->fig_mgr_owned.get();
        ui_ctx_ptr = headless_ui_ctx.get();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 4: Wire UI subsystems + register commands
    //          (identical to App::run_inproc)
    // ═══════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_USE_IMGUI
    auto& imgui_ui = ui_ctx_ptr->imgui_ui;
    auto& figure_tabs = ui_ctx_ptr->figure_tabs;
    auto& dock_system = ui_ctx_ptr->dock_system;
    auto& timeline_editor = ui_ctx_ptr->timeline_editor;
    auto& keyframe_interpolator = ui_ctx_ptr->keyframe_interpolator;
    auto& curve_editor = ui_ctx_ptr->curve_editor;
    auto& shortcut_mgr = ui_ctx_ptr->shortcut_mgr;
    auto& cmd_palette = ui_ctx_ptr->cmd_palette;
    auto& cmd_registry = ui_ctx_ptr->cmd_registry;
    auto& tab_drag_controller = ui_ctx_ptr->tab_drag_controller;
    auto& fig_mgr = *ui_ctx_ptr->fig_mgr;
    auto& input_handler = ui_ctx_ptr->input_handler;
    auto& home_limits = ui_ctx_ptr->home_limits;

    // Sync timeline with figure animation settings
    timeline_editor.set_interpolator(&keyframe_interpolator);
    curve_editor.set_interpolator(&keyframe_interpolator);
    if (active_figure->anim_duration() > 0.0f)
        timeline_editor.set_duration(active_figure->anim_duration());
    else if (frame_state.has_animation)
        timeline_editor.set_duration(60.0f);
    if (active_figure->anim_loop())
        timeline_editor.set_loop_mode(spectra::LoopMode::Loop);
    if (active_figure->anim_fps() > 0.0f)
        timeline_editor.set_fps(active_figure->anim_fps());
    if (frame_state.has_animation)
        timeline_editor.play();

    shortcut_mgr.set_command_registry(&cmd_registry);
    shortcut_mgr.register_defaults();
    cmd_palette.set_command_registry(&cmd_registry);
    cmd_palette.set_shortcut_manager(&shortcut_mgr);

    #ifdef SPECTRA_USE_GLFW
    if (window_mgr)
    {
        tab_drag_controller.set_window_manager(window_mgr.get());
        input_handler.set_figure(active_figure);
        if (!active_figure->axes().empty() && active_figure->axes()[0])
        {
            input_handler.set_active_axes(active_figure->axes()[0].get());
            auto& vp = active_figure->axes()[0]->viewport();
            input_handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
        }
    }
    #endif

    // Tab/pane detach callbacks — forward to session.queue_detach()
    if (figure_tabs)
    {
        figure_tabs->set_tab_split_right_callback(
            [&dock_system, &fig_mgr](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                spectra::FigureId id = fig_mgr.figure_ids()[pos];
                spectra::FigureId new_fig = fig_mgr.duplicate_figure(id);
                if (new_fig == spectra::INVALID_FIGURE_ID)
                    return;
                dock_system.split_figure_right(id, new_fig);
                dock_system.set_active_figure_index(id);
            });
        figure_tabs->set_tab_split_down_callback(
            [&dock_system, &fig_mgr](size_t pos)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                spectra::FigureId id = fig_mgr.figure_ids()[pos];
                spectra::FigureId new_fig = fig_mgr.duplicate_figure(id);
                if (new_fig == spectra::INVALID_FIGURE_ID)
                    return;
                dock_system.split_figure_down(id, new_fig);
                dock_system.set_active_figure_index(id);
            });
        figure_tabs->set_tab_detach_callback(
            [&fig_mgr, &session, &registry](size_t pos, float screen_x, float screen_y)
            {
                if (pos >= fig_mgr.figure_ids().size())
                    return;
                spectra::FigureId id = fig_mgr.figure_ids()[pos];
                auto* fig = registry.get(id);
                if (!fig || fig_mgr.count() <= 1)
                    return;
                uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(id);
                session.queue_detach({id,
                                      win_w,
                                      win_h,
                                      title,
                                      static_cast<int>(screen_x),
                                      static_cast<int>(screen_y)});
            });
    }

    if (ui_ctx_ptr)
    {
        tab_drag_controller.set_on_drop_outside(
            [&fig_mgr, &session, &registry](spectra::FigureId index, float screen_x, float screen_y)
            {
                auto* fig = registry.get(index);
                if (!fig)
                    return;
                uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                std::string title = fig_mgr.get_title(index);
                session.queue_detach({index,
                                      win_w,
                                      win_h,
                                      title,
                                      static_cast<int>(screen_x),
                                      static_cast<int>(screen_y)});
            });

        tab_drag_controller.set_on_drop_on_window(
            [&session, &window_mgr](spectra::FigureId index,
                                    uint32_t target_window_id,
                                    float /*screen_x*/,
                                    float /*screen_y*/)
            {
                int zone = 0;
                float lx = 0.0f, ly = 0.0f;
                if (window_mgr)
                {
                    auto info = window_mgr->cross_window_drop_info();
                    zone = info.zone;
                    lx = info.hx;
                    ly = info.hy;
                }
                session.queue_move({index, target_window_id, zone, lx, ly});
            });

        if (imgui_ui)
        {
            imgui_ui->set_pane_tab_detach_cb(
                [&fig_mgr, &session, &registry](
                    spectra::FigureId index, float screen_x, float screen_y)
                {
                    auto* fig = registry.get(index);
                    if (!fig)
                        return;
                    uint32_t win_w = fig->width() > 0 ? fig->width() : 800;
                    uint32_t win_h = fig->height() > 0 ? fig->height() : 600;
                    std::string title = fig_mgr.get_title(index);
                    session.queue_detach({index,
                                          win_w,
                                          win_h,
                                          title,
                                          static_cast<int>(screen_x),
                                          static_cast<int>(screen_y)});
                });
        }

        cmd_palette.set_body_font(nullptr);
        cmd_palette.set_heading_font(nullptr);

        // Register ALL standard commands (same as inproc)
        spectra::CommandBindings cb;
        cb.ui_ctx = ui_ctx_ptr;
        cb.registry = &registry;
        cb.active_figure = &active_figure;
        cb.active_figure_id = &active_figure_id;
        cb.session = &session;
    #ifdef SPECTRA_USE_GLFW
        cb.window_mgr = window_mgr.get();
    #endif
        spectra::register_standard_commands(cb);
    }
#endif

    scheduler.reset();

    // Capture initial axes limits for Home button
    for (auto id : registry.all_ids())
    {
        spectra::Figure* fig_ptr = registry.get(id);
        if (!fig_ptr)
            continue;
        for (auto& ax : fig_ptr->axes_mut())
        {
            if (ax)
                home_limits[ax.get()] = {ax->x_limits(), ax->y_limits()};
        }
    }

    std::cerr << "[spectra-window] Full UI initialized, entering main loop\n";

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 5: Main loop — SessionRuntime + IPC polling
    // ═══════════════════════════════════════════════════════════════════════

    auto last_heartbeat = std::chrono::steady_clock::now();
    auto heartbeat_interval = std::chrono::milliseconds(heartbeat_ms);

    while (!session.should_exit() && g_running.load(std::memory_order_relaxed))
    {
        // ── Drain all pending IPC messages (non-blocking) ────────────────
#ifdef __linux__
        for (;;)
        {
            struct pollfd pfd;
            pfd.fd = conn->fd();
            pfd.events = POLLIN;
            pfd.revents = 0;
            int poll_ret = ::poll(&pfd, 1, 0);  // non-blocking
            if (poll_ret > 0 && (pfd.revents & (POLLHUP | POLLERR)))
            {
                std::cerr << "[spectra-window] Backend connection lost\n";
                session.request_exit();
                break;
            }
            if (poll_ret <= 0 || !(pfd.revents & POLLIN))
                break;

            auto msg_opt = conn->recv();
            if (!msg_opt)
            {
                std::cerr << "[spectra-window] Connection to backend lost\n";
                session.request_exit();
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
                        ipc_active_figure_id = payload->active_figure_id;
                    }
                    break;
                }
                case spectra::ipc::MessageType::CMD_CLOSE_WINDOW:
                    std::cerr << "[spectra-window] CMD_CLOSE_WINDOW\n";
                    session.request_exit();
                    break;

                case spectra::ipc::MessageType::STATE_SNAPSHOT:
                {
                    auto snap = spectra::ipc::decode_state_snapshot(msg.payload);
                    if (snap)
                    {
                        figure_cache = snap->figures;
                        current_revision = snap->revision;
                        cache_dirty = true;

                        spectra::ipc::AckStatePayload ack;
                        ack.revision = current_revision;
                        send_ipc(*conn,
                                 spectra::ipc::MessageType::ACK_STATE,
                                 session_id,
                                 ipc_window_id,
                                 spectra::ipc::encode_ack_state(ack));
                    }
                    break;
                }

                case spectra::ipc::MessageType::STATE_DIFF:
                {
                    auto diff = spectra::ipc::decode_state_diff(msg.payload);
                    if (diff)
                    {
                        bool needs_rebuild = false;
                        for (const auto& op : diff->ops)
                        {
                            for (auto& fig : figure_cache)
                            {
                                if (fig.figure_id == op.figure_id)
                                {
                                    apply_diff_op_to_cache(fig, op);
                                    break;
                                }
                            }
                            // Structural changes require a full rebuild
                            if (op.type == spectra::ipc::DiffOp::Type::ADD_SERIES
                                || op.type == spectra::ipc::DiffOp::Type::ADD_AXES)
                            {
                                needs_rebuild = true;
                            }
                            else
                            {
                                // Apply directly to the matching live Figure object
                                // (fast path for axis limits, grid toggle, series data).
                                for (size_t mi = 0;
                                     mi < assigned_figures.size() && mi < all_ids.size(); ++mi)
                                {
                                    if (assigned_figures[mi] == op.figure_id)
                                    {
                                        auto* live_fig = registry.get(all_ids[mi]);
                                        if (live_fig)
                                            apply_diff_op_to_figure(*live_fig, op);
                                        break;
                                    }
                                }
                            }
                        }
                        current_revision = diff->new_revision;
                        if (needs_rebuild)
                            cache_dirty = true;

                        spectra::ipc::AckStatePayload ack;
                        ack.revision = current_revision;
                        send_ipc(*conn,
                                 spectra::ipc::MessageType::ACK_STATE,
                                 session_id,
                                 ipc_window_id,
                                 spectra::ipc::encode_ack_state(ack));
                    }
                    break;
                }

                case spectra::ipc::MessageType::RESP_OK:
                case spectra::ipc::MessageType::RESP_ERR:
                    break;

                default:
                    break;
            }
        }
#endif

        // ── Apply full rebuild if snapshot changed ───────────────────────
        if (cache_dirty)
        {
            uint32_t sw = backend->swapchain_width();
            uint32_t sh = backend->swapchain_height();
            all_ids = rebuild_registry_from_cache(registry, figure_cache, sw, sh);
            if (!all_ids.empty())
            {
                // Use ipc_active_figure_id to find the correct figure.
                // The IPC figure IDs don't match registry IDs (registry
                // assigns new IDs), so match by index in assigned_figures.
                spectra::FigureId target_id = all_ids[0];
                if (ipc_active_figure_id != 0)
                {
                    for (size_t i = 0; i < assigned_figures.size() && i < all_ids.size(); ++i)
                    {
                        if (assigned_figures[i] == ipc_active_figure_id)
                        {
                            target_id = all_ids[i];
                            break;
                        }
                    }
                }
                frame_state.active_figure_id = target_id;
                frame_state.active_figure = registry.get(target_id);
                active_figure = frame_state.active_figure;

                // Sync FigureManager so tab bar reflects the new figures
#ifdef SPECTRA_USE_IMGUI
                if (ui_ctx_ptr && ui_ctx_ptr->fig_mgr)
                {
                    auto* fm = ui_ctx_ptr->fig_mgr;
                    // Remove stale figures from FigureManager
                    auto old_ids = fm->figure_ids();
                    for (auto old_id : old_ids)
                    {
                        if (std::find(all_ids.begin(), all_ids.end(), old_id) == all_ids.end())
                            fm->remove_figure(old_id);
                    }
                    // Add new figures that aren't in FigureManager yet
                    for (size_t fi = 0; fi < all_ids.size(); ++fi)
                    {
                        auto new_id = all_ids[fi];
                        if (std::find(old_ids.begin(), old_ids.end(), new_id) == old_ids.end())
                        {
                            spectra::FigureState st;
                            if (fi < figure_cache.size() && !figure_cache[fi].title.empty())
                                st.custom_title = figure_cache[fi].title;
                            fm->add_figure(new_id, std::move(st));
                        }
                    }
                    // Set titles from snapshot for existing figures too
                    for (size_t fi = 0; fi < all_ids.size() && fi < figure_cache.size(); ++fi)
                    {
                        if (!figure_cache[fi].title.empty())
                            fm->set_title(all_ids[fi], figure_cache[fi].title);
                    }
                    // Switch to the target figure
                    fm->switch_to(target_id);
                }
#endif

#ifdef SPECTRA_USE_GLFW
                // Sync WindowContext
                if (window_mgr && !window_mgr->windows().empty())
                {
                    auto* wctx = window_mgr->windows()[0];
                    wctx->assigned_figures.clear();
                    for (auto id : all_ids)
                        wctx->assigned_figures.push_back(id);
                    wctx->active_figure_id = target_id;
                }
#endif
            }
            cache_dirty = false;
        }

        // ── Flush knob changes back to app via IPC ─────────────────────
        if (ui_ctx_ptr)
        {
            auto changes = ui_ctx_ptr->knob_manager.take_pending_changes();
            if (!changes.empty())
            {
                spectra::ipc::StateDiffPayload diff;
                for (auto& [name, val] : changes)
                {
                    spectra::ipc::DiffOp op;
                    op.type = spectra::ipc::DiffOp::Type::SET_KNOB_VALUE;
                    op.str_val = name;
                    op.f1 = val;
                    diff.ops.push_back(std::move(op));
                }
                send_ipc(*conn,
                         spectra::ipc::MessageType::STATE_DIFF,
                         session_id,
                         ipc_window_id,
                         spectra::ipc::encode_state_diff(diff));
            }
        }

        // ── Send heartbeat ───────────────────────────────────────────────
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= heartbeat_interval)
        {
            if (!send_ipc(
                    *conn, spectra::ipc::MessageType::EVT_HEARTBEAT, session_id, ipc_window_id))
            {
                std::cerr << "[spectra-window] Lost connection to backend\n";
                session.request_exit();
                break;
            }
            last_heartbeat = now;
        }

        // ── Standard session tick (same as inproc) ───────────────────────
        session.tick(scheduler,
                     animator,
                     cmd_queue,
                     false,
                     ui_ctx_ptr,
#ifdef SPECTRA_USE_GLFW
                     window_mgr.get(),
#endif
                     frame_state);
        active_figure = frame_state.active_figure;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Phase 6: Clean shutdown
    // ═══════════════════════════════════════════════════════════════════════

    std::cerr << "[spectra-window] Shutting down\n";

    // Notify backend
    send_ipc(*conn, spectra::ipc::MessageType::EVT_WINDOW, session_id, ipc_window_id);

#ifdef SPECTRA_USE_GLFW
    if (window_mgr)
    {
        if (glfw)
            glfw->release_window();
        window_mgr->shutdown();
        window_mgr.reset();
    }
#endif

    if (backend)
        backend->wait_idle();

    renderer_ptr.reset();
    if (backend)
    {
        backend->shutdown();
        backend.reset();
    }

#ifdef SPECTRA_USE_GLFW
    // GlfwAdapter destructor handles glfwTerminate
#endif

    conn->close();

    std::cerr << "[spectra-window] Agent stopped\n";
    return 0;
}

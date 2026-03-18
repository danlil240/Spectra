// automation_server.cpp — JSON-over-Unix-socket automation endpoint for Spectra.
// See automation_server.hpp for architecture overview.

#include "automation_server.hpp"

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "render/backend.hpp"

#include "ui/app/window_ui_context.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/figures/figure_registry.hpp"

#ifdef SPECTRA_USE_GLFW
    #include "ui/input/input.hpp"
    #include "ui/window/window_manager.hpp"
#endif

#ifdef SPECTRA_USE_IMGUI
    #include "imgui.h"
    #include "ui/theme/theme.hpp"
#endif

#ifndef _WIN32
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>

namespace spectra
{

// ─── Minimal JSON helpers (no external dependency) ───────────────────────────

static std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

static std::string json_get_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return "";
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos)
        return "";

    if (json[pos] == '"')
    {
        size_t start = pos + 1;
        size_t end   = start;
        while (end < json.size())
        {
            if (json[end] == '\\')
            {
                end += 2;
                continue;
            }
            if (json[end] == '"')
                break;
            ++end;
        }
        return json.substr(start, end - start);
    }
    size_t start = pos;
    size_t end   = json.find_first_of(",}] \t\n\r", start);
    if (end == std::string::npos)
        end = json.size();
    return json.substr(start, end - start);
}

static double json_get_number(const std::string& json, const std::string& key, double fb = 0.0)
{
    std::string val = json_get_string(json, key);
    if (val.empty())
        return fb;
    try
    {
        return std::stod(val);
    }
    catch (...)
    {
        return fb;
    }
}

static int json_get_int(const std::string& json, const std::string& key, int fb = 0)
{
    return static_cast<int>(json_get_number(json, key, fb));
}

static uint64_t json_get_uint64(const std::string& json, const std::string& key, uint64_t fb = 0)
{
    std::string val = json_get_string(json, key);
    if (val.empty())
        return fb;
    try
    {
        return std::stoull(val);
    }
    catch (...)
    {
        return fb;
    }
}

static std::string json_get_object(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return "{}";
    pos = json.find('{', pos + search.size());
    if (pos == std::string::npos)
        return "{}";
    int    depth = 0;
    size_t start = pos;
    for (size_t i = pos; i < json.size(); ++i)
    {
        if (json[i] == '{')
            ++depth;
        else if (json[i] == '}')
        {
            --depth;
            if (depth == 0)
                return json.substr(start, i - start + 1);
        }
    }
    return "{}";
}

// ─── JSON response builders ──────────────────────────────────────────────────

std::string AutomationServer::json_ok(uint64_t id, const std::string& result_json)
{
    return "{\"id\":" + std::to_string(id) + ",\"ok\":true,\"result\":" + result_json + "}";
}

std::string AutomationServer::json_error(uint64_t id, const std::string& message)
{
    return "{\"id\":" + std::to_string(id) + ",\"ok\":false,\"error\":\"" + json_escape(message)
           + "\"}";
}

// ─── AutomationServer lifecycle ──────────────────────────────────────────────

AutomationServer::AutomationServer() = default;

AutomationServer::~AutomationServer()
{
    stop();
}

std::string AutomationServer::default_socket_path()
{
#ifndef _WIN32
    return "/tmp/spectra-auto-" + std::to_string(::getpid()) + ".sock";
#else
    return "";
#endif
}

bool AutomationServer::start(const std::string& socket_path)
{
#ifdef _WIN32
    (void)socket_path;
    return false;
#else
    if (running_.load())
        return false;
    socket_path_ = socket_path;
    ::unlink(socket_path_.c_str());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        SPECTRA_LOG_ERROR("automation", "socket(): " + std::string(strerror(errno)));
        return false;
    }

    struct sockaddr_un addr
    {
    };
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path))
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    ::chmod(socket_path_.c_str(), 0700);

    if (::listen(listen_fd_, 4) < 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(socket_path_.c_str());
        return false;
    }

    running_.store(true, std::memory_order_release);
    listener_thread_ = std::thread(&AutomationServer::listener_thread_fn, this);
    SPECTRA_LOG_INFO("automation", "Listening on " + socket_path_);
    return true;
#endif
}

void AutomationServer::stop()
{
#ifndef _WIN32
    if (!running_.load(std::memory_order_relaxed))
        return;
    running_.store(false, std::memory_order_release);

    if (listen_fd_ >= 0)
    {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    {
        std::lock_guard lock(clients_mutex_);
        for (int fd : client_fds_)
        {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        client_fds_.clear();
    }

    if (listener_thread_.joinable())
        listener_thread_.join();
    if (!socket_path_.empty())
    {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }
    SPECTRA_LOG_INFO("automation", "Stopped");
#endif
}

std::string AutomationServer::invoke(const std::string&        method,
                                     const std::string&        params_json,
                                     std::chrono::milliseconds timeout)
{
#ifdef _WIN32
    (void)method;
    (void)params_json;
    (void)timeout;
    return json_error(0, "Automation is not supported on this platform");
#else
    if (!running_.load(std::memory_order_relaxed))
        return json_error(0, "Automation server is not running");

    AutomationRequest req;
    req.id                = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    req.method            = method;
    req.params_json       = params_json.empty() ? "{}" : params_json;
    const uint64_t req_id = req.id;

    {
        std::lock_guard lock(pending_mutex_);
        pending_.push_back(std::move(req));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (running_.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard lock(pending_mutex_);
        for (auto it = pending_.begin(); it != pending_.end(); ++it)
        {
            if (it->id == req_id && it->responded)
            {
                std::string response = std::move(it->response_json);
                pending_.erase(it);
                return response;
            }
        }
    }

    {
        std::lock_guard lock(pending_mutex_);
        pending_.erase(std::remove_if(pending_.begin(),
                                      pending_.end(),
                                      [req_id](const AutomationRequest& pending_req)
                                      { return pending_req.id == req_id; }),
                       pending_.end());
    }

    return json_error(req_id, "Timeout");
#endif
}

// ─── Listener thread ─────────────────────────────────────────────────────────

void AutomationServer::listener_thread_fn()
{
#ifndef _WIN32
    while (running_.load(std::memory_order_relaxed))
    {
        struct pollfd pfd
        {
        };
        pfd.fd     = listen_fd_;
        pfd.events = POLLIN;
        int ret    = ::poll(&pfd, 1, 200);
        if (ret <= 0 || !(pfd.revents & POLLIN))
            continue;

        struct sockaddr_un ca
        {
        };
        socklen_t cl  = sizeof(ca);
        int       cfd = ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&ca), &cl);
        if (cfd < 0)
            continue;

        {
            std::lock_guard lock(clients_mutex_);
            client_fds_.push_back(cfd);
        }
        SPECTRA_LOG_INFO("automation", "Client connected fd=" + std::to_string(cfd));
        std::thread(&AutomationServer::handle_client, this, cfd).detach();
    }
#endif
}

void AutomationServer::handle_client(int client_fd)
{
#ifndef _WIN32
    std::string buffer;
    char        chunk[4096];

    while (running_.load(std::memory_order_relaxed))
    {
        ssize_t n = ::read(client_fd, chunk, sizeof(chunk));
        if (n <= 0)
            break;
        buffer.append(chunk, static_cast<size_t>(n));

        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos)
        {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            if (line.empty() || line[0] != '{')
                continue;

            AutomationRequest req;
            if (!parse_request(line, req))
            {
                send_response(client_fd, json_error(0, "Invalid JSON"));
                continue;
            }
            uint64_t req_id = req.id;
            req.client_fd   = client_fd;

            {
                std::lock_guard lock(pending_mutex_);
                pending_.push_back(std::move(req));
            }

            // Wait for main thread to execute (max 30s)
            auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            std::string response;
            bool        got = false;

            while (!got && running_.load(std::memory_order_relaxed)
                   && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard lock(pending_mutex_);
                for (auto it = pending_.begin(); it != pending_.end(); ++it)
                {
                    if (it->id == req_id && it->responded)
                    {
                        response = std::move(it->response_json);
                        got      = true;
                        pending_.erase(it);
                        break;
                    }
                }
            }

            send_response(client_fd, got ? response : json_error(req_id, "Timeout"));
        }
    }

    ::close(client_fd);
    {
        std::lock_guard lock(clients_mutex_);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
                          client_fds_.end());
    }
#else
    (void)client_fd;
#endif
}

bool AutomationServer::parse_request(const std::string& json_str, AutomationRequest& req)
{
    req.id          = next_request_id_.fetch_add(1, std::memory_order_relaxed);
    req.method      = json_get_string(json_str, "method");
    req.params_json = json_get_object(json_str, "params");
    return !req.method.empty();
}

void AutomationServer::send_response(int fd, const std::string& json)
{
#ifndef _WIN32
    std::string msg   = json + "\n";
    size_t      total = 0;
    while (total < msg.size())
    {
        ssize_t w = ::write(fd, msg.data() + total, msg.size() - total);
        if (w <= 0)
            break;
        total += static_cast<size_t>(w);
    }
#else
    (void)fd;
    (void)json;
#endif
}

// ─── Main-thread poll ────────────────────────────────────────────────────────

void AutomationServer::poll(App& app, WindowUIContext* ui_ctx)
{
    {
        std::lock_guard lock(pending_mutex_);

        // Tick down wait_frames counters for deferred requests
        for (auto& pr : pending_)
        {
            if (pr.wait_frames > 0 && !pr.responded)
            {
                --pr.wait_frames;
                if (pr.wait_frames <= 0)
                {
                    pr.response_json = json_ok(pr.id, "{\"waited\":true}");
                    pr.responded     = true;
                }
            }
        }

        // Intercept wait_frames requests: parse the count and set it
        // directly on the pending entry so they defer without executing.
        for (auto& pr : pending_)
        {
            if (!pr.responded && pr.wait_frames <= 0 && pr.method == "wait_frames")
            {
                int count = static_cast<int>(json_get_number(pr.params_json, "count", 1));
                if (count < 1)
                    count = 1;
                if (count > 600)
                    count = 600;
                pr.wait_frames = count;
            }
        }
    }

    std::vector<AutomationRequest> to_exec;
    {
        std::lock_guard lock(pending_mutex_);
        for (auto& r : pending_)
            if (!r.responded && r.wait_frames <= 0)
                to_exec.push_back(r);
    }
    for (auto& req : to_exec)
    {
        execute(req, app, ui_ctx);
        std::lock_guard lock(pending_mutex_);
        for (auto& pr : pending_)
        {
            if (pr.id == req.id)
            {
                pr.responded     = true;
                pr.response_json = req.response_json;
                break;
            }
        }
    }
}

// ─── Command dispatch ────────────────────────────────────────────────────────

void AutomationServer::execute(AutomationRequest& req, App& app, WindowUIContext* ui_ctx)
{
    const auto& method = req.method;
    const auto& params = req.params_json;

    // ── execute_command ─────────────────────────────────────────────────
    if (method == "execute_command")
    {
        std::string cmd_id = json_get_string(params, "command_id");
        if (cmd_id.empty())
        {
            req.response_json = json_error(req.id, "Missing command_id");
            return;
        }
#ifdef SPECTRA_USE_IMGUI
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        bool ok           = ui_ctx->cmd_registry.execute(cmd_id);
        req.response_json = ok ? json_ok(req.id, "{\"executed\":\"" + json_escape(cmd_id) + "\"}")
                               : json_error(req.id, "Command not found or disabled: " + cmd_id);
#else
        (void)ui_ctx;
        req.response_json = json_error(req.id, "ImGui not available");
#endif
        return;
    }

    // ── list_commands ───────────────────────────────────────────────────
    if (method == "list_commands")
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        auto               all = ui_ctx->cmd_registry.all_commands();
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            oss << "{\"id\":\"" << json_escape(all[i]->id) << "\",\"label\":\""
                << json_escape(all[i]->label) << "\",\"category\":\""
                << json_escape(all[i]->category) << "\",\"shortcut\":\""
                << json_escape(all[i]->shortcut)
                << "\",\"enabled\":" << (all[i]->enabled ? "true" : "false") << "}";
        }
        oss << "]";
        req.response_json = json_ok(req.id, "{\"commands\":" + oss.str() + "}");
#else
        req.response_json = json_error(req.id, "ImGui not available");
#endif
        return;
    }

    // ── get_state ───────────────────────────────────────────────────────
    if (method == "get_state")
    {
        std::ostringstream oss;
        oss << "{";
        auto ids = app.figure_registry().all_ids();
        oss << "\"figure_count\":" << ids.size();

        FigureId active_id = INVALID_FIGURE_ID;
#ifdef SPECTRA_USE_IMGUI
        if (ui_ctx && ui_ctx->fig_mgr)
            active_id = ui_ctx->fig_mgr->active_index();
#endif
        oss << ",\"active_figure_id\":" << active_id;

        oss << ",\"figures\":[";
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i > 0)
                oss << ",";
            Figure* fig = app.figure_registry().get(ids[i]);
            oss << "{\"id\":" << ids[i];
            if (fig)
            {
                oss << ",\"width\":" << fig->width() << ",\"height\":" << fig->height()
                    << ",\"axes_count\":" << fig->axes().size();
                size_t ts = 0;
                for (const auto& ax : fig->axes())
                {
                    if (ax)
                        ts += ax->series().size();
                }
                oss << ",\"total_series\":" << ts;
            }
            oss << "}";
        }
        oss << "]";

#ifdef SPECTRA_USE_IMGUI
        if (ui_ctx)
        {
            oss << ",\"undo_count\":" << ui_ctx->undo_mgr.undo_count()
                << ",\"redo_count\":" << ui_ctx->undo_mgr.redo_count()
                << ",\"is_3d_mode\":" << (ui_ctx->is_in_3d_mode ? "true" : "false")
                << ",\"theme\":\"" << json_escape(ui::ThemeManager::instance().current_theme_name()) << '"';
        }
#endif
        oss << "}";
        req.response_json = json_ok(req.id, oss.str());
        return;
    }

    // ── mouse_move ──────────────────────────────────────────────────────
    if (method == "mouse_move")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        double x = json_get_number(params, "x");
        double y = json_get_number(params, "y");
        ui_ctx->input_handler.on_mouse_move(x, y);
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── mouse_click ─────────────────────────────────────────────────────
    if (method == "mouse_click")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        double x   = json_get_number(params, "x");
        double y   = json_get_number(params, "y");
        int    btn = json_get_int(params, "button", 0);
        int    mod = json_get_int(params, "modifiers", 0);
        ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
        ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── mouse_drag ──────────────────────────────────────────────────────
    if (method == "mouse_drag")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        double x1    = json_get_number(params, "x1");
        double y1    = json_get_number(params, "y1");
        double x2    = json_get_number(params, "x2");
        double y2    = json_get_number(params, "y2");
        int    btn   = json_get_int(params, "button", 0);
        int    mod   = json_get_int(params, "modifiers", 0);
        int    steps = json_get_int(params, "steps", 10);
        if (steps < 2)
            steps = 2;

        ui_ctx->input_handler.on_mouse_move(x1, y1);
        ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x1, y1);
        for (int i = 1; i <= steps; ++i)
        {
            double t  = static_cast<double>(i) / steps;
            double mx = x1 + (x2 - x1) * t;
            double my = y1 + (y2 - y1) * t;
            ui_ctx->input_handler.on_mouse_move(mx, my);
        }
        ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x2, y2);
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── scroll ──────────────────────────────────────────────────────────
    if (method == "scroll")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        double x  = json_get_number(params, "x");
        double y  = json_get_number(params, "y");
        double dx = json_get_number(params, "dx", 0.0);
        double dy = json_get_number(params, "dy", 1.0);
        ui_ctx->input_handler.on_scroll(x, y, dx, dy);
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── key_press ───────────────────────────────────────────────────────
    if (method == "key_press")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        int key = json_get_int(params, "key");
        int mod = json_get_int(params, "modifiers", 0);
        ui_ctx->input_handler.on_key(key, 1, mod);
        ui_ctx->input_handler.on_key(key, 0, mod);
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── create_figure ───────────────────────────────────────────────────
    if (method == "create_figure")
    {
        uint32_t w        = static_cast<uint32_t>(json_get_int(params, "width", 1280));
        uint32_t h        = static_cast<uint32_t>(json_get_int(params, "height", 720));
        Figure&  new_fig  = app.figure({w, h});
        FigureId new_id   = app.figure_registry().find_id(&new_fig);
        req.response_json = json_ok(req.id, "{\"figure_id\":" + std::to_string(new_id) + "}");
        return;
    }

    // ── add_series ──────────────────────────────────────────────────────
    if (method == "add_series")
    {
        uint64_t    fig_id = json_get_uint64(params, "figure_id", 0);
        std::string type   = json_get_string(params, "type");
        if (type.empty())
            type = "line";
        int n_points = json_get_int(params, "n_points", 100);

        Figure* fig = app.figure_registry().get(static_cast<FigureId>(fig_id));
        if (!fig)
        {
            req.response_json = json_error(req.id, "Figure not found");
            return;
        }

        if (fig->axes().empty())
            fig->subplot(1, 1, 1);
        auto& ax = *fig->axes_mut()[0];

        std::vector<float> x(n_points), y(n_points);
        for (int i = 0; i < n_points; ++i)
        {
            x[i] = static_cast<float>(i);
            y[i] = std::sin(static_cast<float>(i) * 0.1f);
        }

        std::string label = json_get_string(params, "label");
        if (type == "scatter")
            ax.scatter(x, y).label(label.empty() ? "scatter" : label);
        else
            ax.line(x, y).label(label.empty() ? "line" : label);

        req.response_json =
            json_ok(req.id, "{\"series_count\":" + std::to_string(ax.series().size()) + "}");
        return;
    }

    // ── switch_figure ───────────────────────────────────────────────────
    if (method == "switch_figure")
    {
#ifdef SPECTRA_USE_IMGUI
        if (!ui_ctx || !ui_ctx->fig_mgr)
        {
            req.response_json = json_error(req.id, "No UI");
            return;
        }
        uint64_t fig_id = json_get_uint64(params, "figure_id", 0);
        ui_ctx->fig_mgr->queue_switch(static_cast<FigureId>(fig_id));
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "ImGui not available");
#endif
        return;
    }

    // ── pump_frames ─────────────────────────────────────────────────────
    if (method == "pump_frames")
    {
        // NOTE: We are already inside poll() which runs during app.step().
        // Calling app.step() re-entrantly causes a segfault.
        // Just acknowledge — the normal frame loop will advance frames.
        int count = json_get_int(params, "count", 1);
        if (count < 1)
            count = 1;
        if (count > 600)
            count = 600;
        req.response_json = json_ok(req.id, "{\"pumped\":" + std::to_string(count) + "}");
        return;
    }

    // ── capture_screenshot ──────────────────────────────────────────────
    if (method == "capture_screenshot")
    {
        std::string path = json_get_string(params, "path");
        if (path.empty())
            path = "/tmp/spectra_auto_screenshot.png";

        FigureId active_id = INVALID_FIGURE_ID;
#ifdef SPECTRA_USE_IMGUI
        if (ui_ctx && ui_ctx->fig_mgr)
            active_id = ui_ctx->fig_mgr->active_index();
#endif
        Figure* fig = app.figure_registry().get(active_id);
        if (!fig)
        {
            auto ids = app.figure_registry().all_ids();
            if (!ids.empty())
                fig = app.figure_registry().get(ids[0]);
        }
        if (fig)
        {
            fig->save_png(path);
            // The save is queued; the next normal frame loop iteration will capture it.
            // Do NOT call app.step() here — we are already inside poll() which runs
            // during a step, so re-entrant stepping causes a segfault.
            req.response_json = json_ok(req.id, "{\"path\":\"" + json_escape(path) + "\"}");
        }
        else
        {
            req.response_json = json_error(req.id, "No figure to capture");
        }
        return;
    }

    // ── capture_window ─────────────────────────────────────────────────
    // Captures the full window (plot + ImGui chrome) by reading back the
    // swapchain image.  Unlike capture_screenshot (figure-only), this
    // includes the menubar, sidebar, tabs, status bar — everything visible.
    if (method == "capture_window")
    {
        std::string path = json_get_string(params, "path");
        if (path.empty())
            path = "/tmp/spectra_auto_window.png";

        Backend* backend = app.backend();
        if (!backend)
        {
            req.response_json = json_error(req.id, "No backend available");
            return;
        }

        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        if (w == 0 || h == 0)
        {
            req.response_json = json_error(req.id, "Swapchain not ready");
            return;
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (!backend->readback_framebuffer(pixels.data(), w, h))
        {
            req.response_json = json_error(req.id, "Framebuffer readback failed");
            return;
        }

        if (!ImageExporter::write_png(path, pixels.data(), w, h))
        {
            req.response_json = json_error(req.id, "PNG write failed");
            return;
        }

        SPECTRA_LOG_INFO("automation", "Window screenshot saved: " + path);
        req.response_json =
            json_ok(req.id,
                    "{\"path\":\"" + json_escape(path) + "\",\"width\":" + std::to_string(w)
                        + ",\"height\":" + std::to_string(h) + "}");
        return;
    }

    // ── resize_window ───────────────────────────────────────────────────
    if (method == "resize_window")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        uint32_t w                    = static_cast<uint32_t>(json_get_int(params, "width", 1280));
        uint32_t h                    = static_cast<uint32_t>(json_get_int(params, "height", 720));
        ui_ctx->needs_resize          = true;
        ui_ctx->new_width             = w;
        ui_ctx->new_height            = h;
        ui_ctx->resize_requested_time = std::chrono::steady_clock::now();
        req.response_json             = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── get_screenshot_base64 ──────────────────────────────────────────
    if (method == "get_screenshot_base64")
    {
        Backend* backend = app.backend();
        if (!backend)
        {
            req.response_json = json_error(req.id, "No backend available");
            return;
        }

        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        if (w == 0 || h == 0)
        {
            req.response_json = json_error(req.id, "Swapchain not ready");
            return;
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (!backend->readback_framebuffer(pixels.data(), w, h))
        {
            req.response_json = json_error(req.id, "Framebuffer readback failed");
            return;
        }

        auto png_data = ImageExporter::write_png_to_memory(pixels.data(), w, h);
        if (png_data.empty())
        {
            req.response_json = json_error(req.id, "PNG encoding failed");
            return;
        }

        // Base64 encode
        static constexpr const char* kB64 =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string b64;
        b64.reserve((png_data.size() + 2) / 3 * 4);
        for (size_t i = 0; i < png_data.size(); i += 3)
        {
            uint32_t n = static_cast<uint32_t>(png_data[i]) << 16;
            if (i + 1 < png_data.size())
                n |= static_cast<uint32_t>(png_data[i + 1]) << 8;
            if (i + 2 < png_data.size())
                n |= static_cast<uint32_t>(png_data[i + 2]);
            b64 += kB64[(n >> 18) & 0x3F];
            b64 += kB64[(n >> 12) & 0x3F];
            b64 += (i + 1 < png_data.size()) ? kB64[(n >> 6) & 0x3F] : '=';
            b64 += (i + 2 < png_data.size()) ? kB64[n & 0x3F] : '=';
        }

        req.response_json =
            json_ok(req.id,
                    "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h)
                        + ",\"format\":\"png\"" + ",\"data\":\"" + b64 + "\"}");
        return;
    }

    // ── wait_frames ────────────────────────────────────────────────────
    // Handled specially in poll() — this branch should not normally be reached
    // because poll() intercepts wait_frames before calling execute().
    if (method == "wait_frames")
    {
        req.response_json = json_ok(req.id, "{\"waited\":true}");
        return;
    }

    // ── text_input ─────────────────────────────────────────────────────
    if (method == "text_input")
    {
#ifdef SPECTRA_USE_IMGUI
        std::string text = json_get_string(params, "text");
        if (text.empty())
        {
            req.response_json = json_error(req.id, "Missing text");
            return;
        }
        auto& io = ImGui::GetIO();
        for (char c : text)
            io.AddInputCharacter(static_cast<unsigned int>(c));
        req.response_json = json_ok(req.id, "{\"chars\":" + std::to_string(text.size()) + "}");
#else
        req.response_json = json_error(req.id, "ImGui not available");
#endif
        return;
    }

    // ── double_click ───────────────────────────────────────────────────
    if (method == "double_click")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        double x   = json_get_number(params, "x");
        double y   = json_get_number(params, "y");
        int    btn = json_get_int(params, "button", 0);
        int    mod = json_get_int(params, "modifiers", 0);
        // First click
        ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
        ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
        // Second click (double)
        ui_ctx->input_handler.on_mouse_button(btn, 1, mod, x, y);
        ui_ctx->input_handler.on_mouse_button(btn, 0, mod, x, y);
        req.response_json = json_ok(req.id);
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── get_figure_info ────────────────────────────────────────────────
    if (method == "get_figure_info")
    {
        uint64_t fig_id = json_get_uint64(params, "figure_id", 0);
        Figure*  fig    = app.figure_registry().get(static_cast<FigureId>(fig_id));
        if (!fig)
        {
            req.response_json = json_error(req.id, "Figure not found");
            return;
        }

        std::ostringstream oss;
        oss << "{\"figure_id\":" << fig_id << ",\"width\":" << fig->width()
            << ",\"height\":" << fig->height() << ",\"axes_count\":" << fig->axes().size()
            << ",\"all_axes_count\":" << fig->all_axes().size();

        // 2D axes details
        oss << ",\"axes\":[";
        for (size_t ai = 0; ai < fig->axes().size(); ++ai)
        {
            if (ai > 0)
                oss << ",";
            auto* ax = fig->axes()[ai].get();
            if (!ax)
            {
                oss << "null";
                continue;
            }
            auto xl = ax->x_limits();
            auto yl = ax->y_limits();
            oss << "{\"index\":" << ai << ",\"x_min\":" << xl.min << ",\"x_max\":" << xl.max
                << ",\"y_min\":" << yl.min << ",\"y_max\":" << yl.max << ",\"series\":[";
            for (size_t si = 0; si < ax->series().size(); ++si)
            {
                if (si > 0)
                    oss << ",";
                auto* s = ax->series()[si].get();
                if (!s)
                {
                    oss << "null";
                    continue;
                }
                const char* stype  = "unknown";
                size_t      scount = 0;
                if (auto* ls = dynamic_cast<const LineSeries*>(s))
                {
                    stype  = "line";
                    scount = ls->point_count();
                }
                else if (auto* ss = dynamic_cast<const ScatterSeries*>(s))
                {
                    stype  = "scatter";
                    scount = ss->point_count();
                }
                oss << "{\"label\":\"" << json_escape(s->label()) << "\",\"type\":\"" << stype
                    << "\",\"visible\":" << (s->visible() ? "true" : "false")
                    << ",\"point_count\":" << scount << "}";
            }
            oss << "]}";
        }
        oss << "]";

        // 3D axes count and basic info
        oss << ",\"axes_3d\":[";
        size_t a3i = 0;
        for (size_t ai = 0; ai < fig->all_axes().size(); ++ai)
        {
            auto* ax_base = fig->all_axes()[ai].get();
            if (!ax_base)
                continue;
            auto* ax3d = dynamic_cast<Axes3D*>(ax_base);
            if (!ax3d)
                continue;
            if (a3i > 0)
                oss << ",";
            auto xl = ax3d->x_limits();
            auto yl = ax3d->y_limits();
            auto zl = ax3d->z_limits();
            oss << "{\"index\":" << ai << ",\"x_min\":" << xl.min << ",\"x_max\":" << xl.max
                << ",\"y_min\":" << yl.min << ",\"y_max\":" << yl.max << ",\"z_min\":" << zl.min
                << ",\"z_max\":" << zl.max << ",\"series_count\":" << ax3d->series().size() << "}";
            ++a3i;
        }
        oss << "]";

        oss << "}";
        req.response_json = json_ok(req.id, oss.str());
        return;
    }

    // ── get_window_size ────────────────────────────────────────────────
    if (method == "get_window_size")
    {
#ifdef SPECTRA_USE_GLFW
        if (!ui_ctx)
        {
            req.response_json = json_error(req.id, "No UI context");
            return;
        }
        Backend* backend = app.backend();
        if (!backend)
        {
            req.response_json = json_error(req.id, "No backend available");
            return;
        }
        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        req.response_json =
            json_ok(req.id,
                    "{\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) + "}");
#else
        req.response_json = json_error(req.id, "GLFW not available");
#endif
        return;
    }

    // ── ping ────────────────────────────────────────────────────────────
    if (method == "ping")
    {
        req.response_json = json_ok(req.id, "{\"pong\":true}");
        return;
    }

    req.response_json = json_error(req.id, "Unknown method: " + method);
}

}   // namespace spectra

#pragma once

// automation_server.hpp — Lightweight JSON-over-Unix-socket automation endpoint.
// Runs a listener thread that accepts connections, parses JSON commands,
// queues them for main-thread execution, and sends JSON responses.
// Designed to be driven by an MCP server so external agents can control Spectra.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spectra
{

class App;
class CommandRegistry;
struct WindowUIContext;

// A pending automation request awaiting main-thread execution.
struct AutomationRequest
{
    uint64_t    id = 0;
    std::string method;           // e.g. "execute_command", "mouse_move"
    std::string params_json;      // raw JSON object string for parameters
    int         client_fd = -1;   // fd to send response to
    bool        responded = false;
    std::string response_json;     // filled by main thread after execution
    int         wait_frames = 0;   // >0 means defer response until N frames elapse
};

class AutomationServer
{
   public:
    AutomationServer();
    ~AutomationServer();

    AutomationServer(const AutomationServer&)            = delete;
    AutomationServer& operator=(const AutomationServer&) = delete;

    // Start listening on the given socket path.
    // Returns true on success.
    bool start(const std::string& socket_path);

    // Stop the listener thread and close all connections.
    void stop();

    // Must be called from the main thread each frame.
    // Drains pending requests, executes them, sends responses.
    void poll(App& app, WindowUIContext* ui_ctx);

    std::string invoke(const std::string&        method,
                       const std::string&        params_json = "{}",
                       std::chrono::milliseconds timeout     = std::chrono::seconds(30));

    // Returns the socket path being listened on.
    const std::string& socket_path() const { return socket_path_; }

    bool is_running() const { return running_.load(std::memory_order_relaxed); }

    // Returns the default automation socket path:
    //   /tmp/spectra-auto-<pid>.sock
    static std::string default_socket_path();

   private:
    void listener_thread_fn();
    void handle_client(int client_fd);

    // Parse a single JSON request string into an AutomationRequest.
    // Returns false if parsing fails.
    bool parse_request(const std::string& json_str, AutomationRequest& req);

    // Execute a request on the main thread. Fills req.response_json.
    void execute(AutomationRequest& req, App& app, WindowUIContext* ui_ctx);

    // Send a JSON response string back to the client fd.
    void send_response(int fd, const std::string& json);

    // JSON helpers (minimal, no external dependency)
    static std::string json_ok(uint64_t id, const std::string& result_json = "{}");
    static std::string json_error(uint64_t id, const std::string& message);

    std::string       socket_path_;
    int               listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread       listener_thread_;

    // Pending requests from listener thread → main thread
    std::mutex                     pending_mutex_;
    std::vector<AutomationRequest> pending_;

    // Active client fds (for cleanup)
    std::mutex       clients_mutex_;
    std::vector<int> client_fds_;

    std::atomic<uint64_t> next_request_id_{1};
};

}   // namespace spectra

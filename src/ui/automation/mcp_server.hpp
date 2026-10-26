#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace spectra
{

class AutomationServer;

class McpServer
{
   public:
    McpServer();
    ~McpServer();

    McpServer(const McpServer&)            = delete;
    McpServer& operator=(const McpServer&) = delete;

    bool start(AutomationServer& automation, const std::string& bind_host, uint16_t port);
    void stop();

    bool               is_running() const { return running_.load(std::memory_order_relaxed); }
    const std::string& bind_host() const { return bind_host_; }
    uint16_t           port() const { return port_; }
    std::string        endpoint() const;

    // Bearer token required on every HTTP request.  Set before start() to override;
    // otherwise start() uses $SPECTRA_MCP_TOKEN or a random 256-bit token.
    void               set_auth_token(std::string token) { auth_token_ = std::move(token); }
    const std::string& auth_token() const { return auth_token_; }

    // 0600 JSON file ({"endpoint","token","pid"}) written by start(); empty if none.
    const std::string& token_file() const { return token_file_; }

    // Socket-free request handler: `headers` is the request line plus header block
    // (terminated by CRLFCRLF), `body` the raw body.  Returns the full HTTP response.
    // Enforces Host/Origin/token/Content-Type checks.  Exposed for unit tests.
    std::string handle_http_request(const std::string& headers, const std::string& body);

   private:
    void listener_thread_fn();
    void handle_client(int client_fd);

    AutomationServer* automation_ = nullptr;
    std::string       bind_host_;
    uint16_t          port_      = 0;
    int               listen_fd_ = -1;
    std::string       auth_token_;
    std::string       token_file_;

    std::atomic<bool> running_{false};
    std::thread       listener_thread_;

    std::mutex       clients_mutex_;
    std::vector<int> client_fds_;
};

}   // namespace spectra

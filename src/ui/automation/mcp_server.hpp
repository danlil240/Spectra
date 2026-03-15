#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
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

   private:
    void listener_thread_fn();
    void handle_client(int client_fd);

    AutomationServer* automation_ = nullptr;
    std::string       bind_host_;
    uint16_t          port_      = 0;
    int               listen_fd_ = -1;

    std::atomic<bool> running_{false};
    std::thread       listener_thread_;

    std::mutex       clients_mutex_;
    std::vector<int> client_fds_;
};

}   // namespace spectra

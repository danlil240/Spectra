#pragma once

#include "message.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace spectra::ipc
{

// ─── Connection ──────────────────────────────────────────────────────────────
// Wraps a connected socket fd. Provides send/recv of framed Messages.
// Not thread-safe — caller must synchronize.

class Connection
{
   public:
    explicit Connection(int fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    // Returns true if the underlying fd is valid.
    bool is_open() const { return fd_ >= 0; }

    // Send a complete message. Returns true on success.
    bool send(const Message& msg);

    // Receive a complete message (blocking).
    // Returns std::nullopt on error or connection closed.
    std::optional<Message> recv();

    // Close the connection.
    void close();

    int fd() const { return fd_; }

   private:
    int fd_ = -1;

    // Internal: read exactly `len` bytes into `buf`. Returns false on error/EOF.
    bool read_exact(uint8_t* buf, size_t len);
    // Internal: write exactly `len` bytes from `buf`. Returns false on error.
    bool write_exact(const uint8_t* buf, size_t len);
};

// ─── Server ──────────────────────────────────────────────────────────────────
// Listens on a Unix domain socket. Accepts connections.

class Server
{
   public:
    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Bind and listen on the given socket path.
    // Returns true on success. Removes stale socket file if present.
    bool listen(const std::string& path);

    // Accept a new connection (blocking).
    // Returns nullptr if the server is closed or an error occurs.
    std::unique_ptr<Connection> accept();

    // Accept a new connection (non-blocking).
    // Returns nullptr immediately if no pending connection.
    std::unique_ptr<Connection> try_accept();

    // Close the listening socket and remove the socket file.
    void close();

    bool is_listening() const { return listen_fd_ >= 0; }
    int listen_fd() const { return listen_fd_; }
    const std::string& path() const { return path_; }

   private:
    int listen_fd_ = -1;
    std::string path_;
};

// ─── Client ──────────────────────────────────────────────────────────────────
// Connects to a Unix domain socket server.

class Client
{
   public:
    Client() = default;
    ~Client() = default;

    // Connect to the server at the given socket path.
    // Returns a Connection on success, nullptr on failure.
    static std::unique_ptr<Connection> connect(const std::string& path);
};

// ─── Utility ─────────────────────────────────────────────────────────────────

// Returns the default socket path for this process:
//   $XDG_RUNTIME_DIR/spectra-<pid>.sock
// Falls back to /tmp/spectra-<pid>.sock if XDG_RUNTIME_DIR is not set.
std::string default_socket_path();

}  // namespace spectra::ipc

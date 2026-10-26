#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "message.hpp"

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

    Connection(const Connection&)            = delete;
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
    bool read_exact(uint8_t* buf, size_t len) const;
    // Internal: write exactly `len` bytes from `buf`. Returns false on error.
    bool write_exact(const uint8_t* buf, size_t len) const;
};

// ─── Server ──────────────────────────────────────────────────────────────────
// Listens on a Unix domain socket. Accepts connections.

class Server
{
   public:
    Server();
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    // Bind and listen on the given socket path.
    // Returns true on success. Removes stale socket file if present.
    bool listen(const std::string& path);

    // Accept a new connection (blocking).
    // Returns nullptr if the server is closed or an error occurs.
    std::unique_ptr<Connection> accept() const;

    // Accept a new connection (non-blocking).
    // Returns nullptr immediately if no pending connection.
    std::unique_ptr<Connection> try_accept() const;

    // Close the listening socket and remove the socket file.
    void close();

    bool               is_listening() const { return listen_fd_ >= 0; }
    int                listen_fd() const { return listen_fd_; }
    const std::string& path() const { return path_; }

   private:
    int         listen_fd_ = -1;
    std::string path_;
};

// ─── Client ──────────────────────────────────────────────────────────────────
// Connects to a Unix domain socket server.

class Client
{
   public:
    Client()  = default;
    ~Client() = default;

    // Connect to the server at the given socket path.
    // Returns a Connection on success, nullptr on failure.
    static std::unique_ptr<Connection> connect(const std::string& path);
};

// ─── Utility ─────────────────────────────────────────────────────────────────

// Directory used for Spectra's Unix sockets: $XDG_RUNTIME_DIR when set and
// short enough for a `spectra-<pid>.sock` path to fit in sun_path, otherwise
// std::filesystem::temp_directory_path() ($TMPDIR or /tmp).
// All components (app, daemon, publishers) must agree on it for discovery.
std::string runtime_dir();

// Returns the default socket path for this process:
//   <runtime_dir()>/spectra-<pid>.sock
std::string default_socket_path();

// Maximum length (in bytes, excluding the terminator) of a Unix socket path
// on this platform (sizeof(sockaddr_un::sun_path) - 1); 0 where unsupported.
size_t max_socket_path_length();

}   // namespace spectra::ipc

#include "transport.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "codec.hpp"

#ifdef __linux__
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <sys/un.h>
#endif

namespace spectra::ipc
{

// ─── Connection ──────────────────────────────────────────────────────────────

Connection::Connection(int fd) : fd_(fd) {}

Connection::~Connection()
{
    close();
}

Connection::Connection(Connection&& other) noexcept : fd_(other.fd_)
{
    other.fd_ = -1;
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if (this != &other)
    {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool Connection::read_exact(uint8_t* buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        auto n = ::read(fd_, buf + total, len - total);
        if (n <= 0)
            return false;  // EOF or error
        total += static_cast<size_t>(n);
    }
    return true;
}

bool Connection::write_exact(const uint8_t* buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        auto n = ::write(fd_, buf + total, len - total);
        if (n <= 0)
            return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

bool Connection::send(const Message& msg)
{
    if (fd_ < 0)
        return false;
    auto wire = encode_message(msg);
    return write_exact(wire.data(), wire.size());
}

std::optional<Message> Connection::recv()
{
    if (fd_ < 0)
        return std::nullopt;

    // Read fixed header
    uint8_t hdr_buf[HEADER_SIZE];
    if (!read_exact(hdr_buf, HEADER_SIZE))
        return std::nullopt;

    auto hdr_opt = decode_header(std::span<const uint8_t>(hdr_buf, HEADER_SIZE));
    if (!hdr_opt)
        return std::nullopt;

    auto& hdr = *hdr_opt;
    if (hdr.payload_len > MAX_PAYLOAD_SIZE)
        return std::nullopt;

    Message msg;
    msg.header = hdr;
    msg.payload.resize(hdr.payload_len);

    if (hdr.payload_len > 0)
    {
        if (!read_exact(msg.payload.data(), hdr.payload_len))
            return std::nullopt;
    }

    return msg;
}

void Connection::close()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

// ─── Server ──────────────────────────────────────────────────────────────────

Server::Server() = default;

Server::~Server()
{
    close();
}

bool Server::listen(const std::string& path)
{
#ifdef __linux__
    // Remove stale socket file
    ::unlink(path.c_str());

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr
    {
    };
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        ::close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ::close(fd);
        return false;
    }

    // Set socket file permissions to owner-only
    ::chmod(path.c_str(), 0700);

    if (::listen(fd, 8) < 0)
    {
        ::close(fd);
        ::unlink(path.c_str());
        return false;
    }

    listen_fd_ = fd;
    path_ = path;
    return true;
#else
    (void)path;
    return false;  // UDS not implemented on this platform
#endif
}

std::unique_ptr<Connection> Server::accept()
{
#ifdef __linux__
    if (listen_fd_ < 0)
        return nullptr;

    struct sockaddr_un client_addr
    {
    };
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0)
        return nullptr;

    return std::make_unique<Connection>(client_fd);
#else
    return nullptr;
#endif
}

std::unique_ptr<Connection> Server::try_accept()
{
#ifdef __linux__
    if (listen_fd_ < 0)
        return nullptr;

    struct sockaddr_un client_addr
    {
    };
    socklen_t client_len = sizeof(client_addr);
    // SOCK_NONBLOCK makes accept4 return EAGAIN immediately if no connection
    int client_fd = ::accept4(listen_fd_,
                              reinterpret_cast<struct sockaddr*>(&client_addr),
                              &client_len,
                              SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0)
        return nullptr;  // EAGAIN or error — no pending connection

    // Clear non-blocking on the accepted fd so recv() stays blocking
    int flags = ::fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);

    return std::make_unique<Connection>(client_fd);
#else
    return nullptr;
#endif
}

void Server::close()
{
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty())
    {
        ::unlink(path_.c_str());
        path_.clear();
    }
}

// ─── Client ──────────────────────────────────────────────────────────────────

std::unique_ptr<Connection> Client::connect(const std::string& path)
{
#ifdef __linux__
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return nullptr;

    struct sockaddr_un addr
    {
    };
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        ::close(fd);
        return nullptr;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ::close(fd);
        return nullptr;
    }

    return std::make_unique<Connection>(fd);
#else
    (void)path;
    return nullptr;
#endif
}

// ─── Utility ─────────────────────────────────────────────────────────────────

std::string default_socket_path()
{
    std::string dir;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0')
        dir = xdg;
    else
        dir = "/tmp";

    return dir + "/spectra-" + std::to_string(::getpid()) + ".sock";
}

}  // namespace spectra::ipc

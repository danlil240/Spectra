#include <algorithm>
#include "transport.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include <spectra/logger.hpp>

#include "codec.hpp"

#ifdef _WIN32
    #include <io.h>
    #include <process.h>
    #define getpid _getpid
namespace
{   // POSIX fd compat wrappers for Windows
inline auto fd_read(int fd, void* buf, unsigned len)
{
    return ::_read(fd, buf, len);
}
inline auto fd_write(int fd, const void* buf, unsigned len)
{
    return ::_write(fd, buf, len);
}
inline void fd_close(int fd)
{
    ::_close(fd);
}
}   // namespace
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <sys/un.h>
    #include <unistd.h>
namespace
{
inline auto fd_read(int fd, void* buf, size_t len)
{
    return ::read(fd, buf, len);
}
inline auto fd_write(int fd, const void* buf, size_t len)
{
    #ifdef MSG_NOSIGNAL
    return ::send(fd, buf, len, MSG_NOSIGNAL);
    #else
    // macOS / BSD: no MSG_NOSIGNAL; SO_NOSIGPIPE is set on each socket instead.
    return ::send(fd, buf, len, 0);
    #endif
}

// Suppress SIGPIPE per-socket on platforms without MSG_NOSIGNAL.
inline void disable_sigpipe(int fd)
{
    #if !defined(MSG_NOSIGNAL) && defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    #else
    (void)fd;
    #endif
}
inline void fd_close(int fd)
{
    ::close(fd);
}
}   // namespace
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
        fd_       = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool Connection::read_exact(uint8_t* buf, size_t len, int first_timeout_ms) const
{
    size_t total = 0;
    while (total < len)
    {
#ifndef _WIN32
        // Bound the wait for data: before the first byte by first_timeout_ms,
        // afterwards by the mid-frame stall timeout.
        const int timeout_ms = (total == 0) ? first_timeout_ms : stall_timeout_ms_;
        if (timeout_ms >= 0)
        {
            struct pollfd pfd
            {
                fd_, POLLIN, 0
            };
            int rc = ::poll(&pfd, 1, timeout_ms);
            if (rc < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (rc == 0)
                return false;   // timed out
        }
#else
        (void)first_timeout_ms;
#endif
        auto n = fd_read(fd_, buf + total, len - total);
        if (n < 0)
        {
#ifndef _WIN32
            if (errno == EINTR)
                continue;   // interrupted by a signal handler without SA_RESTART
#endif
            return false;
        }
        if (n == 0)
            return false;   // EOF
        total += static_cast<size_t>(n);
    }
    return true;
}

bool Connection::write_exact(const uint8_t* buf, size_t len) const
{
    size_t total = 0;
    while (total < len)
    {
        auto n = fd_write(fd_, buf + total, len - total);
        if (n < 0)
        {
#ifndef _WIN32
            if (errno == EINTR)
                continue;   // interrupted by a signal handler without SA_RESTART
#endif
            return false;
        }
        if (n == 0)
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
    if (!write_exact(wire.data(), wire.size()))
    {
        close();
        return false;
    }
    return true;
}

std::optional<Message> Connection::recv()
{
    if (fd_ < 0)
        return std::nullopt;

    // Any failure below leaves the stream at an unknown frame boundary, so the
    // connection is closed (mirrors send()).
    auto fail = [this]() -> std::optional<Message>
    {
        close();
        return std::nullopt;
    };

    // Read fixed header.  Only the first byte may wait up to recv_timeout_ms_;
    // once the frame has started, read_exact() bounds every further wait by
    // stall_timeout_ms_.
    uint8_t hdr_buf[HEADER_SIZE];
    if (!read_exact(hdr_buf, HEADER_SIZE, recv_timeout_ms_))
        return fail();

    auto hdr_opt = decode_header(std::span<const uint8_t>(hdr_buf, HEADER_SIZE));
    if (!hdr_opt)
        return fail();

    auto& hdr = *hdr_opt;
    if (hdr.payload_len > MAX_PAYLOAD_SIZE)
        return fail();

    Message msg;
    msg.header = hdr;

    // Grow the payload buffer as bytes actually arrive instead of committing
    // up to MAX_PAYLOAD_SIZE on the strength of an untrusted header.
    static constexpr size_t RECV_CHUNK = 1u << 20;   // 1 MiB
    const size_t            total_len  = hdr.payload_len;
    size_t                  received   = 0;
    while (received < total_len)
    {
        const size_t chunk = std::min(RECV_CHUNK, total_len - received);
        msg.payload.resize(received + chunk);
        if (!read_exact(msg.payload.data() + received, chunk, stall_timeout_ms_))
            return fail();
        received += chunk;
    }

    return msg;
}

void Connection::close()
{
    if (fd_ >= 0)
    {
        fd_close(fd_);
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
#ifndef _WIN32
    // Remove only a stale socket.  Refuse to overwrite regular files, directories,
    // symlinks, or other filesystem objects that happen to use the requested path,
    // and refuse to steal a socket another server is still accepting on.
    struct stat existing = {};
    if (::lstat(path.c_str(), &existing) == 0)
    {
        if (!S_ISSOCK(existing.st_mode))
            return false;
        if (Client::connect(path))
        {
            errno = EADDRINUSE;
            return false;   // live server — not stale
        }
        if (::unlink(path.c_str()) != 0)
            return false;
    }
    else if (errno != ENOENT)
    {
        return false;
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr
    {
    };
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        SPECTRA_LOG_ERROR("ipc",
                          "Socket path is too long ({} bytes, max {}): {} — set "
                          "XDG_RUNTIME_DIR/TMPDIR or pass a shorter --socket path",
                          path.size(),
                          sizeof(addr.sun_path) - 1,
                          path);
        ::close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    // Create the socket file owner-only from the start (no window between
    // bind() and a later chmod() where other users could connect).
    const mode_t old_umask = ::umask(077);
    const int    bind_rc   = ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ::umask(old_umask);
    if (bind_rc < 0)
    {
        ::close(fd);
        return false;
    }

    if (::listen(fd, 8) < 0)
    {
        ::close(fd);
        ::unlink(path.c_str());
        return false;
    }

    struct stat bound = {};
    if (::lstat(path.c_str(), &bound) != 0)
    {
        ::close(fd);
        ::unlink(path.c_str());
        return false;
    }

    listen_fd_ = fd;
    path_      = path;
    bound_dev_ = static_cast<uint64_t>(bound.st_dev);
    bound_ino_ = static_cast<uint64_t>(bound.st_ino);
    return true;
#else
    (void)path;
    return false;   // UDS not implemented on this platform
#endif
}

std::unique_ptr<Connection> Server::accept() const
{
#ifndef _WIN32
    if (listen_fd_ < 0)
        return nullptr;

    struct sockaddr_un client_addr
    {
    };
    socklen_t client_len = sizeof(client_addr);
    int       client_fd =
        ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0)
        return nullptr;

    disable_sigpipe(client_fd);
    auto conn = std::make_unique<Connection>(client_fd);
    conn->set_stall_timeout_ms(SERVER_STALL_TIMEOUT_MS);
    return conn;
#else
    return nullptr;
#endif
}

std::unique_ptr<Connection> Server::try_accept() const
{
#ifndef _WIN32
    if (listen_fd_ < 0)
        return nullptr;

    // Set listen socket to non-blocking for this accept attempt
    int listen_flags = ::fcntl(listen_fd_, F_GETFL, 0);
    if (listen_flags >= 0)
        ::fcntl(listen_fd_, F_SETFL, listen_flags | O_NONBLOCK);

    struct sockaddr_un client_addr
    {
    };
    socklen_t client_len = sizeof(client_addr);
    int       client_fd =
        ::accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

    // Restore listen socket to blocking
    if (listen_flags >= 0)
        ::fcntl(listen_fd_, F_SETFL, listen_flags);

    if (client_fd < 0)
        return nullptr;   // EAGAIN or error — no pending connection

    // Ensure accepted fd is blocking so recv() stays blocking
    int flags = ::fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(client_fd, F_SETFL, flags & ~O_NONBLOCK);

    // A peer that stalls mid-frame must not pin the caller's poll() loop.
    disable_sigpipe(client_fd);
    auto conn = std::make_unique<Connection>(client_fd);
    conn->set_stall_timeout_ms(SERVER_STALL_TIMEOUT_MS);
    return conn;
#else
    return nullptr;
#endif
}

void Server::close()
{
#ifndef _WIN32
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty())
    {
        // Only remove the path if it still refers to the socket we bound; another
        // server may have replaced it (after our socket went stale) since.
        struct stat st = {};
        if (::lstat(path_.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)
            && static_cast<uint64_t>(st.st_dev) == bound_dev_
            && static_cast<uint64_t>(st.st_ino) == bound_ino_)
        {
            ::unlink(path_.c_str());
        }
        path_.clear();
        bound_dev_ = 0;
        bound_ino_ = 0;
    }
#endif
}

// ─── Client ──────────────────────────────────────────────────────────────────

std::unique_ptr<Connection> Client::connect(const std::string& path)
{
#ifndef _WIN32
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return nullptr;

    struct sockaddr_un addr
    {
    };
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        SPECTRA_LOG_ERROR("ipc",
                          "Socket path is too long ({} bytes, max {}): {}",
                          path.size(),
                          sizeof(addr.sun_path) - 1,
                          path);
        ::close(fd);
        return nullptr;
    }
    disable_sigpipe(fd);
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

std::string runtime_dir()
{
    // Keep in sync with _runtime_socket_dir() in python/spectra/_launcher.py.
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0')
    {
        // Use XDG only when a worst-case `spectra-<pid>.sock` still fits in
        // sun_path; otherwise binding would fail, so fall back to the temp dir.
        const size_t max_len = max_socket_path_length();
        const size_t needed  = std::strlen(xdg) + std::strlen("/spectra-4194304.sock");
        if (max_len == 0 || needed <= max_len)
            return xdg;
        SPECTRA_LOG_DEBUG("ipc", "XDG_RUNTIME_DIR too long for a Unix socket; using temp dir");
    }

    std::error_code ec;
    auto            tmp = std::filesystem::temp_directory_path(ec);
    std::string     dir = ec ? std::string() : tmp.string();
    // Strip a trailing separator (macOS $TMPDIR ends with '/').
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    if (dir.empty())
    {
#ifdef _WIN32
        dir = ".";
#else
        dir = "/tmp";   // invalid $TMPDIR: same fallback as the Python launcher
#endif
    }
    return dir;
}

std::string default_socket_path()
{
    return runtime_dir() + "/spectra-" + std::to_string(::getpid()) + ".sock";
}

size_t max_socket_path_length()
{
#ifndef _WIN32
    return sizeof(sockaddr_un::sun_path) - 1;
#else
    return 0;
#endif
}

}   // namespace spectra::ipc

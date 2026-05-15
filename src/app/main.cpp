// spectra main — launches with empty welcome screen (no figure).
// Use the menu bar to create figures, load CSV, etc.
//
// Auto-attach: if a Spectra daemon is already running (e.g. a topic publisher
// spawned one), re-exec spectra-window so the user attaches to that daemon
// and immediately sees the published topics in the Topics panel.  Opt-out
// with SPECTRA_NO_DAEMON_DISCOVERY=1.

#include <spectra/app.hpp>

#if __has_include(<spectra/version.hpp>)
    #include <spectra/version.hpp>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/un.h>
    #include <sys/wait.h>
    #include <time.h>
    #include <unistd.h>
#endif

namespace
{

#ifndef _WIN32

std::string socket_dir()
{
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
        return xdg;
    return "/tmp";
}

// Probe a Unix socket path: returns true if a server is accepting connections.
bool socket_is_live(const std::string& path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
    {
        ::close(fd);
        return false;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

// Find a live `spectra-*.sock` in the runtime dir, newest first.
std::string discover_live_daemon_socket()
{
    namespace fs = std::filesystem;
    std::vector<std::pair<std::string, fs::file_time_type>> hits;
    std::error_code                                         ec;
    for (auto& e : fs::directory_iterator(socket_dir(), ec))
    {
        if (ec)
            break;
        const auto& p    = e.path();
        const auto  name = p.filename().string();
        if (name.rfind("spectra-", 0) != 0 || p.extension() != ".sock")
            continue;
        auto mtime = fs::last_write_time(p, ec);
        if (ec)
        {
            ec.clear();
            continue;
        }
        hits.emplace_back(p.string(), mtime);
    }
    std::sort(hits.begin(),
              hits.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& h : hits)
    {
        if (socket_is_live(h.first))
            return h.first;
    }
    return {};
}

// Find `spectra-window` next to our own binary or on $PATH.
std::string locate_spectra_window()
{
    char        buf[4096];
    ssize_t     n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string dir;
    if (n > 0)
    {
        buf[n] = '\0';
        std::string self(buf);
        auto        slash = self.rfind('/');
        if (slash != std::string::npos)
            dir = self.substr(0, slash + 1);
    }
    std::string candidate = dir + "spectra-window";
    if (::access(candidate.c_str(), X_OK) == 0)
        return candidate;
    return "spectra-window";   // fall back to PATH
}

std::string locate_spectra_backend()
{
    char        buf[4096];
    ssize_t     n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string dir;
    if (n > 0)
    {
        buf[n] = '\0';
        std::string self(buf);
        auto        slash = self.rfind('/');
        if (slash != std::string::npos)
            dir = self.substr(0, slash + 1);
    }
    std::string candidate = dir + "spectra-backend";
    if (::access(candidate.c_str(), X_OK) == 0)
        return candidate;
    return "spectra-backend";   // fall back to PATH
}

// Spawn a fresh daemon on a per-process socket and wait until it's listening.
// Returns the socket path on success, empty string on failure.
std::string spawn_fresh_daemon()
{
    std::string sock = socket_dir() + "/spectra-" + std::to_string(::getpid()) + ".sock";
    // Clean any leftover socket from a previous run with this pid (unlikely
    // but harmless).
    ::unlink(sock.c_str());

    auto  backend_bin = locate_spectra_backend();
    pid_t pid         = ::fork();
    if (pid < 0)
        return {};
    if (pid == 0)
    {
        // Child: detach from parent's controlling tty so we survive when the
        // user closes the window terminal, then exec the backend.
        ::setsid();
        ::execlp(backend_bin.c_str(),
                 backend_bin.c_str(),
                 "--socket",
                 sock.c_str(),
                 static_cast<const char*>(nullptr));
        ::_exit(127);
    }

    // Parent: wait up to ~2s for the socket to become live.
    for (int i = 0; i < 40; ++i)
    {
        timespec ts{0, 50 * 1000 * 1000};   // 50 ms
        ::nanosleep(&ts, nullptr);
        if (socket_is_live(sock))
            return sock;
        // If the child died early, bail out.
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) == pid)
            return {};
    }
    return {};
}

// Returns true if we replaced this process with spectra-window.
// On any failure, returns false and the caller proceeds with the normal
// in-process app.
bool maybe_attach_to_running_daemon()
{
    const char* off = std::getenv("SPECTRA_NO_DAEMON_DISCOVERY");
    if (off && off[0] != '\0' && off[0] != '0')
        return false;
    if (const char* sock_env = std::getenv("SPECTRA_SOCKET"); sock_env && *sock_env)
        return false;   // user explicitly configured a socket; let App::run() handle it

    std::string sock = discover_live_daemon_socket();
    if (sock.empty())
    {
        // No daemon yet — start one so the Topics workflow is available even
        // before any publisher connects.  Without this, `spectra` runs purely
        // in-process and the Topics panel has no transport to talk to.
        sock = spawn_fresh_daemon();
        if (sock.empty())
        {
            std::cerr << "[spectra] Could not start a daemon — running in-process\n";
            return false;
        }
        std::cerr << "[spectra] Started fresh daemon at " << sock << "\n";
    }
    else
    {
        std::cerr << "[spectra] Found running daemon at " << sock << "\n";
    }

    auto window_bin = locate_spectra_window();
    ::execlp(window_bin.c_str(),
             window_bin.c_str(),
             "--socket",
             sock.c_str(),
             static_cast<const char*>(nullptr));
    // execlp only returns on failure.
    std::cerr << "[spectra] Failed to exec " << window_bin << " — continuing in-process\n";
    return false;
}

#else   // _WIN32

bool maybe_attach_to_running_daemon()
{
    return false;
}

#endif

}   // namespace

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0)
        {
#ifdef SPECTRA_VERSION_STRING
            std::cout << "spectra " << SPECTRA_VERSION_STRING << "\n";
#else
            std::cout << "spectra (version unknown)\n";
#endif
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            std::cout << "Usage: spectra [OPTIONS]\n"
                      << "\n"
                      << "GPU-accelerated scientific plotting application.\n"
                      << "\n"
                      << "Options:\n"
                      << "  --version, -v    Print version and exit\n"
                      << "  --help, -h       Show this help\n"
                      << "\n"
                      << "Environment:\n"
                      << "  SPECTRA_NO_DAEMON_DISCOVERY=1   Stay in-process even if a\n"
                      << "                                  Spectra daemon is running.\n";
            return 0;
        }
    }

    // If a publisher (or another tool) has already spawned a daemon, attach
    // to it transparently so its topics are immediately visible.
    maybe_attach_to_running_daemon();

    spectra::App app;
    // No figures created → build_empty_ui() renders the welcome screen
    app.run();
    return 0;
}

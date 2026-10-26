// spectra main — launches with empty welcome screen (no figure).
// Use the menu bar to create figures, load CSV, etc.
//
// Auto-attach: if a Spectra daemon is already running (e.g. a topic publisher
// spawned one), re-exec spectra-window so the user attaches to that daemon
// and immediately sees the published topics in the Topics panel.  Opt-out
// with SPECTRA_NO_DAEMON_DISCOVERY=1.

#include <spectra/app.hpp>
#include <spectra/logger.hpp>

#include "ui/native_dialog_policy.hpp"

#ifndef _WIN32
    #include "ipc/codec.hpp"
    #include "ipc/message.hpp"
    #include "ipc/transport.hpp"
#endif

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
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <sys/un.h>
    #include <sys/wait.h>
    #include <ctime>
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

// Upper bound on how long the daemon probe waits for a WELCOME reply, so a
// wedged or foreign peer cannot hang startup.
constexpr int DAEMON_PROBE_TIMEOUT_MS = 500;

// Probe a Unix socket path: returns true only if a real multiproc daemon
// (spectra-backend) answers on it.  Other Spectra sockets share the
// spectra-*.sock naming — notably the InprocTopicServer of a running in-process
// `spectra` (WELCOME.mode == "inproc"), which only speaks the publisher subset
// of the protocol and can never drive a spectra-window agent.
//
// The probe identifies itself as a "publisher" so the daemon does not register
// it as a render agent; it declares no topics and disconnects right away.
bool socket_is_live_daemon(const std::string& path)
{
    auto conn = spectra::ipc::Client::connect(path);
    if (!conn || !conn->is_open())
        return false;
    conn->set_recv_timeout_ms(DAEMON_PROBE_TIMEOUT_MS);
    conn->set_stall_timeout_ms(DAEMON_PROBE_TIMEOUT_MS);

    spectra::ipc::HelloPayload hello;
    hello.protocol_major = spectra::ipc::PROTOCOL_MAJOR;
    hello.protocol_minor = spectra::ipc::PROTOCOL_MINOR;
    hello.agent_build    = "spectra-daemon-probe";
    hello.client_type    = "publisher";
    hello.capabilities   = spectra::ipc::CAPABILITY_FLATBUFFERS;

    spectra::ipc::Message m;
    m.header.type        = spectra::ipc::MessageType::HELLO;
    m.payload            = spectra::ipc::encode_hello(hello);
    m.header.payload_len = static_cast<uint32_t>(m.payload.size());
    if (!conn->send(m))
        return false;

    auto reply = conn->recv();
    if (!reply || reply->header.type != spectra::ipc::MessageType::WELCOME)
        return false;
    auto welcome = spectra::ipc::decode_welcome(reply->payload);
    if (!welcome)
        return false;
    return welcome->mode != "inproc";
}

// Find a live daemon `spectra-*.sock` in the runtime dir, newest first.
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
        // The automation server's spectra-auto-<pid>.sock (JSON lines) lands
        // here too when XDG_RUNTIME_DIR is unset; it is never a daemon.
        if (name.rfind("spectra-auto-", 0) == 0)
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
        if (socket_is_live_daemon(h.first))
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

// Returns true if we replaced this process with spectra-window.
// On any failure, returns false and the caller proceeds with the normal
// in-process app.
//
// Policy:
//   - If the user already pointed us at a socket via $SPECTRA_SOCKET → leave
//     it to App::run() (it will connect as a window agent).
//   - If a daemon is already running and listening on the standard runtime
//     dir → attach to it (re-exec spectra-window).
//   - Otherwise → stay in-process.  The in-process `InprocTopicServer` will
//     listen on $XDG_RUNTIME_DIR/spectra-<pid>.sock so publishers can still
//     find us, and the user sees the welcome screen.
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
        // No existing daemon — stay in-process.  The InprocTopicServer
        // started by App::run() will accept publishers directly, and the
        // welcome screen renders normally.
        return false;
    }
    SPECTRA_LOG_INFO("app", "Found running daemon at {}", sock);

    auto window_bin = locate_spectra_window();
    ::execlp(window_bin.c_str(),
             window_bin.c_str(),
             "--socket",
             sock.c_str(),
             static_cast<const char*>(nullptr));
    // execlp only returns on failure.
    SPECTRA_LOG_WARN("app", "Failed to exec {} — continuing in-process", window_bin);
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
    spectra::init_native_dialog_policy(argc, argv);

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
                      << "  --no-native-dialogs\n"
                      << "                   Suppress native OS file dialogs (automation / fuzz).\n"
                      << "  --version, -v    Print version and exit\n"
                      << "  --help, -h       Show this help\n"
                      << "\n"
                      << "Environment:\n"
                      << "  SPECTRA_NO_DAEMON_DISCOVERY=1   Stay in-process even if a\n"
                      << "                                  Spectra daemon is running.\n"
                      << "  SPECTRA_NO_NATIVE_DIALOGS=1     Suppress native file dialogs.\n"
                      << "  SPECTRA_AUTOMATION=1            Alias for SPECTRA_NO_NATIVE_DIALOGS.\n";
            return 0;
        }
    }

    // If a publisher (or another tool) has already spawned a daemon, attach
    // to it transparently so its topics are immediately visible.
    spectra::setup_dual_logging(spectra::default_console_log_level(),
                                spectra::default_file_log_level());
    maybe_attach_to_running_daemon();

    spectra::App app;
    // No figures created → build_empty_ui() renders the welcome screen
    app.run();
    return 0;
}

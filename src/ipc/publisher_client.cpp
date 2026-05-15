#include <spectra/topic.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <spectra/logger.hpp>
#include <thread>
#include <vector>

#ifndef _WIN32
    #include <signal.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include "codec.hpp"
#include "message.hpp"
#include "transport.hpp"

namespace spectra
{

namespace
{

ipc::TopicKind to_ipc_kind(Publisher::Kind k)
{
    return (k == Publisher::Kind::Scalar3D) ? ipc::TopicKind::Scalar3D : ipc::TopicKind::Scalar2D;
}

size_t stride_for(Publisher::Kind k)
{
    return (k == Publisher::Kind::Scalar3D) ? 3u : 2u;
}

std::string socket_dir()
{
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg)
        return xdg;
    return "/tmp";
}

// Scan the socket directory for existing `spectra-*.sock` files and return
// them most-recently-modified-first.  Skips broken / non-socket entries.
std::vector<std::string> discover_candidate_sockets()
{
    namespace fs = std::filesystem;
    std::vector<std::pair<std::string, fs::file_time_type>> hits;
    std::error_code                                         ec;
    const auto                                              dir = socket_dir();
    for (auto& e : fs::directory_iterator(dir, ec))
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
    std::vector<std::string> out;
    out.reserve(hits.size());
    for (auto& h : hits)
        out.push_back(std::move(h.first));
    return out;
}

// Try to connect to `path` (transient — does not retain failures).
std::unique_ptr<ipc::Connection> try_connect(const std::string& path)
{
    auto c = ipc::Client::connect(path);
    if (c && c->is_open())
        return c;
    return nullptr;
}

#ifndef _WIN32
// Locate `spectra-backend`: next to /proc/self/exe, then on $PATH.
std::string locate_backend_binary()
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
    for (const auto& candidate :
         {dir + "spectra-backend",
          (dir.size() > 1 ? dir.substr(0, dir.rfind('/', dir.size() - 2) + 1) : std::string{})
              + "spectra-backend"})
    {
        if (!candidate.empty() && ::access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    return "spectra-backend";   // rely on $PATH
}

// Fork+exec the backend.  Returns the pid (>0 on success).
pid_t spawn_backend(const std::string& sock_path)
{
    const std::string bin = locate_backend_binary();
    pid_t             pid = ::fork();
    if (pid == 0)
    {
        ::execlp(bin.c_str(), bin.c_str(), "--socket", sock_path.c_str(), nullptr);
        ::_exit(127);
    }
    return pid;
}
#endif

// Resolve the daemon connection.  Strategy:
//   1. opts.socket_path                    — caller override
//   2. $SPECTRA_SOCKET                     — explicit env override
//   3. Discover existing spectra-*.sock    — pick an already-running daemon
//   4. opts.auto_spawn_daemon              — spawn a fresh backend
//
// Returns an open connection and writes the chosen socket path to *out_path,
// or nullptr if no daemon could be reached.
std::unique_ptr<ipc::Connection> resolve_and_connect(const Publisher::Options& opts,
                                                     std::string*              out_path)
{
    auto take = [&](std::unique_ptr<ipc::Connection> c, std::string path)
    {
        if (out_path)
            *out_path = std::move(path);
        return c;
    };

    if (!opts.socket_path.empty())
    {
        if (auto c = try_connect(opts.socket_path))
            return take(std::move(c), opts.socket_path);
        return nullptr;   // explicit request → no fallback
    }
    if (const char* env = std::getenv("SPECTRA_SOCKET"); env && *env)
    {
        if (auto c = try_connect(env))
            return take(std::move(c), env);
        return nullptr;   // explicit request → no fallback
    }

    // Discover any live daemon.
    for (const auto& p : discover_candidate_sockets())
    {
        if (auto c = try_connect(p))
            return take(std::move(c), p);
    }

#ifndef _WIN32
    if (opts.auto_spawn_daemon)
    {
        const std::string sock = socket_dir() + "/spectra-" + std::to_string(::getpid()) + ".sock";
        pid_t             pid  = spawn_backend(sock);
        if (pid > 0)
        {
            for (int i = 0; i < 50; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (auto c = try_connect(sock))
                    return take(std::move(c), sock);
            }
            ::kill(pid, SIGTERM);
        }
    }
#endif
    return nullptr;
}

}   // namespace

struct Publisher::Impl
{
    std::unique_ptr<ipc::Connection> conn;
    std::string                      name;
    Kind                             kind            = Kind::Scalar2D;
    uint64_t                         next_request_id = 1;
    ipc::SessionId                   session_id      = ipc::INVALID_SESSION;

    bool send_request_and_wait_ok(ipc::MessageType type, std::vector<uint8_t> payload)
    {
        if (!conn || !conn->is_open())
            return false;
        ipc::Message msg;
        msg.header.type        = type;
        msg.header.request_id  = next_request_id++;
        msg.header.session_id  = session_id;
        msg.payload            = std::move(payload);
        msg.header.payload_len = static_cast<uint32_t>(msg.payload.size());
        if (!conn->send(msg))
            return false;
        // Wait for ack — RESP_OK or RESP_ERR, ignore stray events.
        for (int i = 0; i < 32; ++i)
        {
            auto reply = conn->recv();
            if (!reply)
                return false;
            if (reply->header.request_id != msg.header.request_id)
                continue;   // unrelated event, keep reading
            return reply->header.type == ipc::MessageType::RESP_OK;
        }
        return false;
    }
};

Publisher::~Publisher()                               = default;
Publisher::Publisher(Publisher&&) noexcept            = default;
Publisher& Publisher::operator=(Publisher&&) noexcept = default;

std::unique_ptr<Publisher> Publisher::create(std::string_view name)
{
    return Publisher::create(name, Publisher::Options{});
}

std::unique_ptr<Publisher> Publisher::create(std::string_view name, const Options& opts)
{
    if (name.empty())
        return nullptr;

    std::string socket;
    auto        conn = resolve_and_connect(opts, &socket);
    if (!conn || !conn->is_open())
    {
        SPECTRA_LOG_ERROR("publisher",
                          "Failed to reach spectra-backend"
                          " (tried explicit, $SPECTRA_SOCKET, discovery{})",
                          opts.auto_spawn_daemon ? ", auto-spawn" : "");
        return nullptr;
    }

    // ── HELLO ────────────────────────────────────────────────────────────
    ipc::HelloPayload hello;
    hello.protocol_major = ipc::PROTOCOL_MAJOR;
    hello.protocol_minor = ipc::PROTOCOL_MINOR;
    hello.agent_build    = "spectra-publisher/0.1";
    hello.client_type    = "publisher";
    hello.capabilities   = ipc::CAPABILITY_FLATBUFFERS;
    {
        ipc::Message m;
        m.header.type        = ipc::MessageType::HELLO;
        m.payload            = ipc::encode_hello(hello);
        m.header.payload_len = static_cast<uint32_t>(m.payload.size());
        if (!conn->send(m))
            return nullptr;
    }

    auto welcome_msg = conn->recv();
    if (!welcome_msg || welcome_msg->header.type != ipc::MessageType::WELCOME)
    {
        SPECTRA_LOG_ERROR("publisher", "No WELCOME from daemon");
        return nullptr;
    }
    auto welcome = ipc::decode_welcome(welcome_msg->payload);
    if (!welcome)
        return nullptr;

    auto pub        = std::unique_ptr<Publisher>(new Publisher());
    pub->impl_      = std::make_unique<Impl>();
    auto& impl      = *pub->impl_;
    impl.conn       = std::move(conn);
    impl.name       = std::string(name);
    impl.kind       = opts.kind;
    impl.session_id = welcome->session_id;

    // ── DECLARE TOPIC ───────────────────────────────────────────────────
    ipc::ReqDeclareTopicPayload decl;
    decl.name          = impl.name;
    decl.kind          = to_ipc_kind(opts.kind);
    decl.unit          = opts.unit;
    decl.ring_capacity = opts.ring_capacity;
    if (!impl.send_request_and_wait_ok(ipc::MessageType::REQ_DECLARE_TOPIC,
                                       ipc::encode_req_declare_topic(decl)))
    {
        SPECTRA_LOG_ERROR("publisher", "REQ_DECLARE_TOPIC failed for '{}'", impl.name);
        return nullptr;
    }
    return pub;
}

bool Publisher::publish(double x, double y)
{
    if (!impl_ || impl_->kind != Kind::Scalar2D)
        return false;
    double buf[2] = {x, y};
    return publish(std::span<const double>(buf, 2));
}

bool Publisher::publish(double x, double y, double z)
{
    if (!impl_ || impl_->kind != Kind::Scalar3D)
        return false;
    double buf[3] = {x, y, z};
    return publish(std::span<const double>(buf, 3));
}

bool Publisher::publish(std::span<const double> interleaved)
{
    if (!impl_ || !impl_->conn || !impl_->conn->is_open())
        return false;
    if (interleaved.empty())
        return true;
    size_t stride = stride_for(impl_->kind);
    if (interleaved.size() % stride != 0)
        return false;

    ipc::ReqPublishTopicSamplesPayload p;
    p.name = impl_->name;
    p.samples.assign(interleaved.begin(), interleaved.end());
    return impl_->send_request_and_wait_ok(ipc::MessageType::REQ_PUBLISH_TOPIC_SAMPLES,
                                           ipc::encode_req_publish_topic_samples(p));
}

bool Publisher::is_connected() const noexcept
{
    return impl_ && impl_->conn && impl_->conn->is_open();
}

std::string_view Publisher::name() const noexcept
{
    return impl_ ? std::string_view{impl_->name} : std::string_view{};
}

Publisher::Kind Publisher::kind() const noexcept
{
    return impl_ ? impl_->kind : Kind::Scalar2D;
}

}   // namespace spectra

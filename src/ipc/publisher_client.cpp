#include <spectra/topic.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include <spectra/logger.hpp>

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

// Scan the runtime dir for `spectra-<pid>.sock` entries (most recent first).
// The broker writes its socket as spectra-<broker_pid>.sock so the publisher
// must NOT use its own PID — we discover the broker socket instead.
std::vector<std::string> discover_candidate_sockets()
{
    namespace fs = std::filesystem;
    std::vector<std::pair<fs::file_time_type, std::string>> entries;
    std::error_code                                         ec;
    const std::string                                       dir = socket_dir();
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator{};
         it.increment(ec))
    {
        const auto& p = it->path();
        if (p.filename().string().rfind("spectra-", 0) != 0)
            continue;
        if (p.extension() != ".sock")
            continue;
        std::error_code stat_ec;
        auto            mt = fs::last_write_time(p, stat_ec);
        if (stat_ec)
            continue;
        entries.emplace_back(mt, p.string());
    }
    std::sort(entries.begin(),
              entries.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (auto& e : entries)
        out.push_back(std::move(e.second));
    return out;
}

// Resolve the socket path with priority:
//   1. explicit_path (user-supplied)
//   2. $SPECTRA_SOCKET
//   3. most-recent spectra-*.sock in the runtime dir (broker discovery)
//   4. ipc::default_socket_path() as a last resort
std::string resolve_socket(const std::string& explicit_path)
{
    if (!explicit_path.empty())
        return explicit_path;
    if (const char* env = std::getenv("SPECTRA_SOCKET"); env && *env)
        return env;
    auto candidates = discover_candidate_sockets();
    if (!candidates.empty())
        return candidates.front();
    return ipc::default_socket_path();
}

}   // namespace

struct Publisher::Impl
{
    std::unique_ptr<ipc::Connection> conn;
    std::string                      name;
    Kind                             kind            = Kind::Scalar2D;
    uint64_t                         next_request_id = 1;
    ipc::SessionId                   session_id      = ipc::INVALID_SESSION;

    // Reconnect state (set by create()).
    // explicit_socket_path is non-empty when the user pinned a specific path
    // (either via Options::socket_path or $SPECTRA_SOCKET). When empty, each
    // reconnect re-discovers the freshest spectra-*.sock so the publisher
    // follows broker PID changes across restarts.
    std::string                                explicit_socket_path;
    std::string                                socket_path;
    std::string                                unit;
    uint32_t                                   ring_capacity = 4096;
    std::chrono::steady_clock::time_point      next_reconnect_attempt{};
    static constexpr std::chrono::milliseconds reconnect_backoff{500};

    // Establish socket + HELLO + DECLARE_TOPIC.  Replaces any existing
    // connection.  Returns true on success.
    bool connect_and_declare()
    {
        conn.reset();

        // Re-resolve the socket path on every attempt so we pick up the new
        // broker PID after a Spectra restart.
        socket_path = resolve_socket(explicit_socket_path);

        auto c = ipc::Client::connect(socket_path);
        if (!c || !c->is_open())
            return false;

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
            if (!c->send(m))
                return false;
        }

        auto welcome_msg = c->recv();
        if (!welcome_msg || welcome_msg->header.type != ipc::MessageType::WELCOME)
            return false;
        auto welcome = ipc::decode_welcome(welcome_msg->payload);
        if (!welcome)
            return false;

        conn       = std::move(c);
        session_id = welcome->session_id;

        ipc::ReqDeclareTopicPayload decl;
        decl.name          = name;
        decl.kind          = to_ipc_kind(kind);
        decl.unit          = unit;
        decl.ring_capacity = ring_capacity;
        if (!send_request_and_wait_ok(ipc::MessageType::REQ_DECLARE_TOPIC,
                                      ipc::encode_req_declare_topic(decl)))
        {
            conn.reset();
            return false;
        }
        return true;
    }

    // Try to reconnect, rate-limited by reconnect_backoff.  Returns true if
    // the connection is alive after the call (either it already was or the
    // reconnect succeeded).
    bool maybe_reconnect()
    {
        if (conn && conn->is_open())
            return true;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_reconnect_attempt)
            return false;
        next_reconnect_attempt = now + reconnect_backoff;
        return connect_and_declare();
    }

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

    auto pub           = std::unique_ptr<Publisher>(new Publisher());
    pub->impl_         = std::make_unique<Impl>();
    auto& impl         = *pub->impl_;
    impl.name          = std::string(name);
    impl.kind          = opts.kind;
    impl.unit          = opts.unit;
    impl.ring_capacity = opts.ring_capacity;
    // Honour an explicit path (Options) OR $SPECTRA_SOCKET as a pinned target.
    // Otherwise leave empty so connect_and_declare() rediscovers each attempt.
    if (!opts.socket_path.empty())
        impl.explicit_socket_path = opts.socket_path;
    else if (const char* env = std::getenv("SPECTRA_SOCKET"); env && *env)
        impl.explicit_socket_path = env;

    if (!impl.connect_and_declare())
    {
        SPECTRA_LOG_DEBUG("publisher",
                          "spectra not running \u2014 publisher '{}' will auto-reconnect when the "
                          "daemon starts (samples silently dropped until then)",
                          impl.name);
        // Return the publisher in a disconnected state instead of nullptr.
        // publish() already handles a null conn: it calls maybe_reconnect() which
        // is rate-limited and drops samples silently, keeping the caller's loop alive.
        // This prevents a crash when the caller calls publish() without checking for null.
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
    if (!impl_)
        return false;
    if (interleaved.empty())
        return true;
    size_t stride = stride_for(impl_->kind);
    if (interleaved.size() % stride != 0)
        return false;

    ipc::ReqPublishTopicSamplesPayload p;
    p.name = impl_->name;
    p.samples.assign(interleaved.begin(), interleaved.end());

    // First attempt — on the existing connection if any.
    if (impl_->conn && impl_->conn->is_open())
    {
        if (impl_->send_request_and_wait_ok(ipc::MessageType::REQ_PUBLISH_TOPIC_SAMPLES,
                                            ipc::encode_req_publish_topic_samples(p)))
            return true;
        // Send/recv failed — broker likely went away.  Drop the connection
        // and fall through to reconnect.
        impl_->conn.reset();
    }

    // Try to reconnect (rate-limited).  If the broker isn't back yet, drop
    // this sample silently and report success so the caller's loop keeps
    // running — the publisher will reattach as soon as the broker returns.
    if (!impl_->maybe_reconnect())
        return true;

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

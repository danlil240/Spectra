#include "udp_json_bridge.hpp"

#include <charconv>
#include <cstring>
#include <vector>

#include "spectra/logger.hpp"
#include "text_message.hpp"

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <cerrno>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace spectra::daemon
{

namespace
{

// Largest UDP payload over IPv4.
constexpr size_t MAX_DATAGRAM = 65507;

// How often the bridge thread checks for stop().
constexpr int POLL_TIMEOUT_MS = 100;

uint64_t steady_now_ns()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

}   // namespace

bool parse_udp_bridge_spec(std::string_view spec, UdpBridgeConfig& cfg, std::string* error)
{
    auto set_error = [error](const std::string& msg)
    {
        if (error)
            *error = msg;
        return false;
    };

    std::string_view host;
    std::string_view port_str = spec;
    if (auto colon = spec.rfind(':'); colon != std::string_view::npos)
    {
        host     = spec.substr(0, colon);
        port_str = spec.substr(colon + 1);
        if (host.empty())
            return set_error("empty host in UDP bridge spec '" + std::string(spec) + "'");
    }

    unsigned port = 0;
    auto     res  = std::from_chars(port_str.data(), port_str.data() + port_str.size(), port);
    if (port_str.empty() || res.ec != std::errc{} || res.ptr != port_str.data() + port_str.size()
        || port > 65535)
    {
        return set_error("invalid UDP port in '" + std::string(spec) + "'");
    }

    if (!host.empty())
    {
#ifndef _WIN32
        in_addr addr{};
        if (host != "localhost" && ::inet_pton(AF_INET, std::string(host).c_str(), &addr) != 1)
            return set_error("UDP bridge host must be an IPv4 address: '" + std::string(host)
                             + "'");
#endif
        cfg.bind_address = std::string(host);
    }
    cfg.port = static_cast<uint16_t>(port);
    return true;
}

bool is_loopback_address(std::string_view address)
{
    return address == "localhost" || address.substr(0, 4) == "127.";
}

UdpJsonBridge::UdpJsonBridge(UdpBridgeConfig config) : config_(std::move(config)) {}

UdpJsonBridge::~UdpJsonBridge()
{
    stop();
}

std::string UdpJsonBridge::describe() const
{
    uint16_t port = bound_port_.load();
    return "udp://" + config_.bind_address + ":" + std::to_string(port ? port : config_.port)
           + " -> " + config_.prefix + "/*";
}

bool UdpJsonBridge::is_running() const
{
    return running_.load(std::memory_order_acquire);
}

TopicBridgeStats UdpJsonBridge::stats() const
{
    std::lock_guard<std::mutex> lk(stats_mutex_);
    return stats_;
}

bool UdpJsonBridge::start(TopicSink& sink, uint64_t owner_id, std::string* error)
{
    auto set_error = [error](const std::string& msg)
    {
        if (error)
            *error = msg;
        return false;
    };

    if (running_.load())
        return set_error("UDP bridge already running");

#ifdef _WIN32
    (void)sink;
    (void)owner_id;
    return set_error("UDP bridge is not supported on Windows yet");
#else
    in_addr           addr{};
    const std::string host =
        config_.bind_address == "localhost" ? "127.0.0.1" : config_.bind_address;
    if (::inet_pton(AF_INET, host.c_str(), &addr) != 1)
        return set_error("invalid UDP bind address '" + config_.bind_address + "'");

    int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return set_error(std::string("socket(): ") + std::strerror(errno));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(config_.port);
    sa.sin_addr   = addr;
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) != 0)
    {
        std::string msg = "bind(" + config_.bind_address + ":" + std::to_string(config_.port)
                          + "): " + std::strerror(errno);
        ::close(fd);
        return set_error(msg);
    }

    sockaddr_in bound{};
    socklen_t   len = sizeof(bound);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) == 0)
        bound_port_.store(ntohs(bound.sin_port));
    else
        bound_port_.store(config_.port);

    if (!is_loopback_address(config_.bind_address))
    {
        SPECTRA_LOG_WARN("bridge",
                         "UDP bridge listening on {} — any host that can reach this port can "
                         "publish topics (no authentication)",
                         describe());
    }

    fd_         = fd;
    sink_       = &sink;
    owner_id_   = owner_id;
    start_time_ = std::chrono::steady_clock::now();
    topics_.clear();
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        stats_ = {};
    }
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this]() { run(); });
    return true;
#endif
}

void UdpJsonBridge::stop()
{
    running_.store(false, std::memory_order_release);
    if (thread_.joinable())
        thread_.join();
#ifndef _WIN32
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    sink_ = nullptr;
}

void UdpJsonBridge::run()
{
#ifndef _WIN32
    std::vector<char> buf(MAX_DATAGRAM + 1);
    while (running_.load(std::memory_order_acquire))
    {
        pollfd pfd{fd_, POLLIN, 0};
        int    r = ::poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (r <= 0 || (pfd.revents & POLLIN) == 0)
            continue;   // timeout or EINTR: re-check running_

        // Drain everything that is ready before polling again.
        while (true)
        {
            ssize_t n = ::recv(fd_, buf.data(), MAX_DATAGRAM, MSG_DONTWAIT);
            if (n < 0)
                break;   // EAGAIN (drained) or a transient error
            handle_datagram(std::string_view(buf.data(), static_cast<size_t>(n)), steady_now_ns());
        }
    }
#endif
}

void UdpJsonBridge::handle_datagram(std::string_view data, uint64_t receive_ns)
{
    TopicSink* sink = sink_;
    if (!sink)
        return;

    std::vector<std::vector<FlatField>> messages;
    const size_t                        errors = decode_text_datagram(data, messages);

    const double receive_s = std::chrono::duration<double>(std::chrono::steady_clock::time_point(
                                                               std::chrono::nanoseconds(receive_ns))
                                                           - start_time_)
                                 .count();

    uint64_t parsed = 0, published = 0, dropped = 0;
    for (const auto& fields : messages)
    {
        TopicIngestBatch batch;
        batch.owner_id      = owner_id_;
        batch.receive_ns    = receive_ns;
        batch.ring_capacity = config_.ring_capacity;
        size_t skipped      = 0;
        fan_out_message(fields, config_.prefix, receive_s, batch.fields, &skipped);
        dropped += skipped;

        // Enforce the per-bridge topic cap on names never seen before.
        std::erase_if(batch.fields,
                      [&](const TopicFieldSample& s)
                      {
                          if (topics_.count(s.name) != 0)
                              return false;
                          if (topics_.size() >= config_.max_topics)
                          {
                              ++dropped;
                              return true;
                          }
                          topics_.insert(s.name);
                          return false;
                      });
        if (batch.fields.empty())
            continue;

        ++parsed;
        const size_t n = batch.fields.size();
        if (sink->push(std::move(batch)))
            published += n;
        else
            dropped += n;
    }

    std::lock_guard<std::mutex> lk(stats_mutex_);
    stats_.messages_received += 1;
    stats_.messages_parsed += parsed;
    stats_.parse_errors += errors;
    stats_.samples_published += published;
    stats_.samples_dropped += dropped;
    stats_.topics_seen = static_cast<uint32_t>(topics_.size());
}

}   // namespace spectra::daemon

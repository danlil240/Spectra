// Regression tests for ipc transport / publisher discovery fixes.
// POSIX-only: the transport uses Unix domain sockets.

#ifndef _WIN32

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include <spectra/topic.hpp>

#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ipc/transport.hpp"

#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace spectra::ipc;
namespace fs = std::filesystem;

namespace
{

std::string unique_sock(const std::string& tag)
{
    static std::atomic<int> counter{0};
    return "/tmp/spectra-bugfix-" + tag + "-" + std::to_string(::getpid()) + "-"
           + std::to_string(counter++) + ".sock";
}

// Create a socket file at `path` with nothing listening on it (stale socket).
bool make_stale_socket(const std::string& path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    bool ok = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

void noop_handler(int) {}

// Saves and restores an environment variable.
class EnvGuard
{
   public:
    explicit EnvGuard(const char* name) : name_(name)
    {
        if (const char* v = std::getenv(name))
            old_ = v;
    }
    ~EnvGuard()
    {
        if (old_)
            ::setenv(name_, old_->c_str(), 1);
        else
            ::unsetenv(name_);
    }

   private:
    const char*                name_;
    std::optional<std::string> old_;
};

}   // namespace

// ─── Bug #89: EINTR is retried, not treated as connection loss ──────────────

TEST(BugfixIpcTransport, RecvRetriesOnEintr)
{
    struct sigaction sa
    {
    };
    struct sigaction old_sa
    {
    };
    sa.sa_handler = noop_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // no SA_RESTART: blocking read() returns EINTR
    ASSERT_EQ(::sigaction(SIGUSR1, &sa, &old_sa), 0);

    const std::string path = unique_sock("eintr");
    Server            server;
    ASSERT_TRUE(server.listen(path));

    auto client = Client::connect(path);
    ASSERT_NE(client, nullptr);
    auto server_conn = server.accept();
    ASSERT_NE(server_conn, nullptr);

    std::atomic<bool>      started{false};
    std::optional<Message> got;
    pthread_t              reader_tid{};
    std::thread            reader(
        [&]()
        {
            reader_tid = ::pthread_self();
            started    = true;
            got        = server_conn->recv();
        });
    while (!started)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ::pthread_kill(reader_tid, SIGUSR1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Message m;
    m.header.type = MessageType::EVT_HEARTBEAT;
    m.payload     = {1, 2, 3};
    ASSERT_TRUE(client->send(m));
    reader.join();

    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->payload.size(), 3u);
    EXPECT_TRUE(server_conn->is_open());

    ::sigaction(SIGUSR1, &old_sa, nullptr);
    server.close();
}

// ─── Bug #42: a peer stalling mid-frame cannot block recv() forever ─────────

TEST(BugfixIpcTransport, StallMidHeaderTimesOut)
{
    const std::string path = unique_sock("stallhdr");
    Server            server;
    ASSERT_TRUE(server.listen(path));
    auto client = Client::connect(path);
    ASSERT_NE(client, nullptr);
    auto server_conn = server.accept();
    ASSERT_NE(server_conn, nullptr);
    server_conn->set_stall_timeout_ms(100);

    Message m;
    m.header.type = MessageType::EVT_HEARTBEAT;
    auto wire     = encode_message(m);
    ASSERT_EQ(::send(client->fd(), wire.data(), 10, MSG_NOSIGNAL), 10);

    auto t0  = std::chrono::steady_clock::now();
    auto got = server_conn->recv();
    auto dt  = std::chrono::steady_clock::now() - t0;
    EXPECT_FALSE(got.has_value());
    EXPECT_LT(dt, std::chrono::seconds(3));
    EXPECT_FALSE(server_conn->is_open());   // stream desynced -> closed
    server.close();
}

TEST(BugfixIpcTransport, StallMidPayloadWithHugeLengthTimesOut)
{
    const std::string path = unique_sock("stallpl");
    Server            server;
    ASSERT_TRUE(server.listen(path));
    auto client = Client::connect(path);
    ASSERT_NE(client, nullptr);
    auto server_conn = server.accept();
    ASSERT_NE(server_conn, nullptr);
    server_conn->set_stall_timeout_ms(100);

    // Header claims a ~256 MiB payload; only a few bytes are ever sent.
    Message m;
    m.header.type = MessageType::EVT_HEARTBEAT;
    m.payload.assign(16, 0xAB);
    auto wire = encode_message(m);
    // Patch payload_len by re-encoding the header with a large length.
    MessageHeader hdr = m.header;
    hdr.payload_len   = static_cast<uint32_t>(MAX_PAYLOAD_SIZE);
    std::vector<uint8_t> hdr_bytes;
    encode_header(hdr, hdr_bytes);
    ASSERT_EQ(hdr_bytes.size(), HEADER_SIZE);
    ASSERT_EQ(::send(client->fd(), hdr_bytes.data(), hdr_bytes.size(), MSG_NOSIGNAL),
              static_cast<ssize_t>(hdr_bytes.size()));
    ASSERT_EQ(::send(client->fd(), wire.data() + HEADER_SIZE, 16, MSG_NOSIGNAL), 16);

    auto t0  = std::chrono::steady_clock::now();
    auto got = server_conn->recv();
    auto dt  = std::chrono::steady_clock::now() - t0;
    EXPECT_FALSE(got.has_value());
    EXPECT_LT(dt, std::chrono::seconds(3));
    server.close();
}

TEST(BugfixIpcTransport, LargePayloadStillRoundTrips)
{
    const std::string path = unique_sock("large");
    Server            server;
    ASSERT_TRUE(server.listen(path));
    auto client = Client::connect(path);
    ASSERT_NE(client, nullptr);
    auto server_conn = server.accept();
    ASSERT_NE(server_conn, nullptr);

    Message m;
    m.header.type = MessageType::EVT_HEARTBEAT;
    m.payload.resize(3u * 1024u * 1024u + 17u);
    for (size_t i = 0; i < m.payload.size(); ++i)
        m.payload[i] = static_cast<uint8_t>(i * 31u);

    std::thread writer([&]() { EXPECT_TRUE(client->send(m)); });
    auto        got = server_conn->recv();
    writer.join();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->payload, m.payload);
    server.close();
}

TEST(BugfixIpcTransport, RecvTimeoutWithNoData)
{
    const std::string path = unique_sock("rto");
    Server            server;
    ASSERT_TRUE(server.listen(path));
    auto client = Client::connect(path);
    ASSERT_NE(client, nullptr);
    auto server_conn = server.accept();
    ASSERT_NE(server_conn, nullptr);

    client->set_recv_timeout_ms(100);
    auto t0  = std::chrono::steady_clock::now();
    auto got = client->recv();
    EXPECT_FALSE(got.has_value());
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(3));
    server.close();
}

// ─── Bug #90: listen() must not steal a live socket; close() must not unlink
//              a socket another server bound ───────────────────────────────────

TEST(BugfixIpcTransport, ListenRefusesLiveSocket)
{
    const std::string path = unique_sock("live");
    Server            first;
    ASSERT_TRUE(first.listen(path));

    Server second;
    EXPECT_FALSE(second.listen(path));

    // First server is still reachable.
    auto c = Client::connect(path);
    ASSERT_NE(c, nullptr);
    auto accepted = first.accept();
    EXPECT_NE(accepted, nullptr);
    first.close();
}

TEST(BugfixIpcTransport, ListenReplacesStaleSocket)
{
    const std::string path = unique_sock("stale");
    ASSERT_TRUE(make_stale_socket(path));
    Server server;
    EXPECT_TRUE(server.listen(path));
    server.close();
    EXPECT_FALSE(fs::exists(path));
}

TEST(BugfixIpcTransport, CloseDoesNotUnlinkReplacedSocket)
{
    const std::string path = unique_sock("replaced");
    Server            first;
    ASSERT_TRUE(first.listen(path));
    ::unlink(path.c_str());   // e.g. removed externally

    Server second;
    ASSERT_TRUE(second.listen(path));

    first.close();   // must not remove second's socket
    EXPECT_TRUE(fs::exists(path));
    auto c = Client::connect(path);
    EXPECT_NE(c, nullptr);

    second.close();
    EXPECT_FALSE(fs::exists(path));
}

// ─── Bug #41: publisher discovery skips stale/automation sockets ────────────

TEST(BugfixIpcTransport, PublisherSkipsStaleAndAutomationSockets)
{
    EnvGuard xdg_guard("XDG_RUNTIME_DIR");
    EnvGuard sock_guard("SPECTRA_SOCKET");

    char tmpl[] = "/tmp/spectra-bugfix-dir-XXXXXX";
    ASSERT_NE(::mkdtemp(tmpl), nullptr);
    const std::string dir = tmpl;
    ::setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
    ::unsetenv("SPECTRA_SOCKET");

    const std::string broker_path = dir + "/spectra-100.sock";
    const std::string stale_path  = dir + "/spectra-200.sock";
    const std::string auto_path   = dir + "/spectra-auto-300.sock";

    Server broker;
    ASSERT_TRUE(broker.listen(broker_path));
    ASSERT_TRUE(make_stale_socket(stale_path));
    Server automation;   // never replies, like the JSON-lines automation server
    ASSERT_TRUE(automation.listen(auto_path));

    // Broker oldest, stale socket newer, automation socket newest.
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(broker_path, now - std::chrono::seconds(30));
    fs::last_write_time(stale_path, now - std::chrono::seconds(10));
    fs::last_write_time(auto_path, now);

    std::atomic<bool> declared{false};
    std::atomic<bool> stop{false};
    std::thread       broker_thread(
        [&]()
        {
            std::unique_ptr<Connection> conn;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!conn && !stop && std::chrono::steady_clock::now() < deadline)
            {
                conn = broker.try_accept();
                if (!conn)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!conn)
                return;
            auto hello = conn->recv();
            if (!hello || hello->header.type != MessageType::HELLO)
                return;
            Message welcome;
            welcome.header.type = MessageType::WELCOME;
            welcome.payload     = encode_welcome({42, INVALID_WINDOW, 1, 5000, "inproc"});
            conn->send(welcome);
            auto decl = conn->recv();
            if (!decl || decl->header.type != MessageType::REQ_DECLARE_TOPIC)
                return;
            Message ok;
            ok.header.type       = MessageType::RESP_OK;
            ok.header.request_id = decl->header.request_id;
            ok.payload           = encode_resp_ok({decl->header.request_id});
            conn->send(ok);
            declared = true;
            // Keep the connection open until the test is done.
            while (!stop)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        });

    auto t0  = std::chrono::steady_clock::now();
    auto pub = spectra::Publisher::create("bugfix/topic");
    auto dt  = std::chrono::steady_clock::now() - t0;
    ASSERT_NE(pub, nullptr);
    EXPECT_TRUE(pub->is_connected());
    EXPECT_LT(dt, std::chrono::seconds(5));

    stop = true;
    broker_thread.join();
    EXPECT_TRUE(declared.load());

    pub.reset();
    broker.close();
    automation.close();
    ::unlink(stale_path.c_str());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(BugfixIpcTransport, PublisherDoesNotHangOnSilentPeer)
{
    const std::string path = unique_sock("silent");
    Server            silent;   // accepts at the kernel level but never replies
    ASSERT_TRUE(silent.listen(path));

    spectra::Publisher::Options opts;
    opts.socket_path = path;
    auto t0          = std::chrono::steady_clock::now();
    auto pub         = spectra::Publisher::create("bugfix/silent", opts);
    auto dt          = std::chrono::steady_clock::now() - t0;
    ASSERT_NE(pub, nullptr);
    EXPECT_FALSE(pub->is_connected());
    EXPECT_LT(dt, std::chrono::seconds(10));
    silent.close();
}

#endif   // _WIN32

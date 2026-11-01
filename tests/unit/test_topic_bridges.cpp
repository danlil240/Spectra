// test_topic_bridges.cpp — Topic bridges: JSON / CSV decoding, field-path
// fan-out, declare-on-first-sight ingest into TopicRegistry, and the UDP JSON
// bridge over loopback.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "daemon/bridges/field_path.hpp"
#include "daemon/bridges/text_message.hpp"
#include "daemon/bridges/topic_ingest.hpp"
#include "daemon/bridges/udp_json_bridge.hpp"
#include "daemon/topic_registry.hpp"

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>

    #include "app/inproc_topic_server.hpp"
#endif

using namespace spectra::daemon;
namespace ipc = spectra::ipc;

namespace
{

const FlatField* find_field(const std::vector<FlatField>& fields, const std::string& path)
{
    for (const auto& f : fields)
        if (f.path == path)
            return &f;
    return nullptr;
}

const TopicRegistry::Info* find_topic(const std::vector<TopicRegistry::Info>& infos,
                                      const std::string&                      name)
{
    for (const auto& i : infos)
        if (i.name == name)
            return &i;
    return nullptr;
}

// Samples retained in a topic's ring (via a throwaway subscription).
std::vector<double> retained(TopicRegistry& reg, const std::string& name)
{
    std::vector<double> out;
    TopicSubscription   sub{999, 0, 0};
    reg.subscribe(name, sub, nullptr, &out);
    reg.unsubscribe(sub);
    return out;
}

TopicIngestBatch make_batch(uint64_t owner, std::vector<TopicFieldSample> fields, uint64_t ns = 0)
{
    TopicIngestBatch b;
    b.owner_id   = owner;
    b.receive_ns = ns;
    b.fields     = std::move(fields);
    return b;
}

constexpr uint64_t OWNER = BRIDGE_OWNER_ID_BASE + 1;

}   // namespace

// ─── Field paths ────────────────────────────────────────────────────────────

TEST(TopicFieldPath, JoinsPrefixAndPath)
{
    EXPECT_EQ(join_topic_path("udp", "imu/accel/x"), "udp/imu/accel/x");
    EXPECT_EQ(join_topic_path("udp/", "/imu//x/"), "udp/imu/x");
    EXPECT_EQ(join_topic_path("", "a/b"), "a/b");
    EXPECT_EQ(join_topic_path("udp", ""), "udp");
    EXPECT_EQ(join_topic_path("udp", "a\tb\n"), "udp/a_b_");
    EXPECT_EQ(join_topic_path("", ""), "");
    EXPECT_EQ(join_topic_path("//", "/"), "");
    EXPECT_EQ(join_topic_path("p", std::string(MAX_BRIDGE_TOPIC_NAME, 'x')), "");
}

TEST(TopicFieldPath, FanOutSharesMessageTimestamp)
{
    std::vector<FlatField>        fields{{"ax", 1.0}, {"t", 12.5}, {"ay", 2.0}};
    std::vector<TopicFieldSample> out;
    fan_out_message(fields, "imu", 99.0, out);
    ASSERT_EQ(out.size(), 2u);   // "t" is the timestamp, not a topic
    EXPECT_EQ(out[0].name, "imu/ax");
    EXPECT_EQ(out[1].name, "imu/ay");
    EXPECT_DOUBLE_EQ(out[0].x, 12.5);
    EXPECT_DOUBLE_EQ(out[1].x, 12.5);
    EXPECT_DOUBLE_EQ(out[0].y, 1.0);
    EXPECT_DOUBLE_EQ(out[1].y, 2.0);
}

TEST(TopicFieldPath, FanOutUsesTimestampKeyOrReceiveTime)
{
    std::vector<TopicFieldSample> out;
    fan_out_message({{"v", 3.0}, {"timestamp", 4.0}}, "p", 99.0, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_DOUBLE_EQ(out[0].x, 4.0);

    out.clear();
    fan_out_message({{"v", 3.0}}, "p", 99.0, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_DOUBLE_EQ(out[0].x, 99.0);

    // Only the top-level key is a timestamp; a nested "t" is a regular field.
    out.clear();
    fan_out_message({{"a/t", 5.0}}, "p", 99.0, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].name, "p/a/t");
    EXPECT_DOUBLE_EQ(out[0].x, 99.0);
}

TEST(TopicFieldPath, FanOutSkipsInvalidNames)
{
    std::vector<TopicFieldSample> out;
    size_t                        skipped = 0;
    fan_out_message({{"ok", 1.0}, {std::string(300, 'k'), 2.0}}, "p", 0.0, out, &skipped);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(skipped, 1u);
}

// ─── JSON flattening ────────────────────────────────────────────────────────

TEST(TopicJsonFlatten, NestedObjectsAndArrays)
{
    std::vector<FlatField> f;
    std::string            err;
    ASSERT_TRUE(flatten_json(
        R"({"imu": {"accel": {"x": 0.5, "y": -9.81e0}, "gyro": [1, 2, 3]}, "ok": true,
            "bad": false, "name": "drone", "nothing": null, "t": 1.25})",
        f,
        &err))
        << err;
    ASSERT_EQ(f.size(), 8u);
    EXPECT_DOUBLE_EQ(find_field(f, "imu/accel/x")->value, 0.5);
    EXPECT_DOUBLE_EQ(find_field(f, "imu/accel/y")->value, -9.81);
    EXPECT_DOUBLE_EQ(find_field(f, "imu/gyro/0")->value, 1.0);
    EXPECT_DOUBLE_EQ(find_field(f, "imu/gyro/2")->value, 3.0);
    EXPECT_DOUBLE_EQ(find_field(f, "ok")->value, 1.0);
    EXPECT_DOUBLE_EQ(find_field(f, "bad")->value, 0.0);
    EXPECT_DOUBLE_EQ(find_field(f, "t")->value, 1.25);
    EXPECT_EQ(find_field(f, "name"), nullptr);
    EXPECT_EQ(find_field(f, "nothing"), nullptr);
}

TEST(TopicJsonFlatten, TopLevelArrayAndEmptyContainers)
{
    std::vector<FlatField> f;
    ASSERT_TRUE(flatten_json("[10, [20, 30], {}, []]", f));
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].path, "0");
    EXPECT_EQ(f[1].path, "1/0");
    EXPECT_EQ(f[2].path, "1/1");
}

TEST(TopicJsonFlatten, EscapedKeysAreDecoded)
{
    std::vector<FlatField> f;
    // Built outside the macro: MSVC mis-stringifies raw strings containing \" inside
    // ASSERT_*, and the UTF-8 key is spelled as bytes so the source code page does not matter.
    const std::string json =
        std::string(R"({"a\"b": 1, ")") + "\xC3\xA9" + R"(": 2, "": 3, "x\/y": 4})";
    ASSERT_TRUE(flatten_json(json, f));
    ASSERT_EQ(f.size(), 4u);
    EXPECT_EQ(f[0].path, "a\"b");
    EXPECT_EQ(f[1].path, "\xC3\xA9");
    EXPECT_EQ(f[2].path, "_");   // empty key
    EXPECT_EQ(f[3].path, "x/y");
}

TEST(TopicJsonFlatten, RejectsMalformedInput)
{
    const char* bad[] = {
        "{",
        "{\"a\": }",
        "{\"a\" 1}",
        "{\"a\": 1,}",
        "[1 2]",
        "{\"a\": 01x}",
        "{\"a\": -}",
        "{\"a\": tru}",
        "{\"a\": \"unterminated}",
        "{\"a\": 1} trailing",
        "{a: 1}",
    };
    for (const char* text : bad)
    {
        std::vector<FlatField> f;
        std::string            err;
        EXPECT_FALSE(flatten_json(text, f, &err)) << text;
        EXPECT_TRUE(f.empty()) << text;
        EXPECT_FALSE(err.empty()) << text;
    }
}

TEST(TopicJsonFlatten, DepthLimitAndOutOfRangeNumbers)
{
    std::string deep(MAX_JSON_DEPTH + 2, '[');
    deep += "1";
    deep += std::string(MAX_JSON_DEPTH + 2, ']');
    std::vector<FlatField> f;
    EXPECT_FALSE(flatten_json(deep, f));

    // 1e999 is valid JSON but not a finite double: skipped, not an error.
    f.clear();
    ASSERT_TRUE(flatten_json(R"({"huge": 1e999, "ok": 2})", f));
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f[0].path, "ok");
}

TEST(TopicJsonFlatten, FieldCountIsCapped)
{
    std::string text = "[";
    for (size_t i = 0; i < MAX_FIELDS_PER_MESSAGE + 10; ++i)
        text += (i ? ",1" : "1");
    text += "]";
    std::vector<FlatField> f;
    ASSERT_TRUE(flatten_json(text, f));
    EXPECT_EQ(f.size(), MAX_FIELDS_PER_MESSAGE);
}

// ─── CSV lines ──────────────────────────────────────────────────────────────

TEST(TopicCsv, KeyValuePairs)
{
    std::vector<FlatField> f;
    ASSERT_TRUE(parse_csv_line(" ax = 0.5, ay=-1e-3 ,t=2", f));
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].path, "ax");
    EXPECT_DOUBLE_EQ(f[0].value, 0.5);
    EXPECT_DOUBLE_EQ(f[1].value, -1e-3);
    EXPECT_EQ(f[2].path, "t");
}

TEST(TopicCsv, PlainValuesUseColumnIndex)
{
    std::vector<FlatField> f;
    ASSERT_TRUE(parse_csv_line("1.5,,+2,abc,4", f));
    ASSERT_EQ(f.size(), 3u);
    EXPECT_EQ(f[0].path, "0");
    EXPECT_EQ(f[1].path, "2");
    EXPECT_DOUBLE_EQ(f[1].value, 2.0);
    EXPECT_EQ(f[2].path, "4");
}

TEST(TopicCsv, LinesWithoutNumbersFail)
{
    std::vector<FlatField> f;
    EXPECT_FALSE(parse_csv_line("time,x,y", f));
    EXPECT_FALSE(parse_csv_line("a=,=3,b=nan,c=inf", f));
    EXPECT_TRUE(f.empty());
}

TEST(TopicDatagram, JsonStreamsAndCsvLines)
{
    std::vector<std::vector<FlatField>> msgs;
    EXPECT_EQ(decode_text_datagram("{\"a\": 1}\n{\"a\": 2}\n", msgs), 0u);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_DOUBLE_EQ(msgs[1][0].value, 2.0);

    msgs.clear();
    EXPECT_EQ(decode_text_datagram("{\n  \"a\": {\n    \"b\": 3\n  }\n}", msgs), 0u);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0][0].path, "a/b");

    msgs.clear();
    EXPECT_EQ(decode_text_datagram("x=1,y=2\r\nheader,only\r\n3,4\n", msgs), 1u);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0].size(), 2u);
    EXPECT_EQ(msgs[1][1].path, "1");

    // A broken JSON value stops the stream but keeps what came before.
    msgs.clear();
    EXPECT_EQ(decode_text_datagram("{\"a\": 1} {\"a\": ", msgs), 1u);
    EXPECT_EQ(msgs.size(), 1u);

    msgs.clear();
    EXPECT_EQ(decode_text_datagram("   \n", msgs), 0u);
    EXPECT_TRUE(msgs.empty());
}

// ─── TopicRegistry additions ────────────────────────────────────────────────

TEST(TopicRegistryEnsure, DeclaresOnceThenNoOp)
{
    TopicRegistry reg;
    EXPECT_EQ(reg.ensure_declared("a", ipc::TopicKind::Scalar2D, "", 0, OWNER),
              TopicRegistry::EnsureResult::Declared);
    EXPECT_EQ(reg.ensure_declared("a", ipc::TopicKind::Scalar2D, "", 0, OWNER),
              TopicRegistry::EnsureResult::AlreadyDeclared);

    // Another live owner conflicts; once it goes offline the topic is reclaimed.
    EXPECT_EQ(reg.ensure_declared("a", ipc::TopicKind::Scalar2D, "", 0, 7),
              TopicRegistry::EnsureResult::Conflict);
    reg.on_client_disconnect(OWNER);
    EXPECT_EQ(reg.ensure_declared("a", ipc::TopicKind::Scalar2D, "", 0, 7),
              TopicRegistry::EnsureResult::Declared);
}

TEST(TopicRegistryEnsure, PublishTimeDrivesRateEstimate)
{
    TopicRegistry reg;
    reg.declare("a", ipc::TopicKind::Scalar2D, "", 0, 1);
    // Published back-to-back, but stamped 10 ms apart: reads as 100 Hz.
    for (uint64_t i = 1; i <= 20; ++i)
        ASSERT_TRUE(reg.publish("a", {double(i), 0.0}, nullptr, nullptr, i * 10'000'000ull));
    auto infos = reg.snapshot();
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_NEAR(infos[0].estimated_hz, 100.0, 1e-6);
    EXPECT_EQ(infos[0].last_publish_ns, 200'000'000ull);
}

// ─── Ingest ─────────────────────────────────────────────────────────────────

TEST(TopicIngest, AutoDeclaresAndPublishes)
{
    TopicRegistry                 reg;
    std::vector<TopicIngestBatch> batches;
    batches.push_back(make_batch(OWNER, {{"imu/x", 1.0, 10.0}, {"imu/y", 1.0, 20.0}}, 1000));
    batches.push_back(make_batch(OWNER, {{"imu/x", 2.0, 11.0}}, 2000));

    IngestResult r = apply_ingest_batches(reg, batches);
    EXPECT_TRUE(r.topic_list_changed);
    EXPECT_EQ(r.declared, (std::vector<std::string>{"imu/x", "imu/y"}));
    EXPECT_EQ(r.samples_published, 3u);
    EXPECT_TRUE(r.fanout.empty());   // nobody subscribed

    auto infos = reg.snapshot();
    ASSERT_EQ(infos.size(), 2u);
    const auto* x = find_topic(infos, "imu/x");
    ASSERT_NE(x, nullptr);
    EXPECT_TRUE(x->publisher_online);
    EXPECT_EQ(x->total_samples, 2u);
    EXPECT_EQ(retained(reg, "imu/x"), (std::vector<double>{1.0, 10.0, 2.0, 11.0}));

    // Known topics do not re-announce the list.
    batches.clear();
    batches.push_back(make_batch(OWNER, {{"imu/x", 3.0, 12.0}}));
    EXPECT_FALSE(apply_ingest_batches(reg, batches).topic_list_changed);
}

TEST(TopicIngest, FanOutCoalescesPerSubscribedTopic)
{
    TopicRegistry reg;
    reg.declare("s", ipc::TopicKind::Scalar2D, "", 0, OWNER);
    TopicSubscription sub{42, 0, 3};
    ASSERT_TRUE(reg.subscribe("s", sub));

    std::vector<TopicIngestBatch> batches;
    batches.push_back(make_batch(OWNER, {{"s", 1.0, 5.0}, {"other", 1.0, 6.0}}));
    batches.push_back(make_batch(OWNER, {{"s", 2.0, 7.0}}));
    IngestResult r = apply_ingest_batches(reg, batches);

    ASSERT_EQ(r.fanout.size(), 1u);
    EXPECT_EQ(r.fanout[0].name, "s");
    EXPECT_EQ(r.fanout[0].samples, (std::vector<double>{1.0, 5.0, 2.0, 7.0}));
    ASSERT_EQ(r.fanout[0].subs.size(), 1u);
    EXPECT_EQ(r.fanout[0].subs[0], sub);
}

TEST(TopicIngest, TopicsOfLivePublishersAreNotTakenOver)
{
    TopicRegistry reg;
    reg.declare("taken", ipc::TopicKind::Scalar2D, "", 0, 5);   // socket client 5

    std::vector<TopicIngestBatch> batches;
    batches.push_back(make_batch(OWNER, {{"taken", 1.0, 1.0}, {"free", 1.0, 2.0}}));
    IngestResult r = apply_ingest_batches(reg, batches);
    EXPECT_EQ(r.samples_rejected, 1u);
    EXPECT_EQ(r.samples_published, 1u);
    EXPECT_EQ(find_topic(reg.snapshot(), "taken")->total_samples, 0u);
}

TEST(TopicIngest, QueueIsBounded)
{
    TopicIngestQueue q(2);
    EXPECT_TRUE(q.push(make_batch(OWNER, {{"a", 0, 0}})));
    EXPECT_TRUE(q.push(make_batch(OWNER, {{"a", 1, 0}})));
    EXPECT_FALSE(q.push(make_batch(OWNER, {{"a", 2, 0}})));
    EXPECT_EQ(q.dropped(), 1u);
    auto drained = q.drain();
    ASSERT_EQ(drained.size(), 2u);
    EXPECT_DOUBLE_EQ(drained[1].fields[0].x, 1.0);
    EXPECT_EQ(q.size(), 0u);
    EXPECT_TRUE(q.push(make_batch(OWNER, {{"a", 3, 0}})));
}

// ─── UDP bridge ─────────────────────────────────────────────────────────────

TEST(UdpBridgeSpec, ParsesHostAndPort)
{
    UdpBridgeConfig cfg;
    ASSERT_TRUE(parse_udp_bridge_spec("9871", cfg, nullptr));
    EXPECT_EQ(cfg.port, 9871);
    EXPECT_EQ(cfg.bind_address, "127.0.0.1");

    ASSERT_TRUE(parse_udp_bridge_spec("0.0.0.0:5000", cfg, nullptr));
    EXPECT_EQ(cfg.bind_address, "0.0.0.0");
    EXPECT_EQ(cfg.port, 5000);
    EXPECT_FALSE(is_loopback_address(cfg.bind_address));

    ASSERT_TRUE(parse_udp_bridge_spec("localhost:0", cfg, nullptr));
    EXPECT_TRUE(is_loopback_address(cfg.bind_address));

    for (const char* bad : {"", "abc", "70000", ":9870", "host.example:9870", "1.2.3.4:", "-1"})
    {
        UdpBridgeConfig c;
        std::string     err;
        EXPECT_FALSE(parse_udp_bridge_spec(bad, c, &err)) << bad;
        EXPECT_FALSE(err.empty()) << bad;
        EXPECT_EQ(c.port, DEFAULT_UDP_BRIDGE_PORT) << "config must be untouched on error";
    }
}

#ifndef _WIN32

namespace
{

bool send_udp(uint16_t port, const std::string& payload)
{
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;
    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    auto n             = ::sendto(fd,
                      payload.data(),
                      payload.size(),
                      0,
                      reinterpret_cast<const sockaddr*>(&sa),
                      sizeof(sa));
    ::close(fd);
    return n == static_cast<ssize_t>(payload.size());
}

// Wait until the bridge has handled `count` datagrams (bounded).
bool wait_for_messages(const UdpJsonBridge& bridge, uint64_t count)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (bridge.stats().messages_received >= count)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

UdpBridgeConfig loopback_config()
{
    UdpBridgeConfig cfg;
    cfg.port = 0;   // ephemeral
    return cfg;
}

}   // namespace

TEST(UdpJsonBridge, LoopbackJsonReachesTopicStore)
{
    TopicIngestQueue queue;
    UdpJsonBridge    bridge(loopback_config());
    std::string      err;
    ASSERT_TRUE(bridge.start(queue, OWNER, &err)) << err;
    ASSERT_TRUE(bridge.is_running());
    ASSERT_NE(bridge.bound_port(), 0);
    EXPECT_NE(bridge.describe().find("127.0.0.1"), std::string::npos);

    ASSERT_TRUE(send_udp(bridge.bound_port(),
                         R"({"t": 1.5, "imu": {"accel": {"x": 0.1, "y": 0.2, "z": 9.8}}})"));
    ASSERT_TRUE(send_udp(bridge.bound_port(), "t=2.5,imu/accel/x=0.3"));
    ASSERT_TRUE(send_udp(bridge.bound_port(), "not numbers at all"));
    ASSERT_TRUE(wait_for_messages(bridge, 3));

    TopicRegistry reg;
    IngestResult  r = apply_ingest_batches(reg, queue.drain());
    EXPECT_TRUE(r.topic_list_changed);
    EXPECT_EQ(r.samples_published, 4u);

    auto infos = reg.snapshot();
    ASSERT_EQ(infos.size(), 3u);
    for (const char* name : {"udp/imu/accel/x", "udp/imu/accel/y", "udp/imu/accel/z"})
    {
        const auto* t = find_topic(infos, name);
        ASSERT_NE(t, nullptr) << name;
        EXPECT_TRUE(t->publisher_online);
        EXPECT_EQ(t->kind, ipc::TopicKind::Scalar2D);
    }
    EXPECT_EQ(retained(reg, "udp/imu/accel/x"), (std::vector<double>{1.5, 0.1, 2.5, 0.3}));
    EXPECT_EQ(retained(reg, "udp/imu/accel/z"), (std::vector<double>{1.5, 9.8}));

    auto st = bridge.stats();
    EXPECT_EQ(st.messages_received, 3u);
    EXPECT_EQ(st.messages_parsed, 2u);
    EXPECT_EQ(st.parse_errors, 1u);
    EXPECT_EQ(st.samples_published, 4u);
    EXPECT_EQ(st.topics_seen, 3u);

    bridge.stop();
    EXPECT_FALSE(bridge.is_running());
    bridge.stop();   // idempotent
}

TEST(UdpJsonBridge, ReceiveTimeIsUsedWithoutTimestampAndPrefixApplies)
{
    UdpBridgeConfig cfg = loopback_config();
    cfg.prefix          = "rig";
    TopicIngestQueue queue;
    UdpJsonBridge    bridge(cfg);
    ASSERT_TRUE(bridge.start(queue, OWNER, nullptr));
    ASSERT_TRUE(send_udp(bridge.bound_port(), "1,2"));
    ASSERT_TRUE(wait_for_messages(bridge, 1));

    auto batches = queue.drain();
    ASSERT_EQ(batches.size(), 1u);
    ASSERT_EQ(batches[0].fields.size(), 2u);
    EXPECT_EQ(batches[0].fields[0].name, "rig/0");
    EXPECT_EQ(batches[0].fields[1].name, "rig/1");
    EXPECT_EQ(batches[0].owner_id, OWNER);
    EXPECT_NE(batches[0].receive_ns, 0u);
    // Seconds since the bridge started: small, non-negative, shared.
    EXPECT_GE(batches[0].fields[0].x, 0.0);
    EXPECT_LT(batches[0].fields[0].x, 60.0);
    EXPECT_DOUBLE_EQ(batches[0].fields[0].x, batches[0].fields[1].x);
}

TEST(UdpJsonBridge, TopicCapLimitsNewNames)
{
    UdpBridgeConfig cfg = loopback_config();
    cfg.max_topics      = 2;
    TopicIngestQueue queue;
    UdpJsonBridge    bridge(cfg);
    ASSERT_TRUE(bridge.start(queue, OWNER, nullptr));
    ASSERT_TRUE(send_udp(bridge.bound_port(), R"({"a": 1, "b": 2, "c": 3})"));
    ASSERT_TRUE(send_udp(bridge.bound_port(), R"({"a": 4, "d": 5})"));
    ASSERT_TRUE(wait_for_messages(bridge, 2));

    TopicRegistry reg;
    apply_ingest_batches(reg, queue.drain());
    auto infos = reg.snapshot();
    EXPECT_EQ(infos.size(), 2u);
    EXPECT_NE(find_topic(infos, "udp/a"), nullptr);
    EXPECT_NE(find_topic(infos, "udp/b"), nullptr);
    EXPECT_EQ(find_topic(infos, "udp/a")->total_samples, 2u);
    EXPECT_EQ(bridge.stats().samples_dropped, 2u);
}

TEST(UdpJsonBridge, PortInUseFailsCleanly)
{
    TopicIngestQueue queue;
    UdpJsonBridge    first(loopback_config());
    ASSERT_TRUE(first.start(queue, OWNER, nullptr));

    UdpBridgeConfig cfg = loopback_config();
    cfg.port            = first.bound_port();
    UdpJsonBridge second(cfg);
    std::string   err;
    EXPECT_FALSE(second.start(queue, OWNER + 1, &err));
    EXPECT_NE(err.find("bind"), std::string::npos) << err;
    EXPECT_FALSE(second.is_running());
}

TEST(UdpJsonBridge, PublishesIntoInprocTopicServer)
{
    // The in-process app's topic server is itself a TopicSink.
    spectra::InprocTopicServer server;
    UdpJsonBridge              bridge(loopback_config());
    ASSERT_TRUE(bridge.start(server, OWNER, nullptr));
    ASSERT_TRUE(send_udp(bridge.bound_port(), R"({"t": 3, "temp": 21.5})"));
    ASSERT_TRUE(wait_for_messages(bridge, 1));
    bridge.stop();

    auto topics = server.topic_snapshot();
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0].name, "udp/temp");
    EXPECT_TRUE(topics[0].publisher_online);
    EXPECT_EQ(topics[0].total_samples, 1u);

    server.mark_owner_offline(OWNER);
    EXPECT_FALSE(server.topic_snapshot()[0].publisher_online);
}

TEST(UdpJsonBridge, ShortSoak)
{
    // A few hundred datagrams from another thread while the main thread
    // drains, as the daemon loop does.  Kept short for CI.
    TopicIngestQueue queue;
    UdpJsonBridge    bridge(loopback_config());
    ASSERT_TRUE(bridge.start(queue, OWNER, nullptr));
    const uint16_t port = bridge.bound_port();
    constexpr int  N    = 300;

    std::thread sender(
        [port]()
        {
            for (int i = 0; i < N; ++i)
            {
                send_udp(port,
                         "{\"t\": " + std::to_string(i) + ", \"a\": " + std::to_string(i)
                             + ", \"b\": [" + std::to_string(-i) + "]}");
                if (i % 50 == 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

    TopicRegistry reg;
    auto          deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    uint64_t      total    = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        total += apply_ingest_batches(reg, queue.drain()).samples_published;
        if (total >= 2u * N)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    sender.join();
    total += apply_ingest_batches(reg, queue.drain()).samples_published;

    // Loopback UDP can drop under extreme load; it must not here, but allow
    // the kernel a little slack rather than flaking.
    EXPECT_GE(total, 2u * N * 9 / 10);
    auto infos = reg.snapshot();
    ASSERT_EQ(infos.size(), 2u);
    EXPECT_NE(find_topic(infos, "udp/b/0"), nullptr);
    auto a = retained(reg, "udp/a");
    // Samples arrive in order with x == y for topic "a".
    for (size_t i = 0; i + 1 < a.size(); i += 2)
        EXPECT_DOUBLE_EQ(a[i], a[i + 1]);
}

#endif   // !_WIN32

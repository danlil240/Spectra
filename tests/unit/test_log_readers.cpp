// Unit tests for the LogReader layer in src/io/logs/: the registry, the ULog
// and MCAP readers, the ros2msg schema parser and the CDR decoder.
//
// ULog data comes from the committed fixture (tests/data/ulog) and from
// ULogBuilder; MCAP files are written in the test with the vendored
// foxglove/mcap writer.

#include <gtest/gtest.h>

#include "io/logs/log_reader.hpp"
#include "io/logs/log_reader_registry.hpp"
#include "io/logs/mcap_log_reader.hpp"
#include "io/logs/ros2msg_cdr.hpp"
#include "io/logs/ulog_log_reader.hpp"
#include "ulog_builder.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifdef SPECTRA_HAS_MCAP
    #include <mcap/writer.hpp>
#endif

using namespace spectra::logs;

namespace
{

std::string fixture_ulog()
{
    return std::string(SPECTRA_SOURCE_DIR) + "/tests/data/ulog/px4_fixture.ulg";
}

std::string temp_path(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / ("spectra_log_readers_" + name)).string();
}

void write_bytes(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

size_t field_index(const LogReader& r, const std::string& topic, const std::string& path)
{
    auto idx = r.find_field(topic, path);
    EXPECT_TRUE(idx.has_value()) << "missing field " << topic << "/" << path;
    return idx.value_or(0);
}

// Little/big endian XCDR1 writer for test payloads.
class CdrWriter
{
   public:
    explicit CdrWriter(bool little = true) : little_(little)
    {
        buf_ = {std::byte{0},
                std::byte{little ? uint8_t{1} : uint8_t{0}},
                std::byte{0},
                std::byte{0}};
    }

    template <typename T>
    CdrWriter& put(T v)
    {
        align(sizeof(T));
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &v, sizeof(T));
        if (!little_)
            std::reverse(bytes, bytes + sizeof(T));
        for (unsigned char b : bytes)
            buf_.push_back(std::byte{b});
        return *this;
    }

    CdrWriter& str(const std::string& s)
    {
        put<uint32_t>(static_cast<uint32_t>(s.size() + 1));
        for (char c : s)
            buf_.push_back(static_cast<std::byte>(c));
        buf_.push_back(std::byte{0});
        return *this;
    }

    const std::vector<std::byte>& bytes() const { return buf_; }

   private:
    void align(size_t n)
    {
        while ((buf_.size() - 4) % n != 0)
            buf_.push_back(std::byte{0});
    }

    bool                   little_;
    std::vector<std::byte> buf_;
};

// A small ROS 2 style schema exercising nesting, fixed arrays, sequences,
// strings, constants and bounded types.
constexpr const char* POSE_SCHEMA = R"(# A stamped pose
uint8 MODE_A=1
uint8 MODE_B = 2
std_msgs/Header header
geometry_msgs/Pose pose
float64[3] fixed
int32[] dyn
string<=8 label
uint8[] blob
bool flag
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec
================================================================================
MSG: geometry_msgs/Pose
Point position   # same-package reference
float32 w 1.0
================================================================================
MSG: geometry_msgs/Point
float64 x
float64 y
float64 z
)";

std::vector<std::byte> pose_message(int i, bool little = true, size_t blob_size = 2)
{
    CdrWriter w(little);
    w.put<int32_t>(100 + i).put<uint32_t>(500u);                     // header.stamp
    w.str("map");                                                    // header.frame_id
    w.put<double>(1.0 * i).put<double>(2.0 * i).put<double>(-1.0);   // pose.position
    w.put<float>(0.5f);                                              // pose.w
    w.put<double>(10.0).put<double>(11.0).put<double>(12.0);         // fixed[3]
    w.put<uint32_t>(2).put<int32_t>(-i).put<int32_t>(i * 3);         // dyn
    w.str("abc");                                                    // label
    w.put<uint32_t>(static_cast<uint32_t>(blob_size));               // blob
    for (size_t b = 0; b < blob_size; ++b)
        w.put<uint8_t>(static_cast<uint8_t>(b));
    w.put<uint8_t>(i % 2 ? 1 : 0);   // flag
    return w.bytes();
}

}   // namespace

// ─── Field tree / filter ─────────────────────────────────────────────────────

TEST(LogFieldTree, GroupsByTopicAndPathSegments)
{
    std::vector<LogField> fields = {
        {"/imu", "angular_velocity.x", "float64", "", 5},
        {"/imu", "angular_velocity.y", "float64", "", 5},
        {"/imu", "header.stamp.sec", "int32", "", 5},
        {"/odom", "q[0]", "float", "", 7},
    };
    LogFieldNode root = build_field_tree(fields);
    ASSERT_EQ(root.children.size(), 2u);
    const auto& imu = root.children[0];
    EXPECT_EQ(imu.name, "/imu");
    EXPECT_EQ(imu.field_index, -1);
    ASSERT_EQ(imu.children.size(), 2u);
    EXPECT_EQ(imu.children[0].name, "angular_velocity");
    ASSERT_EQ(imu.children[0].children.size(), 2u);
    EXPECT_EQ(imu.children[0].children[1].name, "y");
    EXPECT_EQ(imu.children[0].children[1].field_index, 1);
    EXPECT_EQ(imu.children[1].children[0].children[0].name, "sec");
    EXPECT_EQ(imu.children[1].children[0].children[0].field_index, 2);
    EXPECT_EQ(root.children[1].sample_count, 7u);
    EXPECT_EQ(root.children[1].children[0].name, "q[0]");
}

TEST(LogFieldTree, FilterIsCaseInsensitiveOverTopicAndPath)
{
    LogField f{"vehicle_attitude", "q[0]", "float", "", 1};
    EXPECT_TRUE(field_matches_filter(f, ""));
    EXPECT_TRUE(field_matches_filter(f, "ATTITUDE"));
    EXPECT_TRUE(field_matches_filter(f, "attitude/q"));
    EXPECT_FALSE(field_matches_filter(f, "gyro"));
    EXPECT_EQ(f.full_name(), "vehicle_attitude/q[0]");
}

// ─── Registry ────────────────────────────────────────────────────────────────

TEST(LogReaderRegistry, BuiltinsCoverULogAndMcap)
{
    LogReaderRegistry reg;
    register_builtin_log_readers(reg);
    auto exts = reg.extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), "ulg"), exts.end());
#ifdef SPECTRA_HAS_MCAP
    EXPECT_NE(std::find(exts.begin(), exts.end(), "mcap"), exts.end());
    EXPECT_EQ(reg.count(), 2u);
#else
    EXPECT_EQ(reg.count(), 1u);
#endif
}

TEST(LogReaderRegistry, MagicBytesWinOverExtension)
{
    LogReaderRegistry reg;
    register_builtin_log_readers(reg);

    const uint8_t ulog_head[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35, 0x01};
    const uint8_t mcap_head[] = {0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};
    EXPECT_EQ(reg.detect("bin", ulog_head), "ULog");
    EXPECT_EQ(reg.detect("ulg", ulog_head), "ULog");
#ifdef SPECTRA_HAS_MCAP
    EXPECT_EQ(reg.detect("ulg", mcap_head), "MCAP");   // content beats a wrong name
    EXPECT_EQ(reg.detect("mcap", {}), "MCAP");         // extension fallback
#else
    EXPECT_EQ(reg.detect("ulg", mcap_head), "ULog");
#endif
    EXPECT_EQ(reg.detect("csv", {}), "");

    // Files on disk: the fixture copied under a misleading name is still ULog.
    const std::string renamed = temp_path("renamed.dat");
    std::filesystem::copy_file(fixture_ulog(),
                               renamed,
                               std::filesystem::copy_options::overwrite_existing);
    EXPECT_EQ(reg.detect(renamed), "ULog");
    EXPECT_TRUE(reg.can_open(renamed));
    std::filesystem::remove(renamed);
}

TEST(LogReaderRegistry, OpenReportsErrors)
{
    LogReaderRegistry reg;
    register_builtin_log_readers(reg);
    std::string error;

    EXPECT_EQ(reg.open(temp_path("does_not_exist.ulg"), &error), nullptr);
    EXPECT_NE(error.find("Not a file"), std::string::npos);

    const std::string txt = temp_path("plain.txt");
    write_bytes(txt, {'h', 'i'});
    EXPECT_EQ(reg.open(txt, &error), nullptr);
    EXPECT_NE(error.find("Unsupported"), std::string::npos);

    // Right extension, wrong content: picked by extension, then fails to open.
    const std::string bad = temp_path("bad.ulg");
    write_bytes(bad, {'n', 'o', 't', ' ', 'u', 'l', 'o', 'g'});
    EXPECT_EQ(reg.open(bad, &error), nullptr);
    EXPECT_EQ(error.rfind("ULog: ", 0), 0u) << error;

    std::filesystem::remove(txt);
    std::filesystem::remove(bad);
}

namespace
{
class FakeReader final : public LogReader
{
   public:
    std::string_view format_name() const override { return "Fake"; }
    bool             open(const std::string& p) override
    {
        path_   = p;
        fields_ = {{"topic", "x", "float", "m", 2}};
        return true;
    }
    const std::vector<LogField>& fields() const override { return fields_; }
    bool                         read(size_t i, LogSeries& out) const override
    {
        if (i != 0)
            return fail("bad index");
        out.time   = {1.0, 2.0, 3.0};
        out.values = {10.f, 20.f, 30.f};
        return true;
    }

   private:
    std::vector<LogField> fields_;
};
}   // namespace

TEST(LogReaderRegistry, CustomFormatsCanBeRegisteredAndRemoved)
{
    LogReaderRegistry reg;
    register_builtin_log_readers(reg);
    const size_t builtin = reg.count();

    reg.register_format({"Fake", {".FAKE"}}, []() { return std::make_unique<FakeReader>(); });
    EXPECT_EQ(reg.count(), builtin + 1);
    EXPECT_EQ(reg.detect("fake", {}), "Fake");   // extension normalized

    const std::string path = temp_path("data.fake");
    write_bytes(path, {1, 2, 3});
    std::string error;
    auto        reader = reg.open(path, &error);
    ASSERT_NE(reader, nullptr) << error;
    EXPECT_EQ(reader->format_name(), "Fake");

    // Default read_range() slices read().
    LogSeries s;
    ASSERT_TRUE(reader->read_range(0, 1.5, 3.0, s));
    EXPECT_EQ(s.time, (std::vector<double>{2.0, 3.0}));
    EXPECT_EQ(s.values, (std::vector<float>{20.f, 30.f}));
    EXPECT_FALSE(reader->read(5, s));
    EXPECT_EQ(reader->last_error(), "bad index");

    EXPECT_TRUE(reg.unregister_format("Fake"));
    EXPECT_FALSE(reg.unregister_format("Fake"));
    EXPECT_EQ(reg.detect(path), "");
    std::filesystem::remove(path);
}

// ─── ULog ────────────────────────────────────────────────────────────────────

TEST(ULogLogReader, FixtureFieldsValuesAndTimestamps)
{
    LogReaderRegistry reg;
    register_builtin_log_readers(reg);
    std::string error;
    auto        reader = reg.open(fixture_ulog(), &error);
    ASSERT_NE(reader, nullptr) << error;
    EXPECT_EQ(reader->format_name(), "ULog");

    // battery_status/voltage_v, sensor_combined/gyro_rad[0..2], vehicle_attitude/q[0..3];
    // the per-message timestamp is the time axis, not a field.
    const auto& fields = reader->fields();
    ASSERT_EQ(fields.size(), 8u);
    EXPECT_EQ(fields[0].topic, "battery_status");
    EXPECT_EQ(fields[0].path, "voltage_v");
    EXPECT_EQ(fields[0].type, "float");
    EXPECT_EQ(fields[0].sample_count, 10u);
    EXPECT_FALSE(reader->find_field("battery_status", "timestamp").has_value());

    LogSeries s;
    ASSERT_TRUE(reader->read(field_index(*reader, "sensor_combined", "gyro_rad[1]"), s));
    ASSERT_EQ(s.time.size(), 10u);
    for (size_t i = 0; i < 10; ++i)
    {
        EXPECT_DOUBLE_EQ(s.time[i], 1.0 + 0.1 * static_cast<double>(i));
        EXPECT_FLOAT_EQ(s.values[i], -static_cast<float>(i));
    }

    ASSERT_TRUE(reader->read(field_index(*reader, "vehicle_attitude", "q[1]"), s));
    EXPECT_FLOAT_EQ(s.values[5], 0.05f);

    ASSERT_TRUE(
        reader->read_range(field_index(*reader, "battery_status", "voltage_v"), 1.2, 1.4, s));
    ASSERT_EQ(s.values.size(), 3u);
    EXPECT_FLOAT_EQ(s.values[0], 15.8f);
}

TEST(ULogLogReader, NestedTypesPaddingAndMultiInstance)
{
    ULogBuilder b;
    b.add_format("inner:float a;int16_t b;uint8_t[2] _padding0;");
    b.add_format("outer:uint64_t timestamp;inner[2] arr;bool ok;char[4] name;");
    b.add_subscription(0, 1, "outer");
    b.add_subscription(1, 2, "outer");

    auto row = [](float a0, int16_t b0, float a1, int16_t b1, bool ok)
    {
        std::vector<uint8_t> p;
        auto                 put = [&p](const void* v, size_t n)
        {
            const auto* c = static_cast<const uint8_t*>(v);
            p.insert(p.end(), c, c + n);
        };
        const uint8_t pad[2]  = {0, 0};
        const char    name[4] = {'a', 'b', 'c', 'd'};
        put(&a0, 4);
        put(&b0, 2);
        put(pad, 2);
        put(&a1, 4);
        put(&b1, 2);
        put(pad, 2);
        const uint8_t okb = ok ? 1 : 0;
        put(&okb, 1);
        put(name, 4);
        return p;
    };
    auto r0 = row(1.5f, 7, 2.5f, -3, true);
    auto r1 = row(9.0f, 1, 8.0f, 2, false);
    b.add_data(1, 2'000'000, r0.data(), r0.size());
    b.add_data(2, 3'000'000, r1.data(), r1.size());
    const std::string path = b.write_to_file(temp_path("nested.ulg"));

    ULogLogReader reader;
    ASSERT_TRUE(reader.open(path)) << reader.last_error();

    // Two instances ("outer", "outer[1]"), each with arr[0..1].{a,b} + ok;
    // padding and char fields are hidden.
    ASSERT_EQ(reader.fields().size(), 10u);
    EXPECT_FALSE(reader.find_field("outer", "arr[0]._padding0").has_value());
    EXPECT_FALSE(reader.find_field("outer", "name[0]").has_value());

    LogSeries s;
    ASSERT_TRUE(reader.read(field_index(reader, "outer", "arr[1].a"), s));
    ASSERT_EQ(s.values.size(), 1u);
    EXPECT_FLOAT_EQ(s.values[0], 2.5f);
    EXPECT_DOUBLE_EQ(s.time[0], 2.0);

    ASSERT_TRUE(reader.read(field_index(reader, "outer", "arr[1].b"), s));
    EXPECT_FLOAT_EQ(s.values[0], -3.0f);
    ASSERT_TRUE(reader.read(field_index(reader, "outer", "ok"), s));
    EXPECT_FLOAT_EQ(s.values[0], 1.0f);

    ASSERT_TRUE(reader.read(field_index(reader, "outer[1]", "arr[0].a"), s));
    EXPECT_FLOAT_EQ(s.values[0], 9.0f);
    EXPECT_DOUBLE_EQ(s.time[0], 3.0);

    EXPECT_FALSE(reader.read(99, s));
    std::filesystem::remove(path);
}

// ─── ros2msg schema + CDR ────────────────────────────────────────────────────

TEST(Ros2Msg, ParsesSchemaWithDependencies)
{
    ros2msg::Schema schema;
    std::string     error;
    ASSERT_TRUE(schema.parse("geometry_msgs/msg/PoseStamped", POSE_SCHEMA, &error)) << error;

    const auto* root = schema.root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->name, "geometry_msgs/PoseStamped");
    ASSERT_EQ(root->fields.size(), 7u);   // constants skipped
    EXPECT_EQ(root->fields[0].msg_type, "std_msgs/Header");
    EXPECT_EQ(root->fields[2].array, ros2msg::ArrayKind::Fixed);
    EXPECT_EQ(root->fields[2].array_size, 3u);
    EXPECT_EQ(root->fields[3].array, ros2msg::ArrayKind::Sequence);
    EXPECT_EQ(root->fields[4].type, ros2msg::Primitive::String);

    const auto* pose = schema.find("geometry_msgs/msg/Pose");
    ASSERT_NE(pose, nullptr);
    EXPECT_EQ(pose->fields[0].msg_type, "geometry_msgs/Point");   // same package
    EXPECT_EQ(pose->fields[1].name, "w");                         // default value ignored
    EXPECT_NE(schema.find("Point"), nullptr);                     // short-name fallback

    EXPECT_FALSE(schema.parse("x/Y", "int32 a\n===\nNOT A HEADER\n", &error));
    EXPECT_FALSE(schema.parse("x/Y", "int32[abc] a\n", &error));
}

TEST(Ros2Msg, DecodesCdrLittleAndBigEndian)
{
    ros2msg::Schema schema;
    ASSERT_TRUE(schema.parse("geometry_msgs/msg/PoseStamped", POSE_SCHEMA));

    for (bool little : {true, false})
    {
        std::map<std::string, double> got;
        std::string                   error;
        auto                          msg = pose_message(3, little);
        ASSERT_TRUE(ros2msg::decode_cdr(
            schema,
            msg,
            [&](std::string_view p, double v, const ros2msg::FieldDef&)
            { got[std::string(p)] = v; },
            {},
            &error))
            << error;

        EXPECT_EQ(got.at("header.stamp.sec"), 103.0);
        EXPECT_EQ(got.at("header.stamp.nanosec"), 500.0);
        EXPECT_EQ(got.at("pose.position.x"), 3.0);
        EXPECT_EQ(got.at("pose.position.y"), 6.0);
        EXPECT_EQ(got.at("pose.position.z"), -1.0);
        EXPECT_EQ(got.at("pose.w"), 0.5);
        EXPECT_EQ(got.at("fixed[2]"), 12.0);
        EXPECT_EQ(got.at("dyn[0]"), -3.0);
        EXPECT_EQ(got.at("dyn[1]"), 9.0);
        EXPECT_EQ(got.at("blob[1]"), 1.0);
        EXPECT_EQ(got.at("flag"), 1.0);
        EXPECT_EQ(got.count("label"), 0u);   // strings are skipped
        EXPECT_EQ(got.count("header.frame_id"), 0u);
    }
}

TEST(Ros2Msg, LongArraysAreConsumedButNotReported)
{
    ros2msg::Schema schema;
    ASSERT_TRUE(schema.parse("geometry_msgs/msg/PoseStamped", POSE_SCHEMA));

    std::map<std::string, double> got;
    ros2msg::DecodeOptions        opt;
    opt.max_array_elements = 4;
    auto msg               = pose_message(1, true, 100);
    ASSERT_TRUE(ros2msg::decode_cdr(
        schema,
        msg,
        [&](std::string_view p, double v, const ros2msg::FieldDef&) { got[std::string(p)] = v; },
        opt));
    EXPECT_EQ(got.count("blob[0]"), 0u);
    EXPECT_EQ(got.at("flag"), 1.0);   // decoding continued after the skipped blob
}

TEST(Ros2Msg, RejectsTruncatedAndUnsupportedData)
{
    ros2msg::Schema schema;
    ASSERT_TRUE(schema.parse("geometry_msgs/msg/PoseStamped", POSE_SCHEMA));
    auto        msg = pose_message(1);
    std::string error;
    size_t      leaves = 0;
    auto        count  = [&](std::string_view, double, const ros2msg::FieldDef&) { ++leaves; };

    std::vector<std::byte> truncated(msg.begin(), msg.begin() + 20);
    EXPECT_FALSE(ros2msg::decode_cdr(schema, truncated, count, {}, &error));
    EXPECT_GT(leaves, 0u);   // fields before the cut were still reported

    std::vector<std::byte> xcdr2 = msg;
    xcdr2[1]                     = std::byte{0x07};
    EXPECT_FALSE(ros2msg::decode_cdr(schema, xcdr2, count, {}, &error));
    EXPECT_NE(error.find("0x0007"), std::string::npos) << error;

    ros2msg::Schema wide;
    ASSERT_TRUE(wide.parse("a/W", "int32 a\nwstring w\nint32 b\n"));
    CdrWriter w;
    w.put<int32_t>(1).put<uint32_t>(0).put<int32_t>(2);
    leaves = 0;
    EXPECT_FALSE(ros2msg::decode_cdr(wide, w.bytes(), count, {}, &error));
    EXPECT_EQ(leaves, 1u);
    EXPECT_NE(error.find("wstring"), std::string::npos);
}

// ─── MCAP ────────────────────────────────────────────────────────────────────

#ifdef SPECTRA_HAS_MCAP

namespace
{

// Writes an MCAP with a json channel, a ros2msg/cdr channel and a protobuf
// channel (unsupported, must be skipped).  Log times start at a Unix epoch
// time so double-precision timestamps matter.
constexpr uint64_t T0_NS = 1'700'000'000'000'000'000ull;

std::string write_mcap_fixture(const std::string& name, bool chunked)
{
    const std::string       path = temp_path(name);
    mcap::McapWriter        writer;
    mcap::McapWriterOptions opts("spectra-test");
    opts.compression = mcap::Compression::None;
    opts.noChunking  = !chunked;
    EXPECT_TRUE(writer.open(path, opts).ok());

    mcap::Schema json_schema("Telemetry", "jsonschema", "{}");
    writer.addSchema(json_schema);
    mcap::Channel json_ch("/telemetry", "json", json_schema.id);
    writer.addChannel(json_ch);

    mcap::Schema pose_schema("geometry_msgs/msg/PoseStamped", "ros2msg", POSE_SCHEMA);
    writer.addSchema(pose_schema);
    mcap::Channel pose_ch("/pose", "cdr", pose_schema.id);
    writer.addChannel(pose_ch);

    mcap::Schema pb_schema("pkg.Msg", "protobuf", "\x01\x02");
    writer.addSchema(pb_schema);
    mcap::Channel pb_ch("/proto", "protobuf", pb_schema.id);
    writer.addChannel(pb_ch);

    for (int i = 0; i < 5; ++i)
    {
        const uint64_t t = T0_NS + static_cast<uint64_t>(i) * 100'000'000ull;   // 10 Hz

        const std::string json = "{\"speed\": " + std::to_string(i * 1.5)
                                 + ", \"armed\": " + (i > 2 ? "true" : "false")
                                 + ", \"pos\": {\"x\": " + std::to_string(i)
                                 + "}, \"cells\": [3.7, 3.8], \"mode\": \"auto\"}";
        mcap::Message m;
        m.channelId   = json_ch.id;
        m.sequence    = static_cast<uint32_t>(i);
        m.logTime     = t;
        m.publishTime = t;
        m.data        = reinterpret_cast<const std::byte*>(json.data());
        m.dataSize    = json.size();
        EXPECT_TRUE(writer.write(m).ok());

        auto cdr      = pose_message(i);
        m.channelId   = pose_ch.id;
        m.logTime     = t + 50'000'000ull;
        m.publishTime = m.logTime;
        m.data        = cdr.data();
        m.dataSize    = cdr.size();
        EXPECT_TRUE(writer.write(m).ok());

        const char pb[] = {0x08, 0x01};
        m.channelId     = pb_ch.id;
        m.data          = reinterpret_cast<const std::byte*>(pb);
        m.dataSize      = sizeof(pb);
        EXPECT_TRUE(writer.write(m).ok());
    }
    writer.close();
    return path;
}

}   // namespace

class McapLogReaderTest : public ::testing::TestWithParam<bool>
{
};

TEST_P(McapLogReaderTest, JsonAndRos2CdrChannels)
{
    const std::string path =
        write_mcap_fixture(GetParam() ? "chunked.mcap" : "unchunked.mcap", GetParam());

    LogReaderRegistry reg;
    register_builtin_log_readers(reg);
    EXPECT_EQ(reg.detect(path), "MCAP");
    std::string error;
    auto        reader = reg.open(path, &error);
    ASSERT_NE(reader, nullptr) << error;

    // Topics sorted: /pose, /telemetry; /proto skipped with a warning.
    const auto& fields = reader->fields();
    ASSERT_FALSE(fields.empty());
    EXPECT_EQ(fields.front().topic, "/pose");
    EXPECT_FALSE(reader->find_field("/proto", "").has_value());
    ASSERT_EQ(reader->warnings().size(), 1u);
    EXPECT_NE(reader->warnings()[0].find("/proto"), std::string::npos);

    // JSON: numbers, bools, nested objects and arrays; strings skipped.
    LogSeries s;
    ASSERT_TRUE(reader->read(field_index(*reader, "/telemetry", "speed"), s));
    ASSERT_EQ(s.values.size(), 5u);
    EXPECT_FLOAT_EQ(s.values[4], 6.0f);
    EXPECT_DOUBLE_EQ(s.time[0], 1.7e9);
    EXPECT_NEAR(s.time[4] - s.time[0], 0.4, 1e-6);   // double keeps sub-ms precision

    ASSERT_TRUE(reader->read(field_index(*reader, "/telemetry", "armed"), s));
    EXPECT_EQ(s.values, (std::vector<float>{0, 0, 0, 1, 1}));
    ASSERT_TRUE(reader->read(field_index(*reader, "/telemetry", "pos.x"), s));
    EXPECT_FLOAT_EQ(s.values[3], 3.0f);
    ASSERT_TRUE(reader->read(field_index(*reader, "/telemetry", "cells[1]"), s));
    EXPECT_FLOAT_EQ(s.values[0], 3.8f);
    EXPECT_FALSE(reader->find_field("/telemetry", "mode").has_value());

    // ros2msg/CDR.
    const size_t px = field_index(*reader, "/pose", "pose.position.x");
    EXPECT_EQ(fields[px].type, "float64");
    EXPECT_EQ(fields[px].sample_count, 5u);
    ASSERT_TRUE(reader->read(px, s));
    EXPECT_FLOAT_EQ(s.values[2], 2.0f);
    EXPECT_NEAR(s.time[0] - 1.7e9, 0.05, 1e-6);
    ASSERT_TRUE(reader->read(field_index(*reader, "/pose", "dyn[1]"), s));
    EXPECT_FLOAT_EQ(s.values[4], 12.0f);
    EXPECT_EQ(fields[field_index(*reader, "/pose", "header.stamp.sec")].type, "int32");

    std::filesystem::remove(path);
}

INSTANTIATE_TEST_SUITE_P(Chunking, McapLogReaderTest, ::testing::Values(false, true));

TEST(McapLogReader, RejectsNonMcapFiles)
{
    const std::string path = temp_path("fake.mcap");
    write_bytes(path, {'n', 'o', 'p', 'e'});
    McapLogReader reader;
    EXPECT_FALSE(reader.open(path));
    EXPECT_FALSE(reader.last_error().empty());
    std::filesystem::remove(path);
}

#endif   // SPECTRA_HAS_MCAP

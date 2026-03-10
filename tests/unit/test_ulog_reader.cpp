// Unit tests for ULogReader — PX4 ULog binary file parser.
//
// These tests exercise the ULog reader with synthetic binary data,
// verifying header parsing, format definitions, data extraction, and
// field offset resolution.  No actual .ulg files are required.

#include <gtest/gtest.h>

#include "ulog_reader.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace spectra::adapters::px4;

// ---------------------------------------------------------------------------
// Helper: build a valid ULog binary in memory.
// ---------------------------------------------------------------------------

class ULogBuilder
{
   public:
    ULogBuilder()
    {
        // Write ULog header: magic (7 bytes) + version (1 byte) + timestamp (8 bytes).
        static constexpr uint8_t magic[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
        buf_.insert(buf_.end(), magic, magic + sizeof(magic));
        buf_.push_back(1);   // version
        write_u64(1000);     // timestamp_us = 1000
    }

    // Add a FORMAT definition message.
    void add_format(const std::string& content)
    {
        add_message('F',
                    reinterpret_cast<const uint8_t*>(content.data()),
                    static_cast<uint16_t>(content.size()));
    }

    // Add an INFO message.
    void add_info(const std::string& type_and_key, const uint8_t* val, uint16_t val_len)
    {
        std::vector<uint8_t> payload;
        payload.push_back(static_cast<uint8_t>(type_and_key.size()));
        payload.insert(payload.end(), type_and_key.begin(), type_and_key.end());
        payload.insert(payload.end(), val, val + val_len);
        add_message('I', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a string INFO message.
    void add_info_string(const std::string& key, const std::string& value)
    {
        std::string type_key = "char[" + std::to_string(value.size()) + "] " + key;
        add_info(type_key,
                 reinterpret_cast<const uint8_t*>(value.data()),
                 static_cast<uint16_t>(value.size()));
    }

    // Add a PARAMETER message.
    void add_param_float(const std::string& name, float value)
    {
        std::string          type_key = "float " + name;
        std::vector<uint8_t> payload;
        payload.push_back(static_cast<uint8_t>(type_key.size()));
        payload.insert(payload.end(), type_key.begin(), type_key.end());
        uint8_t val_bytes[4];
        std::memcpy(val_bytes, &value, 4);
        payload.insert(payload.end(), val_bytes, val_bytes + 4);
        add_message('P', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add an ADD_LOGGED_MSG (subscription) message.
    void add_subscription(uint8_t multi_id, uint16_t msg_id, const std::string& name)
    {
        std::vector<uint8_t> payload;
        payload.push_back(multi_id);
        uint8_t id_bytes[2];
        std::memcpy(id_bytes, &msg_id, 2);
        payload.insert(payload.end(), id_bytes, id_bytes + 2);
        payload.insert(payload.end(), name.begin(), name.end());
        add_message('A', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a DATA message.
    void add_data(uint16_t msg_id, uint64_t timestamp, const uint8_t* fields, size_t field_len)
    {
        std::vector<uint8_t> payload;
        uint8_t              id_bytes[2];
        std::memcpy(id_bytes, &msg_id, 2);
        payload.insert(payload.end(), id_bytes, id_bytes + 2);
        uint8_t ts_bytes[8];
        std::memcpy(ts_bytes, &timestamp, 8);
        payload.insert(payload.end(), ts_bytes, ts_bytes + 8);
        payload.insert(payload.end(), fields, fields + field_len);
        add_message('D', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a LOGGING message.
    void add_log_message(uint8_t level, uint64_t timestamp, const std::string& text)
    {
        std::vector<uint8_t> payload;
        payload.push_back(level);
        uint8_t ts_bytes[8];
        std::memcpy(ts_bytes, &timestamp, 8);
        payload.insert(payload.end(), ts_bytes, ts_bytes + 8);
        payload.insert(payload.end(), text.begin(), text.end());
        add_message('L', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a DROPOUT message.
    void add_dropout(uint16_t duration_ms)
    {
        uint8_t payload[2];
        std::memcpy(payload, &duration_ms, 2);
        add_message('O', payload, 2);
    }

    // Write to file and return path.
    std::string write_to_file(const std::string& path) const
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(buf_.data()),
                  static_cast<std::streamsize>(buf_.size()));
        ofs.close();
        return path;
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }

   private:
    void add_message(uint8_t type, const uint8_t* payload, uint16_t len)
    {
        uint8_t hdr[3];
        std::memcpy(hdr, &len, 2);
        hdr[2] = type;
        buf_.insert(buf_.end(), hdr, hdr + 3);
        buf_.insert(buf_.end(), payload, payload + len);
    }

    void write_u64(uint64_t v)
    {
        uint8_t bytes[8];
        std::memcpy(bytes, &v, 8);
        buf_.insert(buf_.end(), bytes, bytes + 8);
    }

    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Helper: generate a temp file path.
// ---------------------------------------------------------------------------

static std::string temp_ulog_path(const std::string& test_name)
{
    return "/tmp/spectra_ulog_test_" + test_name + ".ulg";
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(ULogReaderTest, DefaultState)
{
    ULogReader reader;
    EXPECT_FALSE(reader.is_open());
    EXPECT_EQ(reader.topic_count(), 0u);
    EXPECT_TRUE(reader.last_error().empty());
}

TEST(ULogReaderTest, OpenNonExistentFile)
{
    ULogReader reader;
    EXPECT_FALSE(reader.open("/tmp/nonexistent_ulog_file_xyz.ulg"));
    EXPECT_FALSE(reader.last_error().empty());
}

TEST(ULogReaderTest, OpenEmptyFile)
{
    std::string path = temp_ulog_path("empty");
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.close();
    }
    ULogReader reader;
    EXPECT_FALSE(reader.open(path));
    std::remove(path.c_str());
}

TEST(ULogReaderTest, OpenInvalidMagic)
{
    std::string path = temp_ulog_path("bad_magic");
    {
        std::ofstream        ofs(path, std::ios::binary);
        std::vector<uint8_t> data(16, 0);
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    ULogReader reader;
    EXPECT_FALSE(reader.open(path));
    EXPECT_NE(reader.last_error().find("magic"), std::string::npos);
    std::remove(path.c_str());
}

TEST(ULogReaderTest, ParseHeaderOnly)
{
    ULogBuilder builder;
    std::string path = builder.write_to_file(temp_ulog_path("header_only"));

    ULogReader reader;
    EXPECT_TRUE(reader.open(path));
    EXPECT_TRUE(reader.is_open());
    EXPECT_EQ(reader.metadata().version, 1);
    EXPECT_EQ(reader.metadata().timestamp_us, 1000u);

    reader.close();
    EXPECT_FALSE(reader.is_open());
    std::remove(path.c_str());
}

TEST(ULogReaderTest, ParseFormatDefinition)
{
    ULogBuilder builder;
    builder.add_format("test_msg:uint64_t timestamp;float roll;float pitch;float yaw");
    std::string path = builder.write_to_file(temp_ulog_path("format"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto* fmt = reader.format("test_msg");
    ASSERT_NE(fmt, nullptr);
    EXPECT_EQ(fmt->name, "test_msg");
    EXPECT_EQ(fmt->fields.size(), 4u);

    EXPECT_EQ(fmt->fields[0].name, "timestamp");
    EXPECT_EQ(fmt->fields[0].base_type, ULogFieldType::UInt64);

    EXPECT_EQ(fmt->fields[1].name, "roll");
    EXPECT_EQ(fmt->fields[1].base_type, ULogFieldType::Float);
    EXPECT_EQ(fmt->fields[1].offset, 8u);   // after uint64_t

    EXPECT_EQ(fmt->fields[2].name, "pitch");
    EXPECT_EQ(fmt->fields[2].offset, 12u);

    EXPECT_EQ(fmt->fields[3].name, "yaw");
    EXPECT_EQ(fmt->fields[3].offset, 16u);

    std::remove(path.c_str());
}

TEST(ULogReaderTest, ParseArrayField)
{
    ULogBuilder builder;
    builder.add_format("quat_msg:uint64_t timestamp;float[4] q");
    std::string path = builder.write_to_file(temp_ulog_path("array_field"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto* fmt = reader.format("quat_msg");
    ASSERT_NE(fmt, nullptr);
    ASSERT_EQ(fmt->fields.size(), 2u);

    EXPECT_EQ(fmt->fields[1].name, "q");
    EXPECT_EQ(fmt->fields[1].array_size, 4);
    EXPECT_EQ(fmt->fields[1].base_type, ULogFieldType::Float);
    EXPECT_EQ(fmt->fields[1].offset, 8u);   // after timestamp

    EXPECT_EQ(fmt->byte_size, 8u + 16u);   // uint64_t + float[4]

    std::remove(path.c_str());
}

TEST(ULogReaderTest, ParseInfoMessage)
{
    ULogBuilder builder;
    builder.add_info_string("sys_name", "PX4_Autopilot");
    std::string path = builder.write_to_file(temp_ulog_path("info"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto info = reader.info("sys_name");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->as_string(), "PX4_Autopilot");

    EXPECT_EQ(reader.info_string("sys_name"), "PX4_Autopilot");
    EXPECT_EQ(reader.info_string("nonexistent"), "");

    std::remove(path.c_str());
}

TEST(ULogReaderTest, ParseParameters)
{
    ULogBuilder builder;
    builder.add_param_float("MC_ROLL_P", 6.5f);
    builder.add_param_float("MC_PITCH_P", 6.5f);
    std::string path = builder.write_to_file(temp_ulog_path("params"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto& params = reader.parameters();
    ASSERT_GE(params.size(), 2u);

    EXPECT_EQ(params[0].key, "MC_ROLL_P");
    EXPECT_TRUE(params[0].is_float);
    EXPECT_FLOAT_EQ(params[0].value_float, 6.5f);

    std::remove(path.c_str());
}

TEST(ULogReaderTest, ParseDataMessages)
{
    ULogBuilder builder;
    builder.add_format("simple:uint64_t timestamp;float value");
    builder.add_subscription(0, 1, "simple");

    // Add three data points.
    float val1 = 1.5f, val2 = 2.5f, val3 = 3.5f;
    builder.add_data(1, 100000, reinterpret_cast<const uint8_t*>(&val1), sizeof(float));
    builder.add_data(1, 200000, reinterpret_cast<const uint8_t*>(&val2), sizeof(float));
    builder.add_data(1, 300000, reinterpret_cast<const uint8_t*>(&val3), sizeof(float));

    std::string path = builder.write_to_file(temp_ulog_path("data"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    EXPECT_TRUE(reader.has_topic("simple"));
    EXPECT_EQ(reader.topic_count(), 1u);

    auto* ts = reader.data_for("simple");
    ASSERT_NE(ts, nullptr);
    ASSERT_EQ(ts->rows.size(), 3u);

    EXPECT_EQ(ts->rows[0].timestamp_us, 100000u);
    EXPECT_EQ(ts->rows[1].timestamp_us, 200000u);
    EXPECT_EQ(ts->rows[2].timestamp_us, 300000u);

    // Extract field values.
    auto [times, values] = ts->extract_field("value");
    ASSERT_EQ(times.size(), 3u);
    ASSERT_EQ(values.size(), 3u);

    EXPECT_NEAR(times[0], 0.1f, 0.001f);
    EXPECT_NEAR(times[1], 0.2f, 0.001f);
    EXPECT_NEAR(times[2], 0.3f, 0.001f);

    EXPECT_FLOAT_EQ(values[0], 1.5f);
    EXPECT_FLOAT_EQ(values[1], 2.5f);
    EXPECT_FLOAT_EQ(values[2], 3.5f);

    std::remove(path.c_str());
}

TEST(ULogReaderTest, ExtractArrayElement)
{
    ULogBuilder builder;
    builder.add_format("vec_msg:uint64_t timestamp;float[3] xyz");
    builder.add_subscription(0, 1, "vec_msg");

    // One data point with xyz = [10.0, 20.0, 30.0].
    float xyz[] = {10.0f, 20.0f, 30.0f};
    builder.add_data(1, 500000, reinterpret_cast<const uint8_t*>(xyz), sizeof(xyz));

    std::string path = builder.write_to_file(temp_ulog_path("array_elem"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto* ts = reader.data_for("vec_msg");
    ASSERT_NE(ts, nullptr);

    auto [t0, v0] = ts->extract_array_element("xyz", 0);
    auto [t1, v1] = ts->extract_array_element("xyz", 1);
    auto [t2, v2] = ts->extract_array_element("xyz", 2);

    ASSERT_EQ(v0.size(), 1u);
    ASSERT_EQ(v1.size(), 1u);
    ASSERT_EQ(v2.size(), 1u);

    EXPECT_FLOAT_EQ(v0[0], 10.0f);
    EXPECT_FLOAT_EQ(v1[0], 20.0f);
    EXPECT_FLOAT_EQ(v2[0], 30.0f);

    // Out of bounds.
    auto [t3, v3] = ts->extract_array_element("xyz", 5);
    EXPECT_TRUE(v3.empty());

    std::remove(path.c_str());
}

TEST(ULogReaderTest, MultiInstanceSubscription)
{
    ULogBuilder builder;
    builder.add_format("sensor:uint64_t timestamp;float temp");
    builder.add_subscription(0, 1, "sensor");
    builder.add_subscription(1, 2, "sensor");

    float temp1 = 25.0f, temp2 = 30.0f;
    builder.add_data(1, 100000, reinterpret_cast<const uint8_t*>(&temp1), sizeof(float));
    builder.add_data(2, 100000, reinterpret_cast<const uint8_t*>(&temp2), sizeof(float));

    std::string path = builder.write_to_file(temp_ulog_path("multi_inst"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto* ts0 = reader.data_for("sensor", 0);
    auto* ts1 = reader.data_for("sensor", 1);

    ASSERT_NE(ts0, nullptr);
    ASSERT_NE(ts1, nullptr);

    ASSERT_EQ(ts0->rows.size(), 1u);
    ASSERT_EQ(ts1->rows.size(), 1u);

    auto [times0, v0] = ts0->extract_field("temp");
    auto [times1, v1] = ts1->extract_field("temp");

    ASSERT_EQ(v0.size(), 1u);
    ASSERT_EQ(v1.size(), 1u);
    EXPECT_FLOAT_EQ(v0[0], 25.0f);
    EXPECT_FLOAT_EQ(v1[0], 30.0f);

    std::remove(path.c_str());
}

TEST(ULogReaderTest, LogMessages)
{
    ULogBuilder builder;
    builder.add_log_message('6', 50000, "System booted");
    builder.add_log_message('4', 60000, "Low battery");

    std::string path = builder.write_to_file(temp_ulog_path("log_msgs"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto& logs = reader.log_messages();
    ASSERT_EQ(logs.size(), 2u);

    EXPECT_EQ(logs[0].log_level, '6');
    EXPECT_EQ(std::string(logs[0].level_name()), "INFO");
    EXPECT_EQ(logs[0].message, "System booted");

    EXPECT_EQ(logs[1].log_level, '4');
    EXPECT_EQ(std::string(logs[1].level_name()), "WARN");
    EXPECT_EQ(logs[1].message, "Low battery");

    std::remove(path.c_str());
}

TEST(ULogReaderTest, Dropouts)
{
    ULogBuilder builder;
    builder.add_format("dummy:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "dummy");

    float x = 1.0f;
    builder.add_data(1, 100000, reinterpret_cast<const uint8_t*>(&x), sizeof(float));
    builder.add_dropout(150);
    builder.add_data(1, 250000, reinterpret_cast<const uint8_t*>(&x), sizeof(float));
    builder.add_dropout(50);

    std::string path = builder.write_to_file(temp_ulog_path("dropouts"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    auto& dropouts = reader.dropouts();
    ASSERT_EQ(dropouts.size(), 2u);
    EXPECT_EQ(dropouts[0].duration_ms, 150u);
    EXPECT_EQ(dropouts[1].duration_ms, 50u);

    EXPECT_EQ(reader.metadata().dropout_count, 2u);
    EXPECT_EQ(reader.metadata().total_dropout_ms, 200u);

    std::remove(path.c_str());
}

TEST(ULogReaderTest, MetadataDuration)
{
    ULogBuilder builder;
    builder.add_format("dummy:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "dummy");

    float x = 1.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&x), sizeof(float));
    builder.add_data(1, 5000000, reinterpret_cast<const uint8_t*>(&x), sizeof(float));

    std::string path = builder.write_to_file(temp_ulog_path("duration"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    EXPECT_EQ(reader.metadata().start_time_us, 1000000u);
    EXPECT_EQ(reader.metadata().end_time_us, 5000000u);
    EXPECT_EQ(reader.metadata().duration_us, 4000000u);
    EXPECT_NEAR(reader.metadata().duration_sec(), 4.0, 0.001);

    std::remove(path.c_str());
}

TEST(ULogReaderTest, CloseAndReopen)
{
    ULogBuilder builder;
    builder.add_format("msg:uint64_t timestamp;float val");
    std::string path = builder.write_to_file(temp_ulog_path("reopen"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));
    EXPECT_TRUE(reader.is_open());
    EXPECT_NE(reader.format("msg"), nullptr);

    reader.close();
    EXPECT_FALSE(reader.is_open());
    EXPECT_EQ(reader.format("msg"), nullptr);

    // Reopen.
    ASSERT_TRUE(reader.open(path));
    EXPECT_TRUE(reader.is_open());
    EXPECT_NE(reader.format("msg"), nullptr);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// ULog field type parsing
// ---------------------------------------------------------------------------

TEST(ULogFieldTypeTest, ParsePrimitiveTypes)
{
    int arr;

    EXPECT_EQ(parse_ulog_type("float", arr), ULogFieldType::Float);
    EXPECT_EQ(arr, 1);

    EXPECT_EQ(parse_ulog_type("uint32_t", arr), ULogFieldType::UInt32);
    EXPECT_EQ(arr, 1);

    EXPECT_EQ(parse_ulog_type("int64_t", arr), ULogFieldType::Int64);
    EXPECT_EQ(arr, 1);

    EXPECT_EQ(parse_ulog_type("bool", arr), ULogFieldType::Bool);
    EXPECT_EQ(arr, 1);

    EXPECT_EQ(parse_ulog_type("char", arr), ULogFieldType::Char);
    EXPECT_EQ(arr, 1);
}

TEST(ULogFieldTypeTest, ParseArrayTypes)
{
    int arr;

    EXPECT_EQ(parse_ulog_type("float[4]", arr), ULogFieldType::Float);
    EXPECT_EQ(arr, 4);

    EXPECT_EQ(parse_ulog_type("uint8_t[16]", arr), ULogFieldType::UInt8);
    EXPECT_EQ(arr, 16);

    EXPECT_EQ(parse_ulog_type("char[40]", arr), ULogFieldType::Char);
    EXPECT_EQ(arr, 40);
}

TEST(ULogFieldTypeTest, ParseNestedType)
{
    int arr;
    EXPECT_EQ(parse_ulog_type("vehicle_status", arr), ULogFieldType::Nested);
    EXPECT_EQ(arr, 1);
}

TEST(ULogFieldTypeTest, FieldSizes)
{
    EXPECT_EQ(ulog_field_size(ULogFieldType::Int8), 1u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::UInt8), 1u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Int16), 2u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::UInt16), 2u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Int32), 4u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::UInt32), 4u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Int64), 8u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::UInt64), 8u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Float), 4u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Double), 8u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Bool), 1u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Char), 1u);
    EXPECT_EQ(ulog_field_size(ULogFieldType::Nested), 0u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

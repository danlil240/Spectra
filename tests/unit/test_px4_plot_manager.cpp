// Unit tests for Px4PlotManager — field management and data extraction.
//
// Tests plot field add/remove, ULog data loading, and topic/field listing.
// No GPU, no ImGui context, no network needed.

#include <gtest/gtest.h>

#include "px4_plot_manager.hpp"
#include "ulog_reader.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace spectra::adapters::px4;

// ---------------------------------------------------------------------------
// Reusable ULog builder (same as in test_ulog_reader.cpp).
// ---------------------------------------------------------------------------

class ULogBuilder
{
   public:
    ULogBuilder()
    {
        static constexpr uint8_t magic[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
        buf_.insert(buf_.end(), magic, magic + sizeof(magic));
        buf_.push_back(1);
        write_u64(1000);
    }

    void add_format(const std::string& content)
    {
        add_message('F',
                    reinterpret_cast<const uint8_t*>(content.data()),
                    static_cast<uint16_t>(content.size()));
    }

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

    std::string write_to_file(const std::string& path) const
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(buf_.data()),
                  static_cast<std::streamsize>(buf_.size()));
        return path;
    }

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

static std::string temp_path(const std::string& name)
{
    return "/tmp/spectra_pm_test_" + name + ".ulg";
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(Px4PlotManagerTest, DefaultState)
{
    Px4PlotManager mgr;
    EXPECT_EQ(mgr.field_count(), 0u);
    EXPECT_FALSE(mgr.is_live_mode());
    EXPECT_FALSE(mgr.has_ulog_data());
    EXPECT_TRUE(mgr.available_topics().empty());
}

TEST(Px4PlotManagerTest, AddAndRemoveField)
{
    Px4PlotManager mgr;

    size_t idx = mgr.add_field("attitude", "roll");
    EXPECT_EQ(mgr.field_count(), 1u);
    EXPECT_EQ(mgr.fields()[idx].topic, "attitude");
    EXPECT_EQ(mgr.fields()[idx].field, "roll");
    EXPECT_EQ(mgr.fields()[idx].label, "attitude.roll");

    mgr.add_field("attitude", "pitch");
    EXPECT_EQ(mgr.field_count(), 2u);

    mgr.remove_field(0);
    EXPECT_EQ(mgr.field_count(), 1u);
    EXPECT_EQ(mgr.fields()[0].field, "pitch");

    mgr.remove_topic("attitude");
    EXPECT_EQ(mgr.field_count(), 0u);
}

TEST(Px4PlotManagerTest, AddArrayField)
{
    Px4PlotManager mgr;
    size_t         idx = mgr.add_field("quat_msg", "q", 2, 0);
    EXPECT_EQ(mgr.fields()[idx].label, "quat_msg.q[2]");
    EXPECT_EQ(mgr.fields()[idx].array_idx, 2);
}

TEST(Px4PlotManagerTest, AddMultiInstanceField)
{
    Px4PlotManager mgr;
    size_t         idx = mgr.add_field("sensor", "temp", -1, 1);
    EXPECT_EQ(mgr.fields()[idx].label, "sensor.temp (1)");
}

TEST(Px4PlotManagerTest, AddLiveField)
{
    Px4PlotManager mgr;
    size_t         idx = mgr.add_live_field("ATTITUDE", "roll");
    EXPECT_EQ(mgr.fields()[idx].topic, "ATTITUDE");
    EXPECT_EQ(mgr.fields()[idx].field, "roll");
    EXPECT_EQ(mgr.fields()[idx].label, "ATTITUDE.roll");
}

TEST(Px4PlotManagerTest, LoadULogAndExtractData)
{
    ULogBuilder builder;
    builder.add_format("test_topic:uint64_t timestamp;float value");
    builder.add_subscription(0, 1, "test_topic");

    float v1 = 42.0f, v2 = 84.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&v1), sizeof(float));
    builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(&v2), sizeof(float));

    std::string path = builder.write_to_file(temp_path("load"));

    ULogReader reader;
    ASSERT_TRUE(reader.open(path));

    Px4PlotManager mgr;
    mgr.load_ulog(reader);
    EXPECT_TRUE(mgr.has_ulog_data());

    // Available topics.
    auto topics = mgr.available_topics();
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0], "test_topic");

    // Available fields.
    auto fields = mgr.topic_fields("test_topic");
    ASSERT_GE(fields.size(), 2u);   // timestamp + value

    // Add field and check data.
    size_t idx = mgr.add_field("test_topic", "value");
    auto&  pf  = mgr.fields()[idx];
    ASSERT_EQ(pf.times.size(), 2u);
    ASSERT_EQ(pf.values.size(), 2u);
    EXPECT_FLOAT_EQ(pf.values[0], 42.0f);
    EXPECT_FLOAT_EQ(pf.values[1], 84.0f);

    std::remove(path.c_str());
}

TEST(Px4PlotManagerTest, Clear)
{
    Px4PlotManager mgr;
    mgr.add_field("topic1", "field1");
    mgr.add_field("topic2", "field2");
    EXPECT_EQ(mgr.field_count(), 2u);

    mgr.clear();
    EXPECT_EQ(mgr.field_count(), 0u);
    EXPECT_FALSE(mgr.has_ulog_data());
    EXPECT_FALSE(mgr.is_live_mode());
}

TEST(Px4PlotManagerTest, TimeWindow)
{
    Px4PlotManager mgr;
    EXPECT_DOUBLE_EQ(mgr.time_window(), 30.0);

    mgr.set_time_window(60.0);
    EXPECT_DOUBLE_EQ(mgr.time_window(), 60.0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// Unit tests for Px4AppShell — CLI parsing and offline ULog open flow.

#include <gtest/gtest.h>

#include "px4_app_shell.hpp"

#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace spectra::adapters::px4;

namespace
{

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
        add_message('F', reinterpret_cast<const uint8_t*>(content.data()),
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
        uint8_t id_bytes[2];
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

    void write_u64(uint64_t value)
    {
        uint8_t bytes[8];
        std::memcpy(bytes, &value, 8);
        buf_.insert(buf_.end(), bytes, bytes + 8);
    }

    std::vector<uint8_t> buf_;
};

std::string temp_path(const std::string& name)
{
    return "/tmp/spectra_px4_shell_" + name + ".ulg";
}

}   // namespace

TEST(Px4AppShellTest, ParseArgsAcceptsUlogAndConnectOptions)
{
    std::string err;
    const char* argv[] = {"spectra-px4", "--ulog", "/tmp/demo.ulg", "--host", "10.0.0.2",
                          "--port", "14550", "--connect", "--window-s", "45.5"};

    Px4AppConfig cfg = parse_px4_args(static_cast<int>(std::size(argv)),
                                      const_cast<char**>(argv), err);

    EXPECT_TRUE(err.empty());
    EXPECT_EQ(cfg.ulog_file, "/tmp/demo.ulg");
    EXPECT_EQ(cfg.host, "10.0.0.2");
    EXPECT_EQ(cfg.port, 14550);
    EXPECT_TRUE(cfg.auto_connect);
    EXPECT_DOUBLE_EQ(cfg.time_window_s, 45.5);
}

TEST(Px4AppShellTest, OpenUlogLoadsReaderAndPlotManager)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll");
    builder.add_subscription(0, 1, "vehicle_attitude");

    float roll = 1.25f;
    std::string path = builder.write_to_file(temp_path("open_success"));
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&roll), sizeof(roll));

    // Rewrite with data included.
    path = builder.write_to_file(path);

    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.open_ulog(path));

    EXPECT_TRUE(shell.reader().is_open());
    EXPECT_TRUE(shell.plot_manager().has_ulog_data());

    auto topics = shell.plot_manager().available_topics();
    ASSERT_EQ(topics.size(), 1u);
    EXPECT_EQ(topics[0], "vehicle_attitude");

    std::remove(path.c_str());
}

TEST(Px4AppShellTest, FailedOpenClearsLoadedPlotState)
{
    ULogBuilder builder;
    builder.add_format("vehicle_local_position:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "vehicle_local_position");

    float x = 3.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&x), sizeof(x));
    std::string path = builder.write_to_file(temp_path("open_then_fail"));

    Px4AppShell shell(Px4AppConfig{});
    ASSERT_TRUE(shell.open_ulog(path));
    ASSERT_TRUE(shell.plot_manager().has_ulog_data());

    EXPECT_FALSE(shell.open_ulog("/tmp/this_file_should_not_exist_ulog.ulg"));
    EXPECT_FALSE(shell.reader().is_open());
    EXPECT_FALSE(shell.plot_manager().has_ulog_data());
    EXPECT_TRUE(shell.plot_manager().available_topics().empty());

    std::remove(path.c_str());
}

TEST(Px4AppShellTest, PollSyncsSelectedUlogFieldToCanvasFigure)
{
    ULogBuilder builder;
    builder.add_format("vehicle_attitude:uint64_t timestamp;float roll");
    builder.add_subscription(0, 1, "vehicle_attitude");

    float roll1 = 1.25f;
    float roll2 = 2.50f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&roll1), sizeof(roll1));
    builder.add_data(1, 2000000, reinterpret_cast<const uint8_t*>(&roll2), sizeof(roll2));

    std::string path = builder.write_to_file(temp_path("canvas_sync"));

    Px4AppShell shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));
    shell.plot_manager().add_field("vehicle_attitude", "roll");
    shell.poll();

    ASSERT_EQ(fig.axes().size(), 1u);
    ASSERT_NE(fig.axes()[0], nullptr);
    ASSERT_EQ(fig.axes()[0]->series().size(), 1u);

    auto* line = dynamic_cast<spectra::LineSeries*>(fig.axes()[0]->series()[0].get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->label(), "vehicle_attitude.roll");
    ASSERT_EQ(line->point_count(), 2u);
    EXPECT_FLOAT_EQ(line->x_data()[0], 1.0f);
    EXPECT_FLOAT_EQ(line->x_data()[1], 2.0f);
    EXPECT_FLOAT_EQ(line->y_data()[0], 1.25f);
    EXPECT_FLOAT_EQ(line->y_data()[1], 2.50f);

    std::remove(path.c_str());
}

TEST(Px4AppShellTest, FailedOpenClearsCanvasSeries)
{
    ULogBuilder builder;
    builder.add_format("vehicle_local_position:uint64_t timestamp;float x");
    builder.add_subscription(0, 1, "vehicle_local_position");

    float x = 3.0f;
    builder.add_data(1, 1000000, reinterpret_cast<const uint8_t*>(&x), sizeof(x));
    std::string path = builder.write_to_file(temp_path("canvas_clear"));

    Px4AppShell shell(Px4AppConfig{});
    spectra::Figure fig({.width = 800, .height = 600});
    shell.set_canvas_figure(&fig);

    ASSERT_TRUE(shell.open_ulog(path));
    shell.plot_manager().add_field("vehicle_local_position", "x");
    shell.poll();

    ASSERT_EQ(fig.axes().size(), 1u);
    ASSERT_EQ(fig.axes()[0]->series().size(), 1u);

    EXPECT_FALSE(shell.open_ulog("/tmp/this_file_should_not_exist_canvas_clear.ulg"));
    shell.poll();

    ASSERT_EQ(fig.axes().size(), 1u);
    EXPECT_TRUE(fig.axes()[0]->series().empty());

    std::remove(path.c_str());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

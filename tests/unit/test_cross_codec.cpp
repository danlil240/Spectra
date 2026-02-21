#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ipc/codec.hpp"
#include "ipc/message.hpp"

using namespace spectra::ipc;
namespace fs = std::filesystem;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string cross_dir()
{
    const char* env = std::getenv("SPECTRA_CROSS_DIR");
    if (env && *env)
        return env;
    return "/tmp/spectra_cross";
}

static std::vector<uint8_t> read_bin(const std::string& name)
{
    auto path = fs::path(cross_dir()) / name;
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static void write_bin(const std::string& name, const std::vector<uint8_t>& data)
{
    auto dir = fs::path(cross_dir());
    fs::create_directories(dir);
    auto path = dir / name;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

// ─── Phase 1: C++ writes payloads for Python to decode ───────────────────────

class CrossCodecCppWrite : public ::testing::Test
{
protected:
    void SetUp() override { fs::create_directories(cross_dir()); }
};

TEST_F(CrossCodecCppWrite, WriteHello)
{
    HelloPayload hp;
    hp.protocol_major = 1;
    hp.protocol_minor = 0;
    hp.agent_build = "test-cross-cpp";
    hp.capabilities = 0;
    hp.client_type = "agent";
    write_bin("cpp_hello.bin", encode_hello(hp));
}

TEST_F(CrossCodecCppWrite, WriteRespFigureCreated)
{
    RespFigureCreatedPayload rp;
    rp.request_id = 7;
    rp.figure_id = 42;
    write_bin("cpp_resp_figure_created.bin", encode_resp_figure_created(rp));
}

TEST_F(CrossCodecCppWrite, WriteRespAxesCreated)
{
    RespAxesCreatedPayload rp;
    rp.request_id = 8;
    rp.axes_index = 3;
    write_bin("cpp_resp_axes_created.bin", encode_resp_axes_created(rp));
}

TEST_F(CrossCodecCppWrite, WriteRespSeriesAdded)
{
    RespSeriesAddedPayload rp;
    rp.request_id = 9;
    rp.series_index = 5;
    write_bin("cpp_resp_series_added.bin", encode_resp_series_added(rp));
}

TEST_F(CrossCodecCppWrite, WriteRespErr)
{
    RespErrPayload rp;
    rp.request_id = 10;
    rp.code = 404;
    rp.message = "Figure not found";
    write_bin("cpp_resp_err.bin", encode_resp_err(rp));
}

TEST_F(CrossCodecCppWrite, WriteRespFigureList)
{
    RespFigureListPayload rp;
    rp.request_id = 11;
    rp.figure_ids = {100, 200, 300};
    write_bin("cpp_resp_figure_list.bin", encode_resp_figure_list(rp));
}

TEST_F(CrossCodecCppWrite, WriteWelcome)
{
    WelcomePayload wp;
    wp.session_id = 12345;
    wp.window_id = 0;
    wp.process_id = 67890;
    wp.heartbeat_ms = 5000;
    wp.mode = "multiproc";
    write_bin("cpp_welcome.bin", encode_welcome(wp));
}

TEST_F(CrossCodecCppWrite, WriteReqCreateFigure)
{
    ReqCreateFigurePayload rp;
    rp.title = "Cross Test";
    rp.width = 1024;
    rp.height = 768;
    write_bin("cpp_req_create_figure.bin", encode_req_create_figure(rp));
}

TEST_F(CrossCodecCppWrite, WriteReqSetData)
{
    ReqSetDataPayload rp;
    rp.figure_id = 42;
    rp.series_index = 0;
    rp.dtype = 0;
    rp.data = {1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f, 4.0f, 40.0f, 5.0f, 50.0f};
    write_bin("cpp_req_set_data.bin", encode_req_set_data(rp));
}

// ─── Phase 2: C++ reads Python-encoded payloads ──────────────────────────────

class CrossCodecCppRead : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Skip if Python hasn't written the files yet
        if (!fs::exists(fs::path(cross_dir()) / "hello.bin"))
            GTEST_SKIP() << "Python payloads not found in " << cross_dir()
                         << ". Run: python tests/test_cross_codec.py --write "
                         << cross_dir();
    }
};

TEST_F(CrossCodecCppRead, DecodeHello)
{
    auto data = read_bin("hello.bin");
    ASSERT_FALSE(data.empty());
    auto hello = decode_hello(data);
    ASSERT_TRUE(hello.has_value());
    EXPECT_EQ(hello->client_type, "python");
    EXPECT_EQ(hello->agent_build, "test-cross-1.0");
    EXPECT_EQ(hello->protocol_major, 1);
    EXPECT_EQ(hello->protocol_minor, 0);
}

TEST_F(CrossCodecCppRead, DecodeReqCreateFigure)
{
    auto data = read_bin("req_create_figure.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_create_figure(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->title, "Cross Test");
    EXPECT_EQ(req->width, 1024u);
    EXPECT_EQ(req->height, 768u);
}

TEST_F(CrossCodecCppRead, DecodeReqCreateAxes)
{
    auto data = read_bin("req_create_axes.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_create_axes(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->figure_id, 42u);
    EXPECT_EQ(req->grid_rows, 2);
    EXPECT_EQ(req->grid_cols, 3);
    EXPECT_EQ(req->grid_index, 5);
}

TEST_F(CrossCodecCppRead, DecodeReqAddSeries)
{
    auto data = read_bin("req_add_series.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_add_series(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->figure_id, 42u);
    EXPECT_EQ(req->axes_index, 0u);
    EXPECT_EQ(req->series_type, "line");
    EXPECT_EQ(req->label, "cross-data");
}

TEST_F(CrossCodecCppRead, DecodeReqSetData)
{
    auto data = read_bin("req_set_data.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_set_data(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->figure_id, 42u);
    EXPECT_EQ(req->series_index, 0u);
    ASSERT_EQ(req->data.size(), 10u);
    EXPECT_FLOAT_EQ(req->data[0], 1.0f);
    EXPECT_FLOAT_EQ(req->data[1], 10.0f);
    EXPECT_FLOAT_EQ(req->data[8], 5.0f);
    EXPECT_FLOAT_EQ(req->data[9], 50.0f);
}

TEST_F(CrossCodecCppRead, DecodeReqUpdateProperty)
{
    auto data = read_bin("req_update_property.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_update_property(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->figure_id, 42u);
    EXPECT_EQ(req->axes_index, 0u);
    EXPECT_EQ(req->series_index, 1u);
    EXPECT_EQ(req->property, "color");
    EXPECT_FLOAT_EQ(req->f1, 1.0f);
    EXPECT_FLOAT_EQ(req->f2, 0.5f);
    EXPECT_FLOAT_EQ(req->f3, 0.25f);
    EXPECT_FLOAT_EQ(req->f4, 0.75f);
}

TEST_F(CrossCodecCppRead, DecodeReqShow)
{
    auto data = read_bin("req_show.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_show(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->figure_id, 42u);
}

TEST_F(CrossCodecCppRead, DecodeReqDestroyFigure)
{
    auto data = read_bin("req_destroy_figure.bin");
    ASSERT_FALSE(data.empty());
    auto req = decode_req_destroy_figure(data);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->figure_id, 99u);
}

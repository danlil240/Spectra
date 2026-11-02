// REQ_EXPORT_FIGURE rendering: a backend figure model (as built from Python
// requests) is written to PNG / SVG offscreen.  Needs a Vulkan device
// (lavapipe in CI), so this test carries the "gpu" label.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "daemon/figure_export.hpp"
#include "daemon/figure_model.hpp"

using namespace spectra;
namespace fs = std::filesystem;

namespace
{

class FigureExportGpu : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        dir_ = fs::temp_directory_path()
               / ("spectra_export_gpu_"
                  + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::create_directories(dir_);
        // Keep the user's saved settings out of the export.
        cfg_dir_ = dir_ / "config";
        fs::create_directories(cfg_dir_);
#ifndef _WIN32
        ::setenv("SPECTRA_CONFIG_DIR", cfg_dir_.c_str(), 1);
#endif

        fid_          = model_.create_figure("export", 640, 400);
        const auto ax = model_.add_axes(fid_, -0.5, 3.5, 0.0, 10.0);
        uint32_t   s  = 0;
        model_.add_series_with_diff(fid_, "bars", "bar", ax, s);
        model_.set_series_data(fid_, s, {0.6f, 0.0f, 0.0f, 0.0f, 4.0f, 0, 1, 2, 3, 3, 8, 5, 6});
        model_.add_series_with_diff(fid_, "line", "line", ax, s);
        model_.set_series_data(fid_, s, {0, 2, 1, 4, 2, 3, 3, 9});
    }

    void TearDown() override
    {
#ifndef _WIN32
        ::unsetenv("SPECTRA_CONFIG_DIR");
#endif
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    ipc::SnapshotFigureState figure() const { return model_.snapshot({fid_}).figures.at(0); }

    static std::vector<unsigned char> head(const fs::path& p, size_t n)
    {
        std::ifstream              f(p, std::ios::binary);
        std::vector<unsigned char> b(n);
        f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n));
        b.resize(static_cast<size_t>(f.gcount()));
        return b;
    }

    static size_t temp_files_in(const fs::path& dir)
    {
        size_t n = 0;
        for (const auto& e : fs::directory_iterator(dir))
            n += e.path().filename().string().find(".spectra-") != std::string::npos;
        return n;
    }

    fs::path            dir_;
    fs::path            cfg_dir_;
    daemon::FigureModel model_;
    uint64_t            fid_ = 0;
};

}   // namespace

TEST_F(FigureExportGpu, WritesPngAtRequestedSize)
{
    ipc::ReqExportFigurePayload req;
    req.figure_id  = fid_;
    req.path       = (dir_ / "out.png").string();
    req.width      = 320;
    req.height     = 200;
    const auto res = daemon::export_figure_snapshot(figure(), req);
    if (res.code == 503)
        GTEST_SKIP() << "no Vulkan device: " << res.message;
    ASSERT_TRUE(res.ok()) << res.message;

    const auto h = head(req.path, 24);
    ASSERT_EQ(h.size(), 24u);
    const std::vector<unsigned char> magic = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_TRUE(std::equal(magic.begin(), magic.end(), h.begin()));
    // IHDR width / height (big-endian) at bytes 16..23.
    auto be32 = [&](size_t o)
    {
        return (uint32_t(h[o]) << 24) | (uint32_t(h[o + 1]) << 16) | (uint32_t(h[o + 2]) << 8)
               | h[o + 3];
    };
    EXPECT_EQ(be32(16), 320u);
    EXPECT_EQ(be32(20), 200u);
    EXPECT_EQ(temp_files_in(dir_), 0u);
}

TEST_F(FigureExportGpu, WritesSvgFromExtension)
{
    ipc::ReqExportFigurePayload req;
    req.figure_id  = fid_;
    req.path       = (dir_ / "out.svg").string();
    const auto res = daemon::export_figure_snapshot(figure(), req);
    if (res.code == 503)
        GTEST_SKIP() << "no Vulkan device: " << res.message;
    ASSERT_TRUE(res.ok()) << res.message;

    std::ifstream f(req.path);
    std::string   svg((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("bars"), std::string::npos);   // legend entry
    EXPECT_EQ(temp_files_in(dir_), 0u);
}

TEST_F(FigureExportGpu, InvalidRequestLeavesExistingFile)
{
    const fs::path existing = dir_ / "keep.png";
    {
        std::ofstream(existing) << "previous";
    }
    ipc::ReqExportFigurePayload req;
    req.figure_id  = fid_;
    req.path       = existing.string();
    req.format     = "bmp";
    const auto res = daemon::export_figure_snapshot(figure(), req);
    EXPECT_EQ(res.code, 400u);
    std::ifstream f(existing);
    std::string   content;
    f >> content;
    EXPECT_EQ(content, "previous");
}

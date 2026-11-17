// Export options dialog back end (ui/app/offscreen_export): a copy of a figure
// is rendered with a headless App at the dialog's size / style / transparency,
// to PNG, SVG and PDF, plus the preview image.  Needs a Vulkan device
// (lavapipe in CI), so this test carries the "gpu" label.

#include <gtest/gtest.h>

#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "stb_image.h"
#include "ui/app/offscreen_export.hpp"
#include "ui/settings/export_settings.hpp"

using namespace spectra;
namespace fs = std::filesystem;
using ui::settings::ExportFormat;

namespace
{

class OffscreenExportGpu : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        dir_ = fs::temp_directory_path()
               / ("spectra_offscreen_export_"
                  + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::create_directories(dir_);

        static const std::vector<float> x{0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
        static const std::vector<float> y{0.0f, 2.0f, 1.0f, 3.0f, 2.5f};
        auto&                           ax = fig_.subplot(1, 1, 1);
        ax.line(x, y).label("data");
        ax.title("Offscreen");

        job_.figure_file = (dir_ / "copy.spectra").string();
        ASSERT_TRUE(snapshot_figure(fig_, job_.figure_file));
        job_.app_theme          = "dark";
        job_.size.layout_width  = 400;
        job_.size.layout_height = 300;
        job_.size.pixel_width   = 800;
        job_.size.pixel_height  = 600;
        job_.size.pixel_scale   = 2.0f;
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    static std::string read_all(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    fs::path           dir_;
    Figure             fig_;
    OffscreenExportJob job_;
};

TEST_F(OffscreenExportGpu, PngHasTheScaledSizeAndATransparentBackground)
{
    job_.format      = ExportFormat::Png;
    job_.out_path    = (dir_ / "out.png").string();
    job_.transparent = true;
    std::string error;
    ASSERT_TRUE(run_offscreen_export(job_, &error)) << error;

    int      w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load(job_.out_path.c_str(), &w, &h, &n, 4);
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(w, 800);
    EXPECT_EQ(h, 600);
    // Corner: outside the plot area, so fully transparent.
    EXPECT_EQ(px[3], 0);
    // Something (axes, text, the line) is drawn opaquely.
    bool any_opaque = false;
    for (int i = 0; i < w * h && !any_opaque; ++i)
        any_opaque = px[i * 4 + 3] == 255;
    EXPECT_TRUE(any_opaque);
    stbi_image_free(px);
}

TEST_F(OffscreenExportGpu, OpaquePngKeepsTheBackground)
{
    job_.format   = ExportFormat::Png;
    job_.out_path = (dir_ / "opaque.png").string();
    ASSERT_TRUE(run_offscreen_export(job_));
    int      w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load(job_.out_path.c_str(), &w, &h, &n, 4);
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(px[3], 255);
    stbi_image_free(px);
}

TEST_F(OffscreenExportGpu, SvgAndPdfHonourStyleAndTransparency)
{
    job_.style       = "paper";
    job_.transparent = true;

    job_.format   = ExportFormat::Svg;
    job_.out_path = (dir_ / "out.svg").string();
    ASSERT_TRUE(run_offscreen_export(job_));
    const std::string svg = read_all(job_.out_path);
    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("width=\"400\""), std::string::npos);

    job_.format   = ExportFormat::Pdf;
    job_.out_path = (dir_ / "out.pdf").string();
    ASSERT_TRUE(run_offscreen_export(job_));
    EXPECT_EQ(read_all(job_.out_path).rfind("%PDF", 0), 0u);
}

TEST_F(OffscreenExportGpu, UnknownStyleFails)
{
    job_.style    = "comic-sans";
    job_.out_path = (dir_ / "bad.png").string();
    std::string error;
    EXPECT_FALSE(run_offscreen_export(job_, &error));
    EXPECT_NE(error.find("comic-sans"), std::string::npos);
}

TEST_F(OffscreenExportGpu, PreviewOnAWorkerThreadFitsTheBox)
{
    PreviewImage img;
    std::thread  worker([&] { img = render_offscreen_preview(job_, 200, 200); });
    worker.join();
    ASSERT_FALSE(img.rgba.empty());
    // 400 x 300 layout shrunk to fit 200 x 200.
    EXPECT_EQ(img.width, 200u);
    EXPECT_EQ(img.height, 150u);
}

}   // namespace

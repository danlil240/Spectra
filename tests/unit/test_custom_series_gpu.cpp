// test_custom_series_gpu.cpp — Pixel test for a plugin custom series type.
//
// Loads the shipped series_heatmap example plugin through the real
// PluginManager of a headless App, adds a "heatmap" CustomSeries and exports a
// PNG.  Every cell must be drawn as a filled quad (plugin audit P-B: custom
// series were never drawn; P-C: topology 0 was passed to Vulkan as a point
// list, so only single pixels appeared).

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/custom_series.hpp>
#include <spectra/figure.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "stb_image.h"
#include "ui/app/window_ui_context.hpp"
#include "ui/workspace/plugin_api.hpp"

namespace fs = std::filesystem;
using namespace spectra;

namespace
{

constexpr uint32_t kCols = 8;
constexpr uint32_t kRows = 6;

std::vector<float> heatmap_blob()
{
    // Layout: [cols_bits, rows_bits, min, max, v0, v1, ...] (row-major values).
    std::vector<float> blob(4 + kCols * kRows);
    std::memcpy(&blob[0], &kCols, sizeof(uint32_t));
    std::memcpy(&blob[1], &kRows, sizeof(uint32_t));
    blob[2] = 0.0f;
    blob[3] = static_cast<float>(kCols * kRows - 1);
    for (uint32_t i = 0; i < kCols * kRows; ++i)
        blob[4 + i] = static_cast<float>(i);
    return blob;
}

struct Image
{
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> rgba;

    const uint8_t* at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
};

bool load(const fs::path& p, Image& img)
{
    int      ch   = 0;
    stbi_uc* data = stbi_load(p.string().c_str(), &img.w, &img.h, &ch, 4);
    if (!data)
        return false;
    img.rgba.assign(data, data + static_cast<size_t>(img.w) * img.h * 4);
    stbi_image_free(data);
    return true;
}

// Theme backgrounds, grid and text are greys; viridis cells are saturated.
bool is_colormapped(const uint8_t* p)
{
    const int hi = std::max({p[0], p[1], p[2]});
    const int lo = std::min({p[0], p[1], p[2]});
    return hi - lo > 40;
}

}   // namespace

TEST(CustomSeriesGpu, HeatmapPluginDrawsFilledCells)
{
#ifndef SPECTRA_HEATMAP_PLUGIN_PATH
    GTEST_SKIP() << "series_heatmap example plugin not built";
#else
    const fs::path dir = fs::temp_directory_path() / "spectra_test_custom_series_gpu";
    fs::create_directories(dir);
    const fs::path png = dir / "heatmap.png";
    fs::remove(png);

    constexpr uint32_t W = 640, H = 480;
    Rect               vp{};
    {
        App app({.headless = true, .socket_path = ""});
        if (!app.ok())
            GTEST_SKIP() << "No Vulkan device";

        auto& fig  = app.figure({.width = W, .height = H});
        auto& ax   = fig.subplot(1, 1, 1);
        auto  hm   = std::make_unique<CustomSeries>("heatmap");
        auto  blob = heatmap_blob();
        hm->set_data(blob.data(), blob.size() * sizeof(float), blob.size());
        ax.series_mut().push_back(std::move(hm));
        ax.xlim(0.0, static_cast<double>(kCols));
        ax.ylim(0.0, static_cast<double>(kRows));
        fig.save_png(png.string());

        app.init_runtime();
        auto* ui = app.ui_context();
        ASSERT_NE(ui, nullptr);
        ASSERT_NE(ui->plugin_manager, nullptr);
        ASSERT_TRUE(ui->plugin_manager->load_plugin(SPECTRA_HEATMAP_PLUGIN_PATH));

        app.step();
        app.shutdown_runtime();   // performs the headless PNG export

        fig.compute_layout();
        vp = fig.axes()[0]->viewport();
    }

    Image img;
    ASSERT_TRUE(load(png, img)) << png;
    ASSERT_EQ(img.w, static_cast<int>(W));
    ASSERT_EQ(img.h, static_cast<int>(H));
    ASSERT_GT(vp.w, 100.0f);
    ASSERT_GT(vp.h, 100.0f);

    // The heatmap spans the whole plot area: most of it must be colormapped
    // (points would cover well under 1 %).
    int colored = 0, total = 0;
    for (int y = static_cast<int>(vp.y) + 2; y < static_cast<int>(vp.y + vp.h) - 2; ++y)
    {
        for (int x = static_cast<int>(vp.x) + 2; x < static_cast<int>(vp.x + vp.w) - 2; ++x)
        {
            ++total;
            if (is_colormapped(img.at(x, y)))
                ++colored;
        }
    }
    ASSERT_GT(total, 0);
    EXPECT_GT(colored, total * 8 / 10) << colored << " of " << total << " plot pixels colormapped";

    // The centre of every cell is filled, and the low/high corners differ.
    const float cw     = vp.w / kCols;
    const float ch     = vp.h / kRows;
    int         filled = 0;
    for (uint32_t r = 0; r < kRows; ++r)
    {
        for (uint32_t c = 0; c < kCols; ++c)
        {
            const int x = static_cast<int>(vp.x + (c + 0.5f) * cw);
            const int y = static_cast<int>(vp.y + vp.h - (r + 0.5f) * ch);   // y up
            if (is_colormapped(img.at(x, y)))
                ++filled;
        }
    }
    EXPECT_EQ(filled, static_cast<int>(kCols * kRows));

    const uint8_t* lo =
        img.at(static_cast<int>(vp.x + 0.5f * cw), static_cast<int>(vp.y + vp.h - 0.5f * ch));
    const uint8_t* hi =
        img.at(static_cast<int>(vp.x + vp.w - 0.5f * cw), static_cast<int>(vp.y + 0.5f * ch));
    const int d = std::abs(lo[0] - hi[0]) + std::abs(lo[1] - hi[1]) + std::abs(lo[2] - hi[2]);
    EXPECT_GT(d, 200) << "first and last cells should map to opposite ends of viridis";

    if (!std::getenv("SPECTRA_KEEP_TEST_OUTPUT") && !::testing::Test::HasFailure())
        fs::remove_all(dir);
#endif
}

// The app's SVG export (Figure::save_svg) draws plugin series through their
// type's vector export callback: all heatmap cells appear as rectangles.
TEST(CustomSeriesGpu, HeatmapPluginIsDrawnInSvgExport)
{
#ifndef SPECTRA_HEATMAP_PLUGIN_PATH
    GTEST_SKIP() << "series_heatmap example plugin not built";
#else
    const fs::path dir = fs::temp_directory_path() / "spectra_test_custom_series_svg";
    fs::create_directories(dir);
    const fs::path svg_path = dir / "heatmap.svg";
    fs::remove(svg_path);
    {
        App app({.headless = true, .socket_path = ""});
        if (!app.ok())
            GTEST_SKIP() << "No Vulkan device";

        auto& fig  = app.figure({.width = 640, .height = 480});
        auto& ax   = fig.subplot(1, 1, 1);
        auto  hm   = std::make_unique<CustomSeries>("heatmap");
        auto  blob = heatmap_blob();
        hm->set_data(blob.data(), blob.size() * sizeof(float), blob.size());
        ax.series_mut().push_back(std::move(hm));
        ax.xlim(0.0, static_cast<double>(kCols));
        ax.ylim(0.0, static_cast<double>(kRows));
        fig.save_svg(svg_path.string());

        app.init_runtime();
        auto* ui = app.ui_context();
        ASSERT_NE(ui, nullptr);
        ASSERT_NE(ui->plugin_manager, nullptr);
        ASSERT_TRUE(ui->plugin_manager->load_plugin(SPECTRA_HEATMAP_PLUGIN_PATH));

        app.step();
        app.shutdown_runtime();   // performs the headless SVG export
    }

    std::string svg;
    {
        std::ifstream in(svg_path, std::ios::binary);
        ASSERT_TRUE(in.good()) << svg_path;
        svg.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const size_t g = svg.find("<g class=\"plugin-series\" data-type=\"heatmap\">");
    ASSERT_NE(g, std::string::npos) << svg;
    const std::string group = svg.substr(g, svg.find("</g>", g) - g);
    int               rects = 0;
    for (size_t at = group.find("<rect "); at != std::string::npos;
         at        = group.find("<rect ", at + 1))
        ++rects;
    EXPECT_EQ(rects, static_cast<int>(kCols * kRows));
    EXPECT_EQ(svg.find("not exported"), std::string::npos);

    if (!std::getenv("SPECTRA_KEEP_TEST_OUTPUT") && !::testing::Test::HasFailure())
        fs::remove_all(dir);
#endif
}

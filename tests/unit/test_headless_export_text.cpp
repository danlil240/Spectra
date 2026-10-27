// Headless PNG export must draw plot text (title, tick and axis labels) for every
// figure of a batch, not only the active one, and for scaled (HiDPI) exports.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "stb_image.h"

namespace fs = std::filesystem;

namespace
{

struct Image
{
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> rgba;
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

// Pixels in rows [y0, y1) that differ clearly from the background (top-left pixel).
int ink_in_band(const Image& img, int y0, int y1)
{
    const uint8_t* bg    = img.rgba.data();
    int            count = 0;
    for (int y = std::max(0, y0); y < std::min(img.h, y1); ++y)
    {
        for (int x = 0; x < img.w; ++x)
        {
            const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
            int d = std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]);
            if (d > 60)
                ++count;
        }
    }
    return count;
}

void fill(spectra::Figure& fig, bool with_text)
{
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> x(50), y(50);
    for (int i = 0; i < 50; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = std::sin(i * 0.2f);
    }
    ax.plot(x, y, "b-");
    if (with_text)
    {
        ax.title("Export Title Text");
        ax.xlabel("time");
        ax.ylabel("value");
    }
}

}   // namespace

TEST(HeadlessExportText, BatchAndScaledExportsDrawText)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_export_text";
    fs::create_directories(dir);

    constexpr uint32_t W = 640, H = 400;
    float              title_band_bottom = 0.0f;
    {
        spectra::App app({.headless = true, .socket_path = ""});

        auto& first = app.figure({.width = W, .height = H});
        fill(first, true);
        first.save_png((dir / "first.png").string());

        auto& second = app.figure({.width = W, .height = H});
        fill(second, true);
        second.save_png((dir / "second.png").string());

        auto& scaled = app.figure({.width = W, .height = H});
        fill(scaled, true);
        scaled.save_png((dir / "scaled.png").string(), 2 * W, 2 * H);

        auto& no_text = app.figure({.width = W, .height = H});
        fill(no_text, false);
        no_text.save_png((dir / "no_text.png").string());

        app.run();

        // Title sits between the top of the figure and the top of the plot area.
        second.compute_layout();
        title_band_bottom = second.axes()[0]->viewport().y;
    }
    ASSERT_GT(title_band_bottom, 8.0f);
    const int band = static_cast<int>(title_band_bottom) - 3;

    Image first, second, scaled, no_text;
    ASSERT_TRUE(load(dir / "first.png", first));
    ASSERT_TRUE(load(dir / "second.png", second));
    ASSERT_TRUE(load(dir / "scaled.png", scaled));
    ASSERT_TRUE(load(dir / "no_text.png", no_text));
    ASSERT_EQ(scaled.w, static_cast<int>(2 * W));
    ASSERT_EQ(scaled.h, static_cast<int>(2 * H));

    const int ink_first   = ink_in_band(first, 2, band);
    const int ink_second  = ink_in_band(second, 2, band);
    const int ink_scaled  = ink_in_band(scaled, 4, 2 * band);
    const int ink_no_text = ink_in_band(no_text, 2, band);

    EXPECT_EQ(ink_no_text, 0) << "title band of a figure without a title is not empty";
    EXPECT_GT(ink_first, 40) << "first figure: title missing";
    EXPECT_GT(ink_second, 40) << "second (non-active) figure: title missing";
    EXPECT_NEAR(ink_second, ink_first, ink_first / 5 + 5);
    // 2x export: same layout drawn twice as large, so the title covers ~4x the pixels.
    EXPECT_GT(ink_scaled, ink_first * 5 / 2) << "2x export: title missing or not scaled";
    EXPECT_LT(ink_scaled, ink_first * 6);

    // Tick labels below the plot area must be present too.
    EXPECT_GT(ink_in_band(second, static_cast<int>(H) - 40, static_cast<int>(H)), 40);

    fs::remove_all(dir);
}

namespace
{
// Mean luminance (0..255) of a 4x4 block in the top-left corner (figure background).
float corner_luma(const Image& img)
{
    float sum = 0.0f;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
            sum += 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        }
    return sum / 16.0f;
}

std::string read_text(const fs::path& p)
{
    std::ifstream      in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Luminance (0..255) of the SVG's background rect fill.
float svg_background_luma(const std::string& svg)
{
    const size_t r = svg.find("<rect");
    const size_t f = svg.find("fill=\"#", r);
    if (r == std::string::npos || f == std::string::npos)
        return -1.0f;
    const unsigned v = static_cast<unsigned>(std::stoul(svg.substr(f + 7, 6), nullptr, 16));
    return 0.2126f * ((v >> 16) & 0xFF) + 0.7152f * ((v >> 8) & 0xFF) + 0.0722f * (v & 0xFF);
}
}   // namespace

// ExportOptions::theme draws one export with its own theme, independent of the
// app theme and of the other exports of the batch.
TEST(HeadlessExportText, PerExportThemeIsHonoured)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_export_theme";
    fs::create_directories(dir);
    constexpr uint32_t W = 320, H = 240;
    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        light = app.figure({.width = W, .height = H});
        fill(light, true);
        light.save_png((dir / "light.png").string(), {.theme = "light"});
        light.save_svg((dir / "light.svg").string(), {.theme = "light"});
        auto& dark = app.figure({.width = W, .height = H});
        fill(dark, true);
        dark.save_png((dir / "dark.png").string(), {.theme = "dark"});
        dark.save_svg((dir / "dark.svg").string(), {.theme = "dark"});
        app.run();
    }
    Image light, dark;
    ASSERT_TRUE(load(dir / "light.png", light));
    ASSERT_TRUE(load(dir / "dark.png", dark));
    EXPECT_GT(corner_luma(light), 200.0f);
    EXPECT_LT(corner_luma(dark), 80.0f);

    const float light_svg = svg_background_luma(read_text(dir / "light.svg"));
    const float dark_svg  = svg_background_luma(read_text(dir / "dark.svg"));
    EXPECT_GT(light_svg, 200.0f);
    EXPECT_GE(dark_svg, 0.0f);
    EXPECT_LT(dark_svg, 80.0f);

    fs::remove_all(dir);
}

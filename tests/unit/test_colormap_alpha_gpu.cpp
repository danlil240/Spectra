// Colormap lookup-table alpha on the GPU: a translucent user colormap
// (Colormap::from_colors with alpha) must reach scatter_colormap.frag and
// surface3d.frag.  Renders real headless frames (lavapipe in CI), so this test
// carries the "gpu" label.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/colormap.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "stb_image.h"

namespace fs = std::filesystem;
using namespace spectra;

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

// Strongly red pixels in columns [x0, x1).
size_t red_pixels(const Image& img, int x0, int x1)
{
    size_t n = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.w + x) * 4];
            if (p[0] > 90 && p[1] < 40 && p[2] < 40)
                ++n;
        }
    return n;
}

constexpr Color OPAQUE_RED{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color CLEAR_RED{1.0f, 0.0f, 0.0f, 0.0f};

}   // namespace

TEST(ColormapAlphaGpu, TranslucentLutEntriesAreNotDrawnOpaque)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_colormap_alpha_gpu";
    fs::create_directories(dir);
    const fs::path  scatter_png = dir / "scatter.png";
    const fs::path  surf_clear  = dir / "surface_clear.png";
    const fs::path  surf_opaque = dir / "surface_opaque.png";
    std::error_code ec;
    fs::remove(scatter_png, ec);
    fs::remove(surf_clear, ec);
    fs::remove(surf_opaque, ec);

    {
        App app({.headless = true, .socket_path = ""});

        // Scatter: value 0 -> opaque red (left), value 1 -> fully transparent red (right).
        {
            auto&                    fig     = app.figure({.width = 400, .height = 240});
            auto&                    ax      = fig.subplot(1, 1, 1);
            const std::vector<float> x       = {0.0f, 2.0f};
            const std::vector<float> y       = {0.0f, 0.0f};
            const std::vector<float> c       = {0.0f, 1.0f};
            const Color              stops[] = {OPAQUE_RED, CLEAR_RED};
            ax.scatter(x, y)
                .color_values(c)
                .colormap(Colormap::from_colors(stops))
                .colormap_range(0.0f, 1.0f)
                .size(40.0f)
                .marker_style(MarkerStyle::FilledCircle);
            ax.xlim(-1.0, 3.0);
            ax.ylim(-1.0, 1.0);
            fig.save_png(scatter_png.string());
        }

        // Surfaces: the same red, fully transparent vs opaque.
        auto surface_fig = [&](const Color& red, const fs::path& out)
        {
            auto&              fig = app.figure({.width = 320, .height = 240});
            auto&              ax  = fig.subplot3d(1, 1, 1);
            std::vector<float> g   = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
            // A bowl, so the surface covers a large part of the view.
            std::vector<float> z(g.size() * g.size(), 0.0f);
            for (size_t r = 0; r < g.size(); ++r)
                for (size_t c = 0; c < g.size(); ++c)
                    z[r * g.size() + c] = g[r] * g[r] + g[c] * g[c];
            const Color stops[] = {red, red};
            ax.surface(g, g, z).colormap(Colormap::from_colors(stops));
            fig.save_png(out.string());
        };
        surface_fig(CLEAR_RED, surf_clear);
        surface_fig(OPAQUE_RED, surf_opaque);

        app.run();
    }

    Image scatter;
    if (!load(scatter_png, scatter))
        GTEST_SKIP() << "no frame rendered (no Vulkan device?)";
    const size_t left  = red_pixels(scatter, 0, scatter.w / 2);
    const size_t right = red_pixels(scatter, scatter.w / 2, scatter.w);
    EXPECT_GT(left, 200u) << "opaque LUT entry should draw a red disc";
    EXPECT_EQ(right, 0u) << "alpha-0 LUT entry drew an opaque point";

    Image clear;
    Image opaque;
    ASSERT_TRUE(load(surf_clear, clear));
    ASSERT_TRUE(load(surf_opaque, opaque));
    // The red "X" axis label counts in both frames; only the surface differs.
    const size_t red_opaque = red_pixels(opaque, 0, opaque.w);
    const size_t red_clear  = red_pixels(clear, 0, clear.w);
    EXPECT_GT(red_opaque, red_clear + 500u) << "opaque surface should be red";
    EXPECT_LT(red_clear, 60u) << "alpha-0 colormap drew an opaque surface";

    fs::remove_all(dir, ec);
}

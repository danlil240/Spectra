// test_scatter3d_selection_gpu.cpp — Selection outline of a 3D scatter with
// per-point colours / sizes.
//
// Such a series uploads two vec4 per point (render_upload.cpp); the selection
// highlight must read that layout too, or it outlines bogus positions and
// leaves half the points without an outline.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series3d.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "render/renderer.hpp"
#include "stb_image.h"
#include "ui/overlay/axes3d_pick.hpp"

namespace fs = std::filesystem;
using namespace spectra;

namespace
{

constexpr uint32_t kW = 640;
constexpr uint32_t kH = 480;

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

const std::vector<float> kX{-0.6f, 0.6f, -0.6f, 0.6f, 0.0f, 0.0f};
const std::vector<float> kY{-0.6f, -0.6f, 0.6f, 0.6f, 0.0f, 0.3f};
const std::vector<float> kZ{-0.5f, 0.5f, 0.5f, -0.5f, 0.0f, -0.4f};

// Render the scene (optionally with the scatter selected); return the
// projected screen position of every point.
std::vector<Projected3DPoint> render(const fs::path& png, bool selected, bool& ok)
{
    ok = false;
    std::vector<Projected3DPoint> pts;
    App                           app({.headless = true, .socket_path = ""});
    if (!app.ok() || !app.renderer())
        return pts;

    auto& fig = app.figure({.width = kW, .height = kH});
    auto& ax  = fig.subplot3d(1, 1, 1);
    ax.xlim(-1.0, 1.0);
    ax.ylim(-1.0, 1.0);
    ax.zlim(-1.0, 1.0);
    ax.grid_planes(Axes3D::GridPlane::None);
    ax.show_bounding_box(false);
    std::vector<float> values{0, 1, 2, 3, 4, 5};
    std::vector<float> sizes{10, 12, 14, 10, 12, 14};
    auto&              s = ax.scatter3d(kX, kY, kZ);
    s.color_values(values).colormap(ColormapType::Viridis).sizes(sizes);
    if (selected)
        app.renderer()->set_selected_series({&s});
    fig.save_png(png.string());
    app.run();

    fig.compute_layout();
    for (size_t i = 0; i < kX.size(); ++i)
        pts.push_back(project_axes3d_data_point(ax, {kX[i], kY[i], kZ[i]}));
    ok = true;
    return pts;
}

}   // namespace

TEST(Scatter3DSelectionGpu, OutlinesEveryPointOfPerPointScatter)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_scatter3d_selection_gpu";
    fs::create_directories(dir);
    const fs::path plain_png = dir / "plain.png";
    const fs::path sel_png   = dir / "selected.png";

    bool       ok  = false;
    const auto pts = render(plain_png, false, ok);
    if (!ok)
        GTEST_SKIP() << "No Vulkan device";
    const auto pts_s = render(sel_png, true, ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(pts.size(), pts_s.size());

    Image a, b;
    ASSERT_TRUE(load(plain_png, a)) << plain_png;
    ASSERT_TRUE(load(sel_png, b)) << sel_png;
    ASSERT_EQ(a.w, b.w);
    ASSERT_EQ(a.h, b.h);

    // The outline is the point's own diameter + 3 px: pixels in a ring just
    // outside each point change when the series is selected.
    for (size_t i = 0; i < pts.size(); ++i)
    {
        ASSERT_TRUE(pts[i].visible);
        const int cx      = static_cast<int>(pts[i].screen_x);
        const int cy      = static_cast<int>(pts[i].screen_y);
        int       changed = 0;
        for (int y = cy - 12; y <= cy + 12; ++y)
            for (int x = cx - 12; x <= cx + 12; ++x)
            {
                if (x < 0 || y < 0 || x >= a.w || y >= a.h)
                    continue;
                const uint8_t* p = a.at(x, y);
                const uint8_t* q = b.at(x, y);
                if (std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]) > 60)
                    ++changed;
            }
        EXPECT_GT(changed, 8) << "point " << i << " at (" << cx << ", " << cy
                              << ") has no selection outline";
    }

    // Nothing is outlined away from the points (no bogus positions).
    int stray = 0;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
        {
            const uint8_t* p = a.at(x, y);
            const uint8_t* q = b.at(x, y);
            if (std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]) <= 60)
                continue;
            bool near = false;
            for (const auto& pt : pts)
                near |= std::abs(x - pt.screen_x) <= 14.0f && std::abs(y - pt.screen_y) <= 14.0f;
            if (!near)
                ++stray;
        }
    EXPECT_LT(stray, 10) << stray << " changed pixels away from every point";

    if (!std::getenv("SPECTRA_KEEP_TEST_OUTPUT") && !::testing::Test::HasFailure())
        fs::remove_all(dir);
}

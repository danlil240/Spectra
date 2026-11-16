// Rendered 2D line joins and dash continuity (line.vert / line.frag).
//
// Headless exports (lavapipe in CI) measured on the PNG:
//   * a 3000-point dashed straight line keeps a constant dash period from the
//     first to the last vertex (the dash phase is precomputed per vertex);
//   * a translucent thick polyline is blended exactly once at its vertices
//     (no darker overlap where segment quads meet);
//   * an opaque thick polyline has a round join (no V-notch) at a sharp turn.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "stb_image.h"

namespace fs = std::filesystem;

namespace
{

constexpr uint32_t kW = 800;
constexpr uint32_t kH = 400;

struct Image
{
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> rgba;

    const uint8_t* px(int x, int y) const { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
    // Largest per-channel difference between two pixels.
    int diff(int x0, int y0, int x1, int y1) const
    {
        const uint8_t* a = px(x0, y0);
        const uint8_t* b = px(x1, y1);
        int            d = 0;
        for (int c = 0; c < 3; ++c)
            d = std::max(d, std::abs(int(a[c]) - int(b[c])));
        return d;
    }
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

fs::path out_dir()
{
    const char* env = std::getenv("SPECTRA_LINE_TEST_DIR");
    fs::path    dir = env && *env ? fs::path(env) : fs::temp_directory_path() / "spectra_line_gpu";
    fs::create_directories(dir);
    return dir;
}

// Render one figure filled by `fill` (unit data limits, no grid) and return
// its PNG plus the plot viewport.
template <typename Fill>
bool render(const std::string& name, Fill fill, Image& img, spectra::Rect& vp)
{
    const fs::path path = out_dir() / name;
    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        fig = app.figure({.width = kW, .height = kH});
        auto&        ax  = fig.subplot(1, 1, 1);
        ax.xlim(0.0, 1.0);
        ax.ylim(0.0, 1.0);
        ax.grid(false);
        fill(ax);
        fig.save_png(path.string(), {.width = kW, .height = kH, .theme = "light"});
        app.run();
        fig.compute_layout();
        vp = fig.axes()[0]->viewport();
    }
    return load(path, img);
}

int to_px_x(const spectra::Rect& vp, double x)
{
    return static_cast<int>(std::lround(vp.x + x * vp.w - 0.5));
}
int to_px_y(const spectra::Rect& vp, double y)
{
    return static_cast<int>(std::lround(vp.y + (1.0 - y) * vp.h - 0.5));
}

}   // namespace

// 3000 collinear points, dashed: every dash period along the row through the
// line must equal the pattern length (12 x width), including far beyond the
// first 512 vertices.
TEST(LineRenderingGpu, LongDashedLineKeepsConstantPeriod)
{
    constexpr float  kWidth = 2.0f;
    constexpr size_t kN     = 3000;
    Image            img;
    spectra::Rect    vp{};
    ASSERT_TRUE(render(
        "dash_period.png",
        [&](spectra::Axes& ax)
        {
            std::vector<float> x(kN), y(kN, 0.5f);
            for (size_t i = 0; i < kN; ++i)
                x[i] = 0.02f + 0.96f * static_cast<float>(i) / static_cast<float>(kN - 1);
            ax.plot(x, y, "--").width(kWidth);
        },
        img,
        vp));
    ASSERT_GT(vp.w, 400.0f);

    const int row = to_px_y(vp, 0.5);
    const int bgy = to_px_y(vp, 0.8);
    const int x0  = to_px_x(vp, 0.05);
    const int x1  = to_px_x(vp, 0.95);
    ASSERT_LT(x1, img.w);

    // Ink: clearly different from the canvas just above the line.
    std::vector<bool> ink;
    for (int x = x0; x <= x1; ++x)
        ink.push_back(img.diff(x, row, x, bgy) > 60);

    std::vector<int> rising;
    for (size_t i = 1; i < ink.size(); ++i)
        if (ink[i] && !ink[i - 1])
            rising.push_back(static_cast<int>(i));

    const double period = 12.0 * kWidth;
    const double span   = static_cast<double>(x1 - x0);
    ASSERT_GE(rising.size(), static_cast<size_t>(span / period) - 2);
    for (size_t i = 1; i < rising.size(); ++i)
        EXPECT_NEAR(rising[i] - rising[i - 1], period, 1.5)
            << "dash " << i << " at x=" << rising[i];
}

// A translucent 24 px polyline: the pixels around each interior vertex have
// the same colour as the middle of a segment (blended once, not twice).
TEST(LineRenderingGpu, TranslucentJoinsBlendOnce)
{
    const std::vector<float> x = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
    const std::vector<float> y = {0.25f, 0.75f, 0.3f, 0.75f, 0.25f};
    Image                    img;
    spectra::Rect            vp{};
    ASSERT_TRUE(render(
        "translucent_join.png",
        [&](spectra::Axes& ax)
        { ax.line(x, y).width(24.0f).color(spectra::rgba(0.8f, 0.1f, 0.1f, 0.5f)); },
        img,
        vp));

    // Reference: middle of the first segment.
    const int rx = to_px_x(vp, 0.2);
    const int ry = to_px_y(vp, 0.5);
    // Sanity: the reference pixel is inked.
    ASSERT_GT(img.diff(rx, ry, to_px_x(vp, 0.2), to_px_y(vp, 0.95)), 40);

    for (size_t v = 1; v + 1 < x.size(); ++v)
    {
        const int cx    = to_px_x(vp, x[v]);
        const int cy    = to_px_y(vp, y[v]);
        int       worst = 0;
        for (int dy = -8; dy <= 8; ++dy)
            for (int dx = -8; dx <= 8; ++dx)
                if (dx * dx + dy * dy <= 64)
                    worst = std::max(worst, img.diff(cx + dx, cy + dy, rx, ry));
        EXPECT_LE(worst, 4) << "vertex " << v;
    }
}

// An opaque 24 px polyline with a sharp peak: the outer side of the join is
// filled (round join), where separate segment quads would leave a notch.
TEST(LineRenderingGpu, SharpTurnHasRoundJoin)
{
    const std::vector<float> x = {0.35f, 0.5f, 0.65f};
    const std::vector<float> y = {0.2f, 0.7f, 0.2f};
    Image                    img;
    spectra::Rect            vp{};
    ASSERT_TRUE(render(
        "round_join.png",
        [&](spectra::Axes& ax)
        { ax.line(x, y).width(24.0f).color(spectra::rgb(0.1f, 0.2f, 0.8f)); },
        img,
        vp));

    const int cx = to_px_x(vp, 0.5);
    const int cy = to_px_y(vp, 0.7);
    const int bx = to_px_x(vp, 0.5);
    const int by = to_px_y(vp, 0.95);   // canvas above the peak
    // 8 px straight above the vertex: inside the round join (radius 12).
    EXPECT_GT(img.diff(cx, cy - 8, bx, by), 100);
    // 16 px above: outside the line.
    EXPECT_LT(img.diff(cx, cy - 16, bx, by), 10);
    // Free ends get round caps: 8 px beyond the first point along the
    // segment direction is still inked, 16 px beyond is not.
    const double dxp = (x[1] - x[0]) * vp.w;
    const double dyp = -(y[1] - y[0]) * vp.h;
    const double len = std::hypot(dxp, dyp);
    const int    sx  = to_px_x(vp, x[0]);
    const int    sy  = to_px_y(vp, y[0]);
    auto         at  = [&](double d, int& px, int& py)
    {
        px = static_cast<int>(std::lround(sx - dxp / len * d));
        py = static_cast<int>(std::lround(sy - dyp / len * d));
    };
    int px = 0, py = 0;
    at(8.0, px, py);
    EXPECT_GT(img.diff(px, py, bx, by), 100);
    at(16.0, px, py);
    EXPECT_LT(img.diff(px, py, bx, by), 10);
}

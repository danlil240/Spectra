// GPU pixel tests: a translucent SurfaceSeries / MeshSeries sorts its own
// triangles back to front, so the result does not depend on the order in
// which its triangles were generated relative to the camera.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>
#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#include "render/backend.hpp"

using namespace spectra;

namespace
{

constexpr uint32_t kW = 240;
constexpr uint32_t kH = 240;

Axes3D& setup_axes(App& app, float azimuth, bool lighting)
{
    auto& fig            = app.figure({.width = kW, .height = kH});
    fig.legend().visible = false;
    auto& ax             = fig.subplot3d(1, 1, 1);
    ax.xlim(-1.5, 1.5);
    ax.ylim(-1.5, 1.5);
    ax.zlim(-1.5, 1.5);
    ax.lighting_enabled(lighting);
    ax.camera().set_elevation(15.0f);
    ax.camera().set_azimuth(azimuth);
    return ax;
}

std::vector<uint8_t> read_frame(App& app, const std::string& name)
{
    app.run();
    std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
    auto*                backend = app.backend();
    if (!backend || !backend->readback_framebuffer(px.data(), kW, kH))
    {
        ADD_FAILURE() << "readback failed";
        return {};
    }
    if (const char* dir = std::getenv("SPECTRA_TEST_EVIDENCE_DIR"))
        ImageExporter::write_png(std::string(dir) + "/" + name + ".png", px.data(), kW, kH);
    return px;
}

// Pixels whose RGB differs by more than 8 in any channel.
int count_differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    int n = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
    {
        int d = 0;
        for (int c = 0; c < 3; ++c)
            d = std::max(d, std::abs(int(a[i + c]) - int(b[i + c])));
        if (d > 8)
            ++n;
    }
    return n;
}

// ─── Surface: a steep tent z = 1 - 2|y| with a z colormap ───────────────────
// Each half is planar, so drawn as two separate surfaces neither half can
// overlap itself and the global series sort alone gives the exact reference.
// As one surface its rows are generated in +y order, which is back to front
// from one side and front to back from the other.

std::vector<float> grid(float lo, float hi, int n)
{
    std::vector<float> g(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        g[static_cast<size_t>(i)] = lo + (hi - lo) * static_cast<float>(i) / (n - 1);
    return g;
}

void add_tent_part(Axes3D& ax, float y0, float y1, int ny)
{
    const auto         xs = grid(-1.0f, 1.0f, 9);
    const auto         ys = grid(y0, y1, ny);
    std::vector<float> zs;
    for (float y : ys)
        for (size_t j = 0; j < xs.size(); ++j)
            zs.push_back(1.0f - 2.0f * std::fabs(y));
    auto& s = ax.surface(xs, ys, zs);
    s.color(Color{1.0f, 1.0f, 1.0f, 0.5f});
    s.colormap(ColormapType::Viridis);
}

std::vector<uint8_t> render_tent(bool split, float azimuth, const std::string& name)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto& ax = setup_axes(app, azimuth, false);
    if (split)
    {
        add_tent_part(ax, -1.0f, 0.0f, 9);
        add_tent_part(ax, 0.0f, 1.0f, 9);
    }
    else
    {
        add_tent_part(ax, -1.0f, 1.0f, 17);
    }
    return read_frame(app, name);
}

// ─── Mesh: two parallel lit quads with different normals in one MeshSeries ──
// One colour blends commutatively, so the layers differ through their normals
// (and so their lit shade); the split reference draws each quad as a series.

std::vector<float> quad(float x, vec3 n)
{
    std::vector<float> v;
    for (auto [y, z] : {std::pair{-1.0f, -1.0f},
                        std::pair{1.0f, -1.0f},
                        std::pair{1.0f, 1.0f},
                        std::pair{-1.0f, 1.0f}})
        v.insert(
            v.end(),
            {x, y, z, static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
    return v;
}

std::vector<uint8_t> render_quads(bool split, float azimuth, const std::string& name)
{
    AppConfig config;
    config.headless = true;
    App   app(config);
    auto& ax = setup_axes(app, azimuth, true);

    const Color                 col{0.9f, 0.5f, 0.2f, 0.5f};
    const auto                  a = quad(-0.6f, {1.0f, 0.0f, 0.0f});
    const auto                  b = quad(0.6f, {0.0f, 0.0f, 1.0f});
    const std::vector<uint32_t> one{0, 1, 2, 0, 2, 3};
    if (split)
    {
        ax.mesh(a, one).color(col);
        ax.mesh(b, one).color(col);
    }
    else
    {
        auto v = a;
        v.insert(v.end(), b.begin(), b.end());
        const std::vector<uint32_t> both{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
        ax.mesh(v, both).color(col);
    }
    return read_frame(app, name);
}

}   // namespace

TEST(TranslucentMeshSort, FoldedSurfaceMatchesSplitReferenceFromBothSides)
{
    for (float az : {90.0f, 270.0f})
    {
        const std::string tag      = std::to_string(static_cast<int>(az));
        const auto        joined   = render_tent(false, az, "tent_joined_" + tag);
        const auto        separate = render_tent(true, az, "tent_split_" + tag);
        ASSERT_FALSE(joined.empty());
        ASSERT_EQ(joined.size(), separate.size());
        const int diff = count_differing(joined, separate);
        EXPECT_LT(diff, 40) << diff << " pixels differ at azimuth " << az;
    }
}

TEST(TranslucentMeshSort, OverlappingMeshLayersMatchSeparateSeriesFromBothSides)
{
    for (float az : {0.0f, 180.0f})
    {
        const std::string tag      = std::to_string(static_cast<int>(az));
        const auto        joined   = render_quads(false, az, "quads_joined_" + tag);
        const auto        separate = render_quads(true, az, "quads_split_" + tag);
        ASSERT_FALSE(joined.empty());
        ASSERT_EQ(joined.size(), separate.size());
        const int diff = count_differing(joined, separate);
        EXPECT_LT(diff, 40) << diff << " pixels differ at azimuth " << az;
    }
}

// GPU pixel tests for 3D surface / mesh shading (issue #105):
//   - a mesh supplied without normals is lit from its face normals, not black;
//   - Axes3D::lighting_enabled(false) draws surfaces and meshes in their flat
//     base colour.

#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <spectra/app.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "render/backend.hpp"

using namespace spectra;

namespace
{

constexpr uint32_t kW = 320;
constexpr uint32_t kH = 320;

struct RedStats
{
    int           pixels = 0;   // red-dominant pixels
    int           full   = 0;   // ... at (almost) full red
    std::set<int> levels;       // distinct red levels
};

RedStats red_stats(const std::vector<uint8_t>& px)
{
    RedStats s;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
    {
        const int r = px[i + 0];
        const int g = px[i + 1];
        const int b = px[i + 2];
        if (r > 40 && r > 3 * g && r > 3 * b)
        {
            ++s.pixels;
            s.levels.insert(r);
            if (r >= 250)
                ++s.full;
        }
    }
    return s;
}

// Octahedron, all normals zero (as a user passing positions only would).
void add_octahedron(Axes3D& ax)
{
    const std::vector<float>    v   = {1, 0,  0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1, 0,  0, 0, 0,
                                       0, -1, 0, 0, 0, 0, 0,  0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0};
    const std::vector<uint32_t> idx = {0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4,
                                       2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5};
    ax.mesh(v, idx).color(Color{1.0f, 0.0f, 0.0f, 1.0f});
}

void add_wave_surface(Axes3D& ax)
{
    constexpr int      n = 24;
    std::vector<float> x(n), y(n), z(n * n);
    for (int i = 0; i < n; ++i)
        x[i] = y[i] = -3.0f + 6.0f * static_cast<float>(i) / (n - 1);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            z[j * n + i] = std::sin(x[i]) * std::cos(y[j]);
    ax.surface(x, y, z).color(Color{1.0f, 0.0f, 0.0f, 1.0f});
}

class Lighting3DTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
    }
    void TearDown() override { app_.reset(); }

    Axes3D& new_axes()
    {
        auto& fig = app_->figure({.width = kW, .height = kH});
        auto& ax  = fig.subplot3d(1, 1, 1);
        ax.show_bounding_box(false);
        ax.grid_planes(Axes3D::GridPlane::None);
        return ax;
    }

    RedStats render()
    {
        app_->run();
        std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
        auto*                backend = app_->backend();
        if (!backend || !backend->readback_framebuffer(px.data(), kW, kH))
        {
            ADD_FAILURE() << "readback failed";
            return {};
        }
        return red_stats(px);
    }

    std::unique_ptr<App> app_;
};

}   // namespace

TEST_F(Lighting3DTest, MeshWithoutNormalsIsShadedNotBlack)
{
    add_octahedron(new_axes());
    const RedStats s = render();
    ASSERT_GT(s.pixels, 2000) << "mesh not visible (drawn black?)";
    // Faces facing different ways get different shades from the face normals.
    EXPECT_GE(s.levels.size(), 3u);
}

TEST_F(Lighting3DTest, LightingDisabledDrawsMeshFlat)
{
    auto& ax = new_axes();
    add_octahedron(ax);
    ax.lighting_enabled(false);
    const RedStats s = render();
    ASSERT_GT(s.pixels, 2000);
    // Every interior pixel is the pure base colour; only anti-aliased edge
    // pixels are darker.
    EXPECT_GT(s.full, s.pixels * 9 / 10) << s.full << " of " << s.pixels;
}

TEST_F(Lighting3DTest, LightingDisabledDrawsSurfaceFlat)
{
    auto& ax = new_axes();
    add_wave_surface(ax);
    ax.lighting_enabled(false);
    const RedStats s = render();
    ASSERT_GT(s.pixels, 2000);
    EXPECT_GT(s.full, s.pixels * 9 / 10) << s.full << " of " << s.pixels;
}

TEST_F(Lighting3DTest, LightingEnabledShadesSurface)
{
    add_wave_surface(new_axes());
    const RedStats s = render();
    ASSERT_GT(s.pixels, 2000);
    EXPECT_LT(s.full, s.pixels / 2) << s.full << " of " << s.pixels;
    EXPECT_GE(s.levels.size(), 20u);
}

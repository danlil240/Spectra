// R00: the first headless export of a fresh 2D shape scene must contain every
// primitive with correct limits, exactly like a scene whose geometry was built
// up front with rebuild_geometry().  Before the fix ShapeSeries only generated
// its outline at GPU-upload time, so the renderer autoscaled from empty
// geometry (default [-0.05, 1.05] limits) and every primitive fell outside.
// Shapes added after a frame has already been rendered must likewise be
// included in the next frame's limits.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_shapes.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "stb_image.h"

namespace fs = std::filesystem;

namespace
{

constexpr uint32_t W = 800, H = 400;

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

// Fraction of pixels whose RGB differs by more than a small tolerance.
double diff_fraction(const Image& a, const Image& b)
{
    size_t differing = 0;
    size_t n         = static_cast<size_t>(a.w) * a.h;
    for (size_t i = 0; i < n; ++i)
    {
        int d = std::abs(a.rgba[i * 4 + 0] - b.rgba[i * 4 + 0])
                + std::abs(a.rgba[i * 4 + 1] - b.rgba[i * 4 + 1])
                + std::abs(a.rgba[i * 4 + 2] - b.rgba[i * 4 + 2]);
        if (d > 24)
            ++differing;
    }
    return n ? static_cast<double>(differing) / static_cast<double>(n) : 0.0;
}

struct Scene
{
    spectra::ShapeSeries* closed = nullptr;
    spectra::ShapeSeries* open   = nullptr;
};

Scene make_axes(spectra::Figure& fig)
{
    Scene s;
    s.closed = &fig.subplot(1, 2, 1).shapes();
    s.open   = &fig.subplot(1, 2, 2).shapes();
    return s;
}

// First half of the primitives (all well away from the default [0, 1] limits).
void add_first(const Scene& s)
{
    s.closed->rect(2.0f, 2.0f, 3.0f, 2.0f);
    s.closed->circle(8.0f, 3.0f, 1.5f);
    s.open->line(2.0f, 2.0f, 8.0f, 6.0f);
}

// Second half; extends both subplots' data extents beyond the first half.
void add_second(const Scene& s)
{
    s.closed->ellipse(4.0f, 8.0f, 2.0f, 1.0f);
    s.closed->ring(10.0f, 9.0f, 1.5f, 0.8f);
    s.open->arrow(2.0f, 8.0f, 9.0f, 4.0f);
    const std::vector<float> px = {10.0f, 13.0f, 12.0f, 10.5f};
    const std::vector<float> py = {2.0f, 3.0f, 7.0f, 6.0f};
    s.open->polygon(px, py);
}

void rebuild(const Scene& s)
{
    s.closed->rebuild_geometry();
    s.open->rebuild_geometry();
}

// Reference: every shape added, geometry built up front (the pre-fix workaround).
bool render_prebuilt(const fs::path& png)
{
    spectra::App app({.headless = true, .socket_path = ""});
    if (!app.ok())
        return false;
    auto& fig = app.figure({.width = W, .height = H});
    Scene s   = make_axes(fig);
    add_first(s);
    add_second(s);
    rebuild(s);
    fig.save_png(png.string());
    app.run();
    return true;
}

void expect_matches_prebuilt(const fs::path& dir, const char* name)
{
    Image img, ref;
    ASSERT_TRUE(load(dir / name, img));
    ASSERT_TRUE(load(dir / "prebuilt.png", ref));
    ASSERT_EQ(img.w, ref.w);
    ASSERT_EQ(img.h, ref.h);
    // Before the fix the primitives were missing or misplaced (several percent
    // of the image).  Allow only sub-percent antialiasing noise.
    EXPECT_LT(diff_fraction(img, ref), 0.005) << name;
}

}   // namespace

TEST(Shape2DFirstFrame, FreshExportMatchesPrebuiltExport)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_shape2d_first_frame";
    fs::create_directories(dir);

    if (!render_prebuilt(dir / "prebuilt.png"))
        GTEST_SKIP() << "No Vulkan device";

    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        fig = app.figure({.width = W, .height = H});
        Scene        s   = make_axes(fig);
        add_first(s);
        add_second(s);
        fig.save_png((dir / "fresh.png").string());
        app.run();
    }

    expect_matches_prebuilt(dir, "fresh.png");
}

TEST(Shape2DFirstFrame, ShapesAddedAfterFirstFrameAreIncluded)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_shape2d_first_frame";
    fs::create_directories(dir);

    if (!render_prebuilt(dir / "prebuilt.png"))
        GTEST_SKIP() << "No Vulkan device";

    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        fig = app.figure({.width = W, .height = H});
        Scene        s   = make_axes(fig);
        add_first(s);

        app.init_runtime();
        app.step();   // first frame uploads (and caches) the first half's geometry

        add_second(s);
        fig.save_png((dir / "mutated.png").string());
        app.step();
        app.shutdown_runtime();
    }

    expect_matches_prebuilt(dir, "mutated.png");
}

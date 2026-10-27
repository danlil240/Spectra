// S20: the first headless export of a fresh 3D shape scene must place the
// shapes exactly like a scene whose geometry/limits were made current up front
// with auto_fit().  Shapes generate their mesh lazily; before the fix the
// renderer computed the 3D model from empty-geometry fallback limits.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_shapes3d.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

void fill(spectra::Figure& fig, bool fit_first)
{
    auto& solid = fig.subplot3d(1, 2, 1);
    solid.shapes3d().box(-1.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f);
    solid.shapes3d().sphere(0.0f, 0.0f, 0.0f, 0.65f);
    solid.shapes3d().cylinder(1.5f, -0.6f, -0.7f, 1.5f, -0.6f, 0.7f, 0.35f);
    solid.shapes3d().cone(0.0f, 1.5f, -0.7f, 0.0f, 1.5f, 0.9f, 0.55f);

    auto& arrows = fig.subplot3d(1, 2, 2);
    arrows.shapes3d().arrow3d(0, 0, 0, 1.8f, 0, 0, 0.05f);
    arrows.shapes3d().arrow3d(0, 0, 0, 0, 1.8f, 0, 0.05f);
    arrows.shapes3d().arrow3d(0, 0, 0, 0, 0, 1.8f, 0.05f);
    arrows.shapes3d().plane(0.6f, 0.6f, -0.1f, 0, 0, 1, 1.0f).opacity(0.35f);

    if (fit_first)
    {
        solid.auto_fit();
        arrows.auto_fit();
    }
}

}   // namespace

TEST(Shape3DFirstFrame, FreshExportMatchesAutoFitExport)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_shape3d_first_frame";
    fs::create_directories(dir);

    constexpr uint32_t W = 800, H = 400;
    {
        spectra::App app({.headless = true, .socket_path = ""});

        auto& fresh = app.figure({.width = W, .height = H});
        fill(fresh, false);
        fresh.save_png((dir / "fresh.png").string());

        auto& fitted = app.figure({.width = W, .height = H});
        fill(fitted, true);
        fitted.save_png((dir / "fitted.png").string());

        app.run();
    }

    Image fresh, fitted;
    ASSERT_TRUE(load(dir / "fresh.png", fresh));
    ASSERT_TRUE(load(dir / "fitted.png", fitted));
    ASSERT_EQ(fresh.w, fitted.w);
    ASSERT_EQ(fresh.h, fitted.h);

    // Before the fix roughly a fifth of the image differed (solids missing or
    // misplaced, plane absent).  Allow only sub-percent antialiasing noise.
    EXPECT_LT(diff_fraction(fresh, fitted), 0.005);
}

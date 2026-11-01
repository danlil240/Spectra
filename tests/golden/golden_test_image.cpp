// Golden image tests for grid-valued 2D series: imshow (nearest, bilinear),
// heatmap and contour / contourf, with their colorbars.
//
// Built twice (see tests/CMakeLists.txt): golden_image_tests_image_light and
// golden_image_tests_image_dark, with SPECTRA_GOLDEN_THEME set to the theme
// name.  Baselines are named img_<theme>_<scene>.raw.

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <spectra/series_image.hpp>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#include "image_diff.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"

#ifndef SPECTRA_GOLDEN_THEME
    #define SPECTRA_GOLDEN_THEME "dark"
#endif

namespace fs = std::filesystem;

// Single shared App — one VkInstance for the entire test binary (see
// golden_test_3d.cpp for the rationale).
static spectra::App* g_app = nullptr;

namespace spectra::test
{

static fs::path baseline_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_BASELINE_DIR"))
        return fs::path(env);
    return fs::path(__FILE__).parent_path() / "baseline";
}

static fs::path output_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_OUTPUT_DIR"))
        return fs::path(env);
    return fs::path(__FILE__).parent_path() / "output";
}

static bool update_baselines()
{
    const char* env = std::getenv("SPECTRA_UPDATE_BASELINES");
    return env && std::string(env) == "1";
}

static void run_golden_test(const std::string&                 scene,
                            std::function<void(App&, Figure&)> setup_scene,
                            uint32_t                           width  = 480,
                            uint32_t                           height = 360)
{
    const std::string name          = std::string("img_") + SPECTRA_GOLDEN_THEME + "_" + scene;
    const fs::path    baseline_path = baseline_dir() / (name + ".raw");
    const fs::path    actual_path   = output_dir() / (name + "_actual.raw");
    const fs::path    diff_path     = output_dir() / (name + "_diff.raw");
    fs::create_directories(output_dir());

    ASSERT_NE(g_app, nullptr) << "Shared App not initialized";
    App&     app    = *g_app;
    auto&    fig    = app.figure({.width = width, .height = height});
    FigureId fig_id = app.figure_registry().find_id(&fig);

    setup_scene(app, fig);

    app.run();
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    Backend*             backend = app.backend();
    const bool render_ok = backend && backend->readback_framebuffer(pixels.data(), width, height);
    if (fig_id != INVALID_FIGURE_ID)
    {
        if (auto* r = app.renderer())
        {
            for (auto& axes_ptr : fig.all_axes())
            {
                for (auto& ser_ptr : axes_ptr->series())
                    r->notify_series_removed(ser_ptr.get());
                r->notify_axes_removed(axes_ptr.get());
            }
            r->notify_figure_removed(&fig);
        }
        app.figure_registry().unregister_figure(fig_id);
    }
    ASSERT_TRUE(render_ok) << "Failed to render scene: " << name;
    ASSERT_TRUE(save_raw_rgba(actual_path.string(), pixels.data(), width, height));

    if (update_baselines())
    {
        fs::create_directories(baseline_dir());
        ASSERT_TRUE(save_raw_rgba(baseline_path.string(), pixels.data(), width, height));
        std::cout << "[GOLDEN] Updated baseline: " << baseline_path << "\n";
        return;
    }
    if (!fs::exists(baseline_path))
    {
        if (golden_strict())
            FAIL() << "Baseline not found: " << baseline_path;
        GTEST_SKIP() << "Baseline not found: " << baseline_path
                     << " (run with SPECTRA_UPDATE_BASELINES=1 to generate)";
    }

    std::vector<uint8_t> baseline;
    uint32_t             bw = 0, bh = 0;
    ASSERT_TRUE(load_raw_rgba(baseline_path.string(), baseline, bw, bh));
    ASSERT_EQ(bw, width);
    ASSERT_EQ(bh, height);

    GoldenTolerance tol;
    DiffResult      diff =
        compare_images(pixels.data(), baseline.data(), width, height, tol.pixel_threshold);
    auto diff_img =
        generate_diff_image(pixels.data(), baseline.data(), width, height, tol.pixel_threshold);
    save_raw_rgba(diff_path.string(), diff_img.data(), width, height);
    EXPECT_TRUE(diff.passed(tol)) << "Golden image test FAILED for: " << name << "\n"
                                  << diff.describe(tol) << "  Diff image saved to: " << diff_path;
}

// z = sin(x) * cos(y) sampled on a cols x rows grid over [0, 2pi] x [0, pi].
static std::vector<float> wave_grid(size_t cols, size_t rows)
{
    std::vector<float> z(cols * rows);
    for (size_t r = 0; r < rows; ++r)
    {
        for (size_t c = 0; c < cols; ++c)
        {
            const float x   = 6.2831853f * static_cast<float>(c) / static_cast<float>(cols - 1);
            const float y   = 3.1415927f * static_cast<float>(r) / static_cast<float>(rows - 1);
            z[r * cols + c] = std::sin(x) * std::cos(y);
        }
    }
    return z;
}

// Two Gaussian bumps, one positive and one negative, on [-3, 3]^2.
static std::vector<float> bumps_grid(size_t n)
{
    std::vector<float> z(n * n);
    for (size_t r = 0; r < n; ++r)
    {
        for (size_t c = 0; c < n; ++c)
        {
            const float x = -3.0f + 6.0f * static_cast<float>(c) / static_cast<float>(n - 1);
            const float y = -3.0f + 6.0f * static_cast<float>(r) / static_cast<float>(n - 1);
            const float a = std::exp(-((x - 1.0f) * (x - 1.0f) + (y - 0.5f) * (y - 0.5f)));
            const float b = std::exp(-((x + 1.2f) * (x + 1.2f) + (y + 0.8f) * (y + 0.8f)) / 1.5f);
            z[r * n + c]  = a - 0.8f * b;
        }
    }
    return z;
}

static void scene_imshow_nearest(App& /*app*/, Figure& fig)
{
    auto&      ax = fig.subplot(1, 1, 1);
    const auto z  = wave_grid(16, 10);
    ax.imshow(z, 16, 10);
    ax.show_colorbar(true);
    ax.title("imshow (nearest)");
}

static void scene_imshow_bilinear(App& /*app*/, Figure& fig)
{
    auto&      ax = fig.subplot(1, 1, 1);
    const auto z  = wave_grid(7, 5);
    ax.imshow(z, 7, 5)
        .interpolation(ImageInterpolation::Bilinear)
        .colormap(ColormapType::Magma)
        .extent(0.0, 6.2832, 0.0, 3.1416);
    ax.show_colorbar(true);
    ax.title("imshow (bilinear, magma)");
}

static void scene_heatmap(App& /*app*/, Figure& fig)
{
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> v  = {0.9f, 0.2f,  0.4f,  0.7f,  0.1f,  0.5f,  0.8f,  0.3f,  0.6f,  0.0f,
                             1.0f, 0.45f, 0.25f, 0.65f, 0.85f, 0.35f, 0.15f, 0.55f, 0.95f, 0.75f};
    std::vector<std::string> cols{"Mon", "Tue", "Wed", "Thu", "Fri"};
    std::vector<std::string> rows{"A", "B", "C", "D"};
    ax.heatmap(v, 5, 4, cols, rows).colormap(ColormapType::Coolwarm);
    ax.show_colorbar(true);
    ax.title("heatmap");
}

static void scene_contour(App& /*app*/, Figure& fig)
{
    const size_t n = 61;
    const auto   z = bumps_grid(n);

    auto& left = fig.subplot(1, 2, 1);
    left.contourf(z, n, n, 8);
    left.contour(z, n, n, 8).color(rgb(0.08f, 0.08f, 0.08f)).width(1.0f);
    left.show_colorbar(true);
    left.title("contourf + contour");

    auto& right = fig.subplot(1, 2, 2);
    right.contour(z, n, n, 10).colormap(ColormapType::Plasma).width(2.0f);
    right.show_colorbar(true);
    right.title("contour lines");
}

TEST(GoldenImageGrid, ImshowNearest)
{
    run_golden_test("imshow_nearest", scene_imshow_nearest);
}

TEST(GoldenImageGrid, ImshowBilinear)
{
    run_golden_test("imshow_bilinear", scene_imshow_bilinear);
}

TEST(GoldenImageGrid, Heatmap)
{
    run_golden_test("heatmap", scene_heatmap);
}

TEST(GoldenImageGrid, Contour)
{
    run_golden_test("contour", scene_contour, 720, 360);
}

}   // namespace spectra::test

// Custom main creates a single shared App (one VkInstance) and calls ::_exit()
// after all tests complete, bypassing C++ destructors.  See golden_test_3d.cpp.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    g_app = new spectra::App({.headless = true, .socket_path = "", .theme = SPECTRA_GOLDEN_THEME});
    int result = RUN_ALL_TESTS();
    ::_exit(result);
}

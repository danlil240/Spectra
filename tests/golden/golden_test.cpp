#include <gtest/gtest.h>

#include <plotix/plotix.hpp>

#include "image_diff.hpp"
#include "render/backend.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace plotix::test {

// Directory containing baseline .raw files
static fs::path baseline_dir() {
    // Check environment variable first, then fall back to source-relative path
    if (const char* env = std::getenv("PLOTIX_GOLDEN_BASELINE_DIR")) {
        return fs::path(env);
    }
    // Relative to this source file's location
    return fs::path(__FILE__).parent_path() / "baseline";
}

// Directory for test output artifacts (actual renders, diff images)
static fs::path output_dir() {
    if (const char* env = std::getenv("PLOTIX_GOLDEN_OUTPUT_DIR")) {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "output";
}

// If PLOTIX_UPDATE_BASELINES=1, overwrite baselines instead of comparing
static bool update_baselines() {
    const char* env = std::getenv("PLOTIX_UPDATE_BASELINES");
    return env && std::string(env) == "1";
}

// Render a figure headless and return pixel data
static bool render_headless(Figure& fig, App& app,
                             std::vector<uint8_t>& pixels) {
    uint32_t w = fig.width();
    uint32_t h = fig.height();

    app.run();

    pixels.resize(static_cast<size_t>(w) * h * 4);
    Backend* backend = app.backend();
    if (!backend) return false;

    return backend->readback_framebuffer(pixels.data(), w, h);
}

// Core golden test: render scene, compare against baseline
static void run_golden_test(const std::string& scene_name,
                             std::function<void(App&, Figure&)> setup_scene,
                             uint32_t width = 640, uint32_t height = 480,
                             double tolerance_percent = 1.0,
                             double max_mae = 2.0) {
    fs::path baseline_path = baseline_dir() / (scene_name + ".raw");
    fs::path actual_path   = output_dir() / (scene_name + "_actual.raw");
    fs::path diff_path     = output_dir() / (scene_name + "_diff.raw");

    // Ensure output directory exists
    fs::create_directories(output_dir());

    // Set up and render
    App app({.headless = true});
    auto& fig = app.figure({.width = width, .height = height});

    setup_scene(app, fig);

    std::vector<uint8_t> actual_pixels;
    ASSERT_TRUE(render_headless(fig, app, actual_pixels))
        << "Failed to render scene: " << scene_name;

    // Save actual render
    ASSERT_TRUE(save_raw_rgba(actual_path.string(), actual_pixels.data(), width, height))
        << "Failed to save actual render for: " << scene_name;

    if (update_baselines()) {
        // Update mode: save as new baseline
        fs::create_directories(baseline_dir());
        ASSERT_TRUE(save_raw_rgba(baseline_path.string(), actual_pixels.data(), width, height))
            << "Failed to save baseline for: " << scene_name;
        std::cout << "[GOLDEN] Updated baseline: " << baseline_path << "\n";
        return;
    }

    // Compare mode: load baseline and diff
    if (!fs::exists(baseline_path)) {
        GTEST_SKIP() << "Baseline not found: " << baseline_path
                     << " (run with PLOTIX_UPDATE_BASELINES=1 to generate)";
        return;
    }

    std::vector<uint8_t> baseline_pixels;
    uint32_t bw = 0, bh = 0;
    ASSERT_TRUE(load_raw_rgba(baseline_path.string(), baseline_pixels, bw, bh))
        << "Failed to load baseline: " << baseline_path;

    ASSERT_EQ(bw, width)  << "Baseline width mismatch for: " << scene_name;
    ASSERT_EQ(bh, height) << "Baseline height mismatch for: " << scene_name;

    DiffResult diff = compare_images(actual_pixels.data(), baseline_pixels.data(),
                                      width, height);

    // Save diff visualization
    auto diff_img = generate_diff_image(actual_pixels.data(), baseline_pixels.data(),
                                         width, height);
    save_raw_rgba(diff_path.string(), diff_img.data(), width, height);

    EXPECT_TRUE(diff.passed(tolerance_percent, max_mae))
        << "Golden image test FAILED for: " << scene_name << "\n"
        << "  Mean absolute error: " << diff.mean_absolute_error << " (max allowed: " << max_mae << ")\n"
        << "  Differing pixels:    " << diff.differing_pixels << " / " << diff.total_pixels
        << " (" << diff.percent_different << "%, max allowed: " << tolerance_percent << "%)\n"
        << "  Max channel diff:    " << diff.max_absolute_error << "\n"
        << "  Diff image saved to: " << diff_path;
}

// ─── Scene Definitions ──────────────────────────────────────────────────────

static void scene_basic_line(App& /*app*/, Figure& fig) {
    auto& ax = fig.subplot(1, 1, 1);

    std::vector<float> x(200);
    std::vector<float> y(200);
    for (size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<float>(i) * 0.05f;
        y[i] = std::sin(x[i]);
    }

    ax.line(x, y).label("sin(x)").color(rgb(0.2f, 0.8f, 1.0f));
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Basic Line Plot");
    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");
}

static void scene_scatter(App& /*app*/, Figure& fig) {
    auto& ax = fig.subplot(1, 1, 1);

    std::vector<float> x(50);
    std::vector<float> y(50);
    for (size_t i = 0; i < x.size(); ++i) {
        float t = static_cast<float>(i) * 0.1f;
        x[i] = t;
        y[i] = std::sin(t) * 0.8f + 0.1f * static_cast<float>(i % 5);
    }

    ax.scatter(x, y).label("data").color(rgb(1.0f, 0.4f, 0.0f)).size(6.0f);
    ax.xlim(0.0f, 5.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Scatter Plot");
    ax.xlabel("Time");
    ax.ylabel("Value");
}

static void scene_multi_subplot(App& /*app*/, Figure& fig) {
    auto& ax1 = fig.subplot(2, 1, 1);
    auto& ax2 = fig.subplot(2, 1, 2);

    constexpr size_t N = 150;
    std::vector<float> x(N);
    std::vector<float> y1(N);
    std::vector<float> y2(N);

    for (size_t i = 0; i < N; ++i) {
        x[i]  = static_cast<float>(i) * 0.04f;
        y1[i] = std::sin(x[i] * 3.0f) * std::exp(-x[i] * 0.3f);
        y2[i] = std::cos(x[i] * 2.0f);
    }

    ax1.line(x, y1).label("signal A").color(colors::red);
    ax1.title("Signal A");
    ax1.xlabel("Time");
    ax1.ylabel("Amplitude");
    ax1.xlim(0.0f, 6.0f);
    ax1.ylim(-1.5f, 1.5f);

    ax2.line(x, y2).label("signal B").color(rgb(0.2f, 0.6f, 1.0f));
    ax2.title("Signal B");
    ax2.xlabel("Time");
    ax2.ylabel("Amplitude");
    ax2.xlim(0.0f, 6.0f);
    ax2.ylim(-1.5f, 1.5f);
}

static void scene_empty_axes(App& /*app*/, Figure& fig) {
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);
    ax.title("Empty Axes");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

static void scene_grid_only(App& /*app*/, Figure& fig) {
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(-5.0f, 5.0f);
    ax.ylim(-5.0f, 5.0f);
    ax.grid(true);
    ax.title("Grid Only");
}

// ─── Test Cases ─────────────────────────────────────────────────────────────

TEST(GoldenImage, BasicLine) {
    run_golden_test("basic_line", scene_basic_line);
}

TEST(GoldenImage, Scatter) {
    run_golden_test("scatter", scene_scatter);
}

TEST(GoldenImage, MultiSubplot) {
    run_golden_test("multi_subplot", scene_multi_subplot, 800, 600);
}

TEST(GoldenImage, EmptyAxes) {
    run_golden_test("empty_axes", scene_empty_axes);
}

TEST(GoldenImage, GridOnly) {
    run_golden_test("grid_only", scene_grid_only);
}

// ─── Meta-test: verify the diff framework catches intentional differences ───

TEST(GoldenImageFramework, DetectsDifference) {
    constexpr uint32_t W = 4, H = 4;
    std::vector<uint8_t> a(W * H * 4, 128);
    std::vector<uint8_t> b(W * H * 4, 128);

    // Make one pixel completely different
    b[0] = 0; b[1] = 0; b[2] = 0; b[3] = 255;

    auto result = compare_images(a.data(), b.data(), W, H, 2);
    EXPECT_GT(result.differing_pixels, 0u);
    EXPECT_GT(result.percent_different, 0.0);
}

TEST(GoldenImageFramework, IdenticalImagesPass) {
    constexpr uint32_t W = 4, H = 4;
    std::vector<uint8_t> img(W * H * 4, 200);

    auto result = compare_images(img.data(), img.data(), W, H, 2);
    EXPECT_EQ(result.differing_pixels, 0u);
    EXPECT_DOUBLE_EQ(result.mean_absolute_error, 0.0);
    EXPECT_TRUE(result.passed());
}

TEST(GoldenImageFramework, SmallDiffWithinTolerance) {
    constexpr uint32_t W = 10, H = 10;
    std::vector<uint8_t> a(W * H * 4, 100);
    std::vector<uint8_t> b(W * H * 4, 101); // Off by 1 everywhere

    auto result = compare_images(a.data(), b.data(), W, H, 2);
    EXPECT_EQ(result.differing_pixels, 0u); // Within threshold of 2
    EXPECT_TRUE(result.passed());
}

} // namespace plotix::test

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#include "image_diff.hpp"
#include "render/backend.hpp"

namespace fs = std::filesystem;

namespace spectra::test
{

// ─── Shared infrastructure (mirrors golden_test.cpp) ─────────────────────────

static fs::path baseline_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_BASELINE_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "baseline";
}

static fs::path output_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_OUTPUT_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "output";
}

static bool update_baselines()
{
    const char* env = std::getenv("SPECTRA_UPDATE_BASELINES");
    return env && std::string(env) == "1";
}

static bool render_headless(Figure& fig, App& app, std::vector<uint8_t>& pixels)
{
    uint32_t w = fig.width();
    uint32_t h = fig.height();

    app.run();

    pixels.resize(static_cast<size_t>(w) * h * 4);
    Backend* backend = app.backend();
    if (!backend)
        return false;

    return backend->readback_framebuffer(pixels.data(), w, h);
}

static void run_golden_test(const std::string& scene_name,
                            std::function<void(App&, Figure&)> setup_scene,
                            uint32_t width = 640,
                            uint32_t height = 480,
                            double tolerance_percent = 1.0,
                            double max_mae = 2.0)
{
    fs::path baseline_path = baseline_dir() / (scene_name + ".raw");
    fs::path actual_path = output_dir() / (scene_name + "_actual.raw");
    fs::path diff_path = output_dir() / (scene_name + "_diff.raw");

    fs::create_directories(output_dir());

    App app({.headless = true});
    auto& fig = app.figure({.width = width, .height = height});

    setup_scene(app, fig);

    std::vector<uint8_t> actual_pixels;
    ASSERT_TRUE(render_headless(fig, app, actual_pixels))
        << "Failed to render scene: " << scene_name;

    ASSERT_TRUE(save_raw_rgba(actual_path.string(), actual_pixels.data(), width, height))
        << "Failed to save actual render for: " << scene_name;

    if (update_baselines())
    {
        fs::create_directories(baseline_dir());
        ASSERT_TRUE(save_raw_rgba(baseline_path.string(), actual_pixels.data(), width, height))
            << "Failed to save baseline for: " << scene_name;
        std::cout << "[GOLDEN] Updated baseline: " << baseline_path << "\n";
        return;
    }

    if (!fs::exists(baseline_path))
    {
        GTEST_SKIP() << "Baseline not found: " << baseline_path
                     << " (run with SPECTRA_UPDATE_BASELINES=1 to generate)";
        return;
    }

    std::vector<uint8_t> baseline_pixels;
    uint32_t bw = 0, bh = 0;
    ASSERT_TRUE(load_raw_rgba(baseline_path.string(), baseline_pixels, bw, bh))
        << "Failed to load baseline: " << baseline_path;

    ASSERT_EQ(bw, width) << "Baseline width mismatch for: " << scene_name;
    ASSERT_EQ(bh, height) << "Baseline height mismatch for: " << scene_name;

    DiffResult diff = compare_images(actual_pixels.data(), baseline_pixels.data(), width, height);

    auto diff_img =
        generate_diff_image(actual_pixels.data(), baseline_pixels.data(), width, height);
    save_raw_rgba(diff_path.string(), diff_img.data(), width, height);

    EXPECT_TRUE(diff.passed(tolerance_percent, max_mae))
        << "Golden image test FAILED for: " << scene_name << "\n"
        << "  Mean absolute error: " << diff.mean_absolute_error << " (max allowed: " << max_mae
        << ")\n"
        << "  Differing pixels:    " << diff.differing_pixels << " / " << diff.total_pixels << " ("
        << diff.percent_different << "%, max allowed: " << tolerance_percent << "%)\n"
        << "  Max channel diff:    " << diff.max_absolute_error << "\n"
        << "  Diff image saved to: " << diff_path;
}

// ─── Phase 2 Scene Definitions ──────────────────────────────────────────────

// Multi-series line plot: multiple overlapping series with distinct colors
static void scene_multi_series_line(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 300;
    std::vector<float> x(N);
    std::vector<float> y1(N), y2(N), y3(N);

    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.04f;
        y1[i] = std::sin(x[i] * 2.0f);
        y2[i] = std::cos(x[i] * 1.5f) * 0.8f;
        y3[i] = std::sin(x[i] * 3.0f + 1.0f) * 0.5f;
    }

    ax.line(x, y1).label("sin(2x)").color(rgb(0.2f, 0.6f, 1.0f));
    ax.line(x, y2).label("cos(1.5x)").color(rgb(1.0f, 0.4f, 0.2f));
    ax.line(x, y3).label("sin(3x+1)").color(rgb(0.3f, 0.9f, 0.4f));

    ax.xlim(0.0f, 12.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Multi-Series Line Plot");
    ax.xlabel("Time (s)");
    ax.ylabel("Amplitude");
    ax.grid(true);
}

// Dense scatter plot: 500 points with varying positions
static void scene_dense_scatter(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 500;
    std::vector<float> x(N), y(N);

    for (size_t i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(N);
        // Deterministic pseudo-random pattern using trig
        x[i] = t * 10.0f + std::sin(t * 47.0f) * 0.5f;
        y[i] = std::sin(t * 6.28f) * 3.0f + std::cos(t * 31.0f) * 0.8f;
    }

    ax.scatter(x, y).label("measurements").color(rgb(0.8f, 0.2f, 0.5f)).size(4.0f);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-5.0f, 5.0f);
    ax.title("Dense Scatter (500 pts)");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

// Mixed series: line + scatter on same axes
static void scene_mixed_series(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 100;
    std::vector<float> x(N), y_line(N), x_scatter(N / 5), y_scatter(N / 5);

    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.1f;
        y_line[i] = std::sin(x[i]) * 2.0f;
    }
    for (size_t i = 0; i < N / 5; ++i)
    {
        x_scatter[i] = static_cast<float>(i) * 0.5f;
        y_scatter[i] =
            std::sin(x_scatter[i]) * 2.0f + 0.3f * std::cos(static_cast<float>(i) * 7.0f);
    }

    ax.line(x, y_line).label("model").color(rgb(0.2f, 0.4f, 0.9f)).width(2.5f);
    ax.scatter(x_scatter, y_scatter).label("data").color(rgb(1.0f, 0.5f, 0.0f)).size(5.0f);

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-3.0f, 3.0f);
    ax.title("Line + Scatter Overlay");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

// 2x2 subplot grid
static void scene_subplot_2x2(App& /*app*/, Figure& fig)
{
    constexpr size_t N = 100;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.1f;

    // Top-left: sin
    {
        auto& ax = fig.subplot(2, 2, 1);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i]);
        ax.line(x, y).label("sin").color(rgb(0.2f, 0.6f, 1.0f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("sin(x)");
        ax.grid(true);
    }
    // Top-right: cos
    {
        auto& ax = fig.subplot(2, 2, 2);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::cos(x[i]);
        ax.line(x, y).label("cos").color(rgb(1.0f, 0.4f, 0.2f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("cos(x)");
        ax.grid(true);
    }
    // Bottom-left: tan (clamped)
    {
        auto& ax = fig.subplot(2, 2, 3);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
        {
            float v = std::tan(x[i]);
            y[i] = std::clamp(v, -5.0f, 5.0f);
        }
        ax.line(x, y).label("tan").color(rgb(0.3f, 0.9f, 0.3f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-5.0f, 5.0f);
        ax.title("tan(x)");
        ax.grid(true);
    }
    // Bottom-right: exp decay
    {
        auto& ax = fig.subplot(2, 2, 4);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::exp(-x[i] * 0.3f) * std::sin(x[i] * 3.0f);
        ax.line(x, y).label("decay").color(rgb(0.8f, 0.2f, 0.8f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Damped oscillation");
        ax.grid(true);
    }
}

// No-grid, no-border minimal plot
static void scene_minimal_no_grid(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 80;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.125f;
        y[i] = std::log1p(x[i]);
    }

    ax.line(x, y).label("log(1+x)").color(rgb(0.1f, 0.1f, 0.1f)).width(3.0f);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 3.0f);
    ax.title("Minimal (no grid, no border)");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(false);
    ax.show_border(false);
}

// Wide-aspect ratio plot (simulating dashboard panel)
static void scene_wide_aspect(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 500;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) / static_cast<float>(N) * 100.0f;
        y[i] = std::sin(x[i] * 0.1f) * 50.0f + 50.0f + std::sin(x[i] * 0.37f) * 15.0f;
    }

    ax.line(x, y).label("sensor").color(rgb(0.0f, 0.7f, 0.9f)).width(1.5f);
    ax.xlim(0.0f, 100.0f);
    ax.ylim(0.0f, 120.0f);
    ax.title("Wide Aspect Ratio");
    ax.xlabel("Sample");
    ax.ylabel("Value");
    ax.grid(true);
}

// Zoomed-in view: tight axis limits on a subset of data
static void scene_zoomed_region(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 1000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.01f;
        y[i] = std::sin(x[i] * 10.0f) * std::exp(-x[i] * 0.5f);
    }

    ax.line(x, y).label("signal").color(rgb(0.9f, 0.2f, 0.2f)).width(2.0f);
    // Zoomed into a small region
    ax.xlim(2.0f, 4.0f);
    ax.ylim(-0.5f, 0.5f);
    ax.title("Zoomed Region");
    ax.xlabel("Time");
    ax.ylabel("Amplitude");
    ax.grid(true);
}

// Multiple scatter series with different marker sizes
static void scene_multi_scatter(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 40;
    std::vector<float> x1(N), y1(N), x2(N), y2(N), x3(N), y3(N);

    for (size_t i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(N);
        x1[i] = t * 10.0f;
        y1[i] = std::sin(t * 6.28f) * 2.0f + 5.0f;
        x2[i] = t * 10.0f + 0.1f;
        y2[i] = std::cos(t * 6.28f) * 1.5f + 5.0f;
        x3[i] = t * 10.0f + 0.2f;
        y3[i] = std::sin(t * 12.56f) * 1.0f + 5.0f;
    }

    ax.scatter(x1, y1).label("large").color(rgb(0.2f, 0.6f, 1.0f)).size(8.0f);
    ax.scatter(x2, y2).label("medium").color(rgb(1.0f, 0.5f, 0.0f)).size(5.0f);
    ax.scatter(x3, y3).label("small").color(rgb(0.3f, 0.8f, 0.3f)).size(3.0f);

    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);
    ax.title("Multi-Scatter Sizes");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

// 3x1 vertical subplot layout
static void scene_subplot_3x1(App& /*app*/, Figure& fig)
{
    constexpr size_t N = 200;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.05f;

    {
        auto& ax = fig.subplot(3, 1, 1);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i]);
        ax.line(x, y).label("ch1").color(rgb(0.2f, 0.6f, 1.0f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Channel 1");
        ax.grid(true);
    }
    {
        auto& ax = fig.subplot(3, 1, 2);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * 2.0f) * 0.7f;
        ax.line(x, y).label("ch2").color(rgb(1.0f, 0.4f, 0.2f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Channel 2");
        ax.grid(true);
    }
    {
        auto& ax = fig.subplot(3, 1, 3);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * 0.5f) * 1.2f;
        ax.line(x, y).label("ch3").color(rgb(0.3f, 0.9f, 0.3f));
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Channel 3");
        ax.grid(true);
    }
}

// Negative axis range (centered at origin)
static void scene_negative_axes(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 200;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(N) * 6.28f;
        x[i] = std::cos(t) * (1.0f + 0.5f * std::cos(5.0f * t));
        y[i] = std::sin(t) * (1.0f + 0.5f * std::cos(5.0f * t));
    }

    ax.line(x, y).label("rose").color(rgb(0.8f, 0.2f, 0.6f)).width(2.0f);
    ax.xlim(-2.0f, 2.0f);
    ax.ylim(-2.0f, 2.0f);
    ax.title("Negative Axes (Rose Curve)");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

// ─── Phase 2 Golden Test Cases ──────────────────────────────────────────────

TEST(GoldenImagePhase2, MultiSeriesLine)
{
    run_golden_test("p2_multi_series_line", scene_multi_series_line);
}

TEST(GoldenImagePhase2, DenseScatter)
{
    run_golden_test("p2_dense_scatter", scene_dense_scatter);
}

TEST(GoldenImagePhase2, MixedSeries)
{
    run_golden_test("p2_mixed_series", scene_mixed_series);
}

TEST(GoldenImagePhase2, Subplot2x2)
{
    run_golden_test("p2_subplot_2x2", scene_subplot_2x2, 800, 600);
}

TEST(GoldenImagePhase2, MinimalNoGrid)
{
    run_golden_test("p2_minimal_no_grid", scene_minimal_no_grid);
}

TEST(GoldenImagePhase2, WideAspect)
{
    run_golden_test("p2_wide_aspect", scene_wide_aspect, 1280, 360);
}

TEST(GoldenImagePhase2, ZoomedRegion)
{
    run_golden_test("p2_zoomed_region", scene_zoomed_region);
}

TEST(GoldenImagePhase2, MultiScatter)
{
    run_golden_test("p2_multi_scatter", scene_multi_scatter);
}

TEST(GoldenImagePhase2, Subplot3x1)
{
    run_golden_test("p2_subplot_3x1", scene_subplot_3x1, 640, 720);
}

TEST(GoldenImagePhase2, NegativeAxes)
{
    run_golden_test("p2_negative_axes", scene_negative_axes);
}

// ─── Diff framework stress tests ────────────────────────────────────────────

TEST(GoldenImagePhase2Framework, LargeImageIdentical)
{
    constexpr uint32_t W = 640, H = 480;
    std::vector<uint8_t> img(W * H * 4);
    for (size_t i = 0; i < img.size(); i += 4)
    {
        img[i + 0] = static_cast<uint8_t>((i / 4) % 256);
        img[i + 1] = static_cast<uint8_t>((i / 4 + 85) % 256);
        img[i + 2] = static_cast<uint8_t>((i / 4 + 170) % 256);
        img[i + 3] = 255;
    }

    auto result = compare_images(img.data(), img.data(), W, H);
    EXPECT_EQ(result.differing_pixels, 0u);
    EXPECT_DOUBLE_EQ(result.mean_absolute_error, 0.0);
    EXPECT_TRUE(result.passed());
}

TEST(GoldenImagePhase2Framework, GradientDiffDetection)
{
    constexpr uint32_t W = 100, H = 100;
    std::vector<uint8_t> a(W * H * 4, 128);
    std::vector<uint8_t> b(W * H * 4, 128);

    // Apply a gradient difference to 10% of pixels
    for (uint32_t y = 0; y < 10; ++y)
    {
        for (uint32_t x = 0; x < W; ++x)
        {
            size_t idx = (y * W + x) * 4;
            b[idx] = 0;  // Red channel zeroed
        }
    }

    auto result = compare_images(a.data(), b.data(), W, H);
    EXPECT_GT(result.differing_pixels, 0u);
    EXPECT_NEAR(result.percent_different, 10.0, 0.5);
}

TEST(GoldenImagePhase2Framework, DiffImageGeneration)
{
    constexpr uint32_t W = 8, H = 8;
    std::vector<uint8_t> a(W * H * 4, 100);
    std::vector<uint8_t> b(W * H * 4, 100);

    // Make first pixel different
    b[0] = 200;

    auto diff_img = generate_diff_image(a.data(), b.data(), W, H);
    ASSERT_EQ(diff_img.size(), static_cast<size_t>(W * H * 4));

    // First pixel should be red (differs)
    EXPECT_EQ(diff_img[0], 255);
    EXPECT_EQ(diff_img[1], 0);
    EXPECT_EQ(diff_img[2], 0);
    EXPECT_EQ(diff_img[3], 255);

    // Second pixel should be dimmed (matches)
    EXPECT_LT(diff_img[4], 100);  // Dimmed
}

TEST(GoldenImagePhase2Framework, RawRoundTrip)
{
    constexpr uint32_t W = 16, H = 16;
    std::vector<uint8_t> original(W * H * 4);
    for (size_t i = 0; i < original.size(); ++i)
    {
        original[i] = static_cast<uint8_t>(i % 256);
    }

    auto tmp = fs::temp_directory_path() / "spectra_golden_roundtrip_test.raw";
    ASSERT_TRUE(save_raw_rgba(tmp.string(), original.data(), W, H));

    std::vector<uint8_t> loaded;
    uint32_t lw = 0, lh = 0;
    ASSERT_TRUE(load_raw_rgba(tmp.string(), loaded, lw, lh));

    EXPECT_EQ(lw, W);
    EXPECT_EQ(lh, H);
    EXPECT_EQ(loaded, original);

    fs::remove(tmp);
}

}  // namespace spectra::test

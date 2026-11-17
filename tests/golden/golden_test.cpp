#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#include "image_diff.hpp"
#include "render/backend.hpp"
#include "render/renderer.hpp"

namespace fs = std::filesystem;

// Single shared App — one VkInstance for the entire test binary.  This avoids
// the non-deterministic SIGSEGV in the NVIDIA Vulkan driver that occurs when
// multiple VkInstances are created and destroyed sequentially in the same
// process.  The App is heap-allocated (never deleted) so that its VkInstance
// destructor is never invoked; ::_exit() in main() terminates the process
// without running any C++ destructors.
static spectra::App* g_app = nullptr;

namespace spectra::test
{

// Directory containing baseline .raw files
static fs::path baseline_dir()
{
    // Check environment variable first, then fall back to source-relative path
    if (const char* env = std::getenv("SPECTRA_GOLDEN_BASELINE_DIR"))
    {
        return fs::path(env);
    }
    // Relative to this source file's location
    return fs::path(SPECTRA_GOLDEN_BASELINE_DIR_DEFAULT);
}

// Directory for test output artifacts (actual renders, diff images)
static fs::path output_dir()
{
    if (const char* env = std::getenv("SPECTRA_GOLDEN_OUTPUT_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(SPECTRA_GOLDEN_OUTPUT_DIR_DEFAULT);
}

// If SPECTRA_UPDATE_BASELINES=1, overwrite baselines instead of comparing
static bool update_baselines()
{
    const char* env = std::getenv("SPECTRA_UPDATE_BASELINES");
    return env && std::string(env) == "1";
}

// Render a figure headless and return pixel data
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

// Core golden test: render scene, compare against baseline
static void run_golden_test(const std::string&                 scene_name,
                            std::function<void(App&, Figure&)> setup_scene,
                            uint32_t                           width             = 640,
                            uint32_t                           height            = 480,
                            double                             tolerance_percent = 0.5,
                            double                             max_mae           = 1.0)
{
    fs::path baseline_path = baseline_dir() / (scene_name + ".raw");
    fs::path actual_path   = output_dir() / (scene_name + "_actual.raw");
    fs::path diff_path     = output_dir() / (scene_name + "_diff.raw");

    // Ensure output directory exists
    fs::create_directories(output_dir());

    ASSERT_NE(g_app, nullptr) << "Shared App not initialized";
    App&     app    = *g_app;
    auto&    fig    = app.figure({.width = width, .height = height});
    FigureId fig_id = app.figure_registry().find_id(&fig);

    setup_scene(app, fig);

    std::vector<uint8_t> actual_pixels;
    bool                 render_ok = render_headless(fig, app, actual_pixels);
    // Clean up figure from registry so subsequent tests start with a fresh state.
    // Notify the renderer to purge cached GPU data for all axes of this figure
    // BEFORE unregistering — once the figure is destroyed the axes pointers are
    // dangling, and the allocator may recycle their addresses for the next test.
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

    ASSERT_TRUE(render_ok) << "Failed to render scene: " << scene_name;

    // Save actual render
    ASSERT_TRUE(save_raw_rgba(actual_path.string(), actual_pixels.data(), width, height))
        << "Failed to save actual render for: " << scene_name;

    if (update_baselines())
    {
        // Update mode: save as new baseline
        fs::create_directories(baseline_dir());
        ASSERT_TRUE(save_raw_rgba(baseline_path.string(), actual_pixels.data(), width, height))
            << "Failed to save baseline for: " << scene_name;
        std::cout << "[GOLDEN] Updated baseline: " << baseline_path << "\n";
        return;
    }

    // Compare mode: load baseline and diff
    if (!fs::exists(baseline_path))
    {
        if (golden_strict())
        {
            FAIL() << "Baseline not found: " << baseline_path
                   << " (SPECTRA_GOLDEN_STRICT=1; run with SPECTRA_UPDATE_BASELINES=1 to "
                      "generate it and commit the result)";
        }
        GTEST_SKIP() << "Baseline not found: " << baseline_path
                     << " (run with SPECTRA_UPDATE_BASELINES=1 to generate)";
        return;
    }

    std::vector<uint8_t> baseline_pixels;
    uint32_t             bw = 0, bh = 0;
    ASSERT_TRUE(load_raw_rgba(baseline_path.string(), baseline_pixels, bw, bh))
        << "Failed to load baseline: " << baseline_path;

    ASSERT_EQ(bw, width) << "Baseline width mismatch for: " << scene_name;
    ASSERT_EQ(bh, height) << "Baseline height mismatch for: " << scene_name;

    GoldenTolerance tol;
    tol.max_percent = tolerance_percent;
    tol.max_mae     = max_mae;
    DiffResult diff = compare_images(actual_pixels.data(),
                                     baseline_pixels.data(),
                                     width,
                                     height,
                                     tol.pixel_threshold);

    // Save diff visualization
    auto diff_img = generate_diff_image(actual_pixels.data(),
                                        baseline_pixels.data(),
                                        width,
                                        height,
                                        tol.pixel_threshold);
    save_raw_rgba(diff_path.string(), diff_img.data(), width, height);

    EXPECT_TRUE(diff.passed(tol)) << "Golden image test FAILED for: " << scene_name << "\n"
                                  << diff.describe(tol) << "  Diff image saved to: " << diff_path;
}

// ─── Scene Definitions ──────────────────────────────────────────────────────

static void scene_basic_line(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    std::vector<float> x(200);
    std::vector<float> y(200);
    for (size_t i = 0; i < x.size(); ++i)
    {
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

static void scene_scatter(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    std::vector<float> x(50);
    std::vector<float> y(50);
    for (size_t i = 0; i < x.size(); ++i)
    {
        float t = static_cast<float>(i) * 0.1f;
        x[i]    = t;
        y[i]    = std::sin(t) * 0.8f + 0.1f * static_cast<float>(i % 5);
    }

    ax.scatter(x, y).label("data").color(rgb(1.0f, 0.4f, 0.0f)).size(6.0f);
    ax.xlim(0.0f, 5.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Scatter Plot");
    ax.xlabel("Time");
    ax.ylabel("Value");
}

static void scene_multi_subplot(App& /*app*/, Figure& fig)
{
    auto& ax1 = fig.subplot(2, 1, 1);
    auto& ax2 = fig.subplot(2, 1, 2);

    constexpr size_t   N = 150;
    std::vector<float> x(N);
    std::vector<float> y1(N);
    std::vector<float> y2(N);

    for (size_t i = 0; i < N; ++i)
    {
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

// Suptitle above a 2x2 grid with shared x (per column) and y (per row): the
// inner tick labels are hidden and the constrained layout packs the grid.
static void scene_suptitle_shared(App& /*app*/, Figure& fig)
{
    fig.suptitle("Shared axes overview");
    fig.share_x(ShareAxes::Col);
    fig.share_y(ShareAxes::Row);
    constexpr size_t   N = 120;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.05f;
    for (int k = 1; k <= 4; ++k)
    {
        auto&              ax = fig.subplot(2, 2, k);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * static_cast<float>(k)) * static_cast<float>(k);
        ax.line(x, y).color(k % 2 ? colors::red : rgb(0.2f, 0.6f, 1.0f));
        ax.title("Panel " + std::to_string(k));
        if (k > 2)
            ax.xlabel("Time (s)");
        if (k % 2 == 1)
            ax.ylabel("Amplitude");
    }
}

static void scene_empty_axes(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(0.0f, 10.0f);
    ax.title("Empty Axes");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

static void scene_grid_only(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);
    ax.xlim(-5.0f, 5.0f);
    ax.ylim(-5.0f, 5.0f);
    ax.grid(true);
    ax.title("Grid Only");
}

// Plot style presets: the same two auto-coloured lines under each style.
static std::function<void(App&, Figure&)> scene_style(const std::string& style_name)
{
    return [style_name](App& /*app*/, Figure& fig)
    {
        fig.apply_style(style_name);
        auto& ax = fig.subplot(1, 1, 1);

        constexpr size_t   N = 200;
        std::vector<float> x(N);
        std::vector<float> y1(N);
        std::vector<float> y2(N);
        for (size_t i = 0; i < N; ++i)
        {
            x[i]  = static_cast<float>(i) * 0.05f;
            y1[i] = std::sin(x[i]);
            y2[i] = 0.6f * std::cos(x[i] * 1.5f);
        }
        ax.plot(x, y1).label("sin");
        ax.plot(x, y2).label("cos");
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Style: " + style_name);
        ax.xlabel("Time (s)");
        ax.ylabel("Amplitude");
    };
}

// Mathtext ($...$) in the title, both axis labels (the Y one rotated), legend entries and
// an annotation: Greek letters, symbols, sub/superscripts (stacked and nested).
static void scene_mathtext(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t   N = 200;
    std::vector<float> x(N);
    std::vector<float> y1(N);
    std::vector<float> y2(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i]  = static_cast<float>(i) * 0.05f;
        y1[i] = std::sin(x[i] * 1.5f);
        y2[i] = std::exp(-x[i] * 0.3f) * std::cos(x[i] * 3.0f);
    }

    ax.line(x, y1).label("$\\sin(\\omega t)$, $\\omega = 1.5$").color(rgb(0.2f, 0.6f, 1.0f));
    ax.line(x, y2).label("$e^{-t/\\tau}\\cos(2\\pi f t)$").color(colors::red);
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("$\\alpha_1 = 10^{-3}$ s, $\\Delta T \\approx 5\\degree$C");
    ax.xlabel("Time $t$ ($\\mu$s)");
    ax.ylabel("$\\sigma_{xy}^2$ [V$^2$]");
    ax.text(5.0f, 1.2f, "$x_i^2 \\pm \\delta$, cost \\$5");
}

// Flat stat fills: opaque fills, thin edges, contrasting box medians, and
// bars/histograms standing on their baseline (sticky autoscale).
static void scene_stat_fills(App& /*app*/, Figure& fig)
{
    auto sample = [](int n, float mu, float sd, int seed)
    {
        std::vector<float> v(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            // Deterministic, roughly bell-shaped samples.
            const float t = static_cast<float>(i * 37 + seed * 11);
            v[static_cast<size_t>(i)] =
                mu + sd * (std::sin(t * 0.731f) + std::sin(t * 1.913f) + std::sin(t * 0.277f));
        }
        return v;
    };

    auto&              bars = fig.subplot(2, 2, 1);
    std::vector<float> pos  = {0, 1, 2, 3, 4};
    std::vector<float> h    = {3.2f, 5.1f, 2.4f, 4.4f, 3.9f};
    bars.bar(pos, h).label("2025").color(rgb(0.12f, 0.47f, 0.71f));
    bars.title("Bars");

    auto& hist = fig.subplot(2, 2, 2);
    hist.histogram(sample(800, 0.0f, 1.0f, 1), 24).color(rgb(0.84f, 0.37f, 0.0f));
    hist.title("Histogram");

    auto& box = fig.subplot(2, 2, 3);
    auto& bp  = box.box_plot();
    bp.color(rgb(0.17f, 0.63f, 0.17f));
    for (int i = 0; i < 3; ++i)
        bp.add_box(static_cast<float>(i + 1),
                   sample(120, static_cast<float>(i), 0.6f + 0.2f * static_cast<float>(i), i));
    box.title("Box");

    auto& vio = fig.subplot(2, 2, 4);
    auto& vn  = vio.violin();
    vn.color(rgb(0.58f, 0.40f, 0.74f));
    for (int i = 0; i < 3; ++i)
        vn.add_violin(static_cast<float>(i + 1),
                      sample(200, 0.0f, 0.5f + 0.3f * static_cast<float>(i), i + 5));
    vio.title("Violin");
}

// ─── Test Cases ─────────────────────────────────────────────────────────────

TEST(GoldenImage, BasicLine)
{
    run_golden_test("basic_line", scene_basic_line);
}

TEST(GoldenImage, Scatter)
{
    run_golden_test("scatter", scene_scatter);
}

TEST(GoldenImage, MultiSubplot)
{
    run_golden_test("multi_subplot", scene_multi_subplot, 800, 600);
}

TEST(GoldenImage, SuptitleSharedAxes)
{
    run_golden_test("suptitle_shared", scene_suptitle_shared, 800, 600);
}

TEST(GoldenImage, EmptyAxes)
{
    run_golden_test("empty_axes", scene_empty_axes);
}

TEST(GoldenImage, GridOnly)
{
    run_golden_test("grid_only", scene_grid_only);
}

TEST(GoldenImage, StylePaper)
{
    run_golden_test("style_paper", scene_style("paper"));
}

TEST(GoldenImage, StyleTalk)
{
    run_golden_test("style_talk", scene_style("talk"));
}

TEST(GoldenImage, StyleMinimal)
{
    run_golden_test("style_minimal", scene_style("minimal"));
}

TEST(GoldenImage, MathText)
{
    run_golden_test("mathtext", scene_mathtext);
}

TEST(GoldenImage, StatFills)
{
    run_golden_test("stat_fills", scene_stat_fills, 800, 600);
}

// ─── Meta-test: verify the diff framework catches intentional differences ───

TEST(GoldenImageFramework, DetectsDifference)
{
    constexpr uint32_t   W = 4, H = 4;
    std::vector<uint8_t> a(W * H * 4, 128);
    std::vector<uint8_t> b(W * H * 4, 128);

    // Make one pixel completely different
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;
    b[3] = 255;

    auto result = compare_images(a.data(), b.data(), W, H, 2);
    EXPECT_GT(result.differing_pixels, 0u);
    EXPECT_GT(result.percent_different, 0.0);
}

TEST(GoldenImageFramework, IdenticalImagesPass)
{
    constexpr uint32_t   W = 4, H = 4;
    std::vector<uint8_t> img(W * H * 4, 200);

    auto result = compare_images(img.data(), img.data(), W, H, 2);
    EXPECT_EQ(result.differing_pixels, 0u);
    EXPECT_DOUBLE_EQ(result.mean_absolute_error, 0.0);
    EXPECT_TRUE(result.passed());
}

TEST(GoldenImageFramework, SmallDiffWithinTolerance)
{
    constexpr uint32_t   W = 10, H = 10;
    std::vector<uint8_t> a(W * H * 4, 100);
    std::vector<uint8_t> b(W * H * 4, 101);   // Off by 1 everywhere

    auto result = compare_images(a.data(), b.data(), W, H, 2);
    EXPECT_EQ(result.differing_pixels, 0u);   // Within threshold of 2
    EXPECT_TRUE(result.passed());
}

}   // namespace spectra::test

// Custom main creates a single shared App (one VkInstance) and calls ::_exit()
// after all tests complete, bypassing C++ destructors.  This avoids the
// non-deterministic SIGSEGV in the NVIDIA Vulkan driver during VkInstance
// teardown that occurs when multiple instances are destroyed in the same process.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    g_app      = new spectra::App({.headless = true, .socket_path = ""});
    int result = RUN_ALL_TESTS();
    ::_exit(result);
}

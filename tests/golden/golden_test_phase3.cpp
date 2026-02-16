#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <plotix/plot_style.hpp>
#include <plotix/plotix.hpp>
#include <string>
#include <vector>

#include "image_diff.hpp"
#include "render/backend.hpp"

namespace fs = std::filesystem;

namespace plotix::test
{

// ─── Shared infrastructure (mirrors golden_test.cpp / golden_test_phase2.cpp)

static fs::path baseline_dir()
{
    if (const char* env = std::getenv("PLOTIX_GOLDEN_BASELINE_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "baseline";
}

static fs::path output_dir()
{
    if (const char* env = std::getenv("PLOTIX_GOLDEN_OUTPUT_DIR"))
    {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "output";
}

static bool update_baselines()
{
    const char* env = std::getenv("PLOTIX_UPDATE_BASELINES");
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
                     << " (run with PLOTIX_UPDATE_BASELINES=1 to generate)";
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

// ─── Phase 3 Scene Definitions ──────────────────────────────────────────────

// Dashed line styles showcase: solid, dashed, dotted, dash-dot, dash-dot-dot
static void scene_line_styles(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 200;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.05f;

    // Five series with different line styles
    struct StyleDef
    {
        const char* label;
        LineStyle ls;
        float offset;
        Color color;
    };
    StyleDef styles[] = {
        {"Solid", LineStyle::Solid, 0.0f, rgb(0.2f, 0.6f, 1.0f)},
        {"Dashed", LineStyle::Dashed, 0.8f, rgb(1.0f, 0.4f, 0.2f)},
        {"Dotted", LineStyle::Dotted, 1.6f, rgb(0.3f, 0.9f, 0.4f)},
        {"Dash-Dot", LineStyle::DashDot, 2.4f, rgb(0.9f, 0.2f, 0.8f)},
        {"Dash-Dot-Dot", LineStyle::DashDotDot, 3.2f, rgb(0.8f, 0.7f, 0.1f)},
    };

    for (auto& sd : styles)
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * 2.0f) + sd.offset;

        auto& s = ax.line(x, y).label(sd.label).color(sd.color).width(2.5f);
        s.line_style(sd.ls);
    }

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 5.0f);
    ax.title("Line Style Showcase");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

// Marker styles showcase: multiple marker types on scatter series
static void scene_marker_styles(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    MarkerStyle markers[] = {
        MarkerStyle::Circle,
        MarkerStyle::Square,
        MarkerStyle::Diamond,
        MarkerStyle::TriangleUp,
        MarkerStyle::Star,
        MarkerStyle::Plus,
        MarkerStyle::Cross,
        MarkerStyle::Pentagon,
        MarkerStyle::Hexagon,
    };
    Color marker_colors[] = {
        rgb(0.2f, 0.6f, 1.0f),
        rgb(1.0f, 0.4f, 0.2f),
        rgb(0.3f, 0.9f, 0.4f),
        rgb(0.9f, 0.2f, 0.8f),
        rgb(0.8f, 0.7f, 0.1f),
        rgb(0.1f, 0.8f, 0.8f),
        rgb(0.6f, 0.3f, 0.9f),
        rgb(0.9f, 0.6f, 0.3f),
        rgb(0.4f, 0.4f, 0.9f),
    };

    constexpr size_t N = 10;
    for (int m = 0; m < 9; ++m)
    {
        std::vector<float> x(N), y(N);
        for (size_t i = 0; i < N; ++i)
        {
            x[i] = static_cast<float>(i) + 0.5f;
            y[i] = static_cast<float>(m) + 0.3f * std::sin(static_cast<float>(i) * 0.8f);
        }
        auto& s = ax.scatter(x, y)
                      .label(marker_style_name(markers[m]))
                      .color(marker_colors[m])
                      .size(8.0f);
        s.marker_style(markers[m]);
    }

    ax.xlim(0.0f, 11.0f);
    ax.ylim(-1.0f, 10.0f);
    ax.title("Marker Style Showcase");
    ax.xlabel("Sample");
    ax.ylabel("Type");
    ax.grid(true);
}

// Filled markers: FilledCircle, FilledSquare, FilledDiamond, FilledTriangleUp
static void scene_filled_markers(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    MarkerStyle markers[] = {
        MarkerStyle::FilledCircle,
        MarkerStyle::FilledSquare,
        MarkerStyle::FilledDiamond,
        MarkerStyle::FilledTriangleUp,
    };

    constexpr size_t N = 15;
    for (int m = 0; m < 4; ++m)
    {
        std::vector<float> x(N), y(N);
        for (size_t i = 0; i < N; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(N);
            x[i] = t * 10.0f;
            y[i] = static_cast<float>(m) * 2.0f + std::sin(t * 6.28f);
        }
        auto& s =
            ax.scatter(x, y)
                .label(marker_style_name(markers[m]))
                .color(rgb(
                    0.2f + 0.2f * static_cast<float>(m), 0.5f, 0.9f - 0.2f * static_cast<float>(m)))
                .size(10.0f);
        s.marker_style(markers[m]);
    }

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-2.0f, 9.0f);
    ax.title("Filled Marker Styles");
    ax.grid(true);
}

// Line + marker combo: lines with markers at data points
static void scene_line_with_markers(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 30;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.33f;

    // Dashed line with circle markers
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i]);
        auto& s =
            ax.line(x, y).label("sin(x) dashed+circle").color(rgb(0.2f, 0.6f, 1.0f)).width(2.0f);
        s.line_style(LineStyle::Dashed);
        s.marker_style(MarkerStyle::Circle);
        s.marker_size(6.0f);
    }

    // Dotted line with square markers
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::cos(x[i]);
        auto& s =
            ax.line(x, y).label("cos(x) dotted+square").color(rgb(1.0f, 0.4f, 0.2f)).width(2.0f);
        s.line_style(LineStyle::Dotted);
        s.marker_style(MarkerStyle::Square);
        s.marker_size(5.0f);
    }

    // Dash-dot with diamond markers
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * 0.5f) * 0.7f;
        auto& s = ax.line(x, y)
                      .label("slow sin dash-dot+diamond")
                      .color(rgb(0.3f, 0.9f, 0.3f))
                      .width(2.0f);
        s.line_style(LineStyle::DashDot);
        s.marker_style(MarkerStyle::Diamond);
        s.marker_size(7.0f);
    }

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Line + Marker Combinations");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.grid(true);
}

// Opacity showcase: series with varying opacity
static void scene_opacity_layers(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 200;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.05f;

    float opacities[] = {1.0f, 0.7f, 0.4f, 0.2f};
    for (int o = 0; o < 4; ++o)
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * (1.0f + static_cast<float>(o) * 0.5f));

        auto& s = ax.line(x, y)
                      .label("opacity=" + std::to_string(opacities[o]).substr(0, 3))
                      .color(rgb(0.2f, 0.6f, 1.0f))
                      .width(3.0f);
        s.opacity(opacities[o]);
    }

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("Opacity Layers");
    ax.grid(true);
}

// Split subplot: 2x2 with different styles per subplot
static void scene_styled_subplots(App& /*app*/, Figure& fig)
{
    constexpr size_t N = 100;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.1f;

    // Top-left: solid blue
    {
        auto& ax = fig.subplot(2, 2, 1);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i]);
        ax.line(x, y).label("solid").color(rgb(0.2f, 0.6f, 1.0f)).width(2.0f);
        ax.xlim(0, 10);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Solid");
        ax.grid(true);
    }
    // Top-right: dashed red
    {
        auto& ax = fig.subplot(2, 2, 2);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::cos(x[i]);
        auto& s = ax.line(x, y).label("dashed").color(rgb(1.0f, 0.3f, 0.2f)).width(2.0f);
        s.line_style(LineStyle::Dashed);
        ax.xlim(0, 10);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Dashed");
        ax.grid(true);
    }
    // Bottom-left: dotted with markers
    {
        auto& ax = fig.subplot(2, 2, 3);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * 2.0f) * 0.5f;
        auto& s = ax.line(x, y).label("dotted+markers").color(rgb(0.3f, 0.8f, 0.3f)).width(1.5f);
        s.line_style(LineStyle::Dotted);
        s.marker_style(MarkerStyle::Circle);
        s.marker_size(4.0f);
        ax.xlim(0, 10);
        ax.ylim(-1.5f, 1.5f);
        ax.title("Dotted + Markers");
        ax.grid(true);
    }
    // Bottom-right: scatter only
    {
        auto& ax = fig.subplot(2, 2, 4);
        std::vector<float> sx(50), sy(50);
        for (size_t i = 0; i < 50; ++i)
        {
            float t = static_cast<float>(i) / 50.0f;
            sx[i] = t * 10.0f;
            sy[i] = std::sin(t * 6.28f) + 0.2f * std::cos(t * 31.0f);
        }
        auto& s = ax.scatter(sx, sy).label("scatter").color(rgb(0.8f, 0.3f, 0.8f)).size(6.0f);
        s.marker_style(MarkerStyle::Star);
        ax.xlim(0, 10);
        ax.ylim(-2, 2);
        ax.title("Scatter Stars");
        ax.grid(true);
    }
}

// Dense styled plot: many series with different styles (stress test)
static void scene_dense_styled(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 150;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.067f;

    LineStyle line_styles[] = {LineStyle::Solid,
                               LineStyle::Dashed,
                               LineStyle::Dotted,
                               LineStyle::DashDot,
                               LineStyle::DashDotDot};
    Color line_colors[] = {
        rgb(0.2f, 0.6f, 1.0f),
        rgb(1.0f, 0.4f, 0.2f),
        rgb(0.3f, 0.9f, 0.4f),
        rgb(0.9f, 0.2f, 0.8f),
        rgb(0.8f, 0.7f, 0.1f),
        rgb(0.1f, 0.8f, 0.8f),
        rgb(0.6f, 0.3f, 0.9f),
        rgb(0.9f, 0.6f, 0.3f),
    };

    for (int s_idx = 0; s_idx < 8; ++s_idx)
    {
        std::vector<float> y(N);
        float freq = 1.0f + static_cast<float>(s_idx) * 0.3f;
        float phase = static_cast<float>(s_idx) * 0.5f;
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * freq + phase) * 0.8f + static_cast<float>(s_idx) * 0.25f;

        auto& s =
            ax.line(x, y).label("s" + std::to_string(s_idx)).color(line_colors[s_idx]).width(2.0f);
        s.line_style(line_styles[s_idx % 5]);
    }

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 4.0f);
    ax.title("Dense Multi-Style Plot");
    ax.grid(true);
}

// Format string parsed plot: uses parse_format_string for MATLAB-style setup
static void scene_format_strings(App& /*app*/, Figure& fig)
{
    auto& ax = fig.subplot(1, 1, 1);

    constexpr size_t N = 60;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.167f;

    // "r--o" → red dashed with circles
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i]);
        auto style = parse_format_string("r--o");
        auto& s = ax.line(x, y).label("r--o").width(2.0f);
        if (style.color)
            s.color(*style.color);
        s.line_style(style.line_style);
        s.marker_style(style.marker_style);
    }

    // "b:*" → blue dotted with stars
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::cos(x[i]);
        auto style = parse_format_string("b:*");
        auto& s = ax.line(x, y).label("b:*").width(2.0f);
        if (style.color)
            s.color(*style.color);
        s.line_style(style.line_style);
        s.marker_style(style.marker_style);
    }

    // "g-.s" → green dash-dot with squares
    {
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
            y[i] = std::sin(x[i] * 0.5f) * 0.7f;
        auto style = parse_format_string("g-.s");
        auto& s = ax.line(x, y).label("g-.s").width(2.0f);
        if (style.color)
            s.color(*style.color);
        s.line_style(style.line_style);
        s.marker_style(style.marker_style);
    }

    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("MATLAB Format Strings");
    ax.grid(true);
}

// ─── Phase 3 Golden Test Cases ──────────────────────────────────────────────

TEST(GoldenImagePhase3, LineStyles)
{
    run_golden_test("p3_line_styles", scene_line_styles);
}

TEST(GoldenImagePhase3, MarkerStyles)
{
    run_golden_test("p3_marker_styles", scene_marker_styles, 800, 600);
}

TEST(GoldenImagePhase3, FilledMarkers)
{
    run_golden_test("p3_filled_markers", scene_filled_markers);
}

TEST(GoldenImagePhase3, LineWithMarkers)
{
    run_golden_test("p3_line_with_markers", scene_line_with_markers);
}

TEST(GoldenImagePhase3, OpacityLayers)
{
    run_golden_test("p3_opacity_layers", scene_opacity_layers);
}

TEST(GoldenImagePhase3, StyledSubplots)
{
    run_golden_test("p3_styled_subplots", scene_styled_subplots, 800, 600);
}

TEST(GoldenImagePhase3, DenseStyled)
{
    run_golden_test("p3_dense_styled", scene_dense_styled);
}

TEST(GoldenImagePhase3, FormatStrings)
{
    run_golden_test("p3_format_strings", scene_format_strings);
}

}  // namespace plotix::test

#include <benchmark/benchmark.h>
#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

// ─── Data generation helpers ────────────────────────────────────────────────

static std::vector<float> generate_x(size_t n)
{
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
    {
        x[i] = static_cast<float>(i) / static_cast<float>(n) * 100.0f;
    }
    return x;
}

static std::vector<float> generate_y_sin(const std::vector<float>& x)
{
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        y[i] = std::sin(x[i] * 0.1f);
    }
    return y;
}

// ─── Benchmarks ─────────────────────────────────────────────────────────────

static void BM_HeadlessRender_Line_1K(benchmark::State& state)
{
    auto x = generate_x(1000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1280, .height = 720});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y).label("bench");
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Line_1K)->Unit(benchmark::kMillisecond);

static void BM_HeadlessRender_Line_10K(benchmark::State& state)
{
    auto x = generate_x(10000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1280, .height = 720});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y).label("bench");
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Line_10K)->Unit(benchmark::kMillisecond);

static void BM_HeadlessRender_Line_100K(benchmark::State& state)
{
    auto x = generate_x(100000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1920, .height = 1080});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y).label("bench");
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Line_100K)->Unit(benchmark::kMillisecond);

static void BM_HeadlessRender_Line_1M(benchmark::State& state)
{
    auto x = generate_x(1000000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1920, .height = 1080});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y).label("bench");
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Line_1M)->Unit(benchmark::kMillisecond)->Iterations(5);

static void BM_HeadlessRender_Scatter_1K(benchmark::State& state)
{
    auto x = generate_x(1000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1280, .height = 720});
        auto& ax = fig.subplot(1, 1, 1);
        ax.scatter(x, y).label("bench").size(4.0f);
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Scatter_1K)->Unit(benchmark::kMillisecond);

static void BM_HeadlessRender_Scatter_100K(benchmark::State& state)
{
    auto x = generate_x(100000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1920, .height = 1080});
        auto& ax = fig.subplot(1, 1, 1);
        ax.scatter(x, y).label("bench").size(3.0f);
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_Scatter_100K)->Unit(benchmark::kMillisecond);

static void BM_HeadlessRender_MultiSubplot(benchmark::State& state)
{
    auto x = generate_x(5000);
    auto y1 = generate_y_sin(x);
    std::vector<float> y2(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y2[i] = std::cos(x[i] * 0.1f);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1920, .height = 1080});
        auto& ax1 = fig.subplot(2, 1, 1);
        auto& ax2 = fig.subplot(2, 1, 2);
        ax1.line(x, y1).label("sin");
        ax1.xlim(0.0f, 100.0f);
        ax1.ylim(-1.5f, 1.5f);
        ax2.line(x, y2).label("cos");
        ax2.xlim(0.0f, 100.0f);
        ax2.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_HeadlessRender_MultiSubplot)->Unit(benchmark::kMillisecond);

static void BM_HeadlessExport_PNG(benchmark::State& state)
{
    auto x = generate_x(1000);
    auto y = generate_y_sin(x);

    for (auto _ : state)
    {
        spectra::App app({.headless = true});
        auto& fig = app.figure({.width = 1920, .height = 1080});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y).label("bench");
        ax.xlim(0.0f, 100.0f);
        ax.ylim(-1.5f, 1.5f);
        fig.save_png("/tmp/plotix_bench_output.png");
        app.run();
    }
}
BENCHMARK(BM_HeadlessExport_PNG)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();

#include <benchmark/benchmark.h>
#include <cmath>
#include <spectra/spectra.hpp>

// ─── Startup timing benchmarks ──────────────────────────────────────────────

static void BM_Startup_Headless(benchmark::State& state)
{
    std::vector<float> x(100), y(100);
    for (size_t i = 0; i < 100; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = std::sin(x[i] * 0.1f);
    }

    for (auto _ : state)
    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        fig = app.figure({.width = 800, .height = 600});
        auto&        ax  = fig.subplot(1, 1, 1);
        ax.line(x, y);
        app.run();
    }
}
BENCHMARK(BM_Startup_Headless)->Unit(benchmark::kMillisecond);

static void BM_Startup_HeadlessMultiFigure(benchmark::State& state)
{
    std::vector<float> x(100), y(100);
    for (size_t i = 0; i < 100; ++i)
    {
        x[i] = static_cast<float>(i);
        y[i] = std::sin(x[i] * 0.1f);
    }

    for (auto _ : state)
    {
        spectra::App app({.headless = true, .socket_path = ""});
        for (int f = 0; f < 4; ++f)
        {
            auto& fig = app.figure({.width = 800, .height = 600});
            auto& ax  = fig.subplot(1, 1, 1);
            ax.line(x, y);
        }
        app.run();
    }
}
BENCHMARK(BM_Startup_HeadlessMultiFigure)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();

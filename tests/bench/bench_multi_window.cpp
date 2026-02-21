#include <benchmark/benchmark.h>
#include <cmath>
#include <memory>
#include <spectra/app.hpp>
#include <spectra/spectra.hpp>
#include <vector>

#include "render/backend.hpp"

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// Multi-Window Benchmark Scaffolding
//
// Phase 0: Single-window baselines (always available).
// Phase 2+: Multi-window benchmarks (behind SPECTRA_HAS_WINDOW_MANAGER guard).
//
// These benchmarks establish frame-time baselines before the multi-window
// refactor and will measure per-window overhead after Agent B merges.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Data Helpers ────────────────────────────────────────────────────────────

static std::vector<float> gen_x(size_t n)
{
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(i) / static_cast<float>(n) * 10.0f;
    return x;
}

static std::vector<float> gen_y_sin(const std::vector<float>& x)
{
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = std::sin(x[i]);
    return y;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 0 — Single-Window Baselines
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_SingleWindow_EmptyFigure(benchmark::State& state)
{
    for (auto _ : state)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 640, .height = 480});
        fig.subplot(1, 1, 1);
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_EmptyFigure)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_Line1K(benchmark::State& state)
{
    auto x = gen_x(1000);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 640, .height = 480});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_Line1K)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_Line10K(benchmark::State& state)
{
    auto x = gen_x(10000);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 640, .height = 480});
        auto& ax = fig.subplot(1, 1, 1);
        ax.line(x, y);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_Line10K)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_Scatter1K(benchmark::State& state)
{
    auto x = gen_x(1000);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 640, .height = 480});
        auto& ax = fig.subplot(1, 1, 1);
        ax.scatter(x, y);
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-1.5f, 1.5f);
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_Scatter1K)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_MultiFigure_2(benchmark::State& state)
{
    auto x = gen_x(500);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        App app({.headless = true});
        for (int i = 0; i < 2; ++i)
        {
            auto& fig = app.figure({.width = 640, .height = 480});
            auto& ax = fig.subplot(1, 1, 1);
            ax.line(x, y);
            ax.xlim(0.0f, 10.0f);
            ax.ylim(-1.5f, 1.5f);
        }
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_MultiFigure_2)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_MultiFigure_4(benchmark::State& state)
{
    auto x = gen_x(500);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        App app({.headless = true});
        for (int i = 0; i < 4; ++i)
        {
            auto& fig = app.figure({.width = 640, .height = 480});
            auto& ax = fig.subplot(1, 1, 1);
            ax.line(x, y);
            ax.xlim(0.0f, 10.0f);
            ax.ylim(-1.5f, 1.5f);
        }
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_MultiFigure_4)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_Subplot2x2(benchmark::State& state)
{
    auto x = gen_x(500);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        App app({.headless = true});
        auto& fig = app.figure({.width = 800, .height = 600});
        for (int i = 1; i <= 4; ++i)
        {
            auto& ax = fig.subplot(2, 2, i);
            ax.line(x, y);
            ax.xlim(0.0f, 10.0f);
            ax.ylim(-1.5f, 1.5f);
        }
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_Subplot2x2)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_AppCreateDestroy(benchmark::State& state)
{
    for (auto _ : state)
    {
        App app({.headless = true});
        app.figure({.width = 320, .height = 240}).subplot(1, 1, 1);
        app.run();
    }
}
BENCHMARK(BM_SingleWindow_AppCreateDestroy)->Unit(benchmark::kMillisecond);

static void BM_SingleWindow_PipelineCreation(benchmark::State& state)
{
    App app({.headless = true});
    app.figure({.width = 320, .height = 240}).subplot(1, 1, 1);
    app.run();
    auto* backend = app.backend();

    for (auto _ : state)
    {
        auto line = backend->create_pipeline(PipelineType::Line);
        auto scatter = backend->create_pipeline(PipelineType::Scatter);
        auto grid = backend->create_pipeline(PipelineType::Grid);
        benchmark::DoNotOptimize(line);
        benchmark::DoNotOptimize(scatter);
        benchmark::DoNotOptimize(grid);
    }
}
BENCHMARK(BM_SingleWindow_PipelineCreation)->Unit(benchmark::kMicrosecond);

static void BM_SingleWindow_BufferCreateDestroy(benchmark::State& state)
{
    App app({.headless = true});
    app.figure({.width = 320, .height = 240}).subplot(1, 1, 1);
    app.run();
    auto* backend = app.backend();

    for (auto _ : state)
    {
        auto buf = backend->create_buffer(BufferUsage::Storage, 4096);
        backend->destroy_buffer(buf);
    }
}
BENCHMARK(BM_SingleWindow_BufferCreateDestroy)->Unit(benchmark::kMicrosecond);

static void BM_SingleWindow_Readback(benchmark::State& state)
{
    App app({.headless = true});
    auto& fig = app.figure({.width = 640, .height = 480});
    auto& ax = fig.subplot(1, 1, 1);
    auto x = gen_x(100);
    auto y = gen_y_sin(x);
    ax.line(x, y);
    app.run();

    std::vector<uint8_t> pixels(640 * 480 * 4);
    auto* backend = app.backend();

    for (auto _ : state)
    {
        backend->readback_framebuffer(pixels.data(), 640, 480);
    }
}
BENCHMARK(BM_SingleWindow_Readback)->Unit(benchmark::kMicrosecond);

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 2 — Multi-Window Benchmarks (after Agent B merge)
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef SPECTRA_HAS_WINDOW_MANAGER

// Placeholder benchmarks — will be implemented when WindowManager exists.
// Patterns:
//   BM_MultiWindow_2Windows_Line1K
//   BM_MultiWindow_3Windows_Line1K
//   BM_MultiWindow_WindowCreateDestroy
//   BM_MultiWindow_ResizeOneWindow
//   BM_MultiWindow_FrameTimeScaling (1..8 windows)

#endif  // SPECTRA_HAS_WINDOW_MANAGER

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 2 Stub — Simulated Multi-Window (always runs)
// Uses N independent headless Apps to establish baseline overhead.
// ═══════════════════════════════════════════════════════════════════════════════

#ifndef SPECTRA_HAS_WINDOW_MANAGER

static void BM_StubMultiWindow_2Apps(benchmark::State& state)
{
    auto x = gen_x(500);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        for (int i = 0; i < 2; ++i)
        {
            App app({.headless = true});
            auto& fig = app.figure({.width = 640, .height = 480});
            auto& ax = fig.subplot(1, 1, 1);
            ax.line(x, y);
            ax.xlim(0.0f, 10.0f);
            ax.ylim(-1.5f, 1.5f);
            app.run();
        }
    }
}
BENCHMARK(BM_StubMultiWindow_2Apps)->Unit(benchmark::kMillisecond);

static void BM_StubMultiWindow_4Apps(benchmark::State& state)
{
    auto x = gen_x(500);
    auto y = gen_y_sin(x);

    for (auto _ : state)
    {
        for (int i = 0; i < 4; ++i)
        {
            App app({.headless = true});
            auto& fig = app.figure({.width = 640, .height = 480});
            auto& ax = fig.subplot(1, 1, 1);
            ax.line(x, y);
            ax.xlim(0.0f, 10.0f);
            ax.ylim(-1.5f, 1.5f);
            app.run();
        }
    }
}
BENCHMARK(BM_StubMultiWindow_4Apps)->Unit(benchmark::kMillisecond);

static void BM_StubMultiWindow_SequentialCreateDestroy(benchmark::State& state)
{
    for (auto _ : state)
    {
        for (int i = 0; i < 5; ++i)
        {
            App app({.headless = true});
            app.figure({.width = 320, .height = 240}).subplot(1, 1, 1);
            app.run();
        }
    }
}
BENCHMARK(BM_StubMultiWindow_SequentialCreateDestroy)->Unit(benchmark::kMillisecond);

#endif  // !SPECTRA_HAS_WINDOW_MANAGER

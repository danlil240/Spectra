#include <benchmark/benchmark.h>
#include <cmath>
#include <spectra/animator.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <vector>

#include "ui/animation_controller.hpp"
#include "ui/command_queue.hpp"
#include "ui/layout_manager.hpp"

using namespace spectra;

// ─── Layout update benchmarks ────────────────────────────────────────────────

static void BM_LayoutUpdate_Default(benchmark::State& state)
{
    LayoutManager lm;
    for (auto _ : state)
    {
        lm.update(1920.0f, 1080.0f, 0.016f);
        benchmark::DoNotOptimize(lm.canvas_rect());
    }
}
BENCHMARK(BM_LayoutUpdate_Default)->Unit(benchmark::kMicrosecond);

static void BM_LayoutUpdate_AllPanelsOpen(benchmark::State& state)
{
    LayoutManager lm;
    lm.set_inspector_visible(true);
    lm.set_nav_rail_expanded(true);
    lm.set_tab_bar_visible(true);
    lm.update(1920.0f, 1080.0f, 0.0f);

    for (auto _ : state)
    {
        lm.update(1920.0f, 1080.0f, 0.016f);
        benchmark::DoNotOptimize(lm.canvas_rect());
    }
}
BENCHMARK(BM_LayoutUpdate_AllPanelsOpen)->Unit(benchmark::kMicrosecond);

static void BM_LayoutUpdate_Animating(benchmark::State& state)
{
    LayoutManager lm;
    for (auto _ : state)
    {
        state.PauseTiming();
        lm.set_inspector_visible(true);
        lm.update(1920.0f, 1080.0f, 0.0f);
        lm.set_inspector_visible(false);
        state.ResumeTiming();

        lm.update(1920.0f, 1080.0f, 0.016f);
        benchmark::DoNotOptimize(lm.canvas_rect());
    }
}
BENCHMARK(BM_LayoutUpdate_Animating)->Unit(benchmark::kMicrosecond);

// ─── Animation controller benchmarks ─────────────────────────────────────────

static void BM_AnimationController_Update_1Anim(benchmark::State& state)
{
    AnimationController ctrl;
    Axes                ax;
    ax.xlim(0, 10);
    ax.ylim(0, 10);

    for (auto _ : state)
    {
        state.PauseTiming();
        ctrl.cancel_all();
        ax.xlim(0, 10);
        ax.ylim(0, 10);
        ctrl.animate_axis_limits(ax, {2, 8}, {2, 8}, 1.0f, ease::ease_out);
        state.ResumeTiming();

        ctrl.update(0.016f);
    }
}
BENCHMARK(BM_AnimationController_Update_1Anim)->Unit(benchmark::kMicrosecond);

static void BM_AnimationController_Update_50Anims(benchmark::State& state)
{
    AnimationController ctrl;
    std::vector<Axes>   axes(50);

    for (auto _ : state)
    {
        state.PauseTiming();
        ctrl.cancel_all();
        for (int i = 0; i < 50; ++i)
        {
            axes[i].xlim(0, 10);
            axes[i].ylim(0, 10);
            ctrl.animate_axis_limits(axes[i],
                                     {static_cast<float>(i), static_cast<float>(10 + i)},
                                     {static_cast<float>(i), static_cast<float>(10 + i)},
                                     1.0f,
                                     ease::ease_out);
        }
        state.ResumeTiming();

        ctrl.update(0.016f);
    }
}
BENCHMARK(BM_AnimationController_Update_50Anims)->Unit(benchmark::kMicrosecond);

static void BM_AnimationController_NoAnims(benchmark::State& state)
{
    AnimationController ctrl;
    for (auto _ : state)
    {
        ctrl.update(0.016f);
        benchmark::DoNotOptimize(ctrl.has_active_animations());
    }
}
BENCHMARK(BM_AnimationController_NoAnims)->Unit(benchmark::kNanosecond);

// ─── Nearest-point query benchmarks ──────────────────────────────────────────

namespace
{

struct NearestResult
{
    bool   found    = false;
    size_t index    = 0;
    float  distance = 1e30f;
};

NearestResult find_nearest_bench(float        cursor_x,
                                 float        cursor_y,
                                 const float* x_data,
                                 const float* y_data,
                                 size_t       count,
                                 const Rect&  viewport,
                                 float        xlim_min,
                                 float        xlim_max,
                                 float        ylim_min,
                                 float        ylim_max)
{
    NearestResult best;
    float         x_range = xlim_max - xlim_min;
    float         y_range = ylim_max - ylim_min;
    if (x_range == 0.0f)
        x_range = 1.0f;
    if (y_range == 0.0f)
        y_range = 1.0f;

    for (size_t i = 0; i < count; ++i)
    {
        float norm_x = (x_data[i] - xlim_min) / x_range;
        float norm_y = (y_data[i] - ylim_min) / y_range;
        float sx     = viewport.x + norm_x * viewport.w;
        float sy     = viewport.y + (1.0f - norm_y) * viewport.h;

        float dx   = cursor_x - sx;
        float dy   = cursor_y - sy;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < best.distance)
        {
            best.found    = true;
            best.index    = i;
            best.distance = dist;
        }
    }
    return best;
}

}   // anonymous namespace

static void BM_NearestPoint_1K(benchmark::State& state)
{
    const size_t       N = 1000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) / N * 100.0f;
        y[i] = std::sin(x[i] * 0.1f) * 50.0f + 50.0f;
    }
    Rect vp{0, 0, 1920, 1080};

    for (auto _ : state)
    {
        auto r = find_nearest_bench(960.0f, 540.0f, x.data(), y.data(), N, vp, 0, 100, 0, 100);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_NearestPoint_1K)->Unit(benchmark::kMicrosecond);

static void BM_NearestPoint_10K(benchmark::State& state)
{
    const size_t       N = 10000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) / N * 100.0f;
        y[i] = std::sin(x[i] * 0.1f) * 50.0f + 50.0f;
    }
    Rect vp{0, 0, 1920, 1080};

    for (auto _ : state)
    {
        auto r = find_nearest_bench(960.0f, 540.0f, x.data(), y.data(), N, vp, 0, 100, 0, 100);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_NearestPoint_10K)->Unit(benchmark::kMicrosecond);

static void BM_NearestPoint_100K(benchmark::State& state)
{
    const size_t       N = 100000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) / N * 100.0f;
        y[i] = std::sin(x[i] * 0.1f) * 50.0f + 50.0f;
    }
    Rect vp{0, 0, 1920, 1080};

    for (auto _ : state)
    {
        auto r = find_nearest_bench(960.0f, 540.0f, x.data(), y.data(), N, vp, 0, 100, 0, 100);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_NearestPoint_100K)->Unit(benchmark::kMicrosecond);

// ─── CommandQueue benchmarks ─────────────────────────────────────────────────

static void BM_CommandQueue_PushPop(benchmark::State& state)
{
    CommandQueue q;
    int          counter = 0;

    for (auto _ : state)
    {
        q.push([&counter]() { ++counter; });
        std::function<void()> cmd;
        q.pop(cmd);
        cmd();
    }
    benchmark::DoNotOptimize(counter);
}
BENCHMARK(BM_CommandQueue_PushPop)->Unit(benchmark::kNanosecond);

static void BM_CommandQueue_Drain_100(benchmark::State& state)
{
    CommandQueue q;
    int          counter = 0;

    for (auto _ : state)
    {
        state.PauseTiming();
        for (int i = 0; i < 100; ++i)
        {
            q.push([&counter]() { ++counter; });
        }
        state.ResumeTiming();

        q.drain();
    }
    benchmark::DoNotOptimize(counter);
}
BENCHMARK(BM_CommandQueue_Drain_100)->Unit(benchmark::kMicrosecond);

// ─── Easing function benchmarks ──────────────────────────────────────────────

static void BM_Easing_Linear(benchmark::State& state)
{
    float t = 0.5f;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(ease::linear(t));
        t += 0.001f;
        if (t > 1.0f)
            t = 0.0f;
    }
}
BENCHMARK(BM_Easing_Linear)->Unit(benchmark::kNanosecond);

static void BM_Easing_EaseOut(benchmark::State& state)
{
    float t = 0.5f;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(ease::ease_out(t));
        t += 0.001f;
        if (t > 1.0f)
            t = 0.0f;
    }
}
BENCHMARK(BM_Easing_EaseOut)->Unit(benchmark::kNanosecond);

static void BM_Easing_CubicBezier(benchmark::State& state)
{
    float t = 0.5f;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(ease::ease_out_cubic(t));
        t += 0.001f;
        if (t > 1.0f)
            t = 0.0f;
    }
}
BENCHMARK(BM_Easing_CubicBezier)->Unit(benchmark::kNanosecond);

static void BM_Easing_Spring(benchmark::State& state)
{
    float t = 0.5f;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(ease::spring(t));
        t += 0.001f;
        if (t > 1.0f)
            t = 0.0f;
    }
}
BENCHMARK(BM_Easing_Spring)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();

#include <algorithm>
#include <benchmark/benchmark.h>
#include <cmath>
#include <filesystem>
#include <memory>
#include <numeric>
#include <spectra/animator.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

#include "ui/commands/command_registry.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/figures/figure_registry.hpp"
#include "ui/commands/shortcut_manager.hpp"
#include "ui/animation/transition_engine.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;

// ─── Command Registry benchmarks ────────────────────────────────────────────

static void register_test_commands(CommandRegistry& reg, int count)
{
    for (int i = 0; i < count; ++i)
    {
        std::string id       = "cmd." + std::to_string(i);
        std::string label    = "Command Number " + std::to_string(i);
        std::string category = "Category" + std::to_string(i % 5);
        reg.register_command(
            id,
            label,
            []() {},
            "",
            category);
    }
}

static void BM_CommandRegistry_Register(benchmark::State& state)
{
    for (auto _ : state)
    {
        CommandRegistry reg;
        register_test_commands(reg, static_cast<int>(state.range(0)));
        benchmark::DoNotOptimize(reg.count());
    }
}
BENCHMARK(BM_CommandRegistry_Register)->Arg(10)->Arg(50)->Arg(200)->Unit(benchmark::kMicrosecond);

static void BM_CommandRegistry_FuzzySearch_Empty(benchmark::State& state)
{
    CommandRegistry reg;
    register_test_commands(reg, 50);

    for (auto _ : state)
    {
        auto results = reg.search("");
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(BM_CommandRegistry_FuzzySearch_Empty)->Unit(benchmark::kMicrosecond);

static void BM_CommandRegistry_FuzzySearch_Short(benchmark::State& state)
{
    CommandRegistry reg;
    register_test_commands(reg, 50);

    for (auto _ : state)
    {
        auto results = reg.search("cmd");
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(BM_CommandRegistry_FuzzySearch_Short)->Unit(benchmark::kMicrosecond);

static void BM_CommandRegistry_FuzzySearch_Specific(benchmark::State& state)
{
    CommandRegistry reg;
    register_test_commands(reg, 200);

    for (auto _ : state)
    {
        auto results = reg.search("Command Number 42");
        benchmark::DoNotOptimize(results);
    }
}
BENCHMARK(BM_CommandRegistry_FuzzySearch_Specific)->Unit(benchmark::kMicrosecond);

static void BM_CommandRegistry_Execute(benchmark::State& state)
{
    CommandRegistry reg;
    int             counter = 0;
    reg.register_command("bench.cmd", "Bench", [&counter]() { ++counter; });

    for (auto _ : state)
    {
        reg.execute("bench.cmd");
    }
    benchmark::DoNotOptimize(counter);
}
BENCHMARK(BM_CommandRegistry_Execute)->Unit(benchmark::kNanosecond);

static void BM_CommandRegistry_Find(benchmark::State& state)
{
    CommandRegistry reg;
    register_test_commands(reg, 200);

    for (auto _ : state)
    {
        auto cmd = reg.find("cmd.100");
        benchmark::DoNotOptimize(cmd);
    }
}
BENCHMARK(BM_CommandRegistry_Find)->Unit(benchmark::kNanosecond);

// ─── Shortcut Manager benchmarks ────────────────────────────────────────────

static void BM_ShortcutManager_Bind(benchmark::State& state)
{
    for (auto _ : state)
    {
        ShortcutManager mgr;
        for (int i = 0; i < 30; ++i)
        {
            mgr.bind(Shortcut{32 + i, KeyMod::Control}, "cmd." + std::to_string(i));
        }
        benchmark::DoNotOptimize(mgr.count());
    }
}
BENCHMARK(BM_ShortcutManager_Bind)->Unit(benchmark::kMicrosecond);

static void BM_ShortcutManager_Lookup(benchmark::State& state)
{
    ShortcutManager mgr;
    for (int i = 0; i < 30; ++i)
    {
        mgr.bind(Shortcut{32 + i, KeyMod::Control}, "cmd." + std::to_string(i));
    }

    for (auto _ : state)
    {
        auto id = mgr.command_for_shortcut(Shortcut{47, KeyMod::Control});
        benchmark::DoNotOptimize(id);
    }
}
BENCHMARK(BM_ShortcutManager_Lookup)->Unit(benchmark::kNanosecond);

static void BM_ShortcutManager_OnKey_Hit(benchmark::State& state)
{
    CommandRegistry reg;
    int             counter = 0;
    reg.register_command("bench.shortcut", "Bench Shortcut", [&counter]() { ++counter; });

    ShortcutManager mgr;
    mgr.set_command_registry(&reg);
    mgr.bind(Shortcut{75, KeyMod::Control}, "bench.shortcut");   // Ctrl+K

    for (auto _ : state)
    {
        mgr.on_key(75, 1, 0x02);   // GLFW_PRESS, GLFW_MOD_CONTROL
    }
    benchmark::DoNotOptimize(counter);
}
BENCHMARK(BM_ShortcutManager_OnKey_Hit)->Unit(benchmark::kNanosecond);

static void BM_ShortcutManager_OnKey_Miss(benchmark::State& state)
{
    ShortcutManager mgr;
    for (int i = 0; i < 30; ++i)
    {
        mgr.bind(Shortcut{32 + i, KeyMod::Control}, "cmd." + std::to_string(i));
    }

    for (auto _ : state)
    {
        bool hit = mgr.on_key(999, 1, 0);   // Unbound key
        benchmark::DoNotOptimize(hit);
    }
}
BENCHMARK(BM_ShortcutManager_OnKey_Miss)->Unit(benchmark::kNanosecond);

// ─── Undo Manager benchmarks ────────────────────────────────────────────────

static void BM_UndoManager_Push(benchmark::State& state)
{
    UndoManager mgr;
    int         val = 0;

    for (auto _ : state)
    {
        mgr.push(UndoAction{"change", [&val]() { --val; }, [&val]() { ++val; }});
    }
    benchmark::DoNotOptimize(val);
}
BENCHMARK(BM_UndoManager_Push)->Unit(benchmark::kNanosecond);

static void BM_UndoManager_PushUndo(benchmark::State& state)
{
    UndoManager mgr;
    int         val = 0;

    for (auto _ : state)
    {
        mgr.push(UndoAction{"change", [&val]() { --val; }, [&val]() { ++val; }});
        mgr.undo();
    }
    benchmark::DoNotOptimize(val);
}
BENCHMARK(BM_UndoManager_PushUndo)->Unit(benchmark::kNanosecond);

static void BM_UndoManager_PushUndoRedo(benchmark::State& state)
{
    UndoManager mgr;
    int         val = 0;

    for (auto _ : state)
    {
        mgr.push(UndoAction{"change", [&val]() { --val; }, [&val]() { ++val; }});
        mgr.undo();
        mgr.redo();
    }
    benchmark::DoNotOptimize(val);
}
BENCHMARK(BM_UndoManager_PushUndoRedo)->Unit(benchmark::kNanosecond);

static void BM_UndoManager_PushValue(benchmark::State& state)
{
    UndoManager mgr;
    float       val = 0.0f;

    for (auto _ : state)
    {
        mgr.push_value<float>("change", val, val + 1.0f, [&val](const float& v) { val = v; });
    }
    benchmark::DoNotOptimize(val);
}
BENCHMARK(BM_UndoManager_PushValue)->Unit(benchmark::kNanosecond);

static void BM_UndoManager_GroupedPush(benchmark::State& state)
{
    UndoManager mgr;
    int         val = 0;

    for (auto _ : state)
    {
        mgr.begin_group("group");
        for (int i = 0; i < 5; ++i)
        {
            mgr.push(UndoAction{"sub", [&val]() { --val; }, [&val]() { ++val; }});
        }
        mgr.end_group();
    }
    benchmark::DoNotOptimize(val);
}
BENCHMARK(BM_UndoManager_GroupedPush)->Unit(benchmark::kMicrosecond);

static void BM_UndoManager_StackOverflow(benchmark::State& state)
{
    // Benchmark behavior when stack is at capacity
    UndoManager mgr;
    int         val = 0;

    // Fill to capacity
    for (size_t i = 0; i < UndoManager::MAX_STACK_SIZE; ++i)
    {
        mgr.push(UndoAction{"fill", [&val]() { --val; }, [&val]() { ++val; }});
    }

    for (auto _ : state)
    {
        mgr.push(UndoAction{"overflow", [&val]() { --val; }, [&val]() { ++val; }});
    }
    benchmark::DoNotOptimize(val);
}
BENCHMARK(BM_UndoManager_StackOverflow)->Unit(benchmark::kNanosecond);

// ─── Workspace benchmarks ───────────────────────────────────────────────────

static WorkspaceData make_workspace_data(int num_figures, int series_per_fig)
{
    WorkspaceData data;
    data.theme_name                    = "dark";
    data.active_figure_index           = 0;
    data.panels.inspector_visible      = true;
    data.panels.inspector_width        = 320.0f;
    data.interaction.crosshair_enabled = true;
    data.interaction.tooltip_enabled   = true;

    for (int f = 0; f < num_figures; ++f)
    {
        WorkspaceData::FigureState fig;
        fig.title            = "Figure " + std::to_string(f + 1);
        fig.width            = 1280;
        fig.height           = 720;
        fig.grid_rows        = 1;
        fig.grid_cols        = 1;
        fig.custom_tab_title = "Tab " + std::to_string(f + 1);

        WorkspaceData::AxisState ax;
        ax.x_min   = 0.0f;
        ax.x_max   = 10.0f;
        ax.y_min   = -1.0f;
        ax.y_max   = 1.0f;
        ax.title   = "Axes " + std::to_string(f);
        ax.x_label = "X";
        ax.y_label = "Y";
        fig.axes.push_back(ax);

        for (int s = 0; s < series_per_fig; ++s)
        {
            WorkspaceData::SeriesState ss;
            ss.name        = "Series " + std::to_string(s);
            ss.type        = (s % 2 == 0) ? "line" : "scatter";
            ss.color_r     = 0.2f + 0.1f * static_cast<float>(s);
            ss.color_g     = 0.5f;
            ss.color_b     = 0.8f;
            ss.line_width  = 2.0f;
            ss.point_count = 1000;
            ss.visible     = true;
            fig.series.push_back(ss);
        }

        data.figures.push_back(fig);
    }

    return data;
}

static void BM_Workspace_SaveSmall(benchmark::State& state)
{
    auto data = make_workspace_data(1, 2);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws_small.spectra";

    for (auto _ : state)
    {
        Workspace::save(path.string(), data);
    }

    std::filesystem::remove(path);
}
BENCHMARK(BM_Workspace_SaveSmall)->Unit(benchmark::kMicrosecond);

static void BM_Workspace_SaveLarge(benchmark::State& state)
{
    auto data = make_workspace_data(10, 5);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws_large.spectra";

    for (auto _ : state)
    {
        Workspace::save(path.string(), data);
    }

    std::filesystem::remove(path);
}
BENCHMARK(BM_Workspace_SaveLarge)->Unit(benchmark::kMicrosecond);

static void BM_Workspace_LoadSmall(benchmark::State& state)
{
    auto data = make_workspace_data(1, 2);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws_load_small.spectra";
    Workspace::save(path.string(), data);

    for (auto _ : state)
    {
        WorkspaceData loaded;
        Workspace::load(path.string(), loaded);
        benchmark::DoNotOptimize(loaded.figures.size());
    }

    std::filesystem::remove(path);
}
BENCHMARK(BM_Workspace_LoadSmall)->Unit(benchmark::kMicrosecond);

static void BM_Workspace_LoadLarge(benchmark::State& state)
{
    auto data = make_workspace_data(10, 5);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws_load_large.spectra";
    Workspace::save(path.string(), data);

    for (auto _ : state)
    {
        WorkspaceData loaded;
        Workspace::load(path.string(), loaded);
        benchmark::DoNotOptimize(loaded.figures.size());
    }

    std::filesystem::remove(path);
}
BENCHMARK(BM_Workspace_LoadLarge)->Unit(benchmark::kMicrosecond);

static void BM_Workspace_RoundTrip(benchmark::State& state)
{
    auto data = make_workspace_data(5, 3);
    auto path = std::filesystem::temp_directory_path() / "spectra_bench_ws_rt.spectra";

    for (auto _ : state)
    {
        Workspace::save(path.string(), data);
        WorkspaceData loaded;
        Workspace::load(path.string(), loaded);
        benchmark::DoNotOptimize(loaded.figures.size());
    }

    std::filesystem::remove(path);
}
BENCHMARK(BM_Workspace_RoundTrip)->Unit(benchmark::kMicrosecond);

// ─── Figure Manager benchmarks ──────────────────────────────────────────────

static void BM_FigureManager_Create(benchmark::State& state)
{
    for (auto _ : state)
    {
        state.PauseTiming();
        FigureRegistry registry;
        registry.register_figure(std::make_unique<Figure>());
        FigureManager mgr(registry);
        state.ResumeTiming();

        for (int i = 0; i < static_cast<int>(state.range(0)); ++i)
        {
            mgr.create_figure();
        }
        benchmark::DoNotOptimize(mgr.count());
    }
}
BENCHMARK(BM_FigureManager_Create)->Arg(1)->Arg(5)->Arg(20)->Unit(benchmark::kMicrosecond);

static void BM_FigureManager_SwitchTab(benchmark::State& state)
{
    FigureRegistry registry;
    registry.register_figure(std::make_unique<Figure>());
    FigureManager mgr(registry);
    for (int i = 0; i < 10; ++i)
        mgr.create_figure();

    const auto& ids = mgr.figure_ids();
    size_t      pos = 0;
    for (auto _ : state)
    {
        mgr.switch_to(ids[pos]);
        pos = (pos + 1) % ids.size();
    }
    benchmark::DoNotOptimize(mgr.active_index());
}
BENCHMARK(BM_FigureManager_SwitchTab)->Unit(benchmark::kNanosecond);

static void BM_FigureManager_CycleNextPrev(benchmark::State& state)
{
    FigureRegistry registry;
    registry.register_figure(std::make_unique<Figure>());
    FigureManager mgr(registry);
    for (int i = 0; i < 10; ++i)
        mgr.create_figure();

    for (auto _ : state)
    {
        mgr.switch_to_next();
    }
    benchmark::DoNotOptimize(mgr.active_index());
}
BENCHMARK(BM_FigureManager_CycleNextPrev)->Unit(benchmark::kNanosecond);

static void BM_FigureManager_Duplicate(benchmark::State& state)
{
    for (auto _ : state)
    {
        state.PauseTiming();
        FigureRegistry registry;
        FigureId       first_id = registry.register_figure(std::make_unique<Figure>());
        FigureManager  mgr(registry);
        state.ResumeTiming();

        mgr.duplicate_figure(first_id);
        benchmark::DoNotOptimize(mgr.count());
    }
}
BENCHMARK(BM_FigureManager_Duplicate)->Unit(benchmark::kMicrosecond);

static void BM_FigureManager_ProcessPending(benchmark::State& state)
{
    FigureRegistry registry;
    registry.register_figure(std::make_unique<Figure>());
    FigureManager mgr(registry);
    for (int i = 0; i < 5; ++i)
        mgr.create_figure();

    const auto& ids       = mgr.figure_ids();
    FigureId    target_id = ids.size() > 3 ? ids[3] : ids.back();
    for (auto _ : state)
    {
        mgr.queue_switch(target_id);
        mgr.process_pending();
    }
    benchmark::DoNotOptimize(mgr.active_index());
}
BENCHMARK(BM_FigureManager_ProcessPending)->Unit(benchmark::kNanosecond);

// ─── TransitionEngine Phase 2 benchmarks ────────────────────────────────────

static void BM_TransitionEngine_AnimateFloat_10(benchmark::State& state)
{
    TransitionEngine   te;
    std::vector<float> targets(10, 0.0f);

    for (auto _ : state)
    {
        state.PauseTiming();
        te.cancel_all();
        for (auto& t : targets)
            t = 0.0f;
        for (auto& t : targets)
        {
            te.animate(t, 1.0f, 0.3f, ease::ease_out);
        }
        state.ResumeTiming();

        te.update(0.016f);
    }
}
BENCHMARK(BM_TransitionEngine_AnimateFloat_10)->Unit(benchmark::kMicrosecond);

static void BM_TransitionEngine_AnimateLimits_10(benchmark::State& state)
{
    TransitionEngine  te;
    std::vector<Axes> axes(10);
    for (auto& ax : axes)
    {
        ax.xlim(0, 10);
        ax.ylim(0, 10);
    }

    for (auto _ : state)
    {
        state.PauseTiming();
        te.cancel_all();
        for (auto& ax : axes)
        {
            ax.xlim(0, 10);
            ax.ylim(0, 10);
        }
        for (auto& ax : axes)
        {
            te.animate_limits(ax, {2, 8}, {2, 8}, 0.3f, ease::ease_out);
        }
        state.ResumeTiming();

        te.update(0.016f);
    }
}
BENCHMARK(BM_TransitionEngine_AnimateLimits_10)->Unit(benchmark::kMicrosecond);

static void BM_TransitionEngine_CancelAll(benchmark::State& state)
{
    TransitionEngine   te;
    std::vector<float> targets(50, 0.0f);

    for (auto _ : state)
    {
        state.PauseTiming();
        for (auto& t : targets)
        {
            te.animate(t, 1.0f, 1.0f, ease::ease_out);
        }
        state.ResumeTiming();

        te.cancel_all();
    }
}
BENCHMARK(BM_TransitionEngine_CancelAll)->Unit(benchmark::kMicrosecond);

// ─── Inspector statistics computation benchmarks ────────────────────────────

namespace
{

double compute_percentile_bench(const std::vector<float>& sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    if (sorted.size() == 1)
        return static_cast<double>(sorted[0]);
    double idx = p * static_cast<double>(sorted.size() - 1);
    size_t lo  = static_cast<size_t>(idx);
    size_t hi  = lo + 1;
    if (hi >= sorted.size())
        return static_cast<double>(sorted.back());
    double frac = idx - static_cast<double>(lo);
    return static_cast<double>(sorted[lo]) * (1.0 - frac) + static_cast<double>(sorted[hi]) * frac;
}

struct FullStats
{
    float y_min, y_max, y_mean, y_median, y_std;
    float p5, p25, p75, p95, iqr;
    float x_min, x_max, x_range, x_mean;
};

FullStats compute_full_stats(const float* x, const float* y, size_t n)
{
    FullStats s{};
    if (n == 0)
        return s;

    // Y stats
    std::vector<float> y_sorted(y, y + n);
    std::sort(y_sorted.begin(), y_sorted.end());

    s.y_min      = y_sorted.front();
    s.y_max      = y_sorted.back();
    double y_sum = std::accumulate(y_sorted.begin(), y_sorted.end(), 0.0);
    s.y_mean     = static_cast<float>(y_sum / static_cast<double>(n));
    s.y_median   = static_cast<float>(compute_percentile_bench(y_sorted, 0.5));

    double var_sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        double d = static_cast<double>(y[i]) - static_cast<double>(s.y_mean);
        var_sum += d * d;
    }
    s.y_std = static_cast<float>(std::sqrt(var_sum / static_cast<double>(n)));

    s.p5  = static_cast<float>(compute_percentile_bench(y_sorted, 0.05));
    s.p25 = static_cast<float>(compute_percentile_bench(y_sorted, 0.25));
    s.p75 = static_cast<float>(compute_percentile_bench(y_sorted, 0.75));
    s.p95 = static_cast<float>(compute_percentile_bench(y_sorted, 0.95));
    s.iqr = s.p75 - s.p25;

    // X stats
    s.x_min      = *std::min_element(x, x + n);
    s.x_max      = *std::max_element(x, x + n);
    s.x_range    = s.x_max - s.x_min;
    double x_sum = std::accumulate(x, x + n, 0.0);
    s.x_mean     = static_cast<float>(x_sum / static_cast<double>(n));

    return s;
}

}   // anonymous namespace

static void BM_InspectorStats_1K(benchmark::State& state)
{
    constexpr size_t   N = 1000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.01f;
        y[i] = std::sin(x[i] * 6.28f) + 0.1f * std::cos(static_cast<float>(i) * 0.37f);
    }

    for (auto _ : state)
    {
        auto s = compute_full_stats(x.data(), y.data(), N);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_InspectorStats_1K)->Unit(benchmark::kMicrosecond);

static void BM_InspectorStats_10K(benchmark::State& state)
{
    constexpr size_t   N = 10000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.001f;
        y[i] = std::sin(x[i] * 6.28f) + 0.1f * std::cos(static_cast<float>(i) * 0.37f);
    }

    for (auto _ : state)
    {
        auto s = compute_full_stats(x.data(), y.data(), N);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_InspectorStats_10K)->Unit(benchmark::kMicrosecond);

static void BM_InspectorStats_100K(benchmark::State& state)
{
    constexpr size_t   N = 100000;
    std::vector<float> x(N), y(N);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = static_cast<float>(i) * 0.0001f;
        y[i] = std::sin(x[i] * 6.28f) + 0.1f * std::cos(static_cast<float>(i) * 0.37f);
    }

    for (auto _ : state)
    {
        auto s = compute_full_stats(x.data(), y.data(), N);
        benchmark::DoNotOptimize(s);
    }
}
BENCHMARK(BM_InspectorStats_100K)->Unit(benchmark::kMicrosecond);

// ─── Sparkline downsampling benchmark ───────────────────────────────────────

static void BM_SparklineDownsample(benchmark::State& state)
{
    const size_t     N             = static_cast<size_t>(state.range(0));
    constexpr size_t MAX_SPARKLINE = 200;

    std::vector<float> data(N);
    for (size_t i = 0; i < N; ++i)
    {
        data[i] = std::sin(static_cast<float>(i) * 0.01f);
    }

    for (auto _ : state)
    {
        std::vector<float> downsampled(MAX_SPARKLINE);
        if (N <= MAX_SPARKLINE)
        {
            downsampled.assign(data.begin(), data.end());
        }
        else
        {
            float step = static_cast<float>(N) / static_cast<float>(MAX_SPARKLINE);
            for (size_t i = 0; i < MAX_SPARKLINE; ++i)
            {
                size_t idx = static_cast<size_t>(static_cast<float>(i) * step);
                if (idx >= N)
                    idx = N - 1;
                downsampled[i] = data[idx];
            }
        }
        benchmark::DoNotOptimize(downsampled);
    }
}
BENCHMARK(BM_SparklineDownsample)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();

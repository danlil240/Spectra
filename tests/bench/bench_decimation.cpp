#include <benchmark/benchmark.h>
#include <cmath>
#include <numeric>
#include <vector>

#include "data/decimation.hpp"
#include "data/filters.hpp"

// --- Helpers ---

static std::pair<std::vector<float>, std::vector<float>> make_sine(std::size_t n)
{
    std::vector<float> x(n), y(n);
    std::iota(x.begin(), x.end(), 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        y[i] = std::sin(static_cast<float>(i) * 0.001f) * 100.0f;
    return {std::move(x), std::move(y)};
}

static std::vector<float> make_noisy(std::size_t n)
{
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(i) + ((i % 3 == 0) ? 2.0f : -1.0f);
    return v;
}

// --- LTTB benchmarks ---

static void BM_LTTB_1M_to_2000(benchmark::State& state)
{
    auto [x, y] = make_sine(1'000'000);
    for (auto _ : state)
    {
        auto result = spectra::data::lttb(x, y, 2000);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 1'000'000);
}
BENCHMARK(BM_LTTB_1M_to_2000);

static void BM_LTTB_100K_to_1000(benchmark::State& state)
{
    auto [x, y] = make_sine(100'000);
    for (auto _ : state)
    {
        auto result = spectra::data::lttb(x, y, 1000);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 100'000);
}
BENCHMARK(BM_LTTB_100K_to_1000);

static void BM_LTTB_Varying(benchmark::State& state)
{
    const auto n = static_cast<std::size_t>(state.range(0));
    auto [x, y] = make_sine(n);
    const std::size_t target = std::max<std::size_t>(n / 500, 3);
    for (auto _ : state)
    {
        auto result = spectra::data::lttb(x, y, target);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_LTTB_Varying)->RangeMultiplier(10)->Range(1'000, 10'000'000);

// --- Min-max decimation benchmarks ---

static void BM_MinMax_1M_to_1000(benchmark::State& state)
{
    auto [x, y] = make_sine(1'000'000);
    for (auto _ : state)
    {
        auto result = spectra::data::min_max_decimate(x, y, 1000);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 1'000'000);
}
BENCHMARK(BM_MinMax_1M_to_1000);

// --- Resample benchmarks ---

static void BM_Resample_1M_to_2000(benchmark::State& state)
{
    auto [x, y] = make_sine(1'000'000);
    for (auto _ : state)
    {
        auto result = spectra::data::resample_uniform(x, y, 2000);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 1'000'000);
}
BENCHMARK(BM_Resample_1M_to_2000);

// --- Filter benchmarks ---

static void BM_MovingAverage_1M_W21(benchmark::State& state)
{
    auto v = make_noisy(1'000'000);
    for (auto _ : state)
    {
        auto result = spectra::data::moving_average(v, 21);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 1'000'000);
}
BENCHMARK(BM_MovingAverage_1M_W21);

static void BM_ExponentialSmoothing_1M(benchmark::State& state)
{
    auto v = make_noisy(1'000'000);
    for (auto _ : state)
    {
        auto result = spectra::data::exponential_smoothing(v, 0.1f);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 1'000'000);
}
BENCHMARK(BM_ExponentialSmoothing_1M);

static void BM_GaussianSmooth_1M_S3(benchmark::State& state)
{
    auto v = make_noisy(1'000'000);
    for (auto _ : state)
    {
        auto result = spectra::data::gaussian_smooth(v, 3.0f);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations() * 1'000'000);
}
BENCHMARK(BM_GaussianSmooth_1M_S3);

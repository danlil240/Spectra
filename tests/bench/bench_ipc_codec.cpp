#include <benchmark/benchmark.h>
#include <cstdint>
#include <vector>

#include "ipc/codec.hpp"

using namespace spectra::ipc;

// ─── Hello payload (small) ──────────────────────────────────────────────────

static void BM_IpcCodec_Hello_Encode(benchmark::State& state)
{
    HelloPayload p;
    p.protocol_major = 1;
    p.protocol_minor = 0;
    p.agent_build    = "bench-build";
    p.capabilities   = 0x01;
    p.client_type    = "python";

    for (auto _ : state)
    {
        auto buf = encode_hello(p);
        benchmark::DoNotOptimize(buf.data());
    }
}
BENCHMARK(BM_IpcCodec_Hello_Encode)->Unit(benchmark::kNanosecond);

static void BM_IpcCodec_Hello_Decode(benchmark::State& state)
{
    HelloPayload p;
    p.protocol_major = 1;
    p.protocol_minor = 0;
    p.agent_build    = "bench-build";
    p.capabilities   = 0x01;
    p.client_type    = "python";
    auto buf         = encode_hello(p);

    for (auto _ : state)
    {
        auto decoded = decode_hello(buf);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_IpcCodec_Hello_Decode)->Unit(benchmark::kNanosecond);

// ─── SetData payload (large, with float array) ─────────────────────────────

static void BM_IpcCodec_SetData_Encode(benchmark::State& state)
{
    const auto        n = static_cast<size_t>(state.range(0));
    ReqSetDataPayload p;
    p.figure_id    = 1;
    p.series_index = 0;
    p.data.resize(n);
    for (size_t i = 0; i < n; ++i)
        p.data[i] = static_cast<float>(i) * 0.1f;

    for (auto _ : state)
    {
        auto buf = encode_req_set_data(p);
        benchmark::DoNotOptimize(buf.data());
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_IpcCodec_SetData_Encode)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_IpcCodec_SetData_Decode(benchmark::State& state)
{
    const auto        n = static_cast<size_t>(state.range(0));
    ReqSetDataPayload p;
    p.figure_id    = 1;
    p.series_index = 0;
    p.data.resize(n);
    for (size_t i = 0; i < n; ++i)
        p.data[i] = static_cast<float>(i) * 0.1f;
    auto buf = encode_req_set_data(p);

    for (auto _ : state)
    {
        auto decoded = decode_req_set_data(buf);
        benchmark::DoNotOptimize(decoded);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
}
BENCHMARK(BM_IpcCodec_SetData_Decode)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// ─── UpdateProperty payload (medium, with string fields) ─────────────────────

static void BM_IpcCodec_UpdateProperty_Encode(benchmark::State& state)
{
    ReqUpdatePropertyPayload p;
    p.figure_id    = 1;
    p.axes_index   = 0;
    p.series_index = 0;
    p.property     = "color";
    p.f1           = 1.0;
    p.f2           = 0.0;
    p.f3           = 0.0;
    p.f4           = 1.0;

    for (auto _ : state)
    {
        auto buf = encode_req_update_property(p);
        benchmark::DoNotOptimize(buf.data());
    }
}
BENCHMARK(BM_IpcCodec_UpdateProperty_Encode)->Unit(benchmark::kNanosecond);

static void BM_IpcCodec_UpdateProperty_Decode(benchmark::State& state)
{
    ReqUpdatePropertyPayload p;
    p.figure_id    = 1;
    p.axes_index   = 0;
    p.series_index = 0;
    p.property     = "color";
    p.f1           = 1.0;
    p.f2           = 0.0;
    p.f3           = 0.0;
    p.f4           = 1.0;
    auto buf       = encode_req_update_property(p);

    for (auto _ : state)
    {
        auto decoded = decode_req_update_property(buf);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_IpcCodec_UpdateProperty_Decode)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();

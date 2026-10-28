// mock_lifecycle_plugin.cpp — Tiny plug-in for test_plugin_lifecycle.
//
// Registers one custom series type ("lifecycle_series") and one transform
// ("LifecycleDouble", y * 2).  Every callback counts itself in host-owned
// MockLifecycleCounters (see mock_lifecycle_counters.hpp).  The series type
// allocates per-series state and a host buffer (through the C ABI) in
// upload_fn and frees both in cleanup_fn, so a leaked or double-freed state
// shows up in live_states / live_buffers.

#include "mock_lifecycle_counters.hpp"

#include <cstddef>

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

namespace
{

MockLifecycleCounters* g_counters = nullptr;

// Stub SPIR-V: the test's fake backend never compiles it, but the host only
// creates a pipeline (and calls draw_fn) when both stages are present.
const uint8_t kStubSpirv[] = {0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00};

struct SeriesState
{
    SpectraBufferHandle buffer{};
    size_t              count = 0;
};

void bump(int32_t MockLifecycleCounters::*field)
{
    if (g_counters)
        ++(g_counters->*field);
}

int series_upload(SpectraBackendHandle backend,
                  const void* /*data*/,
                  void*  gpu_state,
                  size_t count,
                  void* /*user_data*/)
{
    bump(&MockLifecycleCounters::upload_calls);
    auto** state = static_cast<SeriesState**>(gpu_state);
    if (!*state)
    {
        *state           = new SeriesState{};
        (*state)->buffer = spectra_backend_create_buffer(backend,
                                                         SPECTRA_BUFFER_USAGE_STORAGE,
                                                         count * sizeof(float) + 16);
        bump(&MockLifecycleCounters::live_states);
        bump(&MockLifecycleCounters::live_buffers);
    }
    (*state)->count = count;
    return 0;
}

int series_draw(SpectraBackendHandle /*backend*/,
                SpectraPipelineHandle /*pipeline*/,
                const void* gpu_state,
                const SpectraViewport* /*viewport*/,
                const SpectraSeriesPushConst* /*pc*/,
                void* /*user_data*/)
{
    // Drawing a series whose state was never uploaded (or already freed)
    // would be a host bug; count it as a draw only with live state.
    auto* const* state = static_cast<SeriesState* const*>(gpu_state);
    if (state && *state && (*state)->count > 0)
        bump(&MockLifecycleCounters::draw_calls);
    return 0;
}

int series_bounds(const void* data, size_t count, SpectraRect* out, void* /*user_data*/)
{
    bump(&MockLifecycleCounters::bounds_calls);
    const auto* v = static_cast<const float*>(data);
    out->x_min    = 0.0f;
    out->x_max    = static_cast<float>(count);
    out->y_min    = count > 0 ? v[0] : 0.0f;
    out->y_max    = count > 0 ? v[count - 1] : 1.0f;
    return 0;
}

void series_cleanup(SpectraBackendHandle backend, void* gpu_state, void* /*user_data*/)
{
    bump(&MockLifecycleCounters::cleanup_calls);
    auto** state = static_cast<SeriesState**>(gpu_state);
    if (state && *state)
    {
        spectra_backend_destroy_buffer(backend, (*state)->buffer);
        delete *state;
        *state = nullptr;
        if (g_counters)
        {
            --g_counters->live_states;
            --g_counters->live_buffers;
        }
    }
}

float double_transform(float value, void* /*user_data*/)
{
    bump(&MockLifecycleCounters::transform_calls);
    return value * 2.0f;
}

}   // namespace

extern "C"
{
    SPECTRA_PLUGIN_API void mock_lifecycle_bind(MockLifecycleCounters* counters)
    {
        g_counters = counters;
    }

    SPECTRA_PLUGIN_API int spectra_plugin_init(const SpectraPluginContext* ctx,
                                               SpectraPluginInfo*          info)
    {
        if (!ctx || !info)
            return 1;
        info->name              = "MockLifecyclePlugin";
        info->version           = "1.0.0";
        info->author            = "Test";
        info->description       = "Counts every callback for the plug-in lifecycle test";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        // A reload maps a fresh copy of this module: nothing is bound yet.
        g_counters = nullptr;

        if (ctx->transform_registry)
            spectra_register_transform(ctx->transform_registry,
                                       "LifecycleDouble",
                                       double_transform,
                                       nullptr,
                                       "y * 2");

        if (spectra_plugin_api_at_least(ctx, 2, 0) && ctx->series_type_registry)
        {
            SpectraSeriesTypeDesc st{};
            st.type_name       = "lifecycle_series";
            st.vert_spirv      = kStubSpirv;
            st.vert_spirv_size = sizeof(kStubSpirv);
            st.frag_spirv      = kStubSpirv;
            st.frag_spirv_size = sizeof(kStubSpirv);
            st.topology        = SPECTRA_TOPOLOGY_TRIANGLE_LIST;
            st.upload_fn       = series_upload;
            st.draw_fn         = series_draw;
            st.bounds_fn       = series_bounds;
            st.cleanup_fn      = series_cleanup;
            if (spectra_register_series_type(ctx->series_type_registry, &st) != 0)
                return 2;
        }
        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown()
    {
        bump(&MockLifecycleCounters::shutdown_calls);
        g_counters = nullptr;
    }
}

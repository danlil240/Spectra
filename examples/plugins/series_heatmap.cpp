// series_heatmap.cpp — Example plugin: custom heatmap series type.
//
// Demonstrates how a plugin registers a fully GPU-accelerated custom series
// type using the Spectra plugin C ABI.  The heatmap renders a grid of colored
// cells whose color is mapped from data values through a viridis colormap
// implemented in the fragment shader.
//
// Build:  cmake --build build --target series_heatmap
// Load:   Place the resulting .so in ~/.config/spectra/plugins/ or load
//         manually via the Plugin Manager UI.
//
// The SPIR-V bytecodes are embedded at compile time via a generated header
// (see CMakeLists.txt for the build-time shader compilation step).

#include "ui/workspace/plugin_api.hpp"

#include <cstdlib>
#include <cstring>

// Generated at build time by CMake — contains heatmap_vert[] and heatmap_frag[].
#include "heatmap_spirv.hpp"

using namespace spectra;

// ─── Plugin GPU state ────────────────────────────────────────────────────────
//
// Each heatmap series instance gets one of these, allocated in upload_fn
// on first call and freed in cleanup_fn.

struct HeatmapGpuState
{
    SpectraBufferHandle ssbo;         // Storage buffer for cell data
    size_t              ssbo_size;    // Current allocation size in bytes
    uint32_t            cell_count;   // rows * cols for draw_instanced
};

// ─── Upload callback ─────────────────────────────────────────────────────────
//
// Data layout (float array): [cols_bits, rows_bits, min_val, max_val, v0, v1, ...]

static int heatmap_upload(SpectraBackendHandle backend,
                          const void*          series_data,
                          void*                gpu_state_ptr,
                          size_t               data_count,
                          void* /* user_data */)
{
    auto** state     = static_cast<HeatmapGpuState**>(gpu_state_ptr);
    auto*  floats    = static_cast<const float*>(series_data);
    size_t byte_size = data_count * sizeof(float);

    if (!*state)
    {
        *state = new HeatmapGpuState{};
        (*state)->ssbo =
            spectra_backend_create_buffer(backend, SPECTRA_BUFFER_USAGE_STORAGE, byte_size);
        (*state)->ssbo_size = byte_size;
    }
    else if (byte_size > (*state)->ssbo_size)
    {
        // Grow: destroy old, create new.
        spectra_backend_destroy_buffer(backend, (*state)->ssbo);
        (*state)->ssbo =
            spectra_backend_create_buffer(backend, SPECTRA_BUFFER_USAGE_STORAGE, byte_size);
        (*state)->ssbo_size = byte_size;
    }

    spectra_backend_upload_buffer(backend, (*state)->ssbo, floats, byte_size, 0);

    // Extract grid dimensions from the header.
    uint32_t cols, rows;
    std::memcpy(&cols, &floats[0], sizeof(uint32_t));
    std::memcpy(&rows, &floats[1], sizeof(uint32_t));
    (*state)->cell_count = cols * rows;

    return 0;
}

// ─── Draw callback ───────────────────────────────────────────────────────────

static int heatmap_draw(SpectraBackendHandle  backend,
                        SpectraPipelineHandle pipeline,
                        const void*           gpu_state_ptr,
                        const SpectraViewport* /* viewport */,
                        const SpectraSeriesPushConst* push_constants,
                        void* /* user_data */)
{
    auto* const* state = static_cast<HeatmapGpuState* const*>(gpu_state_ptr);
    if (!state || !*state || (*state)->cell_count == 0)
        return 0;

    spectra_backend_bind_pipeline(backend, pipeline);
    spectra_backend_bind_buffer(backend, (*state)->ssbo, 0);
    spectra_backend_push_constants(backend, push_constants);
    spectra_backend_draw_instanced(backend,
                                   6,                      // 6 vertices per quad
                                   (*state)->cell_count,   // one instance per cell
                                   0,
                                   0);
    return 0;
}

// ─── Bounds callback ─────────────────────────────────────────────────────────

static int heatmap_bounds(const void*  series_data,
                          size_t       data_count,
                          SpectraRect* bounds_out,
                          void* /* user_data */)
{
    if (data_count < 4)
        return -1;

    auto*    floats = static_cast<const float*>(series_data);
    uint32_t cols, rows;
    std::memcpy(&cols, &floats[0], sizeof(uint32_t));
    std::memcpy(&rows, &floats[1], sizeof(uint32_t));

    bounds_out->x_min = 0.0f;
    bounds_out->x_max = static_cast<float>(cols);
    bounds_out->y_min = 0.0f;
    bounds_out->y_max = static_cast<float>(rows);
    return 0;
}

// ─── Cleanup callback ────────────────────────────────────────────────────────

static void heatmap_cleanup(SpectraBackendHandle backend,
                            void*                gpu_state_ptr,
                            void* /* user_data */)
{
    auto** state = static_cast<HeatmapGpuState**>(gpu_state_ptr);
    if (state && *state)
    {
        if ((*state)->ssbo.id)
            spectra_backend_destroy_buffer(backend, (*state)->ssbo);
        delete *state;
        *state = nullptr;
    }
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "Series: Heatmap";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Custom heatmap series type rendered via instanced quads";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        if (ctx->api_version_major < 2 || !ctx->series_type_registry)
            return 0;   // Host too old — silently skip registration

        SpectraSeriesTypeDesc desc{};
        desc.type_name       = "heatmap";
        desc.flags           = SPECTRA_SERIES_FLAG_NONE;
        desc.vert_spirv      = heatmap_shaders::heatmap_vert;
        desc.vert_spirv_size = heatmap_shaders::heatmap_vert_size;
        desc.frag_spirv      = heatmap_shaders::heatmap_frag;
        desc.frag_spirv_size = heatmap_shaders::heatmap_frag_size;
        desc.topology        = 0;   // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        desc.upload_fn       = heatmap_upload;
        desc.draw_fn         = heatmap_draw;
        desc.bounds_fn       = heatmap_bounds;
        desc.cleanup_fn      = heatmap_cleanup;
        desc.user_data       = nullptr;

        return spectra_register_series_type(ctx->series_type_registry, &desc);
    }

    void spectra_plugin_shutdown()
    {
        // Nothing to clean up — per-series state is freed via cleanup_fn.
    }
}

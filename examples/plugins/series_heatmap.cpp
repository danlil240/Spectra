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

// ─── Vector export callback (API v2.2) ───────────────────────────────────────
//
// SVG exports cannot run the GPU pipeline, so the same cells are described on
// the CPU: one rectangle per cell, coloured with the fragment shader's ramp.

static void heatmap_viridis(float t, float* rgb)
{
    static const float c[5][3] = {
        {0.267f, 0.004f, 0.329f},
        {0.282f, 0.140f, 0.458f},
        {0.127f, 0.566f, 0.551f},
        {0.544f, 0.774f, 0.247f},
        {0.993f, 0.906f, 0.144f},
    };
    t       = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float s = t * 4.0f;
    int   i = static_cast<int>(s);
    if (i >= 4)
    {
        rgb[0] = c[4][0];
        rgb[1] = c[4][1];
        rgb[2] = c[4][2];
        return;
    }
    float f = s - static_cast<float>(i);
    for (int k = 0; k < 3; ++k)
        rgb[k] = c[i][k] + (c[i + 1][k] - c[i][k]) * f;
}

static int heatmap_export_vector(const void*              series_data,
                                 size_t                   data_count,
                                 const SpectraVectorSink* sink,
                                 void* /* user_data */)
{
    if (!sink || !sink->fill_rect || data_count < 4)
        return -1;
    auto*    floats = static_cast<const float*>(series_data);
    uint32_t cols, rows;
    std::memcpy(&cols, &floats[0], sizeof(uint32_t));
    std::memcpy(&rows, &floats[1], sizeof(uint32_t));
    if (cols == 0 || rows == 0 || static_cast<size_t>(cols) * rows > data_count - 4)
        return -1;
    const float min_val = floats[2];
    const float range   = floats[3] - min_val;
    for (uint32_t r = 0; r < rows; ++r)
    {
        for (uint32_t col = 0; col < cols; ++col)
        {
            const float raw = floats[4 + static_cast<size_t>(r) * cols + col];
            float       rgba[4];
            heatmap_viridis(range > 0.0f ? (raw - min_val) / range : 0.5f, rgba);
            rgba[3] = sink->opacity;
            sink->fill_rect(sink->host,
                            static_cast<float>(col),
                            static_cast<float>(r),
                            static_cast<float>(col + 1),
                            static_cast<float>(r + 1),
                            rgba);
        }
    }
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
    SPECTRA_PLUGIN_API int spectra_plugin_init(const SpectraPluginContext* ctx,
                                               SpectraPluginInfo*          info)
    {
        info->name              = "Series: Heatmap";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Custom heatmap series type rendered via instanced quads";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        if (!spectra_plugin_api_at_least(ctx, 2, 0) || !ctx->series_type_registry)
            return 0;   // Host too old — silently skip registration

        SpectraSeriesTypeDesc desc{};
        desc.type_name       = "heatmap";
        desc.flags           = SPECTRA_SERIES_FLAG_NONE;
        desc.vert_spirv      = heatmap_shaders::heatmap_vert;
        desc.vert_spirv_size = heatmap_shaders::heatmap_vert_size;
        desc.frag_spirv      = heatmap_shaders::heatmap_frag;
        desc.frag_spirv_size = heatmap_shaders::heatmap_frag_size;
        desc.topology        = SPECTRA_TOPOLOGY_TRIANGLE_LIST;   // 6 vertices = 2 triangles
        desc.upload_fn       = heatmap_upload;
        desc.draw_fn         = heatmap_draw;
        desc.bounds_fn       = heatmap_bounds;
        desc.cleanup_fn      = heatmap_cleanup;
        desc.user_data       = nullptr;

        const int rc = spectra_register_series_type(ctx->series_type_registry, &desc);
        if (rc != 0)
            return rc;

        // Optional: lets SVG exports draw the cells (hosts older than v2.2 lack it).
        if (spectra_plugin_api_at_least(ctx, 2, 2))
            spectra_register_series_vector_export(ctx->series_type_registry,
                                                  "heatmap",
                                                  heatmap_export_vector,
                                                  nullptr);
        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown()
    {
        // Nothing to clean up — per-series state is freed via cleanup_fn.
    }
}

// transform_smooth.cpp — Example Spectra plugin: moving-average smoothing transform
//
// Build as a shared library:
//   g++ -shared -fPIC -std=c++20 -o libtransform_smooth.so transform_smooth.cpp
//       -I<spectra>/include -I<spectra>/src
//
// Place the resulting .so in ~/.config/spectra/plugins/ or load via View → Plugins.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

#include <algorithm>
#include <cstddef>

using namespace spectra;

// ─── Smoothing state ─────────────────────────────────────────────────────────

struct SmoothConfig
{
    size_t window_size = 5;
};

static SmoothConfig g_config;

// ─── Moving-average XY transform ─────────────────────────────────────────────
//
// Applies a simple moving average to Y values. X values pass through unchanged.
// The first (window_size - 1) output values use a smaller window (expanding
// window), so the output length equals the input length.

static void smooth_xy(const float* x_in,
                      const float* y_in,
                      size_t       count,
                      float*       x_out,
                      float*       y_out,
                      size_t*      out_count,
                      void*        user_data)
{
    auto*  cfg  = static_cast<SmoothConfig*>(user_data);
    size_t wlen = std::min(cfg->window_size, count);

    *out_count = count;

    // Copy X unchanged
    for (size_t i = 0; i < count; ++i)
        x_out[i] = x_in[i];

    // Expanding-window moving average
    float running_sum = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        running_sum += y_in[i];
        if (i >= wlen)
            running_sum -= y_in[i - wlen];

        size_t actual_window = std::min(i + 1, wlen);
        y_out[i]             = running_sum / static_cast<float>(actual_window);
    }
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    SPECTRA_PLUGIN_API int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        // Fill plugin info
        info->name              = "Transform: Moving Average";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Registers a moving-average smoothing XY transform";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        // Register the smoothing transform (requires API v1.1+)
        if (ctx->api_version_minor >= 1 && ctx->transform_registry)
        {
            spectra_register_xy_transform(ctx->transform_registry,
                                          "Moving Average (5-pt)",
                                          smooth_xy,
                                          &g_config,
                                          "5-point moving-average smoothing");
        }

        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown()
    {
        // Nothing to clean up
    }
}

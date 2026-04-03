// mock_transform_plugin.cpp — Test-only plugin that registers scalar + XY transforms.
// Built as a shared library and loaded by test_plugin_transforms.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

#include <cstddef>

using namespace spectra;

// ─── Scalar transform: doubles each value ────────────────────────────────────

static float double_value(float v, void* /* user_data */)
{
    return v * 2.0f;
}

// ─── XY transform: reverses data order ───────────────────────────────────────

static void reverse_xy(const float* x_in,
                       const float* y_in,
                       size_t       count,
                       float*       x_out,
                       float*       y_out,
                       size_t*      out_count,
                       void* /* user_data */)
{
    *out_count = count;
    for (size_t i = 0; i < count; ++i)
    {
        x_out[i] = x_in[count - 1 - i];
        y_out[i] = y_in[count - 1 - i];
    }
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "MockTransformPlugin";
        info->version           = "1.0.0";
        info->author            = "Test";
        info->description       = "Mock plugin for testing transform registration";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        if (ctx->transform_registry)
        {
            spectra_register_transform(ctx->transform_registry,
                                       "PluginDouble",
                                       double_value,
                                       nullptr,
                                       "Doubles values");

            spectra_register_xy_transform(ctx->transform_registry,
                                          "PluginReverse",
                                          reverse_xy,
                                          nullptr,
                                          "Reverses data order");
        }

        return 0;
    }

    void spectra_plugin_shutdown() {}
}

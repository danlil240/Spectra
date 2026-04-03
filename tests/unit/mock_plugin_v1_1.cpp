// mock_plugin_v1_1.cpp — v1.1 plugin that registers commands and transforms.
// Used by test_plugin_version_negotiation.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

static float triple_value(float v, void* /* user_data */)
{
    return v * 3.0f;
}

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "MockPluginV1_1";
        info->version           = "1.1.0";
        info->author            = "Test";
        info->description       = "v1.1 plugin with transforms";
        info->api_version_major = 2;
        info->api_version_minor = 1;

        // v1.1 plugin uses transform_registry
        if (ctx->transform_registry)
        {
            spectra_register_transform(ctx->transform_registry,
                                       "PluginTriple",
                                       triple_value,
                                       nullptr,
                                       "Triples values");
        }

        return 0;
    }

    void spectra_plugin_shutdown(void) {}
}

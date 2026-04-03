// mock_plugin_v1_0.cpp — v1.0 plugin that only registers commands (no transforms).
// Used by test_plugin_version_negotiation.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "MockPluginV1_0";
        info->version           = "1.0.0";
        info->author            = "Test";
        info->description       = "v1.0 command-only plugin";
        info->api_version_major = 2;
        info->api_version_minor = 0;

        // v1.0 plugin only uses command_registry — ignores transform_registry
        if (ctx->command_registry)
        {
            SpectraCommandDesc desc{};
            desc.id       = "mock_v1_0.hello";
            desc.label    = "Hello v1.0";
            desc.category = "Test";
            desc.callback = nullptr;   // no callback — just test registration
            spectra_register_command(ctx->command_registry, &desc);
        }

        return 0;
    }

    void spectra_plugin_shutdown(void) {}
}

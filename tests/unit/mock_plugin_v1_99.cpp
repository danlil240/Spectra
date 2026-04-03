// mock_plugin_v1_99.cpp — Plugin requesting a future API minor version.
// Used by test_plugin_version_negotiation to verify the unsupported version warning.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "MockPluginV1_99";
        info->version           = "9.0.0";
        info->author            = "Test";
        info->description       = "Plugin from the future";
        info->api_version_major = 2;
        info->api_version_minor = 99;

        // Uses only base features (commands) even though it claims v1.99
        if (ctx->command_registry)
        {
            SpectraCommandDesc desc{};
            desc.id       = "mock_v1_99.future";
            desc.label    = "Future Feature";
            desc.category = "Test";
            desc.callback = nullptr;   // no callback — just test registration
            spectra_register_command(ctx->command_registry, &desc);
        }

        return 0;
    }

    void spectra_plugin_shutdown(void) {}
}

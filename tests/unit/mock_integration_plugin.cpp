// mock_integration_plugin.cpp — Registers transform + overlay + export together.

#include <cstdio>

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

static SpectraTransformRegistry    g_transform_registry = nullptr;
static SpectraOverlayRegistry      g_overlay_registry   = nullptr;
static SpectraExportFormatRegistry g_export_registry    = nullptr;

static float integration_shift(float value, void* /*user_data*/)
{
    return value + 1.0f;
}

static void integration_overlay(const SpectraOverlayContext* ctx, void* /*user_data*/)
{
    if (!ctx)
        return;

    spectra_overlay_draw_circle(ctx, ctx->mouse_x, ctx->mouse_y, 4.0f, 0xFF00FFFF, 1.0f, 16);
}

static int integration_export(const SpectraExportContext* ctx, void* /*user_data*/)
{
    if (!ctx || !ctx->output_path)
        return 1;

    FILE* f = std::fopen(ctx->output_path, "w");
    if (!f)
        return 1;

    const int rc = std::fprintf(f, "integration-export,%zu\n", ctx->figure_json_len);
    std::fclose(f);
    return rc > 0 ? 0 : 1;
}

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        if (!ctx || !info)
            return 1;

        info->name              = "MockIntegrationPlugin";
        info->version           = "1.0.0";
        info->author            = "Test";
        info->description       = "Registers transform, overlay, and export format";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        g_transform_registry = ctx->transform_registry;
        g_overlay_registry   = ctx->overlay_registry;
        g_export_registry    = ctx->export_format_registry;

        if (g_transform_registry)
        {
            spectra_register_transform(g_transform_registry,
                                       "IntegrationShift",
                                       integration_shift,
                                       nullptr,
                                       "Adds +1 to values");
        }

        if (g_overlay_registry)
        {
            spectra_register_overlay(g_overlay_registry,
                                     "IntegrationOverlay",
                                     integration_overlay,
                                     nullptr);
        }

        if (g_export_registry)
        {
            spectra_register_export_format(g_export_registry,
                                           "IntegrationExport",
                                           "ix",
                                           integration_export,
                                           nullptr);
        }

        return 0;
    }

    void spectra_plugin_shutdown()
    {
        if (g_export_registry)
        {
            spectra_unregister_export_format(g_export_registry, "IntegrationExport");
            g_export_registry = nullptr;
        }

        if (g_overlay_registry)
        {
            spectra_unregister_overlay(g_overlay_registry, "IntegrationOverlay");
            g_overlay_registry = nullptr;
        }

        if (g_transform_registry)
        {
            spectra_unregister_transform(g_transform_registry, "IntegrationShift");
            g_transform_registry = nullptr;
        }
    }
}

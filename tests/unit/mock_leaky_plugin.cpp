// mock_leaky_plugin.cpp — Registers one extension of every kind and never
// unregisters any of them (its shutdown is empty), like the shipped examples.
// Loaded by test_plugin_unload_cleanup to check that PluginManager removes and
// gates all of a plugin's registrations itself.
//
// The "leaky.register_late" command registers a second overlay after init, to
// check that registrations made outside spectra_plugin_init are tracked too.
//
// Built twice: mock_leaky_plugin (MOCK_LEAKY_TAG "") and mock_leaky_plugin_b
// (MOCK_LEAKY_TAG "B", MOCK_LEAKY_SHIFT 10): a second, independent third-party
// plugin whose names do not collide, for test_plugin_concurrent.

#include <cstddef>

#include "ui/workspace/plugin_api.hpp"

#ifndef MOCK_LEAKY_TAG
    #define MOCK_LEAKY_TAG ""
#endif
#ifndef MOCK_LEAKY_SHIFT
    #define MOCK_LEAKY_SHIFT 1.0f
#endif
#define LEAKY_NAME(base) base MOCK_LEAKY_TAG

using namespace spectra;

namespace
{

SpectraOverlayRegistry g_overlay_registry = nullptr;

int  g_overlay_draws   = 0;
int  g_command_runs    = 0;
int  g_export_calls    = 0;
int  g_transform_calls = 0;
bool g_source_running  = false;

void ping_command(void* /*user_data*/)
{
    ++g_command_runs;
}

void count_overlay(const SpectraOverlayContext* /*ctx*/, void* /*user_data*/)
{
    ++g_overlay_draws;
}

void register_late_command(void* /*user_data*/)
{
    if (g_overlay_registry)
        spectra_register_overlay(g_overlay_registry,
                                 LEAKY_NAME("LeakyLateOverlay"),
                                 count_overlay,
                                 nullptr);
}

float shift_transform(float value, void* /*user_data*/)
{
    ++g_transform_calls;
    return value + MOCK_LEAKY_SHIFT;
}

int count_export(const SpectraExportContext* /*ctx*/, void* /*user_data*/)
{
    ++g_export_calls;
    return 0;
}

void source_start(void* /*user_data*/)
{
    g_source_running = true;
}

void source_stop(void* /*user_data*/)
{
    g_source_running = false;
}

int source_is_running(void* /*user_data*/)
{
    return g_source_running ? 1 : 0;
}

size_t source_poll(SpectraDataPoint* out, size_t max_points, void* /*user_data*/)
{
    if (max_points == 0)
        return 0;
    out[0].series_label = LEAKY_NAME("leaky");
    out[0].timestamp    = 1.0;
    out[0].value        = 42.0f;
    return 1;
}

int series_upload(SpectraBackendHandle, const void*, void*, size_t, void*)
{
    return 0;
}

int series_draw(SpectraBackendHandle,
                SpectraPipelineHandle,
                const void*,
                const SpectraViewport*,
                const SpectraSeriesPushConst*,
                void*)
{
    return 0;
}

int series_bounds(const void*, size_t, SpectraRect* out, void*)
{
    out->x_min = 0.0f;
    out->x_max = 1.0f;
    out->y_min = 0.0f;
    out->y_max = 1.0f;
    return 0;
}

void undo_noop(void* /*user_data*/) {}

}   // namespace

extern "C"
{
    SPECTRA_PLUGIN_API int mock_leaky_overlay_draws()
    {
        return g_overlay_draws;
    }

    SPECTRA_PLUGIN_API int mock_leaky_command_runs()
    {
        return g_command_runs;
    }

    SPECTRA_PLUGIN_API int mock_leaky_export_calls()
    {
        return g_export_calls;
    }

    SPECTRA_PLUGIN_API int mock_leaky_transform_calls()
    {
        return g_transform_calls;
    }

    SPECTRA_PLUGIN_API int mock_leaky_source_running()
    {
        return g_source_running ? 1 : 0;
    }

    SPECTRA_PLUGIN_API int spectra_plugin_init(const SpectraPluginContext* ctx,
                                               SpectraPluginInfo*          info)
    {
        if (!ctx || !info)
            return 1;

        info->name              = LEAKY_NAME("MockLeakyPlugin");
        info->version           = "1.0.0";
        info->author            = "Test";
        info->description       = "Registers every extension kind and never unregisters";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        g_overlay_registry = ctx->overlay_registry;

        if (ctx->command_registry)
        {
            SpectraCommandDesc ping{};
            ping.id       = LEAKY_NAME("leaky.ping");
            ping.label    = "Leaky Ping";
            ping.callback = ping_command;
            spectra_register_command(ctx->command_registry, &ping);

            SpectraCommandDesc late{};
            late.id       = LEAKY_NAME("leaky.register_late");
            late.label    = "Leaky Register Late";
            late.callback = register_late_command;
            spectra_register_command(ctx->command_registry, &late);
        }

        if (ctx->undo_manager)
            spectra_push_undo(ctx->undo_manager,
                              LEAKY_NAME("Leaky edit"),
                              undo_noop,
                              nullptr,
                              undo_noop,
                              nullptr);

        if (ctx->transform_registry)
            spectra_register_transform(ctx->transform_registry,
                                       LEAKY_NAME("LeakyShift"),
                                       shift_transform,
                                       nullptr,
                                       "y + 1");

        if (ctx->overlay_registry)
            spectra_register_overlay(ctx->overlay_registry,
                                     LEAKY_NAME("LeakyOverlay"),
                                     count_overlay,
                                     nullptr);

        if (ctx->export_format_registry)
            spectra_register_export_format(ctx->export_format_registry,
                                           LEAKY_NAME("LeakyExport"),
                                           LEAKY_NAME("lk"),
                                           count_export,
                                           nullptr);

        if (ctx->data_source_registry)
        {
            SpectraDataSourceDesc src{};
            src.name          = LEAKY_NAME("LeakySource");
            src.start_fn      = source_start;
            src.stop_fn       = source_stop;
            src.is_running_fn = source_is_running;
            src.poll_fn       = source_poll;
            spectra_register_data_source(ctx->data_source_registry, &src);
        }

        if (ctx->series_type_registry)
        {
            SpectraSeriesTypeDesc st{};
            st.type_name = LEAKY_NAME("leaky_series");
            st.upload_fn = series_upload;
            st.draw_fn   = series_draw;
            st.bounds_fn = series_bounds;
            spectra_register_series_type(ctx->series_type_registry, &st);
        }

        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown()
    {
        // Deliberately unregisters nothing.
        g_overlay_registry = nullptr;
    }
}

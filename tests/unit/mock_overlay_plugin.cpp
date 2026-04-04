// mock_overlay_plugin.cpp — Test-only plugin that registers an overlay callback.
// Built as a shared library and loaded by test_plugin_overlays.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

// ─── Overlay callback: draws a simple crosshair at mouse position ────────────
// In tests this is called with draw_list == nullptr, so the draw helpers are
// no-ops — but we still exercise the registration and invocation path.

static SpectraOverlayRegistry g_overlay_registry = nullptr;

static void mock_crosshair_draw(const SpectraOverlayContext* ctx, void* /* user_data */)
{
    if (!ctx)
        return;

    // Vertical line through mouse X
    spectra_overlay_draw_line(ctx,
                              ctx->mouse_x,
                              ctx->viewport_y,
                              ctx->mouse_x,
                              ctx->viewport_y + ctx->viewport_h,
                              0xFFFFFFFF,
                              1.0f);

    // Horizontal line through mouse Y
    spectra_overlay_draw_line(ctx,
                              ctx->viewport_x,
                              ctx->mouse_y,
                              ctx->viewport_x + ctx->viewport_w,
                              ctx->mouse_y,
                              0xFFFFFFFF,
                              1.0f);
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    SPECTRA_PLUGIN_API int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "MockOverlayPlugin";
        info->version           = "1.0.0";
        info->author            = "Test";
        info->description       = "Mock plugin for testing overlay registration";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        if (ctx->overlay_registry)
        {
            g_overlay_registry = ctx->overlay_registry;
            spectra_register_overlay(ctx->overlay_registry,
                                     "MockCrosshair",
                                     mock_crosshair_draw,
                                     nullptr);
        }

        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown()
    {
        if (g_overlay_registry)
        {
            spectra_unregister_overlay(g_overlay_registry, "MockCrosshair");
            g_overlay_registry = nullptr;
        }
    }
}

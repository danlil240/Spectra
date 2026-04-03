// overlay_crosshair.cpp — Example plugin: crosshair overlay with coordinate readout.
//
// When loaded, this plugin draws a crosshair at the mouse position on every
// hovered axes viewport.  The crosshair consists of two dashed lines
// (vertical + horizontal) and a text label showing the pixel coordinates.
//
// Build:  cmake --build build --target overlay_crosshair
// Load:   Place the resulting .so in ~/.config/spectra/plugins/ or load
//         manually via the Plugin Manager UI.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

#include <cstdio>

// ─── Configuration ───────────────────────────────────────────────────────────

static constexpr uint32_t kLineColor   = 0xCCFFFFFF;   // white, ~80% alpha
static constexpr uint32_t kTextColor   = 0xFFFFFF00;   // yellow, full alpha
static constexpr float    kThickness   = 1.0f;
static constexpr float    kTextOffsetX = 8.0f;
static constexpr float    kTextOffsetY = -16.0f;

// ─── Overlay draw callback ──────────────────────────────────────────────────

static void crosshair_draw(const SpectraOverlayContext* ctx, void* /* user_data */)
{
    if (!ctx || !ctx->is_hovered)
        return;

    const float vx = ctx->viewport_x;
    const float vy = ctx->viewport_y;
    const float vw = ctx->viewport_w;
    const float vh = ctx->viewport_h;
    const float mx = ctx->mouse_x;
    const float my = ctx->mouse_y;

    // Vertical line through mouse X (full viewport height)
    spectra_overlay_draw_line(ctx, mx, vy, mx, vy + vh, kLineColor, kThickness);

    // Horizontal line through mouse Y (full viewport width)
    spectra_overlay_draw_line(ctx, vx, my, vx + vw, my, kLineColor, kThickness);

    // Small highlight circle at the intersection
    spectra_overlay_draw_circle(ctx, mx, my, 4.0f, kLineColor, kThickness, 12);

    // Coordinate readout label — pixel position relative to viewport origin
    char label[64];
    std::snprintf(label, sizeof(label), "(%.0f, %.0f)", mx - vx, my - vy);

    spectra_overlay_draw_text(ctx, mx + kTextOffsetX, my + kTextOffsetY, label, kTextColor);
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    int spectra_plugin_init(const SpectraPluginContext* ctx, SpectraPluginInfo* info)
    {
        info->name              = "Overlay: Crosshair";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Draws a crosshair with coordinate readout at the mouse position";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        if (ctx->api_version_minor >= 2 && ctx->overlay_registry)
        {
            spectra_register_overlay(ctx->overlay_registry, "Crosshair", crosshair_draw, nullptr);
        }

        return 0;
    }

    void spectra_plugin_shutdown()
    {
        // Nothing to clean up
    }
}

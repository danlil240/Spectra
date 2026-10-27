// overlay_crosshair.cpp — Example plugin: crosshair overlay with coordinate readout.
//
// When loaded, this plugin draws a crosshair at the mouse position on every
// hovered axes viewport.  The crosshair consists of two lines (vertical +
// horizontal) and a label showing the pixel coordinates.  On API v2.1+ hosts it
// uses the host theme colours and UI scale, and the label always stays inside
// the axes.
//
// Build:  cmake --build build --target overlay_crosshair
// Load:   Place the resulting .so in ~/.config/spectra/plugins/ or load
//         manually via the Plugin Manager UI.

#include "ui/workspace/plugin_api.hpp"

using namespace spectra;

#include <cstddef>
#include <cstdio>
#include <cstring>

// ─── Configuration ───────────────────────────────────────────────────────────
// Sizes are at 100% UI scale; they are multiplied by SpectraOverlayContext::ui_scale.

static constexpr float kThickness    = 1.0f;
static constexpr float kMarkerRadius = 4.0f;
static constexpr float kLabelGap     = 8.0f;   // cursor → label distance
static constexpr float kLabelPadX    = 6.0f;   // text inset inside the label box
static constexpr float kLabelPadY    = 3.0f;
static constexpr float kEdgeInset    = 2.0f;   // keep the label off the axes border

// Fallback appearance for hosts older than API v2.1 (no theme in the context):
// a mid grey that is visible on both light and dark canvases, with the label
// drawn as light text on a dark box.  Colours use SPECTRA_OVERLAY_COLOR, which
// packs red into the low byte as the draw helpers expect.
static constexpr uint32_t kFallbackLine     = SPECTRA_OVERLAY_COLOR(128, 128, 128, 204);
static constexpr uint32_t kFallbackText     = SPECTRA_OVERLAY_COLOR(240, 240, 240, 255);
static constexpr uint32_t kFallbackLabelBg  = SPECTRA_OVERLAY_COLOR(32, 32, 32, 220);
static constexpr uint32_t kFallbackLabelBdr = SPECTRA_OVERLAY_COLOR(128, 128, 128, 160);

// Set in spectra_plugin_init: the host fills the v2.1 appearance fields
// (theme, ui_scale, font_size, measure_text) only from API v2.1 on.
static bool g_host_has_overlay_appearance = false;

static bool host_api_at_least(const SpectraPluginContext* ctx, uint32_t major, uint32_t minor)
{
    if (ctx->api_version_major != major)
        return ctx->api_version_major > major;
    return ctx->api_version_minor >= minor;
}

static bool has_appearance_fields(const SpectraOverlayContext* ctx)
{
    return g_host_has_overlay_appearance
           && ctx->struct_size
                  >= offsetof(SpectraOverlayContext, measure_text) + sizeof(ctx->measure_text);
}

// ─── Overlay draw callback ──────────────────────────────────────────────────

static void crosshair_draw(const SpectraOverlayContext* ctx, void* /* user_data */)
{
    if (!ctx || !ctx->is_hovered)
        return;

    float    scale     = 1.0f;
    float    font_size = 13.0f;
    uint32_t line_col  = kFallbackLine;
    uint32_t text_col  = kFallbackText;
    uint32_t label_bg  = kFallbackLabelBg;
    uint32_t label_bdr = kFallbackLabelBdr;
    bool     have_ext  = has_appearance_fields(ctx);
    if (have_ext)
    {
        if (ctx->ui_scale > 0.0f)
            scale = ctx->ui_scale;
        font_size = ctx->font_size > 0.0f ? ctx->font_size : font_size * scale;
        if (ctx->flags & SPECTRA_OVERLAY_FLAG_HAS_THEME)
        {
            line_col  = ctx->theme.crosshair;
            text_col  = ctx->theme.text;
            label_bg  = spectra_overlay_color_scale_alpha(ctx->theme.label_bg, 0.92f);
            label_bdr = ctx->theme.label_border;
        }
    }

    const float vx = ctx->viewport_x;
    const float vy = ctx->viewport_y;
    const float vw = ctx->viewport_w;
    const float vh = ctx->viewport_h;
    const float mx = ctx->mouse_x;
    const float my = ctx->mouse_y;

    const float thickness = kThickness * scale;

    // Vertical line through mouse X (full viewport height)
    spectra_overlay_draw_line(ctx, mx, vy, mx, vy + vh, line_col, thickness);

    // Horizontal line through mouse Y (full viewport width)
    spectra_overlay_draw_line(ctx, vx, my, vx + vw, my, line_col, thickness);

    // Small highlight circle at the intersection
    spectra_overlay_draw_circle(ctx, mx, my, kMarkerRadius * scale, line_col, thickness, 12);

    // Coordinate readout label — pixel position relative to viewport origin
    char label[64];
    std::snprintf(label, sizeof(label), "(%.0f, %.0f)", mx - vx, my - vy);

    float text_w = 0.0f;
    float text_h = 0.0f;
    if (have_ext && ctx->measure_text)
        ctx->measure_text(ctx, label, &text_w, &text_h);
    if (text_w <= 0.0f || text_h <= 0.0f)
    {
        // Rough estimate for hosts that cannot measure text.
        text_w = static_cast<float>(std::strlen(label)) * font_size * 0.6f;
        text_h = font_size;
    }

    const float pad_x = kLabelPadX * scale;
    const float pad_y = kLabelPadY * scale;
    const float box_w = text_w + 2.0f * pad_x;
    const float box_h = text_h + 2.0f * pad_y;

    // Keep the whole label box inside the axes, flipping it to the other side of
    // the cursor near the right/top edges.
    float box_x = 0.0f;
    float box_y = 0.0f;
    spectra_overlay_place_label(ctx,
                                mx,
                                my,
                                box_w,
                                box_h,
                                kLabelGap * scale,
                                kEdgeInset * scale,
                                &box_x,
                                &box_y);

    spectra_overlay_draw_rect_filled(ctx, box_x, box_y, box_w, box_h, label_bg);
    spectra_overlay_draw_rect(ctx, box_x, box_y, box_w, box_h, label_bdr, thickness);
    spectra_overlay_draw_text(ctx, box_x + pad_x, box_y + pad_y, label, text_col);
}

// ─── Plugin entry point ──────────────────────────────────────────────────────

extern "C"
{
    SPECTRA_PLUGIN_API int spectra_plugin_init(const SpectraPluginContext* ctx,
                                               SpectraPluginInfo*          info)
    {
        info->name              = "Overlay: Crosshair";
        info->version           = "1.0.0";
        info->author            = "Spectra Examples";
        info->description       = "Draws a crosshair with coordinate readout at the mouse position";
        info->api_version_major = SPECTRA_PLUGIN_API_VERSION_MAJOR;
        info->api_version_minor = SPECTRA_PLUGIN_API_VERSION_MINOR;

        g_host_has_overlay_appearance = host_api_at_least(ctx, 2, 1);

        if (ctx->api_version_minor >= 2 && ctx->overlay_registry)
        {
            spectra_register_overlay(ctx->overlay_registry, "Crosshair", crosshair_draw, nullptr);
        }

        return 0;
    }

    SPECTRA_PLUGIN_API void spectra_plugin_shutdown()
    {
        // Nothing to clean up
    }
}

#include <spectra/theme_api.hpp>

#include "ui/theme/theme.hpp"

namespace spectra
{

static ThemeChangeCallback g_theme_callback = nullptr;

static void fill_rgba(float out[4], const ui::Color& c)
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

uint32_t get_theme_snapshot(ThemeSnapshot& out)
{
    auto& tm     = ui::ThemeManager::instance();
    auto& colors = tm.colors();
    out.version  = tm.theme_version();

    fill_rgba(out.bg_canvas, colors.bg_canvas);
    fill_rgba(out.bg_primary, colors.bg_primary);
    fill_rgba(out.bg_secondary, colors.bg_secondary);
    fill_rgba(out.bg_elevated, colors.bg_elevated);

    fill_rgba(out.text_primary, colors.text_primary);
    fill_rgba(out.text_secondary, colors.text_secondary);

    fill_rgba(out.border_default, colors.border_default);
    fill_rgba(out.border_subtle, colors.border_subtle);

    fill_rgba(out.accent, colors.accent);
    fill_rgba(out.accent_hover, colors.accent_hover);

    fill_rgba(out.success, colors.success);
    fill_rgba(out.warning, colors.warning);
    fill_rgba(out.error, colors.error);

    fill_rgba(out.grid_major, colors.grid_major);
    fill_rgba(out.grid_minor, colors.grid_minor);
    fill_rgba(out.axis_line, colors.axis_line);
    fill_rgba(out.crosshair, colors.crosshair);

    return out.version;
}

void register_theme_change_callback(ThemeChangeCallback callback)
{
    g_theme_callback = callback;
}

}   // namespace spectra

#pragma once

#include <cstddef>

namespace spectra
{

// ─── Colormap type ──────────────────────────────────────────────────────────

enum class ColormapType
{
    None = 0,
    Viridis,
    Plasma,
    Inferno,
    Magma,
    Jet,
    Coolwarm,
    Grayscale,
};

struct Color
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}
};

inline constexpr Color rgb(float r, float g, float b)
{
    return Color{r, g, b, 1.0f};
}

inline constexpr Color rgba(float r, float g, float b, float a)
{
    return Color{r, g, b, a};
}
namespace colors
{
inline constexpr Color black{0.0f, 0.0f, 0.0f};
inline constexpr Color white{1.0f, 1.0f, 1.0f};
inline constexpr Color red{1.0f, 0.0f, 0.0f};
inline constexpr Color green{0.0f, 1.0f, 0.0f};
inline constexpr Color blue{0.0f, 0.0f, 1.0f};
inline constexpr Color cyan{0.0f, 1.0f, 1.0f};
inline constexpr Color magenta{1.0f, 0.0f, 1.0f};
inline constexpr Color yellow{1.0f, 1.0f, 0.0f};
inline constexpr Color orange{1.0f, 0.65f, 0.0f};
inline constexpr Color gray{0.5f, 0.5f, 0.5f};
inline constexpr Color light_gray{0.75f, 0.75f, 0.75f};
inline constexpr Color dark_gray{0.25f, 0.25f, 0.25f};
}   // namespace colors

// Default colour cycle for automatic series colouring.
//
// Series created through Axes / Axes3D are "auto-coloured": they keep a palette
// slot (Series::auto_color_index()) and the renderer resolves the actual colour
// from the active theme's data palette every frame, so switching the palette or
// the theme recolours existing plots.  This cycle is the dark-theme "default"
// palette; it is used as the provisional colour until the first draw and by
// code that runs without a ThemeManager (headless core, SVG export without UI).
namespace palette
{
inline constexpr Color default_cycle[] = {
    {0x22 / 255.0f, 0xD3 / 255.0f, 0xEE / 255.0f},   // vivid cyan
    {0xFB / 255.0f, 0x92 / 255.0f, 0x3C / 255.0f},   // orange
    {0xE8 / 255.0f, 0x79 / 255.0f, 0xF9 / 255.0f},   // magenta
    {0xA3 / 255.0f, 0xE6 / 255.0f, 0x35 / 255.0f},   // lime
    {0x60 / 255.0f, 0xA5 / 255.0f, 0xFA / 255.0f},   // sky blue
    {0xFB / 255.0f, 0x71 / 255.0f, 0x85 / 255.0f},   // rose
    {0xFA / 255.0f, 0xCC / 255.0f, 0x15 / 255.0f},   // amber
    {0x34 / 255.0f, 0xD3 / 255.0f, 0x99 / 255.0f},   // emerald
    {0xC0 / 255.0f, 0x84 / 255.0f, 0xFC / 255.0f},   // violet
    {0xE5 / 255.0f, 0xE7 / 255.0f, 0xEB / 255.0f},   // light gray
};
inline constexpr size_t default_cycle_size = sizeof(default_cycle) / sizeof(default_cycle[0]);

// Default colour cycle for light backgrounds (Tableau 10): darker, saturated
// values that stay readable on white and give readable translucent fills.
inline constexpr Color light_cycle[] = {
    {0x1F / 255.0f, 0x77 / 255.0f, 0xB4 / 255.0f},   // blue
    {0xFF / 255.0f, 0x7F / 255.0f, 0x0E / 255.0f},   // orange
    {0x2C / 255.0f, 0xA0 / 255.0f, 0x2C / 255.0f},   // green
    {0xD6 / 255.0f, 0x27 / 255.0f, 0x28 / 255.0f},   // red
    {0x94 / 255.0f, 0x67 / 255.0f, 0xBD / 255.0f},   // purple
    {0x8C / 255.0f, 0x56 / 255.0f, 0x4B / 255.0f},   // brown
    {0xE3 / 255.0f, 0x77 / 255.0f, 0xC2 / 255.0f},   // pink
    {0x7F / 255.0f, 0x7F / 255.0f, 0x7F / 255.0f},   // gray
    {0xBC / 255.0f, 0xBD / 255.0f, 0x22 / 255.0f},   // olive
    {0x17 / 255.0f, 0xBE / 255.0f, 0xCF / 255.0f},   // cyan
};
inline constexpr size_t light_cycle_size = sizeof(light_cycle) / sizeof(light_cycle[0]);
}   // namespace palette

}   // namespace spectra

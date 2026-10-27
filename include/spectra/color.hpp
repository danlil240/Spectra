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

// Theme-tuned shades for the MATLAB format-string colour letters
// (r g b c m y k w).
//
// Raw primaries read badly: pure blue almost disappears on a dark canvas and
// pure yellow on a light one.  Like Matplotlib's tab: colours, each letter maps
// to a tuned shade of the same hue, one set for dark backgrounds and one for
// light ones.  'k' and 'w' stay distinct on every theme: on dark backgrounds
// 'w' is white and 'k' a light gray (black itself would be invisible), and on
// light backgrounds 'k' is black and 'w' a dark gray.
//
// A series coloured by a letter keeps the letter (Series::color_letter()) and
// the renderer re-resolves the shade for the active theme, so switching theme
// updates it.  Explicit RGB / hex colours are never remapped.
namespace format_colors
{
// Order of the shade tables below.
inline constexpr char letters[] = {'r', 'g', 'b', 'c', 'm', 'y', 'k', 'w'};

// Shades for dark plot backgrounds (the default).
inline constexpr Color dark[] = {
    {0xFF / 255.0f, 0x5C / 255.0f, 0x5C / 255.0f},   // r  soft red
    {0x4A / 255.0f, 0xDE / 255.0f, 0x5A / 255.0f},   // g  green
    {0x6B / 255.0f, 0x8C / 255.0f, 0xFF / 255.0f},   // b  bright blue
    {0x22 / 255.0f, 0xD3 / 255.0f, 0xEE / 255.0f},   // c  cyan
    {0xEC / 255.0f, 0x6C / 255.0f, 0xF5 / 255.0f},   // m  magenta
    {0xFD / 255.0f, 0xE0 / 255.0f, 0x47 / 255.0f},   // y  yellow
    {0xD6 / 255.0f, 0xD6 / 255.0f, 0xD6 / 255.0f},   // k  light gray (~84% lightness)
    {1.0f, 1.0f, 1.0f},                              // w  white
};

// Shades for light plot backgrounds.
inline constexpr Color light[] = {
    {0xD6 / 255.0f, 0x27 / 255.0f, 0x28 / 255.0f},   // r  tab:red
    {0x2C / 255.0f, 0xA0 / 255.0f, 0x2C / 255.0f},   // g  tab:green
    {0x1D / 255.0f, 0x4E / 255.0f, 0xD8 / 255.0f},   // b  deep blue
    {0x08 / 255.0f, 0x91 / 255.0f, 0xB2 / 255.0f},   // c  dark cyan
    {0xC0 / 255.0f, 0x26 / 255.0f, 0xD3 / 255.0f},   // m  magenta
    {0xB8 / 255.0f, 0x9A / 255.0f, 0x00 / 255.0f},   // y  dark yellow
    {0.0f, 0.0f, 0.0f},                              // k  black
    {0x29 / 255.0f, 0x29 / 255.0f, 0x29 / 255.0f},   // w  dark gray (~16% lightness)
};

/// True if @p letter is one of the MATLAB colour letters r g b c m y k w.
inline constexpr bool is_letter(char letter)
{
    for (char l : letters)
    {
        if (l == letter)
            return true;
    }
    return false;
}

/// Tuned shade of colour letter @p letter for a dark (default) or light
/// background.  Returns black for a character that is not a colour letter.
inline constexpr Color shade(char letter, bool light_background = false)
{
    for (size_t i = 0; i < sizeof(letters); ++i)
    {
        if (letters[i] == letter)
            return light_background ? light[i] : dark[i];
    }
    return Color{};
}
}   // namespace format_colors

}   // namespace spectra

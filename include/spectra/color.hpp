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

// Default color cycle for automatic series coloring (10 neon/glow colors for dark themes)
namespace palette
{
inline constexpr Color default_cycle[] = {
    {0.000f, 0.950f, 1.000f},   // neon cyan
    {0.200f, 1.000f, 0.300f},   // neon lime green
    {1.000f, 0.100f, 0.900f},   // neon magenta
    {1.000f, 0.550f, 0.000f},   // neon orange
    {0.150f, 0.450f, 1.000f},   // electric blue
    {1.000f, 0.250f, 0.600f},   // hot pink
    {0.900f, 1.000f, 0.000f},   // neon yellow
    {0.750f, 0.100f, 1.000f},   // neon purple
    {1.000f, 0.200f, 0.200f},   // neon red
    {0.000f, 1.000f, 0.750f},   // neon teal
};
inline constexpr size_t default_cycle_size = sizeof(default_cycle) / sizeof(default_cycle[0]);
}   // namespace palette

}   // namespace spectra

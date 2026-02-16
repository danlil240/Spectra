#pragma once

#include <cstddef>

namespace spectra
{

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
}  // namespace colors

// Default color cycle for automatic series coloring (10 visually distinct colors)
namespace palette
{
inline constexpr Color default_cycle[] = {
    {0.122f, 0.467f, 0.706f},  // steel blue
    {1.000f, 0.498f, 0.055f},  // orange
    {0.173f, 0.627f, 0.173f},  // green
    {0.839f, 0.153f, 0.157f},  // red
    {0.580f, 0.404f, 0.741f},  // purple
    {0.549f, 0.337f, 0.294f},  // brown
    {0.890f, 0.467f, 0.761f},  // pink
    {0.498f, 0.498f, 0.498f},  // gray
    {0.737f, 0.741f, 0.133f},  // olive
    {0.090f, 0.745f, 0.812f},  // cyan
};
inline constexpr size_t default_cycle_size = sizeof(default_cycle) / sizeof(default_cycle[0]);
}  // namespace palette

}  // namespace spectra

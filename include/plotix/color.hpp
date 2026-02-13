#pragma once

namespace plotix {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f)
        : r(r), g(g), b(b), a(a) {}
};

inline constexpr Color rgb(float r, float g, float b) {
    return Color{r, g, b, 1.0f};
}

inline constexpr Color rgba(float r, float g, float b, float a) {
    return Color{r, g, b, a};
}

namespace colors {
    inline constexpr Color black   {0.0f, 0.0f, 0.0f};
    inline constexpr Color white   {1.0f, 1.0f, 1.0f};
    inline constexpr Color red     {1.0f, 0.0f, 0.0f};
    inline constexpr Color green   {0.0f, 1.0f, 0.0f};
    inline constexpr Color blue    {0.0f, 0.0f, 1.0f};
    inline constexpr Color cyan    {0.0f, 1.0f, 1.0f};
    inline constexpr Color magenta {1.0f, 0.0f, 1.0f};
    inline constexpr Color yellow  {1.0f, 1.0f, 0.0f};
    inline constexpr Color orange  {1.0f, 0.65f, 0.0f};
    inline constexpr Color gray    {0.5f, 0.5f, 0.5f};
    inline constexpr Color light_gray {0.75f, 0.75f, 0.75f};
    inline constexpr Color dark_gray  {0.25f, 0.25f, 0.25f};
} // namespace colors

} // namespace plotix

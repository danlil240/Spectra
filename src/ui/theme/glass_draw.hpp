#pragma once

#include "glass_tokens.hpp"
#include "theme.hpp"

struct ImDrawList;
struct ImVec2;

namespace spectra::ui::glass_draw
{

inline ImU32 color_u32(const Color& c)
{
    return IM_COL32(static_cast<int>(c.r * 255.0f),
                  static_cast<int>(c.g * 255.0f),
                  static_cast<int>(c.b * 255.0f),
                  static_cast<int>(c.a * 255.0f));
}

// Rich navy/violet ambience — visible through frosted chrome (fake glass, no blur).
inline void draw_ambient_gradient(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float glow_strength)
{
    const float g = std::clamp(glow_strength, 0.0f, 1.0f);
    dl->AddRectFilledMultiColor(p0,
                                p1,
                                IM_COL32(14, 28, 58, 255),
                                IM_COL32(32, 16, 52, 255),
                                IM_COL32(38, 14, 48, 255),
                                IM_COL32(12, 26, 54, 255));
    const int cyan_a = static_cast<int>(42.0f + 70.0f * g);
    const int mag_a  = static_cast<int>(38.0f + 62.0f * g);
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p0.x + (p1.x - p0.x) * 0.35f, p1.y),
                                IM_COL32(40, 170, 240, cyan_a),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(40, 170, 240, cyan_a / 2));
    dl->AddRectFilledMultiColor(ImVec2(p0.x + (p1.x - p0.x) * 0.65f, p0.y),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(210, 70, 220, mag_a),
                                IM_COL32(210, 70, 220, mag_a / 2),
                                IM_COL32(0, 0, 0, 0));
}

// Vision.png outer window halo — drawn once behind all chrome.
inline void draw_window_edge_glow(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float glow_strength)
{
    const float g = std::clamp(glow_strength, 0.0f, 1.0f);
    for (int i = 4; i >= 1; --i)
    {
        float expand = static_cast<float>(i) * 2.0f;
        int   a      = static_cast<int>((8.0f + 14.0f * g) * static_cast<float>(i));
        dl->AddRect(ImVec2(p0.x - expand, p0.y - expand),
                    ImVec2(p1.x + expand, p1.y + expand),
                    IM_COL32(40, 160, 230, a),
                    14.0f + expand,
                    0,
                    1.5f);
        dl->AddRect(ImVec2(p0.x - expand * 0.5f, p0.y - expand * 0.5f),
                    ImVec2(p1.x + expand * 0.5f, p1.y + expand * 0.5f),
                    IM_COL32(200, 70, 210, a / 2),
                    12.0f + expand * 0.5f,
                    0,
                    1.0f);
    }
}

// Frosted glass panel: gradient base + translucent tint + layered highlights,
// inner bottom shadow, and dual hairline border for real depth.
inline void draw_glass_rect(ImDrawList*           dl,
                            ImVec2                p0,
                            ImVec2                p1,
                            float                 rounding,
                            const ThemeColors&    colors,
                            const ThemeGlassSettings& glass,
                            GlassSurface          surface,
                            float                 glow_strength)
{
    const float master = std::clamp(glass.master_intensity, 0.0f, 1.0f);
    const float alpha  = glass_surface_alpha_value(glass, surface);
    const float glow   = std::clamp(glow_strength, 0.0f, 1.0f);
    const float h      = p1.y - p0.y;

    draw_ambient_gradient(dl, p0, p1, glow);

    Color frost = colors.bg_secondary.lerp(colors.bg_elevated, 0.35f + master * 0.15f);
    frost       = frost.lerp(glass_palette::kAccentCyan, master * 0.06f);
    dl->AddRectFilled(p0, p1, color_u32(frost.with_alpha(alpha)), rounding);

    // Vertical sheen — brighter at top, settling toward the bottom (glass curvature).
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p1.x, p0.y + h * 0.5f),
                                IM_COL32(210, 232, 255, static_cast<int>(20.0f + 14.0f * master)),
                                IM_COL32(210, 232, 255, static_cast<int>(20.0f + 14.0f * master)),
                                IM_COL32(255, 255, 255, 0),
                                IM_COL32(255, 255, 255, 0));
    // Inner bottom shadow — grounds the panel.
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y + h * 0.6f),
                                p1,
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, static_cast<int>(26.0f + 20.0f * master)),
                                IM_COL32(0, 0, 0, static_cast<int>(26.0f + 20.0f * master)));

    // Top inner highlight (glass edge catch-light)
    const int hi_a = static_cast<int>(30.0f + 26.0f * (1.0f - alpha));
    dl->AddLine(ImVec2(p0.x + rounding * 0.5f, p0.y + 1.0f),
                ImVec2(p1.x - rounding * 0.5f, p0.y + 1.0f),
                IM_COL32(225, 244, 255, hi_a),
                1.0f);

    // Dual hairline: inner dark seat + outer luminous accent edge.
    dl->AddRect(ImVec2(p0.x + 1.0f, p0.y + 1.0f),
                ImVec2(p1.x - 1.0f, p1.y - 1.0f),
                IM_COL32(0, 0, 0, 50),
                std::max(0.0f, rounding - 1.0f),
                0,
                1.0f);
    Color border = colors.accent.lerp(glass_palette::kAccentMagenta, 0.30f);
    const int border_a = static_cast<int>((52.0f + 90.0f * glow) * (0.40f + master * 0.60f));
    dl->AddRect(p0,
                p1,
                color_u32(border.with_alpha(static_cast<float>(border_a) / 255.0f)),
                rounding,
                0,
                1.0f);
    if (glow > 0.08f)
    {
        for (int i = 1; i <= 2; ++i)
        {
            float e = static_cast<float>(i);
            dl->AddRect(ImVec2(p0.x - e, p0.y - e),
                        ImVec2(p1.x + e, p1.y + e),
                        color_u32(border.with_alpha(glow * (0.10f / e) * (0.5f + master * 0.5f))),
                        rounding + e,
                        0,
                        1.5f);
        }
    }
}

// Translucent glass card (legend, tooltip, popups). Caller supplies the rect;
// drop shadow + frosted fill + top highlight + hairline are drawn here so the
// element reads as a floating pane rather than an opaque gray block.
inline void draw_glass_card(ImDrawList*        dl,
                            ImVec2             p0,
                            ImVec2             p1,
                            float              rounding,
                            const ThemeColors& colors,
                            float              fill_alpha,
                            float              glow_strength)
{
    const float glow = std::clamp(glow_strength, 0.0f, 1.0f);
    const float h    = p1.y - p0.y;

    // Multi-layer soft drop shadow.
    for (int i = 4; i >= 1; --i)
    {
        float off = static_cast<float>(i) * 2.0f;
        int   a   = static_cast<int>(14.0f * static_cast<float>(i) / 4.0f);
        dl->AddRectFilled(ImVec2(p0.x - off * 0.4f, p0.y + off * 0.5f),
                          ImVec2(p1.x + off * 0.4f, p1.y + off),
                          IM_COL32(0, 0, 0, a),
                          rounding + off * 0.4f);
    }

    Color frost = colors.bg_elevated.lerp(glass_palette::kAccentCyan, 0.05f);
    dl->AddRectFilled(p0, p1, color_u32(frost.with_alpha(std::clamp(fill_alpha, 0.05f, 1.0f))),
                      rounding);
    dl->AddRectFilledMultiColor(p0,
                                ImVec2(p1.x, p0.y + h * 0.5f),
                                IM_COL32(220, 240, 255, 26),
                                IM_COL32(220, 240, 255, 26),
                                IM_COL32(255, 255, 255, 0),
                                IM_COL32(255, 255, 255, 0));
    dl->AddLine(ImVec2(p0.x + rounding * 0.6f, p0.y + 1.0f),
                ImVec2(p1.x - rounding * 0.6f, p0.y + 1.0f),
                IM_COL32(230, 246, 255, 60),
                1.0f);
    Color border = colors.accent.lerp(glass_palette::kAccentMagenta, 0.25f);
    dl->AddRect(p0, p1, color_u32(border.with_alpha(0.30f + glow * 0.25f)), rounding, 0, 1.0f);
}

// Premium floating app-shell frame: luminous outer border + soft inner edge
// vignette glow. Strokes only — never fills the interior (would hide the plot).
inline void draw_app_shell_frame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float glow_strength)
{
    const float g = std::clamp(glow_strength, 0.0f, 1.0f);
    const float radius = 14.0f;

    // Outer luminous border (cyan→violet) with a faint halo.
    for (int i = 3; i >= 1; --i)
    {
        float e = static_cast<float>(i);
        int   a = static_cast<int>((10.0f + 16.0f * g) * (4 - i) / 3.0f);
        dl->AddRect(ImVec2(p0.x - e, p0.y - e),
                    ImVec2(p1.x + e, p1.y + e),
                    IM_COL32(60, 170, 235, a),
                    radius + e,
                    0,
                    1.5f);
    }
    dl->AddRect(p0, p1, IM_COL32(120, 190, 245, static_cast<int>(60.0f + 60.0f * g)),
                radius, 0, 1.25f);

    // Inner edge vignette — soft dark falloff hugging the window border only.
    const float band = 26.0f;
    int         va   = static_cast<int>(34.0f + 26.0f * g);
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + band),
                                IM_COL32(0, 0, 0, va), IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - band), p1,
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, va), IM_COL32(0, 0, 0, va));
    dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + band, p1.y),
                                IM_COL32(0, 0, 0, va), IM_COL32(0, 0, 0, 0),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, va));
    dl->AddRectFilledMultiColor(ImVec2(p1.x - band, p0.y), p1,
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, va),
                                IM_COL32(0, 0, 0, va), IM_COL32(0, 0, 0, 0));
}

// Vision.png canvas frame — cyan/magenta gradient glow around plot hero area.
inline void draw_vision_canvas_frame(ImDrawList*        dl,
                                     ImVec2             p0,
                                     ImVec2             p1,
                                     float              rounding,
                                     const ThemeColors& colors,
                                     float              glow_strength,
                                     float              master_intensity)
{
    const float glow   = std::clamp(glow_strength, 0.0f, 1.0f);
    const float master = std::clamp(master_intensity, 0.0f, 1.0f);

    for (int i = 3; i >= 1; --i)
    {
        float expand = static_cast<float>(i) * 1.5f;
        int   a      = static_cast<int>((10.0f + 8.0f * glow) * static_cast<float>(i) / 3.0f);
        dl->AddRect(ImVec2(p0.x - expand, p0.y - expand),
                    ImVec2(p1.x + expand, p1.y + expand),
                    IM_COL32(0, 0, 0, a),
                    rounding + expand);
    }

    Color cyan_edge = glass_palette::kAccentCyan;
    Color mag_edge  = glass_palette::kAccentMagenta;

    // Soft outer glow halo around the plot container.
    for (int i = 3; i >= 1; --i)
    {
        float e = static_cast<float>(i) * 1.5f;
        int   a = static_cast<int>((14.0f + 18.0f * glow * master) * (4 - i) / 3.0f);
        dl->AddRect(ImVec2(p0.x - e, p0.y - e),
                    ImVec2(p1.x + e, p1.y + e),
                    color_u32(cyan_edge.lerp(mag_edge, 0.4f).with_alpha(static_cast<float>(a) / 255.0f)),
                    rounding + e,
                    0,
                    1.5f);
    }

    // Softer primary border (lower alpha than before, larger radius).
    const int edge_a = static_cast<int>(80.0f + 70.0f * glow * master);
    dl->AddRect(p0,
                p1,
                color_u32(cyan_edge.lerp(mag_edge, 0.5f).with_alpha(static_cast<float>(edge_a) / 255.0f)),
                rounding,
                0,
                1.0f);

    // Subtle inner glow hugging the frame interior.
    dl->AddRect(ImVec2(p0.x + 1.5f, p0.y + 1.5f),
                ImVec2(p1.x - 1.5f, p1.y - 1.5f),
                color_u32(cyan_edge.with_alpha(0.10f + 0.12f * glow)),
                std::max(0.0f, rounding - 1.5f),
                0,
                1.0f);

    dl->AddLine(ImVec2(p0.x + 5.0f, p0.y + 1.0f),
                ImVec2(p1.x - 5.0f, p0.y + 1.0f),
                color_u32(cyan_edge.with_alpha(0.30f + 0.22f * glow)),
                1.0f);

    dl->AddRectFilledMultiColor(ImVec2(p0.x + 1.0f, p0.y + 1.0f),
                                ImVec2(p1.x - 1.0f, p0.y + (p1.y - p0.y) * 0.35f),
                                IM_COL32(255, 255, 255, 10),
                                IM_COL32(255, 255, 255, 10),
                                IM_COL32(255, 255, 255, 0),
                                IM_COL32(255, 255, 255, 0));
}

}   // namespace spectra::ui::glass_draw

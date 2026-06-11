#pragma once

#include <algorithm>

#include "design_tokens.hpp"
#include "theme.hpp"

namespace spectra::ui
{

// ─── Night Glass semantic palette (base RGB — alpha applied via glass settings) ─

namespace glass_palette
{
// Night Glass surfaces — lifted from pre-redesign night theme (not near-black)
constexpr Color kBaseBackground{0.03f, 0.04f, 0.07f, 1.0f};     // ~#07090D
constexpr Color kSurfacePanel{0.08f, 0.10f, 0.13f, 1.0f};       // ~#141A22
constexpr Color kSurfacePanelHover{0.11f, 0.14f, 0.19f, 1.0f};
constexpr Color kSurfacePanelActive{0.13f, 0.17f, 0.24f, 1.0f};
constexpr Color kSurfaceToolbar{0.06f, 0.08f, 0.11f, 1.0f};
constexpr Color kSurfacePlot{0.07f, 0.13f, 0.19f, 1.0f};        // ~#132032
constexpr Color kSurfaceControl{0.11f, 0.15f, 0.19f, 1.0f};     // ~#1D2530
constexpr Color kSurfacePopup{0.14f, 0.19f, 0.25f, 1.0f};       // ~#243041
constexpr Color kBorderSubtle{0.22f, 0.30f, 0.42f, 0.45f};
constexpr Color kBorderActive{0.42f, 0.58f, 0.82f, 0.75f};
constexpr Color kBorderGlow{0.24f, 0.78f, 0.95f, 0.55f};
constexpr Color kTextPrimary{0.91f, 0.93f, 0.97f, 1.0f};
constexpr Color kTextSecondary{0.62f, 0.68f, 0.78f, 1.0f};
constexpr Color kTextMuted{0.42f, 0.48f, 0.58f, 1.0f};
constexpr Color kAccentBlue{0.29f, 0.56f, 0.96f, 1.0f};
constexpr Color kAccentCyan{0.24f, 0.84f, 0.96f, 1.0f};
constexpr Color kAccentViolet{0.58f, 0.45f, 0.96f, 1.0f};
constexpr Color kAccentMagenta{0.92f, 0.36f, 0.78f, 1.0f};
constexpr Color kSuccessGreen{0.25f, 0.73f, 0.31f, 1.0f};
constexpr Color kWarningAmber{0.82f, 0.60f, 0.13f, 1.0f};
}   // namespace glass_palette

// Geometry + effect aliases
namespace glass_tokens
{
constexpr float radius_small  = tokens::RADIUS_SM;
constexpr float radius_medium = tokens::RADIUS_MD;
constexpr float radius_large  = tokens::RADIUS_LG;
constexpr float spacing_xs    = tokens::SPACE_1;
constexpr float spacing_s     = tokens::SPACE_2;
constexpr float spacing_m     = tokens::SPACE_4;
constexpr float spacing_l     = tokens::SPACE_6;

constexpr float shadow_alpha           = tokens::SHADOW_INTENSITY_NORMAL;
constexpr float glow_alpha             = 0.28f;
constexpr float grid_major_alpha       = tokens::GRID_MAJOR_ALPHA_NIGHT;
constexpr float grid_minor_alpha       = tokens::GRID_MINOR_ALPHA_NIGHT;
constexpr float active_item_glow_alpha = 0.22f;

constexpr float MIN_PLOT_READABILITY_ALPHA = 0.72f;
constexpr float MIN_TEXT_CONTRAST_ALPHA    = 0.92f;
}   // namespace glass_tokens

inline float glass_effective_alpha(float master, float target_alpha)
{
    master       = std::clamp(master, 0.0f, 1.0f);
    target_alpha = std::clamp(target_alpha, 0.0f, 1.0f);
    return 1.0f + (target_alpha - 1.0f) * master;
}

inline Color glass_surface_color(const Color& base, float effective_alpha)
{
    float a = std::clamp(effective_alpha, 0.05f, 1.0f);
    return base.with_alpha(a);
}

inline float glass_surface_alpha_value(const ThemeGlassSettings& glass, GlassSurface surface)
{
    float target = glass.panel_alpha;
    switch (surface)
    {
        case GlassSurface::Toolbar:
            target = glass.toolbar_alpha;
            break;
        case GlassSurface::Plot:
            target = glass.plot_alpha;
            break;
        case GlassSurface::Panel:
        default:
            target = glass.panel_alpha;
            break;
    }
    target = std::clamp(target, 0.30f, 0.95f);

    // master=0 → nearly opaque; master=1 → target (translucent frost)
    constexpr float k_opaque = 0.92f;
    const float     master   = std::clamp(glass.master_intensity, 0.0f, 1.0f);
    float           effective = k_opaque + (target - k_opaque) * master;
    if (surface == GlassSurface::Plot)
    {
        effective = std::max(effective, glass_tokens::MIN_PLOT_READABILITY_ALPHA);
    }
    return effective;
}

inline Color glass_resolved_surface_color(const Color&              base,
                                          const ThemeGlassSettings& glass,
                                          GlassSurface              surface)
{
    const float master = std::clamp(glass.master_intensity, 0.0f, 1.0f);
    const float a      = glass_surface_alpha_value(glass, surface);
    Color       frost  = base.lerp(glass_palette::kAccentCyan, master * 0.05f);
    return glass_surface_color(frost, a);
}

inline float glass_effective_glow_strength(const ThemeGlassSettings& glass,
                                           float                     base_glow_intensity)
{
    return base_glow_intensity * std::clamp(glass.glow_strength, 0.0f, 1.0f);
}

inline Color glass_resolved_plot_background(const Color&              canvas,
                                            const Color&              chrome,
                                            const ThemeGlassSettings& glass)
{
    float t = std::clamp(glass.master_intensity, 0.0f, 1.0f)
              * (1.0f - std::clamp(glass.plot_alpha, 0.0f, 1.0f));
    t       = std::min(t, 1.0f - glass_tokens::MIN_PLOT_READABILITY_ALPHA);
    Color blended = canvas.lerp(chrome, t * 0.35f);
    float a       = glass_surface_alpha_value(glass, GlassSurface::Plot);
    return blended.with_alpha(a);
}

}   // namespace spectra::ui

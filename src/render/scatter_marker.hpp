#pragma once

#include <cstdint>
#include <spectra/plot_style.hpp>

namespace spectra
{

// Shader-only marker code for scatter series that did not pick a marker
// style: a filled circle with a thin edge tinted towards the theme contrast.
// It sits after the last MarkerStyle value and is never exposed publicly.
// Must match the `case 18u` branch in scatter.frag and scatter3d.frag.
inline constexpr uint32_t DEFAULT_SCATTER_MARKER_CODE = 18u;

// Edge tint for the default scatter dot. Positive mixes the edge towards
// white (dark canvas), negative towards black (light canvas). Colormapped
// scatter gets a stronger edge so the dark/light ends of the map stay visible.
inline constexpr float DEFAULT_SCATTER_EDGE_MIX          = 0.30f;
inline constexpr float DEFAULT_SCATTER_COLORMAP_EDGE_MIX = 0.42f;

struct ScatterMarkerParams
{
    uint32_t marker_type = 0;      // value for SeriesPushConstants::marker_type
    float    edge_mix    = 0.0f;   // value for SeriesPushConstants::_pad2[1]
};

// Resolve the marker drawn for a scatter series. An explicit marker style is
// returned unchanged (no edge); MarkerStyle::None selects the default dot.
// `bg_luma` is the relative luminance (0..1) of the plot canvas.
inline ScatterMarkerParams resolve_scatter_marker(MarkerStyle style,
                                                  bool        has_colormap,
                                                  float       bg_luma)
{
    if (style != MarkerStyle::None)
        return {static_cast<uint32_t>(style), 0.0f};

    const float amount =
        has_colormap ? DEFAULT_SCATTER_COLORMAP_EDGE_MIX : DEFAULT_SCATTER_EDGE_MIX;
    return {DEFAULT_SCATTER_MARKER_CODE, bg_luma > 0.5f ? -amount : amount};
}

}   // namespace spectra

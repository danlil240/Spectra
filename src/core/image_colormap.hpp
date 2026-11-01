#pragma once

// Colormap lookup tables for ImageSeries (GPU-free).
//
// The GPU image shader (src/gpu/shaders/image2d.frag) maps a value v to
// t = (v - vmin) / (vmax - vmin), clamps it to [0, 1] and reads LUT entry
// clamp(int(t * n), 0, n - 1).  lut_index() / build_lut() are the CPU side of
// that contract, shared by the renderer (LUT texture), the SVG exporter and
// the colorbar, so all of them show the same colours.

#include <cstddef>
#include <cstdint>
#include <span>
#include <spectra/color.hpp>
#include <vector>

namespace spectra::image_colormap
{

/// Entries of a continuous colormap LUT.
inline constexpr size_t CONTINUOUS_LUT_SIZE = 256;
/// Entries of a banded (filled contour) LUT; larger so band edges land
/// within 1/2048 of the level range of their exact value.
inline constexpr size_t BANDED_LUT_SIZE = 1024;
/// Out-of-range tolerance (in t) before a banded value is dropped.
inline constexpr float BAND_EPS = 1e-6f;

/// Continuous colour of @p cm at t in [0, 1] (the SurfaceSeries colormaps).
Color sample(ColormapType cm, float t);

/// t for value v over [vmin, vmax] (0.5 for a flat range), not clamped.
float normalized(float v, float vmin, float vmax);

/// LUT entry for t (clamped to [0, 1]) in a table of n entries.
size_t lut_index(float t, size_t n);

/// Colour of band b of @p levels (between levels[b] and levels[b + 1]): the
/// colormap at the band midpoint, normalised over the level range.
Color band_color(ColormapType cm, std::span<const float> levels, size_t band);

/// Index of the band holding v, or -1 when v is outside the level range.
int band_of(std::span<const float> levels, float v);

/// RGBA8 lookup table: continuous (empty @p levels, CONTINUOUS_LUT_SIZE
/// entries) or one flat colour per band (BANDED_LUT_SIZE entries).
std::vector<uint8_t> build_lut(ColormapType cm, std::span<const float> levels);

/// Entry @p i of an RGBA8 LUT.
Color lut_color(std::span<const uint8_t> lut, size_t i);

}   // namespace spectra::image_colormap

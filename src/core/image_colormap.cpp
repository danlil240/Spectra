#include "image_colormap.hpp"

#include <algorithm>
#include <cmath>
#include <spectra/series3d.hpp>

namespace spectra::image_colormap
{

Color sample(ColormapType cm, float t)
{
    if (cm == ColormapType::None)
        cm = ColormapType::Grayscale;
    return SurfaceSeries::sample_colormap(cm, std::clamp(t, 0.0f, 1.0f));
}

float normalized(float v, float vmin, float vmax)
{
    const float range = vmax - vmin;
    return range != 0.0f ? (v - vmin) / range : 0.5f;
}

size_t lut_index(float t, size_t n)
{
    if (n == 0)
        return 0;
    if (!(t > 0.0f))
        return 0;   // also NaN
    const auto i = static_cast<size_t>(std::min(t, 1.0f) * static_cast<float>(n));
    return std::min(i, n - 1);
}

Color band_color(ColormapType cm, std::span<const float> levels, size_t band)
{
    if (levels.size() < 2 || band + 1 >= levels.size())
        return sample(cm, 0.5f);
    const float mid = 0.5f * (levels[band] + levels[band + 1]);
    return sample(cm, normalized(mid, levels.front(), levels.back()));
}

int band_of(std::span<const float> levels, float v)
{
    if (levels.size() < 2 || !std::isfinite(v) || v < levels.front() || v > levels.back())
        return -1;
    // Last level <= v; the top level belongs to the last band.
    const auto it = std::upper_bound(levels.begin(), levels.end(), v);
    auto       b  = static_cast<int>(it - levels.begin()) - 1;
    return std::clamp(b, 0, static_cast<int>(levels.size()) - 2);
}

std::vector<uint8_t> build_lut(ColormapType cm, std::span<const float> levels)
{
    const bool           banded = levels.size() >= 2;
    const size_t         n      = banded ? BANDED_LUT_SIZE : CONTINUOUS_LUT_SIZE;
    std::vector<uint8_t> lut(n * 4);
    auto                 to8 = [](float c)
    { return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); };
    const float lo = banded ? levels.front() : 0.0f;
    const float hi = banded ? levels.back() : 1.0f;
    for (size_t i = 0; i < n; ++i)
    {
        // Entry i covers t in [i/n, (i+1)/n); colour it at its centre.
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        Color       c;
        if (banded)
        {
            const float v = lo + t * (hi - lo);
            const int   b = band_of(levels, v);
            c             = band_color(cm, levels, static_cast<size_t>(std::max(b, 0)));
        }
        else
        {
            c = sample(cm, t);
        }
        lut[i * 4 + 0] = to8(c.r);
        lut[i * 4 + 1] = to8(c.g);
        lut[i * 4 + 2] = to8(c.b);
        lut[i * 4 + 3] = 255;
    }
    return lut;
}

Color lut_color(std::span<const uint8_t> lut, size_t i)
{
    if (lut.size() < 4)
        return {0.5f, 0.5f, 0.5f, 1.0f};
    i = std::min(i, lut.size() / 4 - 1);
    return {static_cast<float>(lut[i * 4 + 0]) / 255.0f,
            static_cast<float>(lut[i * 4 + 1]) / 255.0f,
            static_cast<float>(lut[i * 4 + 2]) / 255.0f,
            static_cast<float>(lut[i * 4 + 3]) / 255.0f};
}

}   // namespace spectra::image_colormap

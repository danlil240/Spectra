#pragma once

// Pure (GPU-free) colorbar helpers shared by the layout code (space
// reservation in AxesBase::set_viewport) and the renderer (drawing).
//
// A colorbar is shown for an axes when AxesBase::colorbar_visible() is set
// and the axes holds a visible colormapped series: a ScatterSeries with
// per-point color values + colormap, or a SurfaceSeries with a colormap.
// It sits in a strip carved out of the right side of the axes layout cell,
// so it never covers data.

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/color.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <string>
#include <vector>

namespace spectra::colorbar_layout
{

// Geometry (pixels).  The strip is GAP + BAR_WIDTH + TICK_LENGTH + LABEL_GAP
// + LABEL_WIDTH wide; tick labels are left-aligned inside LABEL_WIDTH.
inline constexpr float GAP            = 14.0f;
inline constexpr float BAR_WIDTH      = 14.0f;
inline constexpr float TICK_LENGTH    = 4.0f;
inline constexpr float LABEL_GAP      = 4.0f;
inline constexpr float LABEL_WIDTH    = 46.0f;
inline constexpr float RESERVED_WIDTH = GAP + BAR_WIDTH + TICK_LENGTH + LABEL_GAP + LABEL_WIDTH;
// Never give the colorbar more than this fraction of a (narrow) layout cell.
inline constexpr float MAX_RESERVED_FRACTION = 0.3f;

// Which colormap mapping the series uses on the GPU.  Scatter and surface
// shaders use different colormap approximations, so the bar must sample the
// matching one to show the same colours as the plot.
enum class Mapping
{
    Scatter,   // scatter.frag apply_colormap (3-stop piecewise linear)
    Surface,   // surface3d.frag apply_colormap == SurfaceSeries::sample_colormap
};

struct Source
{
    ColormapType cmap    = ColormapType::None;
    Mapping      mapping = Mapping::Scatter;
    double       vmin    = 0.0;
    double       vmax    = 1.0;
};

// First visible colormapped series of the axes (nullopt if there is none).
inline std::optional<Source> find_source(const AxesBase& axes)
{
    for (const auto& sp : axes.series())
    {
        if (!sp || !sp->visible())
            continue;
        if (const auto* sc = dynamic_cast<const ScatterSeries*>(sp.get()))
        {
            if (!sc->has_colormap())
                continue;
            return Source{sc->colormap(), Mapping::Scatter, sc->colormap_min(), sc->colormap_max()};
        }
        if (const auto* sf = dynamic_cast<const SurfaceSeries*>(sp.get()))
        {
            if (sf->colormap_type() == ColormapType::None)
                continue;
            // surface3d.frag maps the model-space Z box onto the colormap,
            // i.e. the axes' z limits (SurfaceSeries::colormap_range is not
            // used by the shader).
            AxisLimits zl{0.0, 1.0};
            if (const auto* a3 = dynamic_cast<const Axes3D*>(&axes))
                zl = a3->z_limits();
            return Source{sf->colormap_type(), Mapping::Surface, zl.min, zl.max};
        }
    }
    return std::nullopt;
}

// True when the axes should reserve space for and draw a colorbar.
inline bool wants_colorbar(const AxesBase& axes)
{
    return axes.colorbar_visible() && find_source(axes).has_value();
}

// Width taken from a layout cell of width cell_w for the colorbar strip.
inline float reserved_width(float cell_w)
{
    return std::clamp(cell_w * MAX_RESERVED_FRACTION, 0.0f, RESERVED_WIDTH);
}

// The plot area left after carving the colorbar strip out of a layout cell.
inline Rect shrink_cell(const Rect& cell)
{
    Rect r = cell;
    r.w    = std::max(0.0f, cell.w - reserved_width(cell.w));
    return r;
}

// Gradient bar rectangle for an axes whose (already shrunk) plot area is vp.
// 2D: spans the full plot height.  3D: the central 70% (the 3D viewport also
// holds the title and the projected box, which is smaller than the viewport).
inline Rect bar_rect(const Rect& vp, bool is_3d)
{
    Rect r;
    r.x = vp.x + vp.w + GAP;
    r.w = BAR_WIDTH;
    if (is_3d)
    {
        r.h = vp.h * 0.7f;
        r.y = vp.y + (vp.h - r.h) * 0.5f;
    }
    else
    {
        r.y = vp.y;
        r.h = vp.h;
    }
    return r;
}

// Colour of the colormap at t in [0, 1], matching the GPU shader for mapping.
inline Color sample(ColormapType cm, Mapping mapping, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    if (mapping == Mapping::Surface)
        return SurfaceSeries::sample_colormap(cm, t);

    // Mirror of apply_colormap() in src/gpu/shaders/scatter.frag — keep in sync.
    auto three_stop = [t](Color c0, Color c1, Color c2)
    {
        const Color& a = t < 0.5f ? c0 : c1;
        const Color& b = t < 0.5f ? c1 : c2;
        const float  u = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
        return Color{a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, 1.0f};
    };
    switch (cm)
    {
        case ColormapType::Viridis:
            return three_stop({0.267f, 0.005f, 0.329f},
                              {0.128f, 0.567f, 0.551f},
                              {0.993f, 0.906f, 0.144f});
        case ColormapType::Plasma:
            return three_stop({0.050f, 0.030f, 0.528f},
                              {0.798f, 0.280f, 0.470f},
                              {0.940f, 0.975f, 0.131f});
        case ColormapType::Inferno:
            return three_stop({0.001f, 0.000f, 0.014f},
                              {0.735f, 0.216f, 0.330f},
                              {0.988f, 0.998f, 0.645f});
        case ColormapType::Magma:
            return three_stop({0.001f, 0.000f, 0.014f},
                              {0.716f, 0.215f, 0.475f},
                              {0.987f, 0.991f, 0.750f});
        case ColormapType::Jet:
        {
            auto ch = [t](float c)
            { return std::clamp(1.5f - std::abs(4.0f * t - c), 0.0f, 1.0f); };
            return Color{ch(3.0f), ch(2.0f), ch(1.0f), 1.0f};
        }
        case ColormapType::Coolwarm:
            return three_stop({0.230f, 0.299f, 0.754f},
                              {0.865f, 0.865f, 0.865f},
                              {0.706f, 0.016f, 0.150f});
        case ColormapType::Grayscale:
            return Color{t, t, t, 1.0f};
        case ColormapType::None:
        default:
            return Color{0.5f, 0.5f, 0.5f, 1.0f};
    }
}

struct Tick
{
    double      value = 0.0;
    std::string label;
};

// Format a tick value with just enough decimals for the given step.
inline std::string format_tick(double v, double step)
{
    if (std::abs(v) < step * 1e-6)
        v = 0.0;
    const double av = std::abs(v);
    if (av != 0.0 && (av >= 1e5 || av < 1e-3))
        return std::format("{:.2g}", v);
    // Fewest decimals that represent the step exactly (0.25 -> 2, 5 -> 0).
    int decimals = 0;
    for (double scaled = step; decimals < 6; ++decimals, scaled *= 10.0)
    {
        if (std::abs(scaled - std::round(scaled)) < 1e-6 * std::max(1.0, scaled))
            break;
    }
    return std::format("{:.{}f}", v, decimals);
}

// About 4-6 "nice" ticks (1/2/2.5/5 x 10^n steps) inside [vmin, vmax].  A
// degenerate range yields a single tick at the value.
inline std::vector<Tick> compute_ticks(double vmin, double vmax)
{
    std::vector<Tick> ticks;
    if (!std::isfinite(vmin) || !std::isfinite(vmax))
        return ticks;
    if (vmax < vmin)
        std::swap(vmin, vmax);
    const double range = vmax - vmin;
    if (!(range > std::max(std::abs(vmin), std::abs(vmax)) * 1e-12))
    {
        ticks.push_back({vmin, format_tick(vmin, std::max(std::abs(vmin), 1.0))});
        return ticks;
    }

    auto count_for = [&](double step)
    {
        const double first = std::ceil(vmin / step - 1e-9) * step;
        return static_cast<int>(std::floor((vmax - first) / step + 1e-9)) + 1;
    };

    // Walk down the 10 / 5 / 2.5 / 2 / 1 x 10^n ladder from a step of about
    // the whole range and keep the first (largest) step giving >= 4 ticks.
    // That lands on 4-6 ticks except for awkward ranges (e.g. [0.05, 3.9]
    // gives 3 at step 1, then 7 at step 0.5), where the finer step wins.
    constexpr double LADDER[] = {10.0, 5.0, 2.5, 2.0, 1.0};
    double           mag      = std::pow(10.0, std::ceil(std::log10(range)));
    double           best     = mag;
    bool             found    = false;
    for (int decade = 0; decade < 4 && !found; ++decade, mag /= 10.0)
    {
        for (double m : LADDER)
        {
            const double step = m * mag / 10.0;
            if (count_for(step) >= 4)
            {
                best  = step;
                found = true;
                break;
            }
        }
    }
    const double first = std::ceil(vmin / best - 1e-9) * best;
    for (int i = 0; i < 12; ++i)
    {
        const double v = first + best * i;
        if (v > vmax + best * 1e-9)
            break;
        ticks.push_back({v, format_tick(v, best)});
    }
    return ticks;
}

}   // namespace spectra::colorbar_layout

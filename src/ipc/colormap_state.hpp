#pragma once

// Series colormaps over IPC: the "colormap" / "colormap_range" Python
// properties, the SnapshotSeriesState colormap fields and the
// SET_SERIES_COLORMAP diff op.  Surfaces and colormapped scatters (2D / 3D)
// carry their colormap (built-in, reversed or a user table), the value range
// when one is set, and the scatter per-point values.  Header-only so the app,
// the daemon, the window agent and the unit tests share one interpretation.

#include <spectra/color.hpp>
#include <spectra/colormap.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "message.hpp"

namespace spectra::ipc
{

// Upper bound on the user colours of a Custom colormap accepted from IPC
// (Colormap::from_colors resamples them to Colormap::LUT_SIZE entries).
inline constexpr size_t MAX_COLORMAP_COLORS = Colormap::LUT_SIZE;

// Upper bound on the per-point colormap values accepted from IPC.
inline constexpr size_t MAX_COLOR_VALUES = size_t(1) << 26;

// ─── Colormap <-> snapshot fields ───────────────────────────────────────────

// Store a live colormap: built-in type and direction, and for a user table
// its LUT_SIZE entries (direction not applied, so `reversed` stays separate).
inline void colormap_to_state(const Colormap& cm, SnapshotSeriesState& ss)
{
    ss.colormap          = static_cast<uint8_t>(cm.type());
    ss.colormap_reversed = cm.is_reversed();
    ss.colormap_colors.clear();
    if (!cm.is_custom())
        return;
    const Colormap forward = cm.reversed(false);
    ss.colormap_colors.reserve(Colormap::LUT_SIZE * 4);
    for (size_t i = 0; i < Colormap::LUT_SIZE; ++i)
    {
        // Centre of entry i: exactly that entry (Colormap::sample's rule).
        const Color c =
            forward.sample((static_cast<float>(i) + 0.5f) / static_cast<float>(Colormap::LUT_SIZE));
        ss.colormap_colors.insert(ss.colormap_colors.end(), {c.r, c.g, c.b, c.a});
    }
}

// The colormap the snapshot fields describe; false when they keep the
// series' own (unset, unknown type, or Custom without valid colours).
inline bool colormap_from_state(const SnapshotSeriesState& ss, Colormap& out)
{
    if (ss.colormap > static_cast<uint8_t>(ColormapType::Custom))
        return false;
    const auto type = static_cast<ColormapType>(ss.colormap);
    if (type != ColormapType::Custom)
    {
        out = Colormap(type, ss.colormap_reversed);
        return true;
    }
    const auto& f = ss.colormap_colors;
    if (f.empty() || f.size() % 4 != 0 || f.size() / 4 > MAX_COLORMAP_COLORS)
        return false;
    std::vector<Color> colors;
    colors.reserve(f.size() / 4);
    for (size_t i = 0; i + 3 < f.size(); i += 4)
    {
        if (!std::isfinite(f[i]) || !std::isfinite(f[i + 1]) || !std::isfinite(f[i + 2])
            || !std::isfinite(f[i + 3]))
            return false;
        colors.push_back(Color{f[i], f[i + 1], f[i + 2], f[i + 3]});
    }
    out = Colormap::from_colors(colors).reversed(ss.colormap_reversed);
    return true;
}

// Parse the "colormap" property: a matplotlib name ("viridis", "RdBu_r",
// "none"), or "custom" with `colors` (RGBA floats) for a user table.  false
// for an unknown name or malformed colours; `ss` is then unchanged.
inline bool parse_colormap_property(const std::string&        name,
                                    const std::vector<float>& colors,
                                    SnapshotSeriesState&      ss)
{
    if (name == "custom")
    {
        SnapshotSeriesState probe;
        probe.colormap        = static_cast<uint8_t>(ColormapType::Custom);
        probe.colormap_colors = colors;
        Colormap cm;
        if (!colormap_from_state(probe, cm))
            return false;
        ss.colormap          = probe.colormap;
        ss.colormap_reversed = false;
        ss.colormap_colors   = colors;
        return true;
    }
    const auto cm = Colormap::from_name(name);
    if (!cm || cm->is_custom())
        return false;
    ss.colormap          = static_cast<uint8_t>(cm->type());
    ss.colormap_reversed = cm->is_reversed();
    ss.colormap_colors.clear();
    return true;
}

// ─── Live series ────────────────────────────────────────────────────────────

// Colormap fields of a live series (no-op for a type without a colormap).
inline void series_colormap_to_state(const Series& s, SnapshotSeriesState& ss)
{
    auto store_range = [&](bool set, float lo, float hi)
    {
        ss.colormap_range_set = set;
        ss.colormap_min       = set ? lo : 0.0f;
        ss.colormap_max       = set ? hi : 1.0f;
    };
    if (auto* surf = dynamic_cast<const SurfaceSeries*>(&s))
    {
        colormap_to_state(surf->cmap(), ss);
        store_range(surf->colormap_range_set(), surf->colormap_min(), surf->colormap_max());
    }
    else if (auto* sc = dynamic_cast<const ScatterSeries*>(&s))
    {
        colormap_to_state(sc->cmap(), ss);
        store_range(sc->colormap_range_set(), sc->colormap_min(), sc->colormap_max());
        const auto v = sc->color_values_data();
        ss.color_values.assign(v.begin(), v.end());
    }
    else if (auto* s3 = dynamic_cast<const ScatterSeries3D*>(&s))
    {
        colormap_to_state(s3->cmap(), ss);
        store_range(s3->colormap_range_set(), s3->colormap_min(), s3->colormap_max());
        const auto v = s3->color_values_data();
        ss.color_values.assign(v.begin(), v.end());
    }
}

// Snapshot / op colormap fields -> a live surface or scatter.  An unset
// colormap keeps the series' own; the range is set or cleared; per-point
// values are applied when the state carries some.
inline void apply_series_colormap(Series& s, const SnapshotSeriesState& ss)
{
    Colormap   cm;
    const bool has_cm = colormap_from_state(ss, cm);
    const bool has_range =
        ss.colormap_range_set && std::isfinite(ss.colormap_min) && std::isfinite(ss.colormap_max);
    const bool has_values = !ss.color_values.empty() && ss.color_values.size() <= MAX_COLOR_VALUES;
    if (auto* surf = dynamic_cast<SurfaceSeries*>(&s))
    {
        if (has_cm && !(surf->cmap() == cm))
            surf->colormap(cm);
        if (has_range)
            surf->colormap_range(ss.colormap_min, ss.colormap_max);
        else if (surf->colormap_range_set())
            surf->clear_colormap_range();
    }
    else if (auto* sc = dynamic_cast<ScatterSeries*>(&s))
    {
        if (has_values)
            sc->color_values(ss.color_values);
        if (has_cm && !(sc->cmap() == cm))
            sc->colormap(cm);
        if (has_range)
            sc->colormap_range(ss.colormap_min, ss.colormap_max);
        else if (sc->colormap_range_set())
            sc->clear_colormap_range();
    }
    else if (auto* s3 = dynamic_cast<ScatterSeries3D*>(&s))
    {
        if (has_values)
            s3->color_values(ss.color_values);
        if (has_cm && !(s3->cmap() == cm))
            s3->colormap(cm);
        if (has_range)
            s3->colormap_range(ss.colormap_min, ss.colormap_max);
        else if (s3->colormap_range_set())
            s3->clear_colormap_range();
    }
}

// ─── SET_SERIES_COLORMAP op ─────────────────────────────────────────────────

// Snapshot colormap fields (not the per-point values) -> op fields.
inline void series_colormap_to_op(const SnapshotSeriesState& ss, DiffOp& op)
{
    op.type     = DiffOp::Type::SET_SERIES_COLORMAP;
    op.f1       = ss.colormap;
    op.bool_val = ss.colormap_reversed;
    op.f2       = ss.colormap_range_set ? 1.0 : 0.0;
    op.f3       = ss.colormap_min;
    op.f4       = ss.colormap_max;
    op.data     = ss.colormap == static_cast<uint8_t>(ColormapType::Custom) ? ss.colormap_colors
                                                                            : std::vector<float>{};
}

// Op fields -> snapshot colormap fields (the per-point values are kept).
inline void series_colormap_from_op(const DiffOp& op, SnapshotSeriesState& ss)
{
    const bool valid_type =
        std::isfinite(op.f1) && op.f1 >= 0.0 && op.f1 <= static_cast<double>(ColormapType::Custom);
    ss.colormap           = valid_type ? static_cast<uint8_t>(op.f1) : SERIES_STYLE_UNSET;
    ss.colormap_reversed  = op.bool_val;
    ss.colormap_range_set = op.f2 >= 0.5 && std::isfinite(op.f3) && std::isfinite(op.f4);
    ss.colormap_min       = ss.colormap_range_set ? static_cast<float>(op.f3) : 0.0f;
    ss.colormap_max       = ss.colormap_range_set ? static_cast<float>(op.f4) : 1.0f;
    ss.colormap_colors =
        ss.colormap == static_cast<uint8_t>(ColormapType::Custom) ? op.data : std::vector<float>{};
}

}   // namespace spectra::ipc

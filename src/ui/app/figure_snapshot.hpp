// figure_snapshot.hpp — Serialize a Figure into an IPC SnapshotFigureState.
//
// Used by the multiproc runtime (app_multiproc.cpp) to push figures to the
// backend, and by the window agent (spectra-window) to rebuild the statistical
// series from the snapshot; header-only so unit tests can exercise it directly.
//
// Snapshot order: one SnapshotAxisState per figure grid slot (2D or 3D), and
// one SnapshotSeriesState per supported series, slot by slot, with axes_index
// naming the slot.  The figure-wide series index used by STATE_DIFF ops is the
// position in that list (see for_each_snapshot_series()).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_stats.hpp>
#include <string>
#include <utility>
#include <vector>

#include "ipc/annotation_state.hpp"
#include "ipc/message.hpp"

namespace spectra
{

// ─── Axes slots ─────────────────────────────────────────────────────────────

// Number of grid slots a figure occupies: 2D axes live in axes(), 3D axes in
// all_axes(), both indexed by grid slot (subplot index - 1).
inline size_t snapshot_axes_slot_count(const Figure& fig)
{
    return std::max(fig.axes().size(), fig.all_axes().size());
}

// The axes in grid slot `slot`, or nullptr for an empty slot.
inline const AxesBase* snapshot_axes_at(const Figure& fig, size_t slot)
{
    if (slot < fig.axes().size() && fig.axes()[slot])
        return fig.axes()[slot].get();
    if (slot < fig.all_axes().size() && fig.all_axes()[slot])
        return fig.all_axes()[slot].get();
    return nullptr;
}

// ─── Statistical series payloads ────────────────────────────────────────────
//
// Box, violin, histogram and bar series travel as their *source* data so the
// window rebuilds the very same series object (fills, gradients, outliers and
// legend swatches included).  `data` holds a fixed header of style parameters
// followed by a count and the per-item payload, all as floats:
//
//   "bar"       [bar_width, baseline, orientation(0 vertical/1 horizontal), gradient,
//                n, positions[n], heights[n]]
//   "histogram" [bins, cumulative, density, gradient, n, values[n]]
//   "box"       [box_width, show_outliers, notched, gradient, n_boxes,
//                per box: x, median, q1, q3, whisker_low, whisker_high, count,
//                         n_outliers, outliers[n_outliers]]
//   "violin"    [violin_width, resolution, show_box, gradient, n_violins,
//                per violin: x, n, values[n]]

namespace snapshot_detail
{

// Upper bound on any count read back from a payload (an IPC peer is untrusted).
constexpr float MAX_PAYLOAD_COUNT = 16'777'216.0f;   // 2^24, exact in float

inline float flag(bool b)
{
    return b ? 1.0f : 0.0f;
}

// Bounds-checked reader over a float payload.
class PayloadReader
{
   public:
    explicit PayloadReader(std::span<const float> d) : d_(d) {}

    bool read(float& out)
    {
        if (pos_ >= d_.size())
            return false;
        out = d_[pos_++];
        return true;
    }
    // A non-negative integral count whose items (`item_floats` floats each)
    // fit in the remaining payload.
    bool read_count(size_t& out, size_t item_floats)
    {
        float f = 0.0f;
        if (!read(f) || !std::isfinite(f) || f < 0.0f || f > MAX_PAYLOAD_COUNT
            || f != std::floor(f))
            return false;
        out = static_cast<size_t>(f);
        return out * item_floats <= remaining();
    }
    bool read_span(size_t n, std::span<const float>& out)
    {
        if (n > remaining())
            return false;
        out = d_.subspan(pos_, n);
        pos_ += n;
        return true;
    }
    size_t remaining() const { return d_.size() - pos_; }

   private:
    std::span<const float> d_;
    size_t                 pos_ = 0;
};

}   // namespace snapshot_detail

// Series type name used on the wire, or "" for a series the snapshot does not
// carry.
inline const char* snapshot_series_type(const Series& s)
{
    if (dynamic_cast<const LineSeries*>(&s))
        return "line";
    if (dynamic_cast<const ScatterSeries*>(&s))
        return "scatter";
    if (dynamic_cast<const BoxPlotSeries*>(&s))
        return "box";
    if (dynamic_cast<const ViolinSeries*>(&s))
        return "violin";
    if (dynamic_cast<const HistogramSeries*>(&s))
        return "histogram";
    if (dynamic_cast<const BarSeries*>(&s))
        return "bar";
    if (dynamic_cast<const LineSeries3D*>(&s))
        return "line3d";
    if (dynamic_cast<const ScatterSeries3D*>(&s))
        return "scatter3d";
    if (dynamic_cast<const SurfaceSeries*>(&s))
        return "surface";
    return "";
}

inline bool is_stats_series_type(const std::string& t)
{
    return t == "box" || t == "violin" || t == "histogram" || t == "bar";
}

// Fill ss.data / ss.point_count for `s`.  Returns false for an unsupported type.
inline bool encode_snapshot_series_data(const Series& s, ipc::SnapshotSeriesState& ss)
{
    using snapshot_detail::flag;
    ss.data.clear();
    ss.point_count = 0;

    auto interleave2 = [&](std::span<const float> xd, std::span<const float> yd)
    {
        const size_t n = std::min(xd.size(), yd.size());
        ss.data.reserve(n * 2);
        for (size_t i = 0; i < n; ++i)
        {
            ss.data.push_back(xd[i]);
            ss.data.push_back(yd[i]);
        }
        ss.point_count = static_cast<uint32_t>(n);
    };
    auto interleave3 =
        [&](std::span<const float> xd, std::span<const float> yd, std::span<const float> zd)
    {
        const size_t n = std::min({xd.size(), yd.size(), zd.size()});
        ss.data.reserve(n * 3);
        for (size_t i = 0; i < n; ++i)
        {
            ss.data.push_back(xd[i]);
            ss.data.push_back(yd[i]);
            ss.data.push_back(zd[i]);
        }
        ss.point_count = static_cast<uint32_t>(n);
    };

    if (auto* line = dynamic_cast<const LineSeries*>(&s))
    {
        interleave2(line->x_data(), line->y_data());
        return true;
    }
    if (auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
    {
        interleave2(scatter->x_data(), scatter->y_data());
        return true;
    }
    if (auto* box = dynamic_cast<const BoxPlotSeries*>(&s))
    {
        const auto&  pos   = box->positions();
        const auto&  stats = box->stats();
        const size_t n     = std::min(pos.size(), stats.size());
        ss.data            = {box->box_width(),
                              flag(box->show_outliers()),
                              flag(box->notched()),
                              flag(box->gradient()),
                              static_cast<float>(n)};
        for (size_t i = 0; i < n; ++i)
        {
            const auto& st = stats[i];
            ss.data.insert(ss.data.end(),
                           {pos[i],
                            st.median,
                            st.q1,
                            st.q3,
                            st.whisker_low,
                            st.whisker_high,
                            static_cast<float>(st.count),
                            static_cast<float>(st.outliers.size())});
            ss.data.insert(ss.data.end(), st.outliers.begin(), st.outliers.end());
        }
        ss.point_count = static_cast<uint32_t>(n);
        return true;
    }
    if (auto* violin = dynamic_cast<const ViolinSeries*>(&s))
    {
        const auto& vs = violin->violins();
        ss.data        = {violin->violin_width(),
                          static_cast<float>(violin->resolution()),
                          flag(violin->show_box()),
                          flag(violin->gradient()),
                          static_cast<float>(vs.size())};
        for (const auto& v : vs)
        {
            ss.data.push_back(v.x_position);
            ss.data.push_back(static_cast<float>(v.values.size()));
            ss.data.insert(ss.data.end(), v.values.begin(), v.values.end());
        }
        ss.point_count = static_cast<uint32_t>(vs.size());
        return true;
    }
    if (auto* hist = dynamic_cast<const HistogramSeries*>(&s))
    {
        const auto& raw = hist->raw_values();
        ss.data         = {static_cast<float>(hist->bins()),
                           flag(hist->cumulative()),
                           flag(hist->density()),
                           flag(hist->gradient()),
                           static_cast<float>(raw.size())};
        ss.data.insert(ss.data.end(), raw.begin(), raw.end());
        ss.point_count = static_cast<uint32_t>(raw.size());
        return true;
    }
    if (auto* bar = dynamic_cast<const BarSeries*>(&s))
    {
        const auto&  pos = bar->bar_positions();
        const auto&  h   = bar->bar_heights();
        const size_t n   = std::min(pos.size(), h.size());
        const auto   nd  = static_cast<std::ptrdiff_t>(n);
        ss.data          = {bar->bar_width(),
                            bar->baseline(),
                            flag(bar->orientation() == BarOrientation::Horizontal),
                            flag(bar->gradient()),
                            static_cast<float>(n)};
        ss.data.insert(ss.data.end(), pos.begin(), pos.begin() + nd);
        ss.data.insert(ss.data.end(), h.begin(), h.begin() + nd);
        ss.point_count = static_cast<uint32_t>(n);
        return true;
    }
    if (auto* l3 = dynamic_cast<const LineSeries3D*>(&s))
    {
        interleave3(l3->x_data(), l3->y_data(), l3->z_data());
        return true;
    }
    if (auto* s3 = dynamic_cast<const ScatterSeries3D*>(&s))
    {
        interleave3(s3->x_data(), s3->y_data(), s3->z_data());
        return true;
    }
    if (auto* surf = dynamic_cast<const SurfaceSeries*>(&s))
    {
        // Raveled meshgrid: one (x, y, z) triple per grid node, row-major in y,
        // which is what the window unravels back into grid vectors.
        const auto   xg = surf->x_grid();
        const auto   yg = surf->y_grid();
        const auto   zv = surf->z_values();
        const size_t nx = xg.size();
        const size_t ny = yg.size();
        if (zv.size() < nx * ny)
            return true;   // inconsistent grid: send an empty surface
        ss.data.reserve(nx * ny * 3);
        for (size_t r = 0; r < ny; ++r)
        {
            for (size_t c = 0; c < nx; ++c)
            {
                ss.data.push_back(xg[c]);
                ss.data.push_back(yg[r]);
                ss.data.push_back(zv[r * nx + c]);
            }
        }
        ss.point_count = static_cast<uint32_t>(nx * ny);
        return true;
    }
    return false;
}

// Rebuild a statistical series on `ax` from a snapshot payload.  Returns the
// new series, or nullptr when the type is not a stats type or the payload is
// malformed (nothing is added then).
inline Series* add_stats_series_from_snapshot(Axes& ax, const ipc::SnapshotSeriesState& ss)
{
    if (!is_stats_series_type(ss.type))
        return nullptr;

    snapshot_detail::PayloadReader rd(ss.data);
    float                          p[4]{};
    for (float& v : p)
    {
        if (!rd.read(v) || !std::isfinite(v))
            return nullptr;
    }
    const bool b1 = p[1] != 0.0f;
    const bool b2 = p[2] != 0.0f;
    const bool b3 = p[3] != 0.0f;

    if (ss.type == "bar")
    {
        size_t                 n = 0;
        std::span<const float> pos;
        std::span<const float> h;
        if (!rd.read_count(n, 2) || !rd.read_span(n, pos) || !rd.read_span(n, h))
            return nullptr;
        auto& s = ax.bar(pos, h);
        s.bar_width(p[0]).baseline(p[1]);
        s.orientation(b2 ? BarOrientation::Horizontal : BarOrientation::Vertical);
        s.gradient(b3);
        return &s;
    }
    if (ss.type == "histogram")
    {
        size_t                 n = 0;
        std::span<const float> values;
        if (!rd.read_count(n, 1) || !rd.read_span(n, values))
            return nullptr;
        const int bins = static_cast<int>(std::clamp(p[0], 1.0f, 100'000.0f));
        auto&     s    = ax.histogram(values, bins);
        s.cumulative(b1).density(b2).gradient(b3);
        return &s;
    }
    if (ss.type == "box")
    {
        size_t n = 0;
        if (!rd.read_count(n, 8))
            return nullptr;
        std::vector<float>        positions;
        std::vector<BoxPlotStats> stats;
        positions.reserve(n);
        stats.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            float        x     = 0.0f;
            float        count = 0.0f;
            BoxPlotStats st;
            if (!rd.read(x) || !rd.read(st.median) || !rd.read(st.q1) || !rd.read(st.q3)
                || !rd.read(st.whisker_low) || !rd.read(st.whisker_high) || !rd.read(count))
                return nullptr;
            size_t                 n_out = 0;
            std::span<const float> outliers;
            if (!rd.read_count(n_out, 1) || !rd.read_span(n_out, outliers))
                return nullptr;
            st.count = (std::isfinite(count) && count >= 1.0f && count <= 1e15f)
                           ? static_cast<std::size_t>(count)
                           : 0;
            st.outliers.assign(outliers.begin(), outliers.end());
            positions.push_back(x);
            stats.push_back(std::move(st));
        }
        auto& s = ax.box_plot();
        s.box_width(p[0]).show_outliers(b1).notched(b2).gradient(b3);
        for (size_t i = 0; i < n; ++i)
            s.add_box(positions[i], std::move(stats[i]));
        return &s;
    }

    // "violin"
    size_t n = 0;
    if (!rd.read_count(n, 2))
        return nullptr;
    std::vector<std::pair<float, std::span<const float>>> violins;
    violins.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        float                  x     = 0.0f;
        size_t                 count = 0;
        std::span<const float> values;
        if (!rd.read(x) || !rd.read_count(count, 1) || !rd.read_span(count, values))
            return nullptr;
        violins.emplace_back(x, values);
    }
    auto& s = ax.violin();
    s.violin_width(p[0])
        .resolution(static_cast<int>(std::clamp(p[1], 2.0f, 10'000.0f)))
        .show_box(b2)
        .gradient(b3);
    for (const auto& [x, values] : violins)
        s.add_violin(x, values);
    return &s;
}

// ─── Series enumeration ─────────────────────────────────────────────────────

// Visit every series the snapshot carries, in snapshot order:
// fn(uint32_t axes_slot, uint32_t figure_wide_index, const Series&).
template <typename Fn>
inline void for_each_snapshot_series(const Figure& fig, Fn&& fn)
{
    uint32_t     global = 0;
    const size_t slots  = snapshot_axes_slot_count(fig);
    for (size_t slot = 0; slot < slots; ++slot)
    {
        const AxesBase* ax = snapshot_axes_at(fig, slot);
        if (!ax)
            continue;
        for (const auto& s_ptr : ax->series())
        {
            if (!s_ptr || snapshot_series_type(*s_ptr)[0] == '\0')
                continue;
            fn(static_cast<uint32_t>(slot), global++, *s_ptr);
        }
    }
}

// ─── Serialize a Figure into a SnapshotFigureState ──────────────────────────
inline ipc::SnapshotFigureState figure_to_snapshot(const Figure& fig, uint64_t figure_id)
{
    ipc::SnapshotFigureState snap;
    snap.figure_id = figure_id;
    snap.title     = fig.tab_title();   // empty = caller / window picks "Figure N"
    snap.width     = fig.width();
    snap.height    = fig.height();
    snap.grid_rows = fig.grid_rows();
    snap.grid_cols = fig.grid_cols();

    // One axes entry per grid slot so axes_index == subplot index - 1 on both
    // sides; an empty slot (gap in the grid) becomes a blank 2D axes.
    const size_t slots = snapshot_axes_slot_count(fig);
    for (size_t slot = 0; slot < slots; ++slot)
    {
        ipc::SnapshotAxisState sa;
        const AxesBase*        base = snapshot_axes_at(fig, slot);
        if (auto* ax = dynamic_cast<const Axes*>(base))
        {
            sa.x_min   = ax->x_limits().min;
            sa.x_max   = ax->x_limits().max;
            sa.y_min   = ax->y_limits().min;
            sa.y_max   = ax->y_limits().max;
            sa.x_label = ax->xlabel();
            sa.y_label = ax->ylabel();
            for (const auto& ann : ax->annotations())
            {
                if (ann)
                    sa.annotations.push_back(ipc::annotation_to_state(*ann));
            }
        }
        else if (auto* ax3 = dynamic_cast<const Axes3D*>(base))
        {
            sa.is_3d   = true;
            sa.x_min   = ax3->x_limits().min;
            sa.x_max   = ax3->x_limits().max;
            sa.y_min   = ax3->y_limits().min;
            sa.y_max   = ax3->y_limits().max;
            sa.z_min   = ax3->z_limits().min;
            sa.z_max   = ax3->z_limits().max;
            sa.x_label = ax3->xlabel();
            sa.y_label = ax3->ylabel();
        }
        if (base)
        {
            sa.grid_visible = base->grid_enabled();
            sa.title        = base->title();
        }
        snap.axes.push_back(std::move(sa));
    }

    for_each_snapshot_series(
        fig,
        [&](uint32_t slot, uint32_t /*global*/, const Series& s)
        {
            ipc::SnapshotSeriesState ss;
            ss.type         = snapshot_series_type(s);
            ss.axes_index   = slot;
            ss.name         = s.label();
            ss.color_r      = s.color().r;
            ss.color_g      = s.color().g;
            ss.color_b      = s.color().b;
            ss.color_a      = s.color().a;
            ss.color_index  = s.auto_color_index();   // window resolves from its palette
            ss.color_letter = static_cast<uint8_t>(s.color_letter());   // ... or letter shade
            ss.visible      = s.visible();
            ss.opacity      = s.opacity();
            // x_data()/y_data() are relative to these double origins when the
            // series was fed double data with large values (e.g. timestamps).
            ss.x_origin    = s.x_offset();
            ss.y_origin    = s.y_offset();
            ss.marker_size = s.marker_size();

            if (auto* line = dynamic_cast<const LineSeries*>(&s))
                ss.line_width = line->width();
            else if (auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
                ss.marker_size = scatter->size();
            else if (auto* l3 = dynamic_cast<const LineSeries3D*>(&s))
                ss.line_width = l3->width();
            else if (auto* s3 = dynamic_cast<const ScatterSeries3D*>(&s))
                ss.marker_size = s3->size();

            encode_snapshot_series_data(s, ss);
            snap.series.push_back(std::move(ss));
        });

    return snap;
}

}   // namespace spectra

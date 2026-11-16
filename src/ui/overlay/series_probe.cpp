#include "series_probe.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/series.hpp>
#include <spectra/series_image.hpp>
#include <spectra/series_stats.hpp>

namespace spectra::probe
{

namespace
{

// Random access to the x/y samples of contiguous spans.
struct SpanSamples
{
    std::span<const float> xs;
    std::span<const float> ys;

    size_t size() const { return std::min(xs.size(), ys.size()); }
    float  x(size_t i) const { return xs[i]; }
    float  y(size_t i) const { return ys[i]; }
};

// Random access to a chunked series: chunk i holds samples
// [starts[i], starts[i + 1]); x and y share the chunk layout.
struct ChunkedSamples
{
    const ChunkedLineSeries& s;
    std::vector<size_t>      starts;   // chunk start indices + total at the end

    explicit ChunkedSamples(const ChunkedLineSeries& series) : s(series)
    {
        const size_t n = s.storage_chunk_count();
        starts.reserve(n + 1);
        size_t total = 0;
        for (size_t c = 0; c < n; ++c)
        {
            starts.push_back(total);
            total += std::min(s.x_chunk(c).size(), s.y_chunk(c).size());
        }
        starts.push_back(total);
    }

    size_t size() const { return starts.back(); }

    std::pair<size_t, size_t> locate(size_t i) const
    {
        // Last chunk whose start is <= i.
        const auto it = std::upper_bound(starts.begin(), starts.end() - 1, i);
        const auto c  = static_cast<size_t>(it - starts.begin()) - 1;
        return {c, i - starts[c]};
    }
    float x(size_t i) const
    {
        const auto [c, k] = locate(i);
        return s.x_chunk(c)[k];
    }
    float y(size_t i) const
    {
        const auto [c, k] = locate(i);
        return s.y_chunk(c)[k];
    }
};

// First index whose x is > lx (x sorted ascending).
template <typename Samples>
size_t upper_index(const Samples& d, double lx)
{
    size_t lo = 0;
    size_t hi = d.size();
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (static_cast<double>(d.x(mid)) <= lx)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// First index whose x is >= lx (x sorted ascending).
template <typename Samples>
size_t lower_index(const Samples& d, double lx)
{
    size_t lo = 0;
    size_t hi = d.size();
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (static_cast<double>(d.x(mid)) < lx)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

bool finite_xy(float x, float y)
{
    return std::isfinite(x) && std::isfinite(y);
}

template <typename Samples>
ProbeSample sample_sorted(const Samples& d, double lx, ProbeMode mode)
{
    ProbeSample  out;
    const size_t n = d.size();
    if (n == 0 || !std::isfinite(lx))
        return out;

    const size_t hi = upper_index(d, lx);   // x[hi - 1] <= lx < x[hi]
    if (hi == 0)
        return out;   // left of the first sample
    const size_t lo = hi - 1;
    if (hi == n)
    {
        // At or right of the last sample: only an exact hit counts.
        if (static_cast<double>(d.x(lo)) == lx && finite_xy(d.x(lo), d.y(lo)))
        {
            out.valid = true;
            out.x     = d.x(lo);
            out.y     = d.y(lo);
            out.index = lo;
        }
        return out;
    }

    const double x0 = d.x(lo);
    const double x1 = d.x(hi);
    const double y0 = d.y(lo);
    const double y1 = d.y(hi);

    if (mode == ProbeMode::Nearest)
    {
        // Nearest finite sample of the two around lx (step over NaN gaps).
        const bool ok0 = finite_xy(d.x(lo), d.y(lo));
        const bool ok1 = finite_xy(d.x(hi), d.y(hi));
        if (!ok0 && !ok1)
            return out;
        const bool   pick_hi = !ok0 || (ok1 && (x1 - lx) < (lx - x0));
        const size_t idx     = pick_hi ? hi : lo;
        out.valid            = true;
        out.x                = d.x(idx);
        out.y                = d.y(idx);
        out.index            = idx;
        return out;
    }

    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1))
        return out;   // NaN break: the line is not drawn here
    out.valid = true;
    out.x     = lx;
    if (x1 == x0)
    {
        out.y     = y1;
        out.index = hi;
        return out;
    }
    const double t   = (lx - x0) / (x1 - x0);
    out.y            = y0 + t * (y1 - y0);
    out.index        = t < 0.5 ? lo : hi;
    out.interpolated = lx != x0;
    if (!out.interpolated)
        out.y = y0;
    return out;
}

// Shift a local sample into absolute coordinates.
ProbeSample to_absolute(ProbeSample s, double xoff, double yoff)
{
    if (s.valid)
    {
        s.x += xoff;
        s.y += yoff;
    }
    return s;
}

}   // namespace

ProbeSample sample_sorted_at_x(std::span<const float> x,
                               std::span<const float> y,
                               double                 lx,
                               ProbeMode              mode)
{
    return sample_sorted(SpanSamples{x, y}, lx, mode);
}

ProbeSample sample_unsorted_at_x(std::span<const float> x,
                                 std::span<const float> y,
                                 double                 lx,
                                 ProbeMode              mode)
{
    ProbeSample  out;
    const size_t n = std::min(x.size(), y.size());
    if (n == 0 || !std::isfinite(lx))
        return out;

    if (mode == ProbeMode::Interpolate)
    {
        for (size_t i = 0; i + 1 < n; ++i)
        {
            const double x0 = x[i];
            const double x1 = x[i + 1];
            const double y0 = y[i];
            const double y1 = y[i + 1];
            if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0)
                || !std::isfinite(y1))
                continue;
            if (lx < std::min(x0, x1) || lx > std::max(x0, x1))
                continue;
            out.valid = true;
            out.x     = lx;
            if (x1 == x0)
            {
                out.y     = y0;
                out.index = i;
                return out;
            }
            const double t   = (lx - x0) / (x1 - x0);
            out.y            = y0 + t * (y1 - y0);
            out.index        = t < 0.5 ? i : i + 1;
            out.interpolated = t > 0.0 && t < 1.0;
            return out;
        }
        if (n == 1 && static_cast<double>(x[0]) == lx && finite_xy(x[0], y[0]))
        {
            out.valid = true;
            out.x     = x[0];
            out.y     = y[0];
        }
        return out;
    }

    double xmin = std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double best = std::numeric_limits<double>::infinity();
    size_t bi   = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (!finite_xy(x[i], y[i]))
            continue;
        const double xi = x[i];
        xmin            = std::min(xmin, xi);
        xmax            = std::max(xmax, xi);
        const double d  = std::abs(xi - lx);
        if (d < best)
        {
            best = d;
            bi   = i;
        }
    }
    if (!std::isfinite(best) || lx < xmin || lx > xmax)
        return out;
    out.valid = true;
    out.x     = x[bi];
    out.y     = y[bi];
    out.index = bi;
    return out;
}

ProbeSample sample_chunked_at_x(const ChunkedLineSeries& s, double lx, ProbeMode mode)
{
    return sample_sorted(ChunkedSamples(s), lx, mode);
}

ProbeSample sample_series_at_x(const Series& s, double abs_x, ProbeMode mode)
{
    const double xoff = s.x_offset();
    const double yoff = s.y_offset();
    const double lx   = abs_x - xoff;

    if (const auto* ls = dynamic_cast<const LineSeries*>(&s))
    {
        // Markers without a line connect nothing: read the nearest sample.
        const ProbeMode m = ls->line_style() == LineStyle::None ? ProbeMode::Nearest : mode;
        const auto r = ls->x_is_sorted() ? sample_sorted_at_x(ls->x_data(), ls->y_data(), lx, m)
                                         : sample_unsorted_at_x(ls->x_data(), ls->y_data(), lx, m);
        return to_absolute(r, xoff, yoff);
    }
    if (const auto* sc = dynamic_cast<const ScatterSeries*>(&s))
    {
        const auto r =
            sc->x_is_sorted()
                ? sample_sorted_at_x(sc->x_data(), sc->y_data(), lx, ProbeMode::Nearest)
                : sample_unsorted_at_x(sc->x_data(), sc->y_data(), lx, ProbeMode::Nearest);
        return to_absolute(r, xoff, yoff);
    }
    if (const auto* st = dynamic_cast<const StemSeries*>(&s))
    {
        const auto r = sample_unsorted_at_x(st->stem_x(), st->stem_y(), lx, ProbeMode::Nearest);
        return to_absolute(r, xoff, yoff);
    }
    if (const auto* cs = dynamic_cast<const ChunkedLineSeries*>(&s))
        return to_absolute(sample_chunked_at_x(*cs, lx, mode), xoff, yoff);
    if (const auto* bs = dynamic_cast<const BarSeries*>(&s))
    {
        ProbeSample out;
        if (bs->orientation() != BarOrientation::Vertical)
            return out;
        const auto&  pos = bs->bar_positions();
        const auto&  h   = bs->bar_heights();
        const double hw  = static_cast<double>(bs->bar_width()) * 0.5;
        const size_t n   = std::min(pos.size(), h.size());
        for (size_t i = n; i-- > 0;)   // last drawn bar wins
        {
            if (lx >= pos[i] - hw && lx <= pos[i] + hw && std::isfinite(h[i]))
            {
                out.valid = true;
                out.x     = pos[i];
                out.y     = h[i];
                out.index = i;
                return to_absolute(out, xoff, yoff);
            }
        }
        return out;
    }
    if (const auto* hs = dynamic_cast<const HistogramSeries*>(&s))
    {
        ProbeSample out;
        const auto  bin = histogram_bin_at_x(*hs, lx);
        if (!bin)
            return out;
        const auto& e = hs->bin_edges();
        out.valid     = true;
        out.x         = (static_cast<double>(e[*bin]) + e[*bin + 1]) * 0.5;
        out.y         = hs->bin_counts()[*bin];
        out.index     = *bin;
        return to_absolute(out, xoff, yoff);
    }
    return {};
}

void sort_compare_rows(std::vector<CompareRow>& rows, CompareSort sort)
{
    if (sort == CompareSort::Legend)
    {
        std::stable_sort(rows.begin(),
                         rows.end(),
                         [](const CompareRow& a, const CompareRow& b)
                         { return a.series_index < b.series_index; });
        return;
    }
    std::stable_sort(rows.begin(),
                     rows.end(),
                     [](const CompareRow& a, const CompareRow& b)
                     {
                         const bool fa = std::isfinite(a.sample.y);
                         const bool fb = std::isfinite(b.sample.y);
                         if (fa != fb)
                             return fa;   // finite values first
                         if (!fa)
                             return a.series_index < b.series_index;
                         return a.sample.y > b.sample.y;
                     });
}

std::vector<CompareRow> compare_at_x(const Axes& axes,
                                     double      abs_x,
                                     ProbeMode   mode,
                                     CompareSort sort)
{
    std::vector<CompareRow> rows;
    int                     idx = -1;
    for (const auto& sp : axes.series())
    {
        ++idx;
        if (!sp || !sp->visible() || sp->is_reference_line())
            continue;
        const ProbeSample smp = sample_series_at_x(*sp, abs_x, mode);
        if (!smp.valid)
            continue;
        rows.push_back({sp.get(), idx, smp});
    }
    sort_compare_rows(rows, sort);
    return rows;
}

SegmentClosest closest_on_segment(double ax, double ay, double bx, double by, double px, double py)
{
    const double dx  = bx - ax;
    const double dy  = by - ay;
    const double len = dx * dx + dy * dy;
    double       t   = 0.0;
    if (len > 0.0)
        t = std::clamp(((px - ax) * dx + (py - ay) * dy) / len, 0.0, 1.0);
    const double cx = ax + t * dx - px;
    const double cy = ay + t * dy - py;
    return {t, cx * cx + cy * cy};
}

IndexRange sorted_window(std::span<const float> x, double lo, double hi)
{
    const SpanSamples d{x, x};
    size_t            b = lower_index(d, lo);
    size_t            e = upper_index(d, hi);
    if (b > 0)
        --b;
    if (e < x.size())
        ++e;
    return {b, std::max(b, e)};
}

std::optional<size_t> bar_at(const BarSeries& bars, double x, double y)
{
    const auto&  pos  = bars.bar_positions();
    const auto&  h    = bars.bar_heights();
    const double hw   = static_cast<double>(bars.bar_width()) * 0.5;
    const double base = bars.baseline();
    const bool   vert = bars.orientation() == BarOrientation::Vertical;
    // Along the bar: x for vertical bars, y for horizontal ones.
    const double along  = vert ? x : y;
    const double across = vert ? y : x;
    const size_t n      = std::min(pos.size(), h.size());
    for (size_t i = n; i-- > 0;)   // last drawn bar wins
    {
        if (!std::isfinite(h[i]))
            continue;
        if (along < pos[i] - hw || along > pos[i] + hw)
            continue;
        const double v0 = std::min(base, static_cast<double>(h[i]));
        const double v1 = std::max(base, static_cast<double>(h[i]));
        if (across >= v0 && across <= v1)
            return i;
    }
    return std::nullopt;
}

std::optional<size_t> histogram_bin_at_x(const HistogramSeries& hist, double x)
{
    const auto& e = hist.bin_edges();
    const auto& c = hist.bin_counts();
    if (e.size() < 2 || c.size() + 1 != e.size() || !std::isfinite(x))
        return std::nullopt;
    if (x < e.front() || x > e.back())
        return std::nullopt;
    const auto it =
        std::upper_bound(e.begin(),
                         e.end(),
                         x,
                         [](double v, float edge) { return v < static_cast<double>(edge); });
    size_t bin = static_cast<size_t>(it - e.begin());
    bin        = bin == 0 ? 0 : bin - 1;
    return std::min(bin, c.size() - 1);
}

std::optional<size_t> histogram_bin_at(const HistogramSeries& hist, double x, double y)
{
    const auto bin = histogram_bin_at_x(hist, x);
    if (!bin)
        return std::nullopt;
    const double h = hist.bin_counts()[*bin];
    if (y < std::min(0.0, h) || y > std::max(0.0, h))
        return std::nullopt;
    return bin;
}

std::optional<float> contour_level_for_point(const ContourSeries& contour, size_t point_index)
{
    for (const auto& r : contour.level_ranges())
    {
        if (point_index >= r.first_point && point_index < r.first_point + r.point_count)
            return r.level;
    }
    return std::nullopt;
}

}   // namespace spectra::probe

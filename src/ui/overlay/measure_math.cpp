#include "measure_math.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <span>

#include "core/axis_scale.hpp"
#include "data/sorted_x_query.hpp"

namespace spectra::measure
{

namespace
{

struct SeriesSamples
{
    std::span<const float> x;
    std::span<const float> y;
    size_t                 count  = 0;
    bool                   sorted = false;
};

SeriesSamples samples_of(const Series& series)
{
    SeriesSamples s;
    if (auto* ls = dynamic_cast<const LineSeries*>(&series))
    {
        s.x      = ls->x_data();
        s.y      = ls->y_data();
        s.count  = ls->point_count();
        s.sorted = ls->x_is_sorted();
    }
    else if (auto* sc = dynamic_cast<const ScatterSeries*>(&series))
    {
        s.x      = sc->x_data();
        s.y      = sc->y_data();
        s.count  = sc->point_count();
        s.sorted = sc->x_is_sorted();
    }
    return s;
}

std::string trim(std::string_view s)
{
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string_view::npos)
        return {};
    const auto e = s.find_last_not_of(" \t");
    return std::string(s.substr(b, e - b + 1));
}

}   // namespace

// ─── Series sample access ───────────────────────────────────────────────────

size_t point_count(const Series& series)
{
    return samples_of(series).count;
}

bool point_at(const Series& series, size_t index, double& x, double& y)
{
    const SeriesSamples s = samples_of(series);
    if (index >= s.count)
        return false;
    x = static_cast<double>(s.x[index]) + series.x_offset();
    y = static_cast<double>(s.y[index]) + series.y_offset();
    return true;
}

bool point_derivative(const Series& series, size_t index, float& dy_dx)
{
    const SeriesSamples s = samples_of(series);
    if (s.count < 2 || index >= s.count)
        return false;
    const size_t lo = index > 0 ? index - 1 : 0;
    const size_t hi = index + 1 < s.count ? index + 1 : s.count - 1;
    const float  dx = s.x[hi] - s.x[lo];
    if (!(std::abs(dx) > 1e-30f))
        return false;
    dy_dx = (s.y[hi] - s.y[lo]) / dx;
    return std::isfinite(dy_dx);
}

// ─── Snapping ───────────────────────────────────────────────────────────────

SnapPoint snap_to_data(const Axes&        axes,
                       const AxisMapping& map,
                       double             sx,
                       double             sy,
                       double             radius_px)
{
    SnapPoint   best;
    double      best_d2 = radius_px * radius_px;
    const Rect& vp      = map.viewport();

    for (const auto& series_ptr : axes.series())
    {
        if (!series_ptr || !series_ptr->visible())
            continue;
        const SeriesSamples s = samples_of(*series_ptr);
        if (s.count == 0)
            continue;
        const double xoff = series_ptr->x_offset();
        const double yoff = series_ptr->y_offset();

        size_t begin = 0;
        size_t end   = s.count;
        if (s.sorted && vp.w > 0.0f)
        {
            // Only samples within the radius horizontally can win.
            const double lo = map.to_data_x(sx - radius_px);
            const double hi = map.to_data_x(sx + radius_px);
            if (std::isfinite(lo) && std::isfinite(hi))
            {
                const auto r = sorted_x_query_range(s.x.first(s.count),
                                                    (lo + hi) * 0.5 - xoff,
                                                    std::abs(hi - lo) * 0.5);
                begin        = r.begin;
                end          = r.end;
            }
        }

        for (size_t i = begin; i < end; ++i)
        {
            const double x = static_cast<double>(s.x[i]) + xoff;
            const double y = static_cast<double>(s.y[i]) + yoff;
            if (!std::isfinite(x) || !std::isfinite(y) || !map.x_drawable(x) || !map.y_drawable(y))
                continue;
            const double dx = map.to_screen_x(x) - sx;
            const double dy = map.to_screen_y(y) - sy;
            const double d2 = dx * dx + dy * dy;
            if (d2 <= best_d2)
            {
                best_d2          = d2;
                best.found       = true;
                best.x           = x;
                best.y           = y;
                best.series      = series_ptr.get();
                best.index       = i;
                best.distance_px = std::sqrt(d2);
            }
        }
    }
    return best;
}

// ─── Measurement ────────────────────────────────────────────────────────────

std::string axis_unit(std::string_view label)
{
    const std::string t = trim(label);
    if (t.size() < 3)
        return {};
    const char close = t.back();
    char       open  = 0;
    if (close == ')')
        open = '(';
    else if (close == ']')
        open = '[';
    else
        return {};
    // Match the bracket that opens the trailing group (allows "(m/s)").
    int depth = 0;
    for (size_t i = t.size(); i-- > 0;)
    {
        if (t[i] == close)
            ++depth;
        else if (t[i] == open && --depth == 0)
        {
            if (i == 0)
                return {};   // the whole label is in brackets: not a unit
            return trim(std::string_view(t).substr(i + 1, t.size() - i - 2));
        }
    }
    return {};
}

Measurement compute_measurement(double           x0,
                                double           y0,
                                double           x1,
                                double           y1,
                                ScaleType        xscale,
                                ScaleType        yscale,
                                std::string_view x_unit,
                                std::string_view y_unit)
{
    Measurement m;
    m.dx = x1 - x0;
    m.dy = y1 - y0;

    if (m.dx != 0.0 && std::isfinite(m.dx) && std::isfinite(m.dy))
    {
        m.slope       = m.dy / m.dx;
        m.slope_valid = std::isfinite(m.slope);
    }

    if (is_log_scale(xscale) && x0 > 0.0 && x1 > 0.0)
    {
        m.x_ratio       = x1 / x0;
        m.x_ratio_valid = std::isfinite(m.x_ratio);
    }
    if (is_log_scale(yscale) && y0 > 0.0 && y1 > 0.0)
    {
        m.y_ratio       = y1 / y0;
        m.y_ratio_valid = std::isfinite(m.y_ratio);
    }

    // A length only makes sense when one unit spans both axes, linearly.
    if (xscale == ScaleType::Linear && yscale == ScaleType::Linear && !x_unit.empty()
        && x_unit == y_unit)
    {
        m.distance       = std::hypot(m.dx, m.dy);
        m.distance_valid = std::isfinite(m.distance);
    }
    return m;
}

double pixel_resolution(ScaleType scale, AxisLimits limits, double pixels, double v)
{
    if (!(pixels > 0.0))
        return 0.0;
    const AxisLimits a    = axis_space_limits(scale, limits);
    const double     step = (a.max - a.min) / pixels;
    if (!std::isfinite(step) || step == 0.0)
        return 0.0;
    if (scale == ScaleType::Linear)
        return std::abs(step);
    if (is_log_scale(scale) && !(v > 0.0))
        return 0.0;
    const double av  = to_axis_space(scale, v);
    const double res = std::abs(from_axis_space(scale, av + step) - v);
    return std::isfinite(res) ? res : 0.0;
}

std::string format_value(double v, double resolution)
{
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0.0 ? "inf" : "-inf";
    if (resolution > 0.0 && std::isfinite(resolution))
    {
        if (std::abs(v) < resolution * 0.5)
            return "0";
        // Enough decimals that the last digit is about one pixel (0.5 -> 0,
        // 0.03 -> 2, 0.01 -> 2).
        const int decimals =
            std::max(0, static_cast<int>(std::floor(-std::log10(resolution) + 0.5)));
        if (decimals <= 6 && std::abs(v) >= 1e-4 && std::abs(v) < 1e12)
            return std::format("{:.{}f}", v, decimals);
        const int sig =
            std::clamp(static_cast<int>(std::ceil(std::log10(std::abs(v) / resolution))) + 1,
                       2,
                       12);
        return std::format("{:.{}g}", v, sig);
    }
    if (v == 0.0)
        return "0";
    if (std::abs(v) >= 1e6 && std::abs(v) < 1e12)
        return std::format("{:.0f}", v);   // epoch seconds etc.: no "1.7e+09"
    return std::format("{:.6g}", v);
}

MeasureFormat measure_format(const Axes& axes, double x_at, double y_at)
{
    MeasureFormat f;
    f.x_unit = axis_unit(axes.xlabel());
    f.y_unit = axis_unit(axes.ylabel());
    if (f.x_unit.empty() && axes.xaxis_datetime())
        f.x_unit = "s";
    if (f.y_unit.empty() && axes.yaxis_datetime())
        f.y_unit = "s";
    f.xscale       = axes.xscale();
    f.yscale       = axes.yscale();
    const Rect& vp = axes.viewport();
    f.x_resolution = pixel_resolution(f.xscale, axes.x_limits(), vp.w, x_at);
    f.y_resolution = pixel_resolution(f.yscale, axes.y_limits(), vp.h, y_at);
    return f;
}

std::vector<std::string> measurement_lines(const Measurement& m, const MeasureFormat& fmt)
{
    auto with_unit = [](std::string s, const std::string& unit)
    {
        if (!unit.empty())
            s += " " + unit;
        return s;
    };
    auto ratio = [](double r)
    { return std::format("  (\xC3\x97{:.3g}, {:.2f} dec)", r, std::log10(r)); };

    std::vector<std::string> lines;
    std::string dx = "\xCE\x94x  " + with_unit(format_value(m.dx, fmt.x_resolution), fmt.x_unit);
    if (m.x_ratio_valid)
        dx += ratio(m.x_ratio);
    lines.push_back(std::move(dx));

    std::string dy = "\xCE\x94y  " + with_unit(format_value(m.dy, fmt.y_resolution), fmt.y_unit);
    if (m.y_ratio_valid)
        dy += ratio(m.y_ratio);
    lines.push_back(std::move(dy));

    if (m.slope_valid)
    {
        std::string unit;
        if (!fmt.x_unit.empty() && !fmt.y_unit.empty())
            unit = fmt.y_unit + "/" + fmt.x_unit;
        lines.push_back(with_unit("slope  " + std::format("{:.4g}", m.slope), unit));
    }
    if (m.distance_valid)
    {
        const double res = std::max(fmt.x_resolution, fmt.y_resolution);
        lines.push_back(with_unit("dist  " + format_value(m.distance, res), fmt.x_unit));
    }
    return lines;
}

// ─── Data tip navigation ────────────────────────────────────────────────────

std::optional<size_t> step_index(const Series& series, size_t index, int delta)
{
    const size_t n = point_count(series);
    if (n == 0 || delta == 0)
        return std::nullopt;
    const long long target =
        std::clamp(static_cast<long long>(index) + delta, 0LL, static_cast<long long>(n) - 1);
    if (static_cast<size_t>(target) == index)
        return std::nullopt;
    return static_cast<size_t>(target);
}

std::optional<size_t> drag_index(const Series& series, const AxisMapping& map, double sx, double sy)
{
    const SeriesSamples s = samples_of(series);
    if (s.count == 0)
        return std::nullopt;

    if (s.sorted)
    {
        const double target = map.to_data_x(sx) - series.x_offset();
        if (!std::isfinite(target))
            return std::nullopt;
        const auto first = s.x.begin();
        const auto last  = s.x.begin() + static_cast<std::ptrdiff_t>(s.count);
        const auto it =
            std::lower_bound(first,
                             last,
                             target,
                             [](float lhs, double rhs) { return static_cast<double>(lhs) < rhs; });
        size_t i = static_cast<size_t>(it - first);
        if (i >= s.count)
            return s.count - 1;
        if (i > 0)
        {
            // Pick the closer neighbour on screen (log axes: in axis space).
            const double xoff = series.x_offset();
            const double d_hi = std::abs(map.to_screen_x(s.x[i] + xoff) - sx);
            const double d_lo = std::abs(map.to_screen_x(s.x[i - 1] + xoff) - sx);
            if (d_lo < d_hi || !std::isfinite(d_hi))
                --i;
        }
        return i;
    }

    double best_d2 = std::numeric_limits<double>::max();
    size_t best    = s.count;
    for (size_t i = 0; i < s.count; ++i)
    {
        const double x = static_cast<double>(s.x[i]) + series.x_offset();
        const double y = static_cast<double>(s.y[i]) + series.y_offset();
        if (!map.x_drawable(x) || !map.y_drawable(y))
            continue;
        const double dx = map.to_screen_x(x) - sx;
        const double dy = map.to_screen_y(y) - sy;
        const double d2 = dx * dx + dy * dy;
        if (std::isfinite(d2) && d2 < best_d2)
        {
            best_d2 = d2;
            best    = i;
        }
    }
    if (best == s.count)
        return std::nullopt;
    return best;
}

}   // namespace spectra::measure

#include "accessible_summary.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

namespace spectra
{

namespace
{

// Determine a human-readable type name for a series.
static std::string series_type_name(const Series& s)
{
    if (dynamic_cast<const ChunkedLineSeries*>(&s))
        return "chunked line";
    if (dynamic_cast<const LineSeries*>(&s))
        return "line";
    if (dynamic_cast<const ScatterSeries*>(&s))
        return "scatter";
    return "series";
}

// Compute y-range summary for a LineSeries or ScatterSeries.
// Returns true and sets out_min/out_max on success.
static bool compute_y_range(const Series& s, float& out_min, float& out_max)
{
    std::span<const float> y;

    if (const auto* line = dynamic_cast<const LineSeries*>(&s))
        y = line->y_data();
    else if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
        y = scatter->y_data();
    else
        return false;

    if (y.empty())
        return false;

    out_min = *std::min_element(y.begin(), y.end());
    out_max = *std::max_element(y.begin(), y.end());
    return true;
}

// Compute point count for a series.
static size_t get_point_count(const Series& s)
{
    if (const auto* line = dynamic_cast<const LineSeries*>(&s))
        return line->point_count();
    if (const auto* scatter = dynamic_cast<const ScatterSeries*>(&s))
        return scatter->point_count();
    if (const auto* chunked = dynamic_cast<const ChunkedLineSeries*>(&s))
        return chunked->point_count();
    return 0;
}

static std::string fmt_float(float v)
{
    return std::format("{:.4g}", v);
}

}   // namespace

// ─────────────────────────────────────────────────────────────────────────────

std::string accessible_series_summary(const Series&         series,
                                      size_t                series_index,
                                      const SummaryOptions& options)
{
    std::ostringstream ss;

    std::string type = series_type_name(series);
    std::string name = series.label();
    if (name.empty())
        name = "series " + std::to_string(series_index + 1);

    ss << type << " '" << name << "'";

    if (options.include_point_count)
    {
        size_t n = get_point_count(series);
        if (n > 0)
            ss << " (" << n << " points)";
    }

    if (options.include_series_ranges)
    {
        float ymin = 0.0f;
        float ymax = 0.0f;
        if (compute_y_range(series, ymin, ymax))
            ss << ", Y range: " << fmt_float(ymin) << " to " << fmt_float(ymax);
    }

    if (options.include_lod_info)
    {
        if (const auto* chunked = dynamic_cast<const ChunkedLineSeries*>(&series))
        {
            auto stats = chunked->last_query_stats();
            ss << ", LoD level: " << stats.lod_level_used;
        }
    }

    if (!series.visible())
        ss << " (hidden)";

    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────

std::string accessible_axes_summary(const Axes&           axes,
                                    size_t                axes_index,
                                    const SummaryOptions& options)
{
    std::ostringstream ss;

    ss << "Axes " << (axes_index + 1);

    const std::string& t = axes.title();
    if (!t.empty())
        ss << " ('" << t << "')";

    const std::string& xl = axes.xlabel();
    const std::string& yl = axes.ylabel();
    if (!xl.empty() || !yl.empty())
    {
        ss << " [";
        if (!xl.empty())
            ss << "X: " << xl;
        if (!xl.empty() && !yl.empty())
            ss << ", ";
        if (!yl.empty())
            ss << "Y: " << yl;
        ss << "]";
    }

    if (options.include_axis_ranges)
    {
        AxisLimits xl = axes.x_limits();
        AxisLimits yl = axes.y_limits();
        ss << ", X view: " << fmt_float(static_cast<float>(xl.min)) << " to "
           << fmt_float(static_cast<float>(xl.max));
        ss << ", Y view: " << fmt_float(static_cast<float>(yl.min)) << " to "
           << fmt_float(static_cast<float>(yl.max));
    }

    const auto& sv = axes.series();
    ss << ": " << sv.size() << " series";

    int limit = std::min(static_cast<int>(sv.size()), options.max_series_in_summary);
    for (int i = 0; i < limit; ++i)
    {
        ss << " — " << accessible_series_summary(*sv[i], static_cast<size_t>(i), options);
    }
    if (static_cast<int>(sv.size()) > limit)
        ss << " — (and " << (sv.size() - static_cast<size_t>(limit)) << " more)";

    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────

std::string accessible_figure_summary(const Figure& figure, const SummaryOptions& options)
{
    std::ostringstream ss;

    const auto& axes_vec = figure.axes();
    ss << "Figure with " << axes_vec.size() << (axes_vec.size() == 1 ? " axis" : " axes");

    int rows = figure.grid_rows();
    int cols = figure.grid_cols();
    if (rows > 1 || cols > 1)
        ss << " (" << rows << "\xc3\x97" << cols << " grid)";

    ss << ".";

    int limit = std::min(static_cast<int>(axes_vec.size()), options.max_series_in_summary);
    for (int i = 0; i < limit; ++i)
    {
        ss << " " << accessible_axes_summary(*axes_vec[i], static_cast<size_t>(i), options) << ".";
    }
    if (static_cast<int>(axes_vec.size()) > limit)
        ss << " (and " << (axes_vec.size() - static_cast<size_t>(limit)) << " more axes).";

    return ss.str();
}

}   // namespace spectra

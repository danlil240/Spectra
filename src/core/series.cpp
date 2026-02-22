#include <cassert>
#include <spectra/series.hpp>

namespace spectra
{

// --- Series (base) ---

Series& Series::plot_style(const PlotStyle& ps)
{
    style_ = ps;
    if (ps.color.has_value())
    {
        color_ = *ps.color;
    }
    dirty_ = true;
    return *this;
}

// --- LineSeries ---

LineSeries::LineSeries(std::span<const float> x, std::span<const float> y)
    : x_(x.begin(), x.end()), y_(y.begin(), y.end())
{
    assert(x.size() == y.size());
    dirty_ = true;
}

LineSeries& LineSeries::set_x(std::span<const float> x)
{
    x_.assign(x.begin(), x.end());
    dirty_ = true;
    return *this;
}

LineSeries& LineSeries::set_y(std::span<const float> y)
{
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    return *this;
}

void LineSeries::append(float x, float y)
{
    x_.push_back(x);
    y_.push_back(y);
    dirty_ = true;
}

LineSeries& LineSeries::format(std::string_view fmt)
{
    PlotStyle ps        = parse_format_string(fmt);
    style_.line_style   = ps.line_style;
    style_.marker_style = ps.marker_style;
    if (ps.color.has_value())
    {
        color_ = *ps.color;
    }
    dirty_ = true;
    return *this;
}

void LineSeries::record_commands(Renderer& /*renderer*/)
{
    // Actual GPU command recording is handled by the Renderer (Agent 1).
    // This is a hook for the renderer to dispatch based on series type.
}

// --- ScatterSeries ---

ScatterSeries::ScatterSeries(std::span<const float> x, std::span<const float> y)
    : x_(x.begin(), x.end()), y_(y.begin(), y.end())
{
    assert(x.size() == y.size());
    dirty_ = true;
}

ScatterSeries& ScatterSeries::set_x(std::span<const float> x)
{
    x_.assign(x.begin(), x.end());
    dirty_ = true;
    return *this;
}

ScatterSeries& ScatterSeries::set_y(std::span<const float> y)
{
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    return *this;
}

void ScatterSeries::append(float x, float y)
{
    x_.push_back(x);
    y_.push_back(y);
    dirty_ = true;
}

ScatterSeries& ScatterSeries::format(std::string_view fmt)
{
    PlotStyle ps        = parse_format_string(fmt);
    style_.line_style   = ps.line_style;
    style_.marker_style = ps.marker_style;
    if (ps.color.has_value())
    {
        color_ = *ps.color;
    }
    dirty_ = true;
    return *this;
}

void ScatterSeries::record_commands(Renderer& /*renderer*/)
{
    // Actual GPU command recording is handled by the Renderer (Agent 1).
}

}   // namespace spectra

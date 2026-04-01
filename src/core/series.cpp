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

Series& Series::apply_format_string(std::string_view fmt)
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

size_t LineSeries::erase_before(float x_threshold)
{
    if (x_.empty())
        return 0;

    // Binary search for the first element >= x_threshold (x_ is sorted ascending).
    size_t lo = 0, hi = x_.size();
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (x_[mid] < x_threshold)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return 0;

    x_.erase(x_.begin(), x_.begin() + static_cast<ptrdiff_t>(lo));
    y_.erase(y_.begin(), y_.begin() + static_cast<ptrdiff_t>(lo));
    dirty_ = true;
    return lo;
}

LineSeries& LineSeries::format(std::string_view fmt)
{
    Series::apply_format_string(fmt);
    return *this;
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
    Series::apply_format_string(fmt);
    return *this;
}

}   // namespace spectra

#include <cassert>
#include <spectra/event_bus.hpp>
#include <spectra/series.hpp>

#include "pending_series_data.hpp"

namespace spectra
{

// --- Series (base) ---

void PendingSeriesDataDeleter::operator()(PendingSeriesData* ptr) const
{
    delete ptr;
}

Series::~Series() = default;

Series::Series(const Series& other)
    : label_(other.label_), color_(other.color_), style_(other.style_), visible_(other.visible_),
      dirty_(other.dirty_.load(std::memory_order_relaxed)), event_system_(other.event_system_),
      owning_axes_(other.owning_axes_), thread_safe_(false)   // Thread-safe state is not copied.
      ,
      pending_(nullptr)
{
}

Series& Series::operator=(const Series& other)
{
    if (this != &other)
    {
        label_   = other.label_;
        color_   = other.color_;
        style_   = other.style_;
        visible_ = other.visible_;
        dirty_.store(other.dirty_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        event_system_ = other.event_system_;
        owning_axes_  = other.owning_axes_;
        // Thread-safe state is NOT copied — must be re-enabled explicitly.
    }
    return *this;
}

void Series::mark_dirty()
{
    dirty_ = true;
    if (event_system_)
        event_system_->series_data_changed().emit({owning_axes_, this});
}

void Series::set_thread_safe(bool enabled)
{
    thread_safe_ = enabled;
    if (enabled && !pending_)
        pending_.reset(new PendingSeriesData());
}

bool Series::commit_pending()
{
    return false;
}

void Series::set_wake_fn(std::function<void()> fn)
{
    if (pending_)
        pending_->set_wake_fn(std::move(fn));
}

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
    if (thread_safe_ && pending_)
    {
        pending_->replace_x(x);
        return *this;
    }
    x_.assign(x.begin(), x.end());
    dirty_ = true;
    return *this;
}

LineSeries& LineSeries::set_y(std::span<const float> y)
{
    if (thread_safe_ && pending_)
    {
        pending_->replace_y(y);
        return *this;
    }
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    return *this;
}

void LineSeries::append(float x, float y)
{
    if (thread_safe_ && pending_)
    {
        pending_->append(x, y);
        return;
    }
    x_.push_back(x);
    y_.push_back(y);
    dirty_ = true;
}

size_t LineSeries::erase_before(float x_threshold)
{
    if (thread_safe_ && pending_)
    {
        pending_->erase_before(x_threshold);
        return 0;   // Actual count available after commit.
    }
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

bool LineSeries::commit_pending()
{
    if (!pending_ || !pending_->has_pending())
        return false;
    if (pending_->commit(x_, y_))
    {
        mark_dirty();
        return true;
    }
    return false;
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
    if (thread_safe_ && pending_)
    {
        pending_->replace_x(x);
        return *this;
    }
    x_.assign(x.begin(), x.end());
    dirty_ = true;
    return *this;
}

ScatterSeries& ScatterSeries::set_y(std::span<const float> y)
{
    if (thread_safe_ && pending_)
    {
        pending_->replace_y(y);
        return *this;
    }
    y_.assign(y.begin(), y.end());
    dirty_ = true;
    return *this;
}

void ScatterSeries::append(float x, float y)
{
    if (thread_safe_ && pending_)
    {
        pending_->append(x, y);
        return;
    }
    x_.push_back(x);
    y_.push_back(y);
    dirty_ = true;
}

ScatterSeries& ScatterSeries::format(std::string_view fmt)
{
    Series::apply_format_string(fmt);
    return *this;
}

bool ScatterSeries::commit_pending()
{
    if (!pending_ || !pending_->has_pending())
        return false;
    if (pending_->commit(x_, y_))
    {
        mark_dirty();
        return true;
    }
    return false;
}

}   // namespace spectra

#include <algorithm>
#include <cmath>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/series_stats.hpp>

namespace spectra
{

// --- Safe series removal ---

void AxesBase::clear_series()
{
    if (on_series_removed_)
    {
        for (auto& s : series_)
            on_series_removed_(s.get());
    }
    series_.clear();
}

bool AxesBase::remove_series(size_t index)
{
    if (index >= series_.size())
        return false;
    if (on_series_removed_)
        on_series_removed_(series_[index].get());
    series_.erase(series_.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

// --- Series creation ---

LineSeries& Axes::line(std::span<const float> x, std::span<const float> y)
{
    auto  s   = std::make_unique<LineSeries>(x, y);
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

LineSeries& Axes::line()
{
    auto  s   = std::make_unique<LineSeries>();
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

ScatterSeries& Axes::scatter(std::span<const float> x, std::span<const float> y)
{
    auto  s   = std::make_unique<ScatterSeries>(x, y);
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

ScatterSeries& Axes::scatter()
{
    auto  s   = std::make_unique<ScatterSeries>();
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

// --- MATLAB-style plot ---

LineSeries& Axes::plot(std::span<const float> x, std::span<const float> y, std::string_view fmt)
{
    auto& ref = line(x, y);
    ref.format(fmt);
    return ref;
}

LineSeries& Axes::plot(std::span<const float> x, std::span<const float> y, const PlotStyle& style)
{
    auto& ref = line(x, y);
    ref.plot_style(style);
    return ref;
}

// --- Statistical series creation ---

BoxPlotSeries& Axes::box_plot()
{
    auto  s   = std::make_unique<BoxPlotSeries>();
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

ViolinSeries& Axes::violin()
{
    auto  s   = std::make_unique<ViolinSeries>();
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

HistogramSeries& Axes::histogram(std::span<const float> values, int bins)
{
    auto  s   = std::make_unique<HistogramSeries>(values, bins);
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

BarSeries& Axes::bar(std::span<const float> positions, std::span<const float> heights)
{
    auto  s   = std::make_unique<BarSeries>(positions, heights);
    auto& ref = *s;
    ref.set_color(palette::default_cycle[series_.size() % palette::default_cycle_size]);
    series_.push_back(std::move(s));
    return ref;
}

// --- Axis configuration ---

void Axes::xlim(float min, float max)
{
    // Explicit manual limits pause streaming follow mode, but keep the
    // configured buffer so users can resume via the Live button.
    presented_buffer_following_ = false;
    xlim_                       = AxisLimits{min, max};
}

void Axes::ylim(float min, float max)
{
    // Explicit manual limits pause streaming follow mode, but keep the
    // configured buffer so users can resume via the Live button.
    presented_buffer_following_ = false;
    ylim_                       = AxisLimits{min, max};
}

void Axes::title(const std::string& t)
{
    title_ = t;
}

void Axes::xlabel(const std::string& lbl)
{
    xlabel_ = lbl;
}

void Axes::ylabel(const std::string& lbl)
{
    ylabel_ = lbl;
}

void Axes::grid(bool enabled)
{
    grid_enabled_ = enabled;
}

void Axes::show_border(bool enabled)
{
    border_enabled_ = enabled;
}

void Axes::autoscale_mode(AutoscaleMode mode)
{
    if (mode == AutoscaleMode::Manual && autoscale_mode_ != AutoscaleMode::Manual)
    {
        // Switching TO Manual: freeze current computed limits so pan/zoom works
        if (!xlim_.has_value())
        {
            auto lim = x_limits();
            xlim_    = lim;
        }
        if (!ylim_.has_value())
        {
            auto lim = y_limits();
            ylim_    = lim;
        }
        // Manual mode should not keep a moving streaming window.
        presented_buffer_following_ = false;
    }
    else if (mode != AutoscaleMode::Manual)
    {
        // Switching to an auto mode: clear explicit limits so the
        // auto-computed limits take effect immediately.
        xlim_.reset();
        ylim_.reset();
    }
    autoscale_mode_ = mode;
}

void Axes::presented_buffer(float seconds)
{
    if (seconds > 0.0f)
    {
        presented_buffer_seconds_   = seconds;
        presented_buffer_following_ = true;
        // Presented buffer drives limits from data, so clear explicit limits.
        xlim_.reset();
        ylim_.reset();
        if (autoscale_mode_ == AutoscaleMode::Manual)
            autoscale_mode_ = AutoscaleMode::Padded;
    }
    else
    {
        presented_buffer_seconds_.reset();
        presented_buffer_following_ = false;
    }
}

// --- Limits ---

// Compute data extent across all series
static void data_extent(const std::vector<std::unique_ptr<Series>>& series,
                        float&                                      x_min,
                        float&                                      x_max,
                        float&                                      y_min,
                        float&                                      y_max)
{
    x_min = std::numeric_limits<float>::max();
    x_max = -std::numeric_limits<float>::max();
    y_min = std::numeric_limits<float>::max();
    y_max = -std::numeric_limits<float>::max();

    for (const auto& s : series)
    {
        // Try LineSeries
        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
        {
            for (auto v : ls->x_data())
            {
                x_min = std::min(x_min, v);
                x_max = std::max(x_max, v);
            }
            for (auto v : ls->y_data())
            {
                y_min = std::min(y_min, v);
                y_max = std::max(y_max, v);
            }
        }
        // Try ScatterSeries
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
        {
            for (auto v : ss->x_data())
            {
                x_min = std::min(x_min, v);
                x_max = std::max(x_max, v);
            }
            for (auto v : ss->y_data())
            {
                y_min = std::min(y_min, v);
                y_max = std::max(y_max, v);
            }
        }
        // Try statistical series (all expose x_data/y_data with NaN breaks)
        auto try_stat = [&](std::span<const float> xd, std::span<const float> yd)
        {
            for (auto v : xd)
            {
                if (!std::isnan(v))
                {
                    x_min = std::min(x_min, v);
                    x_max = std::max(x_max, v);
                }
            }
            for (auto v : yd)
            {
                if (!std::isnan(v))
                {
                    y_min = std::min(y_min, v);
                    y_max = std::max(y_max, v);
                }
            }
        };
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
        {
            try_stat(bp->x_data(), bp->y_data());
            // Include outlier extents
            for (auto v : bp->outlier_y())
            {
                y_min = std::min(y_min, v);
                y_max = std::max(y_max, v);
            }
        }
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            try_stat(vn->x_data(), vn->y_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            try_stat(hs->x_data(), hs->y_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            try_stat(bs->x_data(), bs->y_data());
    }

    // Fallback if no data
    if (x_min > x_max)
    {
        x_min = 0.0f;
        x_max = 1.0f;
    }
    if (y_min > y_max)
    {
        y_min = 0.0f;
        y_max = 1.0f;
    }

    // Add 5% padding
    float x_pad = (x_max - x_min) * 0.05f;
    float y_pad = (y_max - y_min) * 0.05f;
    if (x_pad == 0.0f)
        x_pad = 0.5f;
    if (y_pad == 0.0f)
        y_pad = 0.5f;
    x_min -= x_pad;
    x_max += x_pad;
    y_min -= y_pad;
    y_max += y_pad;
}

static void data_extent_with_mode(const std::vector<std::unique_ptr<Series>>& series,
                                  AutoscaleMode                               mode,
                                  float&                                      x_min,
                                  float&                                      x_max,
                                  float&                                      y_min,
                                  float&                                      y_max)
{
    // Compute raw extent
    data_extent(series, x_min, x_max, y_min, y_max);

    if (mode == AutoscaleMode::Tight)
    {
        // Re-compute without padding (data_extent adds 5% padding)
        x_min = std::numeric_limits<float>::max();
        x_max = -std::numeric_limits<float>::max();
        y_min = std::numeric_limits<float>::max();
        y_max = -std::numeric_limits<float>::max();
        for (const auto& s : series)
        {
            if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
            {
                for (auto v : ls->x_data())
                {
                    x_min = std::min(x_min, v);
                    x_max = std::max(x_max, v);
                }
                for (auto v : ls->y_data())
                {
                    y_min = std::min(y_min, v);
                    y_max = std::max(y_max, v);
                }
            }
            if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
            {
                for (auto v : ss->x_data())
                {
                    x_min = std::min(x_min, v);
                    x_max = std::max(x_max, v);
                }
                for (auto v : ss->y_data())
                {
                    y_min = std::min(y_min, v);
                    y_max = std::max(y_max, v);
                }
            }
        }
        if (x_min > x_max)
        {
            x_min = 0.0f;
            x_max = 1.0f;
        }
        if (y_min > y_max)
        {
            y_min = 0.0f;
            y_max = 1.0f;
        }
        // Handle zero range
        if (x_max == x_min)
        {
            x_min -= 0.5f;
            x_max += 0.5f;
        }
        if (y_max == y_min)
        {
            y_min -= 0.5f;
            y_max += 0.5f;
        }
    }
    // Fit and Padded use the default data_extent behavior (with padding)
}

static bool latest_x_value(const std::vector<std::unique_ptr<Series>>& series, float& latest_x)
{
    bool has_value = false;

    auto update_latest = [&](float x)
    {
        if (!std::isfinite(x))
            return;
        if (!has_value || x > latest_x)
        {
            latest_x  = x;
            has_value = true;
        }
    };

    for (const auto& s : series)
    {
        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
        {
            for (float x : ls->x_data())
                update_latest(x);
        }
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
        {
            for (float x : ss->x_data())
                update_latest(x);
        }

        auto update_from_span = [&](std::span<const float> xd)
        {
            for (float x : xd)
                update_latest(x);
        };
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
            update_from_span(bp->x_data());
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            update_from_span(vn->x_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            update_from_span(hs->x_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            update_from_span(bs->x_data());
    }

    return has_value;
}

static bool windowed_y_extent(const std::vector<std::unique_ptr<Series>>& series,
                              float                                       window_min,
                              float                                       window_max,
                              float&                                      y_min,
                              float&                                      y_max)
{
    y_min      = std::numeric_limits<float>::max();
    y_max      = -std::numeric_limits<float>::max();
    bool has_y = false;

    auto consume_xy = [&](std::span<const float> xd, std::span<const float> yd)
    {
        size_t n = std::min(xd.size(), yd.size());
        for (size_t i = 0; i < n; ++i)
        {
            float x = xd[i];
            float y = yd[i];
            if (!std::isfinite(x) || !std::isfinite(y))
                continue;
            if (x < window_min || x > window_max)
                continue;
            y_min = std::min(y_min, y);
            y_max = std::max(y_max, y);
            has_y = true;
        }
    };

    for (const auto& s : series)
    {
        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
            consume_xy(ls->x_data(), ls->y_data());
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
            consume_xy(ss->x_data(), ss->y_data());
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
            consume_xy(bp->x_data(), bp->y_data());
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            consume_xy(vn->x_data(), vn->y_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            consume_xy(hs->x_data(), hs->y_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            consume_xy(bs->x_data(), bs->y_data());
    }

    return has_y;
}

AxisLimits Axes::x_limits() const
{
    if (presented_buffer_following_ && presented_buffer_seconds_.has_value()
        && presented_buffer_seconds_.value() > 0.0f)
    {
        float latest_x = 0.0f;
        if (latest_x_value(series_, latest_x))
        {
            return {latest_x - presented_buffer_seconds_.value(), latest_x};
        }
    }

    if (xlim_.has_value() || autoscale_mode_ == AutoscaleMode::Manual)
        return xlim_.value_or(AxisLimits{0.0f, 1.0f});
    float xmin, xmax, ymin, ymax;
    data_extent_with_mode(series_, autoscale_mode_, xmin, xmax, ymin, ymax);
    return {xmin, xmax};
}

AxisLimits Axes::y_limits() const
{
    if (presented_buffer_following_ && presented_buffer_seconds_.has_value()
        && presented_buffer_seconds_.value() > 0.0f)
    {
        float latest_x = 0.0f;
        if (latest_x_value(series_, latest_x))
        {
            float y_min = 0.0f;
            float y_max = 0.0f;
            float x_min = latest_x - presented_buffer_seconds_.value();
            if (windowed_y_extent(series_, x_min, latest_x, y_min, y_max))
            {
                if (autoscale_mode_ == AutoscaleMode::Tight)
                    return {y_min, y_max};

                float y_pad = (y_max - y_min) * 0.05f;
                if (y_pad == 0.0f)
                    y_pad = 0.5f;
                return {y_min - y_pad, y_max + y_pad};
            }
        }
    }

    if (ylim_.has_value() || autoscale_mode_ == AutoscaleMode::Manual)
        return ylim_.value_or(AxisLimits{0.0f, 1.0f});
    float xmin, xmax, ymin, ymax;
    data_extent_with_mode(series_, autoscale_mode_, xmin, xmax, ymin, ymax);
    return {ymin, ymax};
}

void Axes::auto_fit()
{
    xlim_.reset();
    ylim_.reset();
}

// --- Tick generation ---
// Simple "nice numbers" algorithm: pick tick spacing as 1, 2, or 5 × 10^n
// to produce roughly 5–10 ticks in the given range.

static float nice_ceil(float x, bool round_flag)
{
    float exp_v = std::floor(std::log10(x));
    float frac  = x / std::pow(10.0f, exp_v);
    float nice;
    if (round_flag)
    {
        if (frac < 1.5f)
            nice = 1.0f;
        else if (frac < 3.0f)
            nice = 2.0f;
        else if (frac < 7.0f)
            nice = 5.0f;
        else
            nice = 10.0f;
    }
    else
    {
        if (frac <= 1.0f)
            nice = 1.0f;
        else if (frac <= 2.0f)
            nice = 2.0f;
        else if (frac <= 5.0f)
            nice = 5.0f;
        else
            nice = 10.0f;
    }
    return nice * std::pow(10.0f, exp_v);
}

static TickResult generate_ticks(float range_min, float range_max, int target_ticks = 7)
{
    TickResult result;

    float range = range_max - range_min;

    // Edge case: zero or negative range
    if (range <= 0.0f)
    {
        // If range is exactly zero, center a tick on the value
        if (range == 0.0f && range_min != 0.0f)
        {
            // Expand around the single value
            float half = std::abs(range_min) * 0.1f;
            if (half == 0.0f)
                half = 0.5f;
            return generate_ticks(range_min - half, range_min + half, target_ticks);
        }
        result.positions.push_back(range_min);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(range_min));
        result.labels.emplace_back(buf);
        return result;
    }

    // Edge case: very small range (< 1e-10) — avoid log10 of near-zero
    if (range < 1e-10f)
    {
        float mid = (range_min + range_max) * 0.5f;
        result.positions.push_back(mid);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(mid));
        result.labels.emplace_back(buf);
        return result;
    }

    float nice_range = nice_ceil(range, false);
    float spacing    = nice_ceil(nice_range / static_cast<float>(target_ticks - 1), true);

    // Guard against degenerate spacing
    if (spacing <= 0.0f || !std::isfinite(spacing))
    {
        result.positions.push_back(range_min);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(range_min));
        result.labels.emplace_back(buf);
        return result;
    }

    float nice_min = std::floor(range_min / spacing) * spacing;
    float nice_max = std::ceil(range_max / spacing) * spacing;

    // Safety: cap iterations to avoid infinite loops
    int max_iters = target_ticks * 3;
    int iters     = 0;
    for (float v = nice_min; v <= nice_max + spacing * 0.5f && iters < max_iters;
         v += spacing, ++iters)
    {
        if (v >= range_min - spacing * 0.01f && v <= range_max + spacing * 0.01f)
        {
            // Snap near-zero values to exactly zero to avoid "-0" labels
            if (std::abs(v) < spacing * 1e-6f)
                v = 0.0f;
            result.positions.push_back(v);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
            result.labels.emplace_back(buf);
        }
    }

    return result;
}

TickResult Axes::compute_x_ticks() const
{
    auto lim = x_limits();
    return generate_ticks(lim.min, lim.max);
}

TickResult Axes::compute_y_ticks() const
{
    auto lim = y_limits();
    return generate_ticks(lim.min, lim.max);
}

}   // namespace spectra

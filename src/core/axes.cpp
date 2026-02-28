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

void Axes::xlim(double min, double max)
{
    // Explicit manual limits pause streaming follow mode, but keep the
    // configured buffer so users can resume via the Live button.
    presented_buffer_following_ = false;
    xlim_                       = AxisLimits{min, max};
}

void Axes::ylim(double min, double max)
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
        return xlim_.value_or(AxisLimits{0.0, 1.0});
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
        return ylim_.value_or(AxisLimits{0.0, 1.0});
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

static double nice_ceil_d(double x, bool round_flag)
{
    double exp_v = std::floor(std::log10(x));
    double frac  = x / std::pow(10.0, exp_v);
    double nice;
    if (round_flag)
    {
        if (frac < 1.5)
            nice = 1.0;
        else if (frac < 3.0)
            nice = 2.0;
        else if (frac < 7.0)
            nice = 5.0;
        else
            nice = 10.0;
    }
    else
    {
        if (frac <= 1.0)
            nice = 1.0;
        else if (frac <= 2.0)
            nice = 2.0;
        else if (frac <= 5.0)
            nice = 5.0;
        else
            nice = 10.0;
    }
    return nice * std::pow(10.0, exp_v);
}

// Format a tick value smartly: use enough decimal digits so that ticks at
// the given spacing are distinguishable.  Falls back to scientific notation
// when the offset is large relative to the spacing (deep-zoom regime).
static std::string format_tick_value(double value, double spacing)
{
    char buf[64];

    // Snap near-zero to exactly zero
    if (std::abs(value) < spacing * 1e-6)
    {
        return "0";
    }

    double abs_val     = std::abs(value);
    double abs_spacing = std::abs(spacing);

    // How many significant digits do we need?
    // We need enough digits to distinguish value from value±spacing.
    // digits_needed = ceil(-log10(spacing)) + 1, but at least 1.
    int digits_after_decimal = 0;
    if (abs_spacing > 0 && std::isfinite(abs_spacing))
    {
        digits_after_decimal = static_cast<int>(std::ceil(-std::log10(abs_spacing))) + 1;
        if (digits_after_decimal < 0)
            digits_after_decimal = 0;
    }

    // If the absolute value is much larger than the spacing, we need
    // scientific/engineering notation to show the difference.
    // e.g. value=7.9000012, spacing=1e-6 → need "7.9000012" not "7.9"
    int total_sig_digits = 0;
    if (abs_val > 0 && abs_spacing > 0)
    {
        // Total significant digits = log10(abs_val / abs_spacing) + 1
        total_sig_digits = static_cast<int>(std::ceil(std::log10(abs_val / abs_spacing))) + 2;
        if (total_sig_digits < 4)
            total_sig_digits = 4;
        if (total_sig_digits > 15)
            total_sig_digits = 15;
    }
    else
    {
        total_sig_digits = 6;
    }

    // Use fixed notation if it results in a reasonable string,
    // otherwise switch to scientific notation.
    if (digits_after_decimal <= 9 && abs_val < 1e9 && abs_val >= 0.001)
    {
        // Fixed notation with enough decimals
        std::snprintf(buf, sizeof(buf), "%.*f", digits_after_decimal, value);
        // Trim trailing zeros after decimal point, but keep at least
        // min_digits digits so all ticks at this spacing have consistent
        // digit counts (e.g. "6.0819710" stays, not "6.081971").
        int min_digits = 0;
        if (abs_spacing > 0 && std::isfinite(abs_spacing))
        {
            min_digits = static_cast<int>(std::ceil(-std::log10(abs_spacing)));
            if (min_digits < 0)
                min_digits = 0;
        }
        std::string str(buf);
        auto        dot_pos = str.find('.');
        if (dot_pos != std::string::npos)
        {
            size_t current_decimals = str.size() - dot_pos - 1;
            while (current_decimals > static_cast<size_t>(min_digits) && str.back() == '0')
            {
                str.pop_back();
                current_decimals--;
            }
            if (str.back() == '.')
                str.pop_back();
        }
        return str;
    }
    else
    {
        // Scientific notation with enough significant digits
        std::snprintf(buf, sizeof(buf), "%.*e", total_sig_digits - 1, value);
        return std::string(buf);
    }
}

static TickResult generate_ticks(double dmin, double dmax, int target_ticks = 7)
{
    TickResult result;

    double range = dmax - dmin;

    // Edge case: zero or negative range
    if (range <= 0.0)
    {
        if (range == 0.0 && dmin != 0.0)
        {
            double half = std::abs(dmin) * 0.1;
            if (half == 0.0)
                half = 0.5;
            return generate_ticks(dmin - half, dmin + half, target_ticks);
        }
        result.positions.push_back(dmin);
        result.labels.push_back(format_tick_value(dmin, 1.0));
        return result;
    }

    // Minimum range: limited by double precision of the values themselves.
    // For value V stored as double, the smallest distinguishable step is
    // ~|V| * DBL_EPSILON.  Below that, ticks would be identical.
    double abs_max   = std::max(std::abs(dmin), std::abs(dmax));
    double min_range = abs_max * std::numeric_limits<double>::epsilon() * 16.0;
    if (min_range < 1e-300)
        min_range = 1e-300;   // absolute floor for values near zero

    if (range < min_range)
    {
        // Range is at double precision limit — show a single centered tick
        double mid = (dmin + dmax) * 0.5;
        result.positions.push_back(mid);
        result.labels.push_back(format_tick_value(mid, range));
        return result;
    }

    double nice_range = nice_ceil_d(range, false);
    double spacing    = nice_ceil_d(nice_range / static_cast<double>(target_ticks - 1), true);

    // Guard against degenerate spacing
    if (spacing <= 0.0 || !std::isfinite(spacing))
    {
        result.positions.push_back(dmin);
        result.labels.push_back(format_tick_value(dmin, range));
        return result;
    }

    double nice_min = std::floor(dmin / spacing) * spacing;
    double nice_max = std::ceil(dmax / spacing) * spacing;

    // Safety: cap iterations to avoid infinite loops
    int max_iters = target_ticks * 3;
    int iters     = 0;
    for (double v = nice_min; v <= nice_max + spacing * 0.5 && iters < max_iters;
         v += spacing, ++iters)
    {
        if (v >= dmin - spacing * 0.01 && v <= dmax + spacing * 0.01)
        {
            // Snap near-zero values to exactly zero to avoid "-0" labels
            if (std::abs(v) < spacing * 1e-6)
                v = 0.0;
            result.positions.push_back(v);
            result.labels.push_back(format_tick_value(v, spacing));
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

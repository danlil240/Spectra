#include <cmath>

#include <algorithm>
#include <format>
#include <limits>
#include <vector>
#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/event_bus.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_stats.hpp>

#include "colorbar_layout.hpp"
#include "core/axis_scale.hpp"
#include "core/axis_ticks.hpp"

namespace spectra
{

void AxesBase::set_viewport(const Rect& r)
{
    viewport_ = colorbar_layout::wants_colorbar(*this) ? colorbar_layout::shrink_cell(r) : r;
}

// --- Safe series removal ---

void AxesBase::clear_series()
{
    for (auto& s : series_)
    {
        if (on_series_removed_)
            on_series_removed_(s.get());
        if (event_system_)
            event_system_->series_removed().emit({this, s.get()});
    }
    series_.clear();
}

bool AxesBase::remove_series(size_t index)
{
    if (index >= series_.size())
        return false;
    auto* ptr = series_[index].get();
    if (on_series_removed_)
        on_series_removed_(ptr);
    if (event_system_)
        event_system_->series_removed().emit({this, ptr});
    series_.erase(series_.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

bool AxesBase::move_series(size_t from, size_t to)
{
    if (from >= series_.size() || to >= series_.size() || from == to)
        return false;
    auto tmp = std::move(series_[from]);
    series_.erase(series_.begin() + static_cast<ptrdiff_t>(from));
    series_.insert(series_.begin() + static_cast<ptrdiff_t>(to), std::move(tmp));
    return true;
}

// --- Internal series factory ---

template <typename T, typename... Args>
T& Axes::add_series(Args&&... args)
{
    auto  s   = std::make_unique<T>(std::forward<Args>(args)...);
    auto& ref = *s;
    // Auto colour: the renderer resolves the slot from the theme's data palette.
    ref.set_auto_color_index(static_cast<int>(series_.size()));
    ref.set_event_context(event_system_, this);
    series_.push_back(std::move(s));
    if (event_system_)
        event_system_->series_added().emit({this, &ref});
    return ref;
}

// --- Series creation ---

LineSeries& Axes::line(std::span<const float> x, std::span<const float> y)
{
    return add_series<LineSeries>(x, y);
}

LineSeries& Axes::line()
{
    return add_series<LineSeries>();
}

ScatterSeries& Axes::scatter(std::span<const float> x, std::span<const float> y)
{
    return add_series<ScatterSeries>(x, y);
}

ScatterSeries& Axes::scatter()
{
    return add_series<ScatterSeries>();
}

LineSeries& Axes::line_f64(std::span<const double> x, std::span<const double> y)
{
    return add_series<LineSeries>(x, y);
}

ScatterSeries& Axes::scatter_f64(std::span<const double> x, std::span<const double> y)
{
    return add_series<ScatterSeries>(x, y);
}

ChunkedLineSeries& Axes::chunked_line()
{
    return add_series<ChunkedLineSeries>();
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

namespace
{
constexpr float k_ref_line_span = 1e12f;
}   // namespace

LineSeries& Axes::hline(double y, std::string_view fmt)
{
    const float xs[] = {-k_ref_line_span, k_ref_line_span};
    const float ys[] = {static_cast<float>(y), static_cast<float>(y)};
    auto&       line = plot(xs, ys, fmt);
    line.set_excluded_from_autoscale(true);
    line.set_show_in_legend(false);
    line.set_reference_line(true);
    return line;
}

LineSeries& Axes::vline(double x, std::string_view fmt)
{
    const float xs[] = {static_cast<float>(x), static_cast<float>(x)};
    const float ys[] = {-k_ref_line_span, k_ref_line_span};
    auto&       line = plot(xs, ys, fmt);
    line.set_excluded_from_autoscale(true);
    line.set_show_in_legend(false);
    line.set_reference_line(true);
    return line;
}

LineSeries& Axes::fplot(std::function<double(double)> func,
                        double                        xmin,
                        double                        xmax,
                        int                           n,
                        std::string_view              fmt)
{
    n = std::max(n, 2);
    std::vector<float> xs(static_cast<size_t>(n));
    std::vector<float> ys(static_cast<size_t>(n));
    const double       dx = (xmax - xmin) / static_cast<double>(n - 1);
    for (int i = 0; i < n; ++i)
    {
        const double x             = xmin + dx * static_cast<double>(i);
        xs[static_cast<size_t>(i)] = static_cast<float>(x);
        ys[static_cast<size_t>(i)] = static_cast<float>(func(x));
    }
    return plot(xs, ys, fmt);
}

// --- Statistical series creation ---

BoxPlotSeries& Axes::box_plot()
{
    return add_series<BoxPlotSeries>();
}

ViolinSeries& Axes::violin()
{
    return add_series<ViolinSeries>();
}

HistogramSeries& Axes::histogram(std::span<const float> values, int bins)
{
    return add_series<HistogramSeries>(values, bins);
}

BarSeries& Axes::bar(std::span<const float> positions, std::span<const float> heights)
{
    return add_series<BarSeries>(positions, heights);
}

BandSeries& Axes::band(std::span<const float> x,
                       std::span<const float> lower,
                       std::span<const float> upper)
{
    return add_series<BandSeries>(x, lower, upper);
}

StemSeries& Axes::stem(std::span<const float> x, std::span<const float> y)
{
    auto& ref = add_series<StemSeries>();
    ref.set_data(x, y);
    return ref;
}

ShapeSeries& Axes::shapes()
{
    return add_series<ShapeSeries>();
}

// --- Text annotations ---

TextAnnotation& Axes::text(float x, float y, std::string_view text)
{
    annotations_.push_back(std::make_unique<TextAnnotation>(x, y, text));
    return *annotations_.back();
}

TextAnnotation& Axes::annotate(std::string_view text, float x, float y, float text_x, float text_y)
{
    auto& a = this->text(text_x, text_y, text);
    a.halign(TextAnnotation::HAlign::Center).valign(TextAnnotation::VAlign::Middle).arrow_to(x, y);
    return a;
}

// --- Axis configuration ---

void Axes::xlim(double min, double max)
{
    // Explicit X limits freeze live follow, but keep the configured buffer so
    // users can resume via the Live button.
    presented_buffer_following_ = false;
    xlim_                       = AxisLimits{min, max};
    if (event_system_)
    {
        auto xl = x_limits();
        auto yl = y_limits();
        event_system_->axes_limits_changed().emit({this, xl.min, xl.max, yl.min, yl.max});
    }
}

void Axes::ylim(double min, double max)
{
    // Y overrides are independent from live X-follow: callers can keep a
    // streaming time window while locking or zooming the Y axis manually.
    ylim_ = AxisLimits{min, max};
    if (event_system_)
    {
        auto xl = x_limits();
        auto yl = y_limits();
        event_system_->axes_limits_changed().emit({this, xl.min, xl.max, yl.min, yl.max});
    }
}

void Axes::clear_ylim()
{
    ylim_.reset();
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
        // Presented buffer drives the X window from data, so clear any manual
        // X override but preserve manual Y if the user explicitly set one.
        xlim_.reset();
        if (autoscale_mode_ == AutoscaleMode::Manual)
            autoscale_mode_ = AutoscaleMode::Padded;
    }
    else
    {
        presented_buffer_seconds_.reset();
        presented_buffer_following_ = false;
        presented_buffer_right_edge_.reset();
    }
}

void Axes::resume_follow()
{
    if (presented_buffer_seconds_.has_value())
    {
        presented_buffer_following_ = true;
        xlim_.reset();
    }
}

void Axes::set_presented_buffer_right_edge(double x)
{
    if (std::isfinite(x))
        presented_buffer_right_edge_ = x;
}

// --- Limits ---

namespace
{

// Running min/max of one axis, plus the smallest strictly positive value so
// log-scaled axes can be fitted without crossing zero.
struct AxisExtent
{
    double min     = std::numeric_limits<double>::max();
    double max     = -std::numeric_limits<double>::max();
    double min_pos = std::numeric_limits<double>::max();

    void consume(double value)
    {
        if (!std::isfinite(value))
            return;
        min = std::min(min, value);
        max = std::max(max, value);
        if (value > 0.0)
            min_pos = std::min(min_pos, value);
    }

    bool valid() const { return min <= max; }
    bool has_positive() const { return min_pos <= max; }
};

}   // namespace

static void consume_finite_extent(std::span<const float> values,
                                  AxisExtent&            extent,
                                  double                 offset = 0.0)
{
    for (float value : values)
    {
        if (!std::isfinite(value))
            continue;
        extent.consume(static_cast<double>(value) + offset);
    }
}

static void consume_finite_value(float value, AxisExtent& extent, double offset = 0.0)
{
    if (!std::isfinite(value))
        return;
    extent.consume(static_cast<double>(value) + offset);
}

// Fold one axis of a chunked series' cached summary into `extent`.  Logical
// value = stored float + offset.  The min/max carry over directly; the
// smallest positive logical value only needs a sample scan in the rare case
// where a non-zero offset moves the stored range across zero.
static void consume_chunked_axis(const ChunkedLineSeries& cs,
                                 bool                     y_axis,
                                 double                   offset,
                                 AxisExtent&              extent)
{
    const auto   b       = cs.data_bounds();
    const bool   has     = y_axis ? b.has_y : b.has_x;
    const double lo      = y_axis ? b.y_min : b.x_min;
    const double hi      = y_axis ? b.y_max : b.x_max;
    const bool   has_pos = y_axis ? b.has_y_positive : b.has_x_positive;
    const double lo_pos  = y_axis ? b.y_min_positive : b.x_min_positive;
    if (!has)
        return;

    extent.consume(lo + offset);
    extent.consume(hi + offset);

    if (offset == 0.0)
    {
        if (has_pos)
            extent.consume(lo_pos);
        return;
    }
    if (lo + offset > 0.0 || hi + offset <= 0.0)
        return;   // smallest positive is lo + offset, or there is none

    double best     = 0.0;
    bool   has_best = false;
    for (std::size_t i = 0; i < cs.storage_chunk_count(); ++i)
    {
        for (float v : y_axis ? cs.y_chunk(i) : cs.x_chunk(i))
        {
            if (!std::isfinite(v))
                continue;
            const double d = static_cast<double>(v) + offset;
            if (d > 0.0 && (!has_best || d < best))
            {
                best     = d;
                has_best = true;
            }
        }
    }
    if (has_best)
        extent.consume(best);
}

// Compute the raw (unpadded) data extent across all series.  Every autoscale
// mode shares this so they all cover the same series types.
static void data_extent_raw(const std::vector<std::unique_ptr<Series>>& series,
                            AxisExtent&                                 x,
                            AxisExtent&                                 y)
{
    for (const auto& s : series)
    {
        if (s->excluded_from_autoscale())
            continue;

        // Per-series double offsets: logical x = x_offset + stored float
        // (likewise for y).  Accumulating in double preserves precision for
        // epoch-scale offsets.
        const double xoff = s->x_offset();
        const double yoff = s->y_offset();

        auto try_xy = [&](std::span<const float> xd, std::span<const float> yd)
        {
            consume_finite_extent(xd, x, xoff);
            consume_finite_extent(yd, y, yoff);
        };

        if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
            try_xy(ls->x_data(), ls->y_data());
        if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
            try_xy(ss->x_data(), ss->y_data());
        // Statistical series (all expose x_data/y_data with NaN breaks)
        if (auto* bp = dynamic_cast<const BoxPlotSeries*>(s.get()))
        {
            try_xy(bp->x_data(), bp->y_data());
            // Include outlier extents
            consume_finite_extent(bp->outlier_y(), y, yoff);
        }
        if (auto* vn = dynamic_cast<const ViolinSeries*>(s.get()))
            try_xy(vn->x_data(), vn->y_data());
        if (auto* hs = dynamic_cast<const HistogramSeries*>(s.get()))
            try_xy(hs->x_data(), hs->y_data());
        if (auto* bs = dynamic_cast<const BarSeries*>(s.get()))
            try_xy(bs->x_data(), bs->y_data());
        if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
        {
            consume_finite_extent(band->x_values(), x, xoff);
            consume_finite_extent(band->lower_values(), y, yoff);
            consume_finite_extent(band->upper_values(), y, yoff);
        }
        if (auto* stem = dynamic_cast<const StemSeries*>(s.get()))
        {
            try_xy(stem->stem_x(), stem->stem_y());
            consume_finite_value(stem->baseline(), y, yoff);
        }
        if (auto* sh = dynamic_cast<const ShapeSeries*>(s.get()))
            try_xy(sh->x_data(), sh->y_data());
        if (auto* cs = dynamic_cast<const ChunkedLineSeries*>(s.get()))
        {
            consume_chunked_axis(*cs, false, xoff, x);
            consume_chunked_axis(*cs, true, yoff, y);
        }
    }
}

// Turn a raw extent into axis limits.  `pad` adds 5% of the span on each side
// (Fit/Padded); otherwise the extent is used as-is (Tight).  For log axes the
// fit uses only positive values and pads multiplicatively so the lower limit
// never reaches zero or below.
static AxisLimits finalize_extent(const AxisExtent& e, bool pad, bool log_scale)
{
    if (log_scale && e.has_positive())
    {
        double lo = e.min_pos;
        double hi = e.max;
        if (hi <= lo)
        {
            lo *= 0.5;
            hi *= 2.0;
        }
        else if (pad)
        {
            const double factor = std::pow(hi / lo, 0.05);
            lo /= factor;
            hi *= factor;
        }
        return {lo, hi};
    }

    double lo = 0.0;
    double hi = 1.0;
    if (e.valid())
    {
        lo = e.min;
        hi = e.max;
    }

    if (pad)
    {
        double p = (hi - lo) * 0.05;
        if (p == 0.0)
            p = 0.5;
        lo -= p;
        hi += p;
    }
    else if (hi == lo)
    {
        lo -= 0.5;
        hi += 0.5;
    }
    return {lo, hi};
}

static void data_extent_with_mode(const std::vector<std::unique_ptr<Series>>& series,
                                  AutoscaleMode                               mode,
                                  bool                                        x_log,
                                  bool                                        y_log,
                                  AxisLimits&                                 x_lim,
                                  AxisLimits&                                 y_lim)
{
    AxisExtent x;
    AxisExtent y;
    data_extent_raw(series, x, y);
    // Tight skips padding; Fit and Padded (and Manual's fallback) add 5%.
    const bool pad = mode != AutoscaleMode::Tight;
    x_lim          = finalize_extent(x, pad, x_log);
    y_lim          = finalize_extent(y, pad, y_log);
}

static bool latest_x_value(const std::vector<std::unique_ptr<Series>>& series, double& latest_x)
{
    bool has_value = false;

    double current_xoff = 0.0;

    auto update_latest = [&](float x)
    {
        if (!std::isfinite(x))
            return;
        const double abs_x = static_cast<double>(x) + current_xoff;
        if (!has_value || abs_x > latest_x)
        {
            latest_x  = abs_x;
            has_value = true;
        }
    };

    for (const auto& s : series)
    {
        if (s->excluded_from_autoscale())
            continue;
        current_xoff = s->x_offset();
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
        if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
            update_from_span(band->x_values());
        if (auto* stem = dynamic_cast<const StemSeries*>(s.get()))
            update_from_span(stem->stem_x());
        if (auto* cs = dynamic_cast<const ChunkedLineSeries*>(s.get()))
        {
            // The cached maximum is the latest x without scanning the samples.
            const auto b = cs->data_bounds();
            if (b.has_x && (!has_value || b.x_max + current_xoff > latest_x))
            {
                latest_x  = b.x_max + current_xoff;
                has_value = true;
            }
        }
    }

    return has_value;
}

static bool windowed_y_extent(const std::vector<std::unique_ptr<Series>>& series,
                              double                                      window_min,
                              double                                      window_max,
                              AxisExtent&                                 y_ext)
{
    y_ext      = AxisExtent{};
    bool has_y = false;

    double current_xoff = 0.0;
    double current_yoff = 0.0;

    auto consume_xy = [&](std::span<const float> xd, std::span<const float> yd)
    {
        size_t n = std::min(xd.size(), yd.size());
        for (size_t i = 0; i < n; ++i)
        {
            float x = xd[i];
            float y = yd[i];
            if (!std::isfinite(x) || !std::isfinite(y))
                continue;
            const double abs_x = static_cast<double>(x) + current_xoff;
            if (abs_x < window_min || abs_x > window_max)
                continue;
            y_ext.consume(static_cast<double>(y) + current_yoff);
            has_y = true;
        }
    };

    auto consume_stems = [&](const StemSeries& stem)
    {
        const auto   xd = stem.stem_x();
        const auto   yd = stem.stem_y();
        const size_t n  = std::min(xd.size(), yd.size());
        for (size_t i = 0; i < n; ++i)
        {
            if (!std::isfinite(xd[i]) || !std::isfinite(yd[i]))
                continue;
            const double abs_x = static_cast<double>(xd[i]) + current_xoff;
            if (abs_x < window_min || abs_x > window_max)
                continue;
            y_ext.consume(static_cast<double>(yd[i]) + current_yoff);
            consume_finite_value(stem.baseline(), y_ext, current_yoff);
            has_y = true;
        }
    };

    for (const auto& s : series)
    {
        if (s->excluded_from_autoscale())
            continue;
        current_xoff = s->x_offset();
        current_yoff = s->y_offset();
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
        if (auto* band = dynamic_cast<const BandSeries*>(s.get()))
        {
            consume_xy(band->x_values(), band->lower_values());
            consume_xy(band->x_values(), band->upper_values());
        }
        if (auto* stem = dynamic_cast<const StemSeries*>(s.get()))
            consume_stems(*stem);
        if (auto* sh = dynamic_cast<const ShapeSeries*>(s.get()))
            consume_xy(sh->x_data(), sh->y_data());
        if (auto* cs = dynamic_cast<const ChunkedLineSeries*>(s.get()))
        {
            // x and y share one chunk layout: scan pairs in place, no copy.
            for (std::size_t i = 0; i < cs->storage_chunk_count(); ++i)
                consume_xy(cs->x_chunk(i), cs->y_chunk(i));
        }
    }

    return has_y;
}

static double positive_data_floor(const std::vector<std::unique_ptr<Series>>& series, bool x_axis)
{
    AxisExtent x;
    AxisExtent y;
    data_extent_raw(series, x, y);
    const AxisExtent& e = x_axis ? x : y;
    return e.has_positive() ? e.min_pos : 0.0;
}

// Log axes cannot show non-positive limits (e.g. a user xlim(0, 100)): clamp
// the lower limit to the smallest positive datum so ticks, rendering and
// interaction all agree on one valid range.
static AxisLimits sanitize_limits(ScaleType                                   scale,
                                  AxisLimits                                  lim,
                                  const std::vector<std::unique_ptr<Series>>& series,
                                  bool                                        x_axis)
{
    if (!is_log_scale(scale) || (lim.min > 0.0 && lim.max > 0.0))
        return lim;
    return sanitize_log_limits(scale, lim, positive_data_floor(series, x_axis));
}

AxisLimits Axes::x_limits() const
{
    return sanitize_limits(xscale_, x_limits_unclamped(), series_, true);
}

AxisLimits Axes::y_limits() const
{
    return sanitize_limits(yscale_, y_limits_unclamped(), series_, false);
}

AxisLimits Axes::x_limits_unclamped() const
{
    if (presented_buffer_following_ && presented_buffer_seconds_.has_value()
        && presented_buffer_seconds_.value() > 0.0f)
    {
        if (presented_buffer_right_edge_.has_value())
        {
            const double right = presented_buffer_right_edge_.value();
            const double left  = right - static_cast<double>(presented_buffer_seconds_.value());
            return {left, right};
        }

        double latest_x = 0.0;
        if (latest_x_value(series_, latest_x))
        {
            return {latest_x - static_cast<double>(presented_buffer_seconds_.value()), latest_x};
        }
    }

    if (xlim_.has_value() || autoscale_mode_ == AutoscaleMode::Manual)
        return xlim_.value_or(AxisLimits{0.0, 1.0});
    AxisLimits xl;
    AxisLimits yl;
    data_extent_with_mode(series_,
                          autoscale_mode_,
                          is_log_scale(xscale_),
                          is_log_scale(yscale_),
                          xl,
                          yl);
    return xl;
}

AxisLimits Axes::y_limits_unclamped() const
{
    if (ylim_.has_value() || autoscale_mode_ == AutoscaleMode::Manual)
        return ylim_.value_or(AxisLimits{0.0, 1.0});

    if (presented_buffer_seconds_.has_value() && presented_buffer_seconds_.value() > 0.0f)
    {
        double window_min  = 0.0;
        double window_max  = 0.0;
        bool   have_window = false;

        if (presented_buffer_following_)
        {
            if (presented_buffer_right_edge_.has_value())
            {
                window_max  = presented_buffer_right_edge_.value();
                window_min  = window_max - static_cast<double>(presented_buffer_seconds_.value());
                have_window = true;
            }
            else
            {
                double latest_x = 0.0;
                if (latest_x_value(series_, latest_x))
                {
                    window_min  = latest_x - static_cast<double>(presented_buffer_seconds_.value());
                    window_max  = latest_x;
                    have_window = true;
                }
            }
        }
        else if (xlim_.has_value())
        {
            window_min  = xlim_->min;
            window_max  = xlim_->max;
            have_window = true;
        }

        if (have_window)
        {
            AxisExtent y_ext;
            if (windowed_y_extent(series_, window_min, window_max, y_ext))
            {
                return finalize_extent(y_ext,
                                       autoscale_mode_ != AutoscaleMode::Tight,
                                       is_log_scale(yscale_));
            }
        }
    }

    AxisLimits xl;
    AxisLimits yl;
    data_extent_with_mode(series_,
                          autoscale_mode_,
                          is_log_scale(xscale_),
                          is_log_scale(yscale_),
                          xl,
                          yl);
    return yl;
}

void Axes::auto_fit()
{
    xlim_.reset();
    ylim_.reset();
}

void Axes::set_topic_auto_zoom(bool enabled)
{
    topic_auto_zoom_ = enabled;
    if (!enabled)
    {
        last_auto_xlim_.reset();
        last_auto_ylim_.reset();
    }
}

void Axes::topic_auto_zoom_tick()
{
    if (!topic_auto_zoom_)
        return;

    // ── Step 0: defer to presented-buffer follow ────────────────────────────
    //
    // While the axes follows a live presented buffer, x_limits() ignores
    // xlim_ and slides with every sample, and y_limits() already autoscales Y
    // over the visible window.  Auto-zoom would misread each slide as a user
    // override and leave its own stale ylim_ frozen, so hand control over to
    // the follow logic and release any limits this tick installed.
    if (presented_buffer_following_ && presented_buffer_seconds_.has_value()
        && presented_buffer_seconds_.value() > 0.0f)
    {
        auto same = [](const std::optional<AxisLimits>& a, const std::optional<AxisLimits>& b)
        { return a.has_value() && b.has_value() && a->min == b->min && a->max == b->max; };
        if (same(xlim_, last_auto_xlim_))
            xlim_.reset();
        if (same(ylim_, last_auto_ylim_))
            ylim_.reset();
        topic_auto_zoom_ = false;
        last_auto_xlim_.reset();
        last_auto_ylim_.reset();
        return;
    }

    // ── Step 1: detect user override ────────────────────────────────────────
    //
    // If the limits we set on the previous tick no longer match the current
    // ones (i.e. user panned/zoomed/typed an explicit range), disable
    // auto-zoom so the user's view is preserved.
    const AxisLimits cur_x = x_limits();
    const AxisLimits cur_y = y_limits();

    if (last_auto_xlim_.has_value() && last_auto_ylim_.has_value())
    {
        const AxisLimits& lx    = *last_auto_xlim_;
        const AxisLimits& ly    = *last_auto_ylim_;
        const double      eps_x = std::max(std::abs(lx.max - lx.min), 1.0) * 1e-9;
        const double      eps_y = std::max(std::abs(ly.max - ly.min), 1.0) * 1e-9;
        if (std::abs(cur_x.min - lx.min) > eps_x || std::abs(cur_x.max - lx.max) > eps_x
            || std::abs(cur_y.min - ly.min) > eps_y || std::abs(cur_y.max - ly.max) > eps_y)
        {
            topic_auto_zoom_ = false;
            last_auto_xlim_.reset();
            last_auto_ylim_.reset();
            return;
        }
    }

    // ── Step 2: recompute auto-fit limits from current data ────────────────
    //
    // Fit directly from the data rather than via x_limits()/y_limits(): those
    // return a fixed [0,1] fallback in Manual mode instead of the data extent.
    // Manual axes get the default padded fit.
    const AutoscaleMode fit_mode =
        autoscale_mode_ == AutoscaleMode::Manual ? AutoscaleMode::Padded : autoscale_mode_;
    AxisLimits new_x;
    AxisLimits new_y;
    data_extent_with_mode(series_,
                          fit_mode,
                          is_log_scale(xscale_),
                          is_log_scale(yscale_),
                          new_x,
                          new_y);

    if (!std::isfinite(new_x.min) || !std::isfinite(new_x.max) || !std::isfinite(new_y.min)
        || !std::isfinite(new_y.max))
    {
        // Degenerate; keep the previous limits and try again next frame.
        return;
    }

    xlim_           = new_x;
    ylim_           = new_y;
    last_auto_xlim_ = new_x;
    last_auto_ylim_ = new_y;

    if (event_system_)
        event_system_->axes_limits_changed().emit(
            {this, new_x.min, new_x.max, new_y.min, new_y.max});
}

// --- Tick generation ---
// Linear ticks and their labels come from core/axis_ticks.cpp.

// Format a tick value smartly: use enough decimal digits so that ticks at
// the given spacing are distinguishable.  Falls back to scientific notation
// when the offset is large relative to the spacing (deep-zoom regime).
static std::string format_tick_value(double value, double spacing)
{
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
    // otherwise switch to scientific notation.  The 1e12 bound keeps
    // epoch-scale timestamps (~1.7e9) in plain fixed notation like
    // PlotJuggler/rqt instead of scientific notation.
    if (digits_after_decimal <= 9 && abs_val < 1e12 && abs_val >= 0.001)
    {
        // Fixed notation with enough decimals
        std::string str = std::format("{:.{}f}", value, digits_after_decimal);
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
        auto dot_pos = str.find('.');
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

    // Scientific notation with enough significant digits
    return std::format("{:.{}e}", value, total_sig_digits - 1);
}

// Label for base^e on a log axis: plain numbers near 1, compact exponent
// form ("1e-6", "2^20") for far-away decades.
static std::string format_log_tick(double base, int e)
{
    const double v = std::pow(base, static_cast<double>(e));
    if (base == 10.0)
    {
        if (e >= 0 && e <= 5)
            return std::format("{:.0f}", v);
        if (e < 0 && e >= -4)
            return std::format("{:.{}f}", v, -e);
        return std::format("1e{}", e);
    }
    if (e >= 0 && e <= 16)
        return std::format("{:.0f}", v);
    if (e < 0 && e >= -4)
        return std::format("{:.{}f}", v, -e);
    return std::format("2^{}", e);
}

// Logarithmic ticks at integer powers of `base` within [lo, hi] (both > 0).
// Wide ranges step over decades so at most ~8 labels appear; ranges holding
// fewer than two powers fall back to ordinary "nice" ticks, which still read
// correctly on the log axis.
static TickResult generate_log_ticks(double                   lo,
                                     double                   hi,
                                     double                   base,
                                     const LinearTickOptions& opt)
{
    TickResult result;
    if (!(lo > 0.0) || !(hi > lo) || !std::isfinite(hi))
    {
        const double v = lo > 0.0 ? lo : 1.0;
        result.positions.push_back(v);
        result.labels.push_back(format_tick_value(v, v * 0.1));
        return result;
    }

    const double log_base = std::log(base);
    const double a_lo     = std::log(lo) / log_base;
    const double a_hi     = std::log(hi) / log_base;
    // Tolerance so a limit sitting exactly on a power still gets its tick.
    constexpr double EPS  = 1e-9;
    const int        e_lo = static_cast<int>(std::ceil(a_lo - EPS));
    const int        e_hi = static_cast<int>(std::floor(a_hi + EPS));

    if (e_hi - e_lo + 1 < 2)
    {
        const TickResult lin = generate_linear_ticks(lo, hi, opt);
        for (size_t i = 0; i < lin.positions.size(); ++i)
        {
            if (lin.positions[i] > 0.0)
            {
                result.positions.push_back(lin.positions[i]);
                result.labels.push_back(lin.labels[i]);
            }
        }
        result.offset_label = lin.offset_label;
        if (result.positions.empty())
        {
            result.positions.push_back(lo);
            result.labels.push_back(format_tick_value(lo, hi - lo));
        }
        return result;
    }

    constexpr int MAX_LOG_TICKS = 8;
    const int     n_powers      = e_hi - e_lo + 1;
    const int     stride        = (n_powers + MAX_LOG_TICKS - 1) / MAX_LOG_TICKS;
    // Align strided ticks to multiples of the stride so they include 10^0.
    int first = e_lo;
    if (stride > 1)
    {
        const int rem = ((e_lo % stride) + stride) % stride;
        if (rem != 0)
            first += stride - rem;
    }
    for (int e = first; e <= e_hi; e += stride)
    {
        result.positions.push_back(std::pow(base, static_cast<double>(e)));
        result.labels.push_back(format_log_tick(base, e));
    }
    return result;
}

// Sqrt-scale ticks: evenly spaced in squared space (signed, so negative
// limits stay monotonic), placed at their square roots and labelled with the
// coordinate they are actually drawn at.
static TickResult generate_sqrt_ticks(double lo, double hi, const LinearTickOptions& opt)
{
    auto signed_sq   = [](double v) { return v < 0.0 ? -v * v : v * v; };
    auto signed_sqrt = [](double v) { return v < 0.0 ? -std::sqrt(-v) : std::sqrt(v); };

    const TickResult inner = generate_linear_ticks(signed_sq(lo), signed_sq(hi), opt);
    TickResult       result;
    result.positions.reserve(inner.positions.size());
    for (double p : inner.positions)
        result.positions.push_back(signed_sqrt(p));

    double gap = std::numeric_limits<double>::max();
    for (size_t i = 1; i < result.positions.size(); ++i)
    {
        const double d = std::abs(result.positions[i] - result.positions[i - 1]);
        if (d > 0.0)
            gap = std::min(gap, d);
    }
    if (gap == std::numeric_limits<double>::max())
        gap = (hi - lo) > 0.0 ? (hi - lo) : 1.0;

    result.labels.reserve(result.positions.size());
    for (double p : result.positions)
        result.labels.push_back(format_tick_value(p, gap));
    return result;
}

// Positions of category-like series (box, violin, bar) along one axis.
// `along_x` selects vertical categories (box, violin, vertical bars) or
// horizontal bars.  Sets `any` when such a series is present.
static std::vector<double> category_positions(const std::vector<std::unique_ptr<Series>>& series,
                                              bool                                        along_x,
                                              bool&                                       any)
{
    std::vector<double> out;
    any = false;
    for (const auto& s : series)
    {
        if (!s || !s->visible())
            continue;
        if (along_x)
        {
            if (const auto* box = dynamic_cast<const BoxPlotSeries*>(s.get()))
            {
                any = true;
                out.insert(out.end(), box->positions().begin(), box->positions().end());
            }
            else if (const auto* vio = dynamic_cast<const ViolinSeries*>(s.get()))
            {
                any = true;
                for (const auto& v : vio->violins())
                    out.push_back(v.x_position);
            }
        }
        if (const auto* bar = dynamic_cast<const BarSeries*>(s.get()))
        {
            if ((bar->orientation() == BarOrientation::Vertical) == along_x)
            {
                any = true;
                out.insert(out.end(), bar->bar_positions().begin(), bar->bar_positions().end());
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    // Ticks sit exactly on categories only when every category is an integer
    // (grouped bars at 0.8 / 1.2 get ordinary integer-step ticks instead).
    for (double v : out)
    {
        if (!std::isfinite(v) || v != std::round(v))
            return {};
    }
    return out;
}

static LinearTickOptions tick_options(const std::vector<std::unique_ptr<Series>>& series,
                                      bool                                        along_x,
                                      double                                      axis_px)
{
    LinearTickOptions opt;
    opt.axis_px             = axis_px;
    opt.target_px           = along_x ? X_TICK_TARGET_PX : Y_TICK_TARGET_PX;
    opt.labels_side_by_side = along_x;
    opt.categories          = category_positions(series, along_x, opt.integer_steps);
    return opt;
}

TickResult Axes::compute_x_ticks() const
{
    auto       lim = x_limits();
    const auto opt = tick_options(series_, true, viewport_.w);
    if (is_log_scale(xscale_))
    {
        return generate_log_ticks(lim.min, lim.max, log_scale_base(xscale_), opt);
    }
    if (xscale_ == ScaleType::Sqrt)
        return generate_sqrt_ticks(lim.min, lim.max, opt);
    return generate_linear_ticks(lim.min, lim.max, opt);
}

TickResult Axes::compute_y_ticks() const
{
    auto       lim = y_limits();
    const auto opt = tick_options(series_, false, viewport_.h);
    if (is_log_scale(yscale_))
    {
        return generate_log_ticks(lim.min, lim.max, log_scale_base(yscale_), opt);
    }
    if (yscale_ == ScaleType::Sqrt)
        return generate_sqrt_ticks(lim.min, lim.max, opt);
    return generate_linear_ticks(lim.min, lim.max, opt);
}

}   // namespace spectra

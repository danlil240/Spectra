#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

enum class AutoscaleMode
{
    Fit,      // Fit to data range exactly
    Tight,    // Fit with no padding
    Padded,   // Fit with small padding (default)
    Manual,   // User-specified limits only
};

struct AxisStyle
{
    Color tick_color  = colors::black;
    Color label_color = colors::black;
    Color grid_color  = {0.0f, 0.0f, 0.0f, 0.0f};   // alpha=0 → use theme grid_line color
    float tick_length = 5.0f;
    float label_size  = 14.0f;
    float title_size  = 16.0f;
    float grid_width  = 1.0f;
};

struct AxisLimits
{
    float min = 0.0f;
    float max = 1.0f;
};

struct TickResult
{
    std::vector<float>       positions;
    std::vector<std::string> labels;
};

class AxesBase
{
   public:
    virtual ~AxesBase() = default;

    virtual void auto_fit() = 0;

    const std::vector<std::unique_ptr<Series>>& series() const { return series_; }
    std::vector<std::unique_ptr<Series>>&       series_mut() { return series_; }

    // Safely remove all series, notifying the renderer to defer GPU cleanup.
    // Always prefer this over series_mut().clear().
    void clear_series();

    // Remove a single series by index (0-based).  Returns false if out of range.
    bool remove_series(size_t index);

    // Called by the framework to wire up deferred GPU cleanup.
    using SeriesRemovedCallback = std::function<void(const Series*)>;
    void set_series_removed_callback(SeriesRemovedCallback cb)
    {
        on_series_removed_ = std::move(cb);
    }
    bool has_series_removed_callback() const { return static_cast<bool>(on_series_removed_); }

    void        set_viewport(const Rect& r) { viewport_ = r; }
    const Rect& viewport() const { return viewport_; }

    const std::string& title() const { return title_; }
    void               title(const std::string& t) { title_ = t; }

    bool grid_enabled() const { return grid_enabled_; }
    void grid(bool enabled) { grid_enabled_ = enabled; }

    bool border_enabled() const { return border_enabled_; }
    void show_border(bool enabled) { border_enabled_ = enabled; }

    AxisStyle&       axis_style() { return axis_style_; }
    const AxisStyle& axis_style() const { return axis_style_; }

    // Deprecated aliases — prefer grid(bool) and show_border(bool)
    void               set_grid_enabled(bool e) { grid_enabled_ = e; }
    void               set_border_enabled(bool e) { border_enabled_ = e; }
    const std::string& get_title() const { return title_; }

   protected:
    std::vector<std::unique_ptr<Series>> series_;
    std::string                          title_;
    bool                                 grid_enabled_   = true;
    bool                                 border_enabled_ = true;
    AxisStyle                            axis_style_;
    Rect                                 viewport_;
    SeriesRemovedCallback                on_series_removed_;
};

class Axes : public AxesBase
{
   public:
    Axes() = default;

    // Series creation — returns reference for fluent API
    LineSeries& line(std::span<const float> x, std::span<const float> y);
    LineSeries& line();

    ScatterSeries& scatter(std::span<const float> x, std::span<const float> y);
    ScatterSeries& scatter();

    // MATLAB-style plot: plot(x, y, "r--o") creates a line series with the
    // given format string applied. See parse_format_string() in plot_style.hpp.
    LineSeries& plot(std::span<const float> x,
                     std::span<const float> y,
                     std::string_view       fmt = "-");
    LineSeries& plot(std::span<const float> x, std::span<const float> y, const PlotStyle& style);

    // Axis configuration
    void xlim(float min, float max);
    void ylim(float min, float max);
    void title(const std::string& t);
    void xlabel(const std::string& lbl);
    void ylabel(const std::string& lbl);
    void grid(bool enabled);
    void show_border(bool enabled);
    void autoscale_mode(AutoscaleMode mode);

    // Accessors
    AxisLimits         x_limits() const;
    AxisLimits         y_limits() const;
    const std::string& title() const { return title_; }
    const std::string& xlabel() const { return xlabel_; }
    const std::string& ylabel() const { return ylabel_; }
    bool               grid_enabled() const { return grid_enabled_; }
    bool               border_enabled() const { return border_enabled_; }
    AutoscaleMode      autoscale_mode() const { return autoscale_mode_; }

    // Deprecated aliases
    const std::string& get_title() const { return title_; }
    const std::string& get_xlabel() const { return xlabel_; }
    const std::string& get_ylabel() const { return ylabel_; }
    AutoscaleMode      get_autoscale_mode() const { return autoscale_mode_; }

    // Tick computation
    TickResult compute_x_ticks() const;
    TickResult compute_y_ticks() const;

    // Auto-fit limits to data
    void auto_fit() override;

   private:
    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;

    std::string   xlabel_;
    std::string   ylabel_;
    AutoscaleMode autoscale_mode_ = AutoscaleMode::Padded;
};

}   // namespace spectra

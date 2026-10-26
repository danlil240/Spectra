#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <spectra/series_shapes.hpp>
#include <string>
#include <vector>

namespace spectra
{

/// @brief How Axes::auto_fit() chooses axis limits.
enum class AutoscaleMode
{
    Fit,      ///< Fit to data range exactly
    Tight,    ///< Fit with no padding
    Padded,   ///< Fit with small padding (default)
    Manual,   ///< User-specified limits only
};

/// @brief Axis scale transform.
enum class ScaleType
{
    Linear,   ///< Standard linear scale (default)
    Log10,    ///< Base-10 logarithmic scale
    Log2,     ///< Base-2 logarithmic scale
    Sqrt,     ///< Square-root scale
};

/// @brief Colours and sizes of ticks, labels and grid lines.
struct AxisStyle
{
    Color tick_color  = colors::black;
    Color label_color = colors::black;
    Color grid_color  = {0.0f, 0.0f, 0.0f, 0.0f};   ///< alpha=0 → use theme grid_line color
    float tick_length = 5.0f;
    float label_size  = 14.0f;
    float title_size  = 16.0f;
    float grid_width  = 1.0f;
};

/// @brief A closed axis range [min, max] in data coordinates.
struct AxisLimits
{
    double min = 0.0;
    double max = 1.0;
};

/// @brief Tick positions and their formatted labels.
struct TickResult
{
    std::vector<double>      positions;
    std::vector<std::string> labels;
};

/// @brief Common base of 2D Axes and 3D Axes3D: owns series, title, grid and border state.
class AxesBase
{
   public:
    virtual ~AxesBase() = default;

    /// @brief Fit axis limits to the data of all visible series.
    virtual void auto_fit() = 0;

    /// @brief Series plotted in these axes, in draw order.
    const std::vector<std::unique_ptr<Series>>& series() const { return series_; }
    std::vector<std::unique_ptr<Series>>&       series_mut() { return series_; }

    /// Safely remove all series, notifying the renderer to defer GPU cleanup.
    /// Always prefer this over series_mut().clear().
    void clear_series();

    /// Remove a single series by index (0-based).  Returns false if out of range.
    bool remove_series(size_t index);

    /// Move a series from one index to another (reorder).  Returns false if out of range.
    bool move_series(size_t from, size_t to);

    /// Called by the framework to wire up deferred GPU cleanup.
    using SeriesRemovedCallback = std::function<void(const Series*)>;
    void set_series_removed_callback(SeriesRemovedCallback cb)
    {
        on_series_removed_ = std::move(cb);
    }
    bool has_series_removed_callback() const { return static_cast<bool>(on_series_removed_); }

    // ── Event system integration ──
    void         set_event_system(EventSystem* es) { event_system_ = es; }
    EventSystem* event_system() const { return event_system_; }

    void        set_viewport(const Rect& r) { viewport_ = r; }
    const Rect& viewport() const { return viewport_; }

    /// @brief Axes title.
    const std::string& title() const { return title_; }
    void               title(const std::string& t) { title_ = t; }

    /// @brief Whether grid lines are drawn.
    bool grid_enabled() const { return grid_enabled_; }
    void grid(bool enabled) { grid_enabled_ = enabled; }

    bool border_enabled() const { return border_enabled_; }
    void show_border(bool enabled) { border_enabled_ = enabled; }

    /// Colormap colorbar visibility.
    bool colorbar_visible() const { return colorbar_visible_; }
    void show_colorbar(bool visible) { colorbar_visible_ = visible; }

    /// @brief Mutable tick/label/grid style.
    AxisStyle&       axis_style() { return axis_style_; }
    const AxisStyle& axis_style() const { return axis_style_; }

    // Deprecated aliases — prefer grid(bool) and show_border(bool)
    void               set_grid_enabled(bool e) { grid_enabled_ = e; }
    void               set_border_enabled(bool e) { border_enabled_ = e; }
    const std::string& get_title() const { return title_; }

   protected:
    std::vector<std::unique_ptr<Series>> series_;
    std::string                          title_;
    bool                                 grid_enabled_     = true;
    bool                                 border_enabled_   = true;
    bool                                 colorbar_visible_ = false;
    AxisStyle                            axis_style_;
    Rect                                 viewport_;
    SeriesRemovedCallback                on_series_removed_;
    EventSystem*                         event_system_ = nullptr;
};

/// @brief A 2D coordinate system that owns line, scatter, statistical and shape series.
///
/// Series factory methods return a reference to the new series for fluent styling:
/// @code
/// auto& ax = fig.subplot(1, 1, 1);
/// ax.plot(x, y, "r--o").label("measured");
/// ax.xlabel("time (s)");
/// @endcode
class Axes : public AxesBase
{
   public:
    Axes() = default;

    // Series creation — returns reference for fluent API
    /// @brief Add a line series from x/y data.
    LineSeries& line(std::span<const float> x, std::span<const float> y);
    /// @brief Add an empty line series (fill it later with set_x/set_y/append).
    LineSeries& line();

    /// @brief Add a scatter series from x/y data.
    ScatterSeries& scatter(std::span<const float> x, std::span<const float> y);
    /// @brief Add an empty scatter series.
    ScatterSeries& scatter();

    /// MATLAB-style plot: plot(x, y, "r--o") creates a line series with the
    /// given format string applied. See parse_format_string() in plot_style.hpp.
    LineSeries& plot(std::span<const float> x,
                     std::span<const float> y,
                     std::string_view       fmt = "-");
    LineSeries& plot(std::span<const float> x, std::span<const float> y, const PlotStyle& style);

    /// Horizontal / vertical reference lines (e.g. y=0, x=0). Span uses sentinel
    /// coordinates so auto_fit ignores them.
    LineSeries& hline(double y, std::string_view fmt = "-");
    LineSeries& vline(double x, std::string_view fmt = "-");

    /// Sample y = f(x) over [xmin, xmax] with n evenly spaced points.
    LineSeries& fplot(std::function<double(double)> func,
                      double                        xmin,
                      double                        xmax,
                      int                           n   = 200,
                      std::string_view              fmt = "-");

    /// Chunked line series — for large datasets and streaming (ROS2, PX4, etc.)
    ChunkedLineSeries& chunked_line();

    // Statistical plot series creation
    /// @brief Add an empty box-plot series.
    BoxPlotSeries& box_plot();
    /// @brief Add an empty violin series.
    ViolinSeries& violin();
    /// @brief Add a histogram of @p values with @p bins bins.
    HistogramSeries& histogram(std::span<const float> values, int bins = 30);
    /// @brief Add a bar chart with bars at @p positions.
    BarSeries& bar(std::span<const float> positions, std::span<const float> heights);
    /// @brief Add a filled band between @p lower and @p upper (e.g. a confidence interval).
    BandSeries& band(std::span<const float> x,
                     std::span<const float> lower,
                     std::span<const float> upper);
    /// @brief Add a stem plot.
    StemSeries& stem(std::span<const float> x, std::span<const float> y);

    /// Shape annotation series (rectangles, circles, arrows, polygons, etc.)
    ShapeSeries& shapes();

    // Axis configuration
    /// @brief Set fixed x-axis limits (disables x autoscale).
    void xlim(double min, double max);
    /// @brief Set fixed y-axis limits (disables y autoscale).
    void ylim(double min, double max);
    /// @brief Remove the fixed y limits and return to autoscale.
    void clear_ylim();
    /// @brief Set the axes title.
    void title(const std::string& t);
    /// @brief Set the x-axis label.
    void xlabel(const std::string& lbl);
    /// @brief Set the y-axis label.
    void ylabel(const std::string& lbl);
    /// @brief Show or hide grid lines.
    void grid(bool enabled);
    /// @brief Show or hide the axes border.
    void show_border(bool enabled);
    /// @brief Choose how auto_fit() computes limits.
    void autoscale_mode(AutoscaleMode mode);
    /// @brief Show only the most recent @p seconds of x data and follow new data (streaming).
    void presented_buffer(float seconds);
    /// @brief Pin the right edge of the presented buffer window.
    void set_presented_buffer_right_edge(double x);

    /// Axis scale type (linear, log10, log2, sqrt)
    void      xscale(ScaleType s) { xscale_ = s; }
    ScaleType xscale() const { return xscale_; }
    void      yscale(ScaleType s) { yscale_ = s; }
    ScaleType yscale() const { return yscale_; }

    // Accessors
    /// @brief Current x-axis limits (explicit or auto-fitted).
    AxisLimits x_limits() const;
    /// @brief Current y-axis limits (explicit or auto-fitted).
    AxisLimits         y_limits() const;
    const std::string& title() const { return title_; }
    const std::string& xlabel() const { return xlabel_; }
    const std::string& ylabel() const { return ylabel_; }
    bool               grid_enabled() const { return grid_enabled_; }
    bool               border_enabled() const { return border_enabled_; }
    AutoscaleMode      autoscale_mode() const { return autoscale_mode_; }
    bool has_presented_buffer() const { return presented_buffer_seconds_.has_value(); }
    bool is_presented_buffer_following() const
    {
        return presented_buffer_following_ && presented_buffer_seconds_.has_value();
    }
    float presented_buffer_seconds() const { return presented_buffer_seconds_.value_or(0.0f); }

    /// Re-enable live X-follow without clearing any manual Y override.
    /// No-op if no presented_buffer has been configured.
    void resume_follow();

    // Deprecated aliases
    const std::string& get_title() const { return title_; }
    const std::string& get_xlabel() const { return xlabel_; }
    const std::string& get_ylabel() const { return ylabel_; }
    AutoscaleMode      get_autoscale_mode() const { return autoscale_mode_; }

    // Tick computation
    /// @brief Compute x tick positions and labels for the current limits.
    TickResult compute_x_ticks() const;
    /// @brief Compute y tick positions and labels for the current limits.
    TickResult compute_y_ticks() const;

    /// @brief Fit axis limits to the data of all visible series.
    void auto_fit() override;

    // ── Continuous topic-driven auto-zoom ──
    //
    /// When enabled (typically by the Topics drag-drop / subscribe path), the
    /// session runtime calls topic_auto_zoom_tick() each frame after thread-safe
    /// series have been committed.  The tick re-runs auto_fit() so the view
    /// keeps following the live data extent.  If the user pans or zooms (which
    /// mutates xlim_/ylim_ to values that differ from the last auto-fit result),
    /// auto-zoom is automatically disabled so the user's view is preserved.
    void set_topic_auto_zoom(bool enabled);
    bool topic_auto_zoom() const { return topic_auto_zoom_; }
    void topic_auto_zoom_tick();

   private:
    // Internal helper: construct T(args...), assign default cycle color, push to series_
    template <typename T, typename... Args>
    T& add_series(Args&&... args);

    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;

    std::string           xlabel_;
    std::string           ylabel_;
    ScaleType             xscale_         = ScaleType::Linear;
    ScaleType             yscale_         = ScaleType::Linear;
    AutoscaleMode         autoscale_mode_ = AutoscaleMode::Padded;
    std::optional<float>  presented_buffer_seconds_;
    bool                  presented_buffer_following_ = false;
    std::optional<double> presented_buffer_right_edge_;

    // Topic-driven continuous auto-zoom state (see set_topic_auto_zoom).
    bool                      topic_auto_zoom_ = false;
    std::optional<AxisLimits> last_auto_xlim_;
    std::optional<AxisLimits> last_auto_ylim_;
};

}   // namespace spectra

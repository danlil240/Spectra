#pragma once

#include <chrono>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <spectra/annotation.hpp>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <spectra/series_shapes.hpp>
#include <string>
#include <vector>

namespace spectra
{

struct AxesShareGroup;

/// @brief How Axes::auto_fit() chooses axis limits.
enum class AutoscaleMode
{
    Fit,      ///< Fit to data range exactly
    Tight,    ///< Fit with no padding
    Padded,   ///< Fit with small padding (default)
    Manual,   ///< User-specified limits only
};

/// @brief How several bar series on one axes share a category position.
enum class BarMode : uint8_t
{
    Grouped,   ///< Side by side, each series in its own slot of the bar width (default)
    Stacked,   ///< On top of each other; negative values stack downwards separately
    Overlay,   ///< Drawn over each other at full width
};

/// @brief Axis scale transform.
enum class ScaleType
{
    Linear,   ///< Standard linear scale (default)
    Log10,    ///< Base-10 logarithmic scale
    Log2,     ///< Base-2 logarithmic scale
    Sqrt,     ///< Square-root scale
};

/// @brief Which side of the axis line tick marks are drawn on.
enum class TickDirection : uint8_t
{
    Out,   ///< Outside the plot area (default)
    In,    ///< Inside the plot area
};

struct StyleSheet;

/// @brief Colours and sizes of ticks, labels and grid lines.
///
/// Colours with alpha 0 and sizes <= 0 follow the theme.  Sizes are in logical
/// pixels; on screen they are multiplied by the UI scale setting (exports keep
/// them as given).
struct AxisStyle
{
    Color tick_color  = {0.0f, 0.0f, 0.0f, 0.0f};   ///< tick label colour; alpha=0 → theme
    Color label_color = {0.0f, 0.0f, 0.0f, 0.0f};   ///< axis label colour; alpha=0 → theme
    Color grid_color  = {0.0f, 0.0f, 0.0f, 0.0f};   ///< alpha=0 → use theme grid_line color
    float tick_length = 5.0f;
    float label_size  = 14.0f;   ///< axis label font size (px); <= 0 → theme (14)
    float title_size  = 18.0f;   ///< axes title font size (px); <= 0 → theme (18)
    float grid_width  = 1.0f;
    float tick_size   = 0.0f;   ///< tick label font size (px); <= 0 → theme (13)
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
    /// Offset / multiplier shared by all labels ("×10⁻⁶", "+1.7×10⁹"), drawn in
    /// the axis corner; value = label × 10^n + offset.  Empty for plain labels.
    std::string offset_label;
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
    const SeriesRemovedCallback& series_removed_callback() const { return on_series_removed_; }

    // ── Event system integration ──
    void         set_event_system(EventSystem* es) { event_system_ = es; }
    EventSystem* event_system() const { return event_system_; }

    /// Assign the axes' layout cell.  When a colorbar is shown (see
    /// show_colorbar), a strip on the right of the cell is reserved for it and
    /// viewport() is the remaining plot area.  A positive
    /// @p outside_legend_width first reserves a strip of that width on the
    /// right of the cell for a LegendPosition::OutsideRight legend
    /// (see outside_legend_area()).
    virtual void set_viewport(const Rect& r, float outside_legend_width = 0.0f);
    const Rect&  viewport() const { return viewport_; }
    /// Strip reserved right of the plot area (and colorbar) for an outside
    /// legend; zero-sized when none is reserved.
    const Rect& outside_legend_area() const { return outside_legend_area_; }

    /// Screen scale of the axes text, tick marks and their padding (the UI
    /// scale setting).  Set by the layout together with the viewport; 1 for
    /// exports and headless layouts.
    float text_scale() const { return text_scale_; }
    void  set_text_scale(float s) { text_scale_ = (s > 0.0f && s < 16.0f) ? s : 1.0f; }

    /// @brief Axes title.
    const std::string& title() const { return title_; }
    void               title(const std::string& t) { title_ = t; }

    /// @brief Whether grid lines are drawn.
    bool grid_enabled() const { return grid_enabled_; }
    void grid(bool enabled) { grid_enabled_ = enabled; }

    bool border_enabled() const { return border_enabled_; }
    void show_border(bool enabled) { border_enabled_ = enabled; }

    /// @brief Whether minor grid lines are drawn between the major ones (2D axes;
    /// only while the grid is on).  On by default.
    bool minor_grid_enabled() const { return minor_grid_enabled_; }
    void minor_grid(bool enabled) { minor_grid_enabled_ = enabled; }

    /// @brief Whether minor tick marks are drawn between the major ones (2D axes).
    bool minor_ticks_enabled() const { return minor_ticks_enabled_; }
    void minor_ticks(bool enabled) { minor_ticks_enabled_ = enabled; }

    /// @brief Hide the top and right axis lines (2D axes), keeping the bottom
    /// and left ones.  Has no effect while the border is hidden.
    bool despined() const { return despined_; }
    void despine(bool enabled = true) { despined_ = enabled; }

    /// @brief Side of the axis lines the tick marks point to (2D axes).
    TickDirection tick_direction() const { return tick_direction_; }
    void          tick_direction(TickDirection d) { tick_direction_ = d; }

    /// @brief Width of new line series (pixels).  Set by apply_style().
    float default_line_width() const { return default_line_width_; }
    /// @brief Marker size of new series (pixels).  Set by apply_style().
    float default_marker_size() const { return default_marker_size_; }
    /// @brief Set the line width and marker size of new series (values <= 0 or
    /// non-finite are ignored).  Existing series are not changed.
    void set_series_defaults(float line_width, float marker_size)
    {
        if (line_width > 0.0f && line_width < 1e4f)
            default_line_width_ = line_width;
        if (marker_size > 0.0f && marker_size < 1e4f)
            default_marker_size_ = marker_size;
    }

    /// @brief Apply the axes part of a plot style: grid, minor grid / ticks,
    /// spines, tick direction and length, font sizes, and the line width and
    /// marker size of new series.  Existing series that still have the previous
    /// default width / marker size take the new one; explicitly set ones keep theirs.
    void apply_style(const StyleSheet& style);

    /// Colorbar visibility.  Drawn (and given layout space) when the axes holds a
    /// colormapped ScatterSeries or SurfaceSeries.
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
    bool                                 grid_enabled_        = true;
    bool                                 border_enabled_      = true;
    bool                                 colorbar_visible_    = false;
    bool                                 minor_grid_enabled_  = true;
    bool                                 minor_ticks_enabled_ = false;
    bool                                 despined_            = false;
    TickDirection                        tick_direction_      = TickDirection::Out;
    float                                default_line_width_  = 2.0f;
    float                                default_marker_size_ = 6.0f;
    AxisStyle                            axis_style_;
    Rect                                 viewport_;
    Rect                                 outside_legend_area_{0.0f, 0.0f, 0.0f, 0.0f};
    float                                text_scale_ = 1.0f;
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
    ~Axes() override;
    Axes(const Axes&)            = delete;
    Axes& operator=(const Axes&) = delete;

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

    /// Double-precision overloads of line(), scatter() and plot().  Use them for
    /// data that float cannot hold precisely, such as Unix timestamps
    /// (~1.7e9 s) or 1e7 + small variations: the series stores float offsets
    /// from a double origin (see LineSeries::set_x(std::span<const double>)),
    /// and the plot, axis limits, ticks and readouts show the true values.
    /// (Templates only so that existing calls such as line({}, {}) still pick
    /// the float overloads.)
    template <typename = void>
    LineSeries& line(std::span<const double> x, std::span<const double> y)
    {
        return line_f64(x, y);
    }
    template <typename = void>
    ScatterSeries& scatter(std::span<const double> x, std::span<const double> y)
    {
        return scatter_f64(x, y);
    }
    template <typename = void>
    LineSeries& plot(std::span<const double> x,
                     std::span<const double> y,
                     std::string_view        fmt = "-")
    {
        auto& ref = line_f64(x, y);
        ref.format(fmt);
        return ref;
    }
    template <typename = void>
    LineSeries& plot(std::span<const double> x, std::span<const double> y, const PlotStyle& style)
    {
        auto& ref = line_f64(x, y);
        ref.plot_style(style);
        return ref;
    }

    /// Step plot, like matplotlib's step(): a line with the given format whose
    /// value changes in steps at each x.  `where` picks where the step sits
    /// (StepsPre, the matplotlib default, StepsMid or StepsPost).
    LineSeries& step(std::span<const float> x,
                     std::span<const float> y,
                     std::string_view       fmt   = "-",
                     DrawStyle              where = DrawStyle::StepsPre);

    /// Stairs, like matplotlib's stairs(): values[i] holds from edges[i] to
    /// edges[i + 1], so `edges` has one entry more than `values` (otherwise
    /// the series is empty).  A post-step line through the edges.
    LineSeries& stairs(std::span<const float> values,
                       std::span<const float> edges,
                       std::string_view       fmt = "-");

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
    /// @brief Add a bar chart of named categories: bar i sits at x = i and the
    /// x axis shows one tick per category, labelled with its name.
    BarSeries& bar(const std::vector<std::string>& categories, std::span<const float> heights);
    /// @brief Add a filled band between @p lower and @p upper (e.g. a confidence interval).
    BandSeries& band(std::span<const float> x,
                     std::span<const float> lower,
                     std::span<const float> upper);
    /// @brief Add a stem plot.
    StemSeries& stem(std::span<const float> x, std::span<const float> y);

    /// Shape annotation series (rectangles, circles, arrows, polygons, etc.)
    ShapeSeries& shapes();

    // Grid-valued series (configure them through <spectra/series_image.hpp>).
    // The axes fit tightly to their extent, and AxesBase::show_colorbar(true)
    // adds a colorbar for their colormap.
    /// @brief Show a scalar grid (row-major, @p cols x @p rows) as a colormapped
    /// image; row 0 is drawn at the top, cell (c, r) is centred on x = c.
    ImageSeries& imshow(std::span<const float> values, size_t cols, size_t rows);
    /// @brief Heatmap: imshow() with each cell's value printed in it.  Non-empty
    /// @p col_names / @p row_names label the columns / rows on the axes.
    ImageSeries& heatmap(std::span<const float>   values,
                         size_t                   cols,
                         size_t                   rows,
                         std::vector<std::string> col_names = {},
                         std::vector<std::string> row_names = {});
    /// @brief Contour lines of z (row-major, row 0 at the bottom, node (c, r)
    /// at x = c, y = r) at about @p levels nice levels.
    ContourSeries& contour(std::span<const float> z, size_t cols, size_t rows, int levels = 8);
    /// @brief Filled contours of z (same layout as contour()): bands between
    /// about @p levels nice levels covering the data range.
    ImageSeries& contourf(std::span<const float> z, size_t cols, size_t rows, int levels = 8);

    // Text annotations (drawn on top of the series, clipped to the plot area)
    /// @brief Place @p text with its anchor at data coordinates (x, y).
    /// Defaults: left-aligned, text sitting just above the anchor, theme text colour.
    TextAnnotation& text(float x, float y, std::string_view text);
    /// @brief Place @p text centred at (text_x, text_y) with an arrow pointing to (x, y).
    TextAnnotation& annotate(std::string_view text, float x, float y, float text_x, float text_y);
    /// @brief Annotations in draw order. They do not take part in auto_fit().
    const std::vector<std::unique_ptr<TextAnnotation>>& annotations() const { return annotations_; }
    /// @brief Remove all annotations (invalidates references returned by text()/annotate()).
    void clear_annotations() { annotations_.clear(); }

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

    /// How bar series of the same orientation share a position (default
    /// BarMode::Grouped).  Grouped places the series that have bars at the
    /// same positions side by side within the widest bar width; series whose
    /// positions no other series uses keep their full width.  Stacked stacks
    /// the values of every visible bar series, starting from the first
    /// series' baseline; positive and negative values stack separately.
    void    bar_mode(BarMode mode) { bar_mode_ = mode; }
    BarMode bar_mode() const { return bar_mode_; }
    /// Apply bar_mode() to the bar series' layout (offset, width, per-bar
    /// base).  Cheap when nothing changed; limits and the renderers call it.
    void sync_bar_layout() const;

    /// Date-time axis: the values are Unix epoch seconds (UTC, e.g. from the
    /// double-precision plot()/line() overloads) and the ticks fall on calendar
    /// steps (seconds .. years) labelled as times and dates ("14:30", "Mar 05",
    /// "2024"), with the date or year context in the axis corner.  Only applies
    /// while the axis scale is ScaleType::Linear.
    void xaxis_datetime(bool enabled) { x_datetime_ = enabled; }
    bool xaxis_datetime() const { return x_datetime_; }
    void yaxis_datetime(bool enabled) { y_datetime_ = enabled; }
    bool yaxis_datetime() const { return y_datetime_; }

    /// Fixed ticks (matplotlib's xticks / yticks): marks only at `positions`
    /// inside the axis limits, labelled with `labels` (same length) or, when
    /// `labels` is empty, with the formatted values.  An empty `positions`
    /// hides the ticks and their labels.  Overrides the automatic, category
    /// and date-time ticks until cleared.
    void xticks(std::vector<double> positions, std::vector<std::string> labels = {});
    void yticks(std::vector<double> positions, std::vector<std::string> labels = {});
    /// Back to automatic ticks.
    void clear_xticks() { manual_xticks_.reset(); }
    void clear_yticks() { manual_yticks_.reset(); }
    /// The fixed ticks as given (labels empty when formatted), or nullptr.
    const TickResult* manual_xticks() const { return manual_xticks_ ? &*manual_xticks_ : nullptr; }
    const TickResult* manual_yticks() const { return manual_yticks_ ? &*manual_yticks_ : nullptr; }

    // Twin axes (matplotlib's twinx)
    /// @brief A second y axis on the right of this axes, sharing its x axis.
    /// The twin is drawn in the same plot area: its series use its own y
    /// limits, ticks, scale and label (shown on the right), while x limits,
    /// pan and zoom follow this axes.  Its series join this axes' legend and
    /// continue its colour cycle.  Created on the first call; later calls
    /// return the same twin.  Calling it on a twin returns that twin.
    Axes& twinx();
    /// Lays out the plot area and, with a twin, keeps room on the right for
    /// the twin's tick labels and label; the twin gets the same plot area.
    void set_viewport(const Rect& r, float outside_legend_width = 0.0f) override;
    /// @brief The twin made by twinx(), or nullptr.
    Axes* twin() const { return twin_.get(); }
    /// @brief For a twin: the axes it belongs to; nullptr otherwise.
    Axes* twin_host() const { return twin_host_; }
    bool  is_twin() const { return twin_host_ != nullptr; }
    /// @brief Delete the twin and its series.
    void remove_twin();
    /// @brief Carry a change of this axes' y view over to its twin, so pan and
    /// zoom move both y axes together.  Called once per frame by the renderer:
    /// when this axes' explicit y limits changed since the last call, the
    /// twin's y range is scaled and shifted by the same fractions of its
    /// height.  Autoscaled limits (no explicit ylim) are only recorded.
    void sync_twin_y();

    // Shared axes (matplotlib's sharex / sharey)
    /// @brief Share the x axis with @p other.  All axes of a share group show the
    /// same x limits: setting them on one (xlim(), pan, zoom) sets them on all,
    /// auto_fit() resets all, and autoscaling fits the data of the whole group.
    /// Explicit limits already set on members are merged (their union) on joining.
    void sharex(Axes& other);
    /// @brief Share the y axis with @p other (see sharex()).
    void sharey(Axes& other);
    /// @brief Leave the x share group; the current limits stay.
    void unshare_x();
    /// @brief Leave the y share group; the current limits stay.
    void unshare_y();
    /// @brief True when this axes shares its x / y axis with at least one other axes.
    bool x_shared() const;
    bool y_shared() const;
    /// @brief The other axes this one shares its x / y axis with.
    std::vector<Axes*> shared_x_axes() const;
    std::vector<Axes*> shared_y_axes() const;

    /// @brief Whether the x / y tick labels are drawn.  The figure layout hides
    /// them on the inner axes of a shared column / row, so only the outer axes
    /// (bottom row for x, left column for y) carry them.
    bool x_tick_labels_visible() const { return x_tick_labels_visible_; }
    bool y_tick_labels_visible() const { return y_tick_labels_visible_; }
    void set_tick_labels_visible(bool x, bool y)
    {
        x_tick_labels_visible_ = x;
        y_tick_labels_visible_ = y;
    }

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
    /// Right edge the live window follows: the pinned right edge when one is
    /// set, else the latest x sample.  nullopt without a presented buffer or data.
    std::optional<double> presented_buffer_head() const;

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

    /// Layout hold (internal; maintained by layout_figure()).  While the view
    /// is in motion (limits changing quickly), the room reserved for tick
    /// labels and offset labels never shrinks, so the plot area keeps its size
    /// instead of twitching by a pixel or two as labels scroll.  The hold is
    /// released once the view has been at rest for a moment.
    struct LayoutHold
    {
        bool                                  have_limits = false;
        double                                x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
        bool                                  have_change = false;
        std::chrono::steady_clock::time_point last_change{};
        bool                                  moving = false;
        // Held measurements (only meaningful while moving).
        float y_tick_w     = 0.0f;    // widest y tick label
        float over_l       = 0.0f;    // x tick label overhang reserved on the left
        float over_r       = 0.0f;    // ... and on the right
        bool  x_offset_row = false;   // row for the x tick offset label
        bool  y_offset_row = false;   // row for the y tick offset label
        // What the hold was measured for; a change resets it.
        float text_scale = 0.0f;
        float cell_w     = 0.0f;
        float cell_h     = 0.0f;

        void release()
        {
            moving       = false;
            have_change  = false;
            y_tick_w     = 0.0f;
            over_l       = 0.0f;
            over_r       = 0.0f;
            x_offset_row = false;
            y_offset_row = false;
        }
    };
    LayoutHold& layout_hold() const { return layout_hold_; }

   private:
    mutable LayoutHold layout_hold_;

    LineSeries&    line_f64(std::span<const double> x, std::span<const double> y);
    ScatterSeries& scatter_f64(std::span<const double> x, std::span<const double> y);

    // Internal helper: construct T(args...), assign default cycle color, push to series_
    template <typename T, typename... Args>
    T& add_series(Args&&... args);

    // Limits before log-axis clamping (see x_limits()).
    AxisLimits x_limits_unclamped() const;
    AxisLimits y_limits_unclamped() const;

    // Data extent of all autoscaled series, cached between limit queries:
    // x_limits()/y_limits() run many times per frame and a full scan of a
    // million-point series costs tens of milliseconds.  Keyed on each series'
    // identity, change_generation() and autoscale exclusion.
    struct DataExtentCache
    {
        std::vector<std::uint64_t> key;
        double                     x[3]  = {0.0, 0.0, 0.0};   // min, max, min_pos
        double                     y[3]  = {0.0, 0.0, 0.0};
        double                     sx[3] = {0.0, 0.0, 0.0};   // sticky (grid, bar baseline) extent
        double                     sy[3] = {0.0, 0.0, 0.0};
        bool                       valid = false;
    };
    const DataExtentCache& data_extent_cached() const;   // caller holds extent_mutex_
    // Union of the cached data extents of every member of a share group, x
    // (x_axis) or y: ext/sticky are [min, max, min_pos] as in DataExtentCache.
    // Locks each member's extent_mutex_ in turn; the caller holds none.
    static void group_extent(const AxesShareGroup& g,
                             bool                  x_axis,
                             double (&ext)[3],
                             double (&sticky)[3]);
    // Autoscaled limits from the (group) data extents and the autoscale mode.
    void auto_limits(AxisLimits& xl, AxisLimits& yl) const;

    mutable std::mutex      extent_mutex_;
    mutable DataExtentCache extent_cache_;

    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;

    std::vector<std::unique_ptr<TextAnnotation>> annotations_;

    std::string               xlabel_;
    std::string               ylabel_;
    ScaleType                 xscale_     = ScaleType::Linear;
    ScaleType                 yscale_     = ScaleType::Linear;
    bool                      x_datetime_ = false;
    bool                      y_datetime_ = false;
    std::optional<TickResult> manual_xticks_;
    std::optional<TickResult> manual_yticks_;
    AutoscaleMode             autoscale_mode_ = AutoscaleMode::Padded;
    std::optional<float>      presented_buffer_seconds_;
    bool                      presented_buffer_following_ = false;
    std::optional<double>     presented_buffer_right_edge_;

    // Bar layout: the mode and the inputs of the last sync_bar_layout().
    BarMode                            bar_mode_ = BarMode::Grouped;
    mutable std::mutex                 bar_layout_mutex_;
    mutable std::vector<std::uint64_t> bar_layout_key_;
    // twinx(): the owned twin, or for a twin the axes that owns it.
    std::unique_ptr<Axes> twin_;
    Axes*                 twin_host_ = nullptr;
    // sync_twin_y(): the y limits seen at the previous call.
    std::optional<AxisLimits> twin_seen_ylim_;
    // Share groups (sharex()/sharey()); null when not shared.
    std::shared_ptr<AxesShareGroup> share_x_;
    std::shared_ptr<AxesShareGroup> share_y_;
    bool                            x_tick_labels_visible_ = true;
    bool                            y_tick_labels_visible_ = true;

    // Topic-driven continuous auto-zoom state (see set_topic_auto_zoom).
    bool                      topic_auto_zoom_ = false;
    std::optional<AxisLimits> last_auto_xlim_;
    std::optional<AxisLimits> last_auto_ylim_;
};

}   // namespace spectra

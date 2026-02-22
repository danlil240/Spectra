#pragma once

#include <cmath>
#include <span>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

// ─── Box Plot Series ────────────────────────────────────────────────────────
// Renders one or more box-and-whisker plots.
// Each box is defined by a dataset; statistics (median, Q1, Q3, whiskers,
// outliers) are computed automatically.

struct BoxPlotStats
{
    float              median       = 0.0f;
    float              q1           = 0.0f;   // 25th percentile
    float              q3           = 0.0f;   // 75th percentile
    float              whisker_low  = 0.0f;
    float              whisker_high = 0.0f;
    std::vector<float> outliers;
};

class BoxPlotSeries : public Series
{
   public:
    BoxPlotSeries() = default;

    // Add a box at the given x position from raw data values.
    BoxPlotSeries& add_box(float x_position, std::span<const float> values);

    // Add a box from pre-computed statistics.
    BoxPlotSeries& add_box(float                  x_position,
                           float                  median,
                           float                  q1,
                           float                  q3,
                           float                  whisker_low,
                           float                  whisker_high,
                           std::span<const float> outliers = {});

    // Box visual width (in data units). Default: 0.6
    BoxPlotSeries& box_width(float w)
    {
        box_width_ = w;
        dirty_     = true;
        return *this;
    }
    float box_width() const { return box_width_; }

    // Show outlier points
    BoxPlotSeries& show_outliers(bool show)
    {
        show_outliers_ = show;
        dirty_         = true;
        return *this;
    }
    bool show_outliers() const { return show_outliers_; }

    // Notched box plot (narrows at median)
    BoxPlotSeries& notched(bool n)
    {
        notched_ = n;
        dirty_   = true;
        return *this;
    }
    bool notched() const { return notched_; }

    // Access computed geometry (line segments for rendering)
    std::span<const float> x_data() const { return line_x_; }
    std::span<const float> y_data() const { return line_y_; }
    size_t                 point_count() const { return line_x_.size(); }

    // Access outlier points (scatter rendering)
    std::span<const float> outlier_x() const { return outlier_x_; }
    std::span<const float> outlier_y() const { return outlier_y_; }
    size_t                 outlier_count() const { return outlier_x_.size(); }

    // Access statistics
    const std::vector<float>&        positions() const { return positions_; }
    const std::vector<BoxPlotStats>& stats() const { return stats_; }

    // Fluent setters (covariant return)
    using Series::color;
    using Series::label;
    using Series::opacity;
    BoxPlotSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    BoxPlotSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    BoxPlotSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    void record_commands(Renderer& renderer) override;

    // Rebuild line/scatter geometry from stats
    void rebuild_geometry();

    // Compute statistics from raw data
    static BoxPlotStats compute_stats(std::span<const float> values);

   private:
    std::vector<float>        positions_;
    std::vector<BoxPlotStats> stats_;
    float                     box_width_     = 0.6f;
    bool                      show_outliers_ = true;
    bool                      notched_       = false;

    // Generated geometry
    std::vector<float> line_x_;
    std::vector<float> line_y_;
    std::vector<float> outlier_x_;
    std::vector<float> outlier_y_;
};

// ─── Violin Series ──────────────────────────────────────────────────────────
// Renders one or more violin plots (mirrored kernel density estimate).

class ViolinSeries : public Series
{
   public:
    ViolinSeries() = default;

    // Add a violin at the given x position from raw data values.
    ViolinSeries& add_violin(float x_position, std::span<const float> values);

    // Violin visual width (in data units). Default: 0.8
    ViolinSeries& violin_width(float w)
    {
        violin_width_ = w;
        dirty_        = true;
        return *this;
    }
    float violin_width() const { return violin_width_; }

    // Number of points in the KDE curve. Default: 50
    ViolinSeries& resolution(int n)
    {
        resolution_ = n;
        dirty_      = true;
        return *this;
    }
    int resolution() const { return resolution_; }

    // Show inner box plot
    ViolinSeries& show_box(bool show)
    {
        show_box_ = show;
        dirty_    = true;
        return *this;
    }
    bool show_box() const { return show_box_; }

    // Access computed geometry
    std::span<const float> x_data() const { return line_x_; }
    std::span<const float> y_data() const { return line_y_; }
    size_t                 point_count() const { return line_x_.size(); }

    // Fluent setters
    using Series::color;
    using Series::label;
    using Series::opacity;
    ViolinSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ViolinSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ViolinSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    void record_commands(Renderer& renderer) override;
    void rebuild_geometry();

   private:
    struct ViolinData
    {
        float              x_position;
        std::vector<float> values;
    };
    std::vector<ViolinData> violins_;
    float                   violin_width_ = 0.8f;
    int                     resolution_   = 50;
    bool                    show_box_     = true;

    // Generated geometry
    std::vector<float> line_x_;
    std::vector<float> line_y_;
};

// ─── Histogram Series ───────────────────────────────────────────────────────
// Renders a histogram from raw data values.

class HistogramSeries : public Series
{
   public:
    HistogramSeries() = default;
    HistogramSeries(std::span<const float> values, int bins = 30);

    // Set data and recompute bins
    HistogramSeries& set_data(std::span<const float> values, int bins = 30);

    // Number of bins
    HistogramSeries& bins(int n)
    {
        bins_  = n;
        dirty_ = true;
        return *this;
    }
    int bins() const { return bins_; }

    // Cumulative histogram
    HistogramSeries& cumulative(bool c)
    {
        cumulative_ = c;
        dirty_      = true;
        return *this;
    }
    bool cumulative() const { return cumulative_; }

    // Density normalization (area = 1)
    HistogramSeries& density(bool d)
    {
        density_ = d;
        dirty_   = true;
        return *this;
    }
    bool density() const { return density_; }

    // Access computed step-function geometry
    std::span<const float> x_data() const { return line_x_; }
    std::span<const float> y_data() const { return line_y_; }
    size_t                 point_count() const { return line_x_.size(); }

    // Access bin edges and counts
    const std::vector<float>& bin_edges() const { return bin_edges_; }
    const std::vector<float>& bin_counts() const { return bin_counts_; }

    // Fluent setters
    using Series::color;
    using Series::label;
    using Series::opacity;
    HistogramSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    HistogramSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    HistogramSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    void record_commands(Renderer& renderer) override;
    void rebuild_geometry();

   private:
    std::vector<float> raw_values_;
    int                bins_       = 30;
    bool               cumulative_ = false;
    bool               density_    = false;

    // Computed
    std::vector<float> bin_edges_;
    std::vector<float> bin_counts_;

    // Generated geometry (step function)
    std::vector<float> line_x_;
    std::vector<float> line_y_;
};

// ─── Bar Series ─────────────────────────────────────────────────────────────
// Renders a bar chart from category positions and heights.

enum class BarOrientation
{
    Vertical,
    Horizontal,
};

class BarSeries : public Series
{
   public:
    BarSeries() = default;
    BarSeries(std::span<const float> positions, std::span<const float> heights);

    // Set bar data
    BarSeries& set_data(std::span<const float> positions, std::span<const float> heights);

    // Bar width (in data units). Default: 0.8
    BarSeries& bar_width(float w)
    {
        bar_width_ = w;
        dirty_     = true;
        return *this;
    }
    float bar_width() const { return bar_width_; }

    // Baseline value (bottom of bars). Default: 0
    BarSeries& baseline(float b)
    {
        baseline_ = b;
        dirty_    = true;
        return *this;
    }
    float baseline() const { return baseline_; }

    // Orientation
    BarSeries& orientation(BarOrientation o)
    {
        orientation_ = o;
        dirty_       = true;
        return *this;
    }
    BarOrientation orientation() const { return orientation_; }

    // Access computed geometry (rectangle outlines as line segments)
    std::span<const float> x_data() const { return line_x_; }
    std::span<const float> y_data() const { return line_y_; }
    size_t                 point_count() const { return line_x_.size(); }

    // Access raw positions/heights
    const std::vector<float>& bar_positions() const { return positions_; }
    const std::vector<float>& bar_heights() const { return heights_; }

    // Fluent setters
    using Series::color;
    using Series::label;
    using Series::opacity;
    BarSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    BarSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    BarSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    void record_commands(Renderer& renderer) override;
    void rebuild_geometry();

   private:
    std::vector<float> positions_;
    std::vector<float> heights_;
    float              bar_width_   = 0.8f;
    float              baseline_    = 0.0f;
    BarOrientation     orientation_ = BarOrientation::Vertical;

    // Generated geometry (rectangle outlines)
    std::vector<float> line_x_;
    std::vector<float> line_y_;
};

}   // namespace spectra

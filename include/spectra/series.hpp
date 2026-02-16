#pragma once

#include <memory>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <spectra/plot_style.hpp>
#include <span>
#include <string>
#include <vector>

namespace spectra
{

struct SeriesStyle
{
    Color color = colors::blue;
    float line_width = 2.0f;
    float point_size = 4.0f;
    float opacity = 1.0f;
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class Series
{
   public:
    virtual ~Series() = default;

    Series& label(const std::string& lbl)
    {
        label_ = lbl;
        return *this;
    }
    Series& color(const Color& c)
    {
        color_ = c;
        dirty_ = true;
        return *this;
    }
    Series& visible(bool v)
    {
        visible_ = v;
        return *this;
    }

    const std::string& label() const { return label_; }
    const Color& color() const { return color_; }
    bool visible() const { return visible_; }

    bool is_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }
    void mark_dirty() { dirty_ = true; }
    void set_color(const Color& c)
    {
        color_ = c;
        dirty_ = true;
    }

    // ── Plot style (line style, marker style, etc.) ──
    Series& line_style(LineStyle s)
    {
        style_.line_style = s;
        dirty_ = true;
        return *this;
    }
    Series& marker_style(MarkerStyle s)
    {
        style_.marker_style = s;
        dirty_ = true;
        return *this;
    }
    Series& marker_size(float s)
    {
        style_.marker_size = s;
        dirty_ = true;
        return *this;
    }
    Series& opacity(float o)
    {
        style_.opacity = o;
        dirty_ = true;
        return *this;
    }
    Series& plot_style(const PlotStyle& ps);

    LineStyle line_style() const { return style_.line_style; }
    MarkerStyle marker_style() const { return style_.marker_style; }
    float marker_size() const { return style_.marker_size; }
    float opacity() const { return style_.opacity; }
    const PlotStyle& plot_style() const { return style_; }
    PlotStyle& plot_style_mut() { return style_; }

    virtual void record_commands(Renderer& renderer) = 0;

   protected:
    std::string label_;
    Color color_ = colors::blue;
    PlotStyle style_;  // line/marker style, sizes, opacity
    bool visible_ = true;
    bool dirty_ = true;
};

class LineSeries : public Series
{
   public:
    LineSeries() = default;
    LineSeries(std::span<const float> x, std::span<const float> y);

    LineSeries& set_x(std::span<const float> x);
    LineSeries& set_y(std::span<const float> y);
    void append(float x, float y);

    LineSeries& width(float w)
    {
        line_width_ = w;
        dirty_ = true;
        return *this;
    }
    float width() const { return line_width_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    size_t point_count() const { return x_.size(); }

    void record_commands(Renderer& renderer) override;

    // Bring base-class getters into scope (setters below would otherwise hide them)
    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    // Re-declare fluent setters with correct return type
    LineSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    LineSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    LineSeries& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    LineSeries& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    LineSeries& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    LineSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Apply a MATLAB-style format string (e.g. "r--o")
    LineSeries& format(std::string_view fmt);

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    float line_width_ = 2.0f;
};

class ScatterSeries : public Series
{
   public:
    ScatterSeries() = default;
    ScatterSeries(std::span<const float> x, std::span<const float> y);

    ScatterSeries& set_x(std::span<const float> x);
    ScatterSeries& set_y(std::span<const float> y);
    void append(float x, float y);

    ScatterSeries& size(float s)
    {
        point_size_ = s;
        dirty_ = true;
        return *this;
    }
    float size() const { return point_size_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    size_t point_count() const { return x_.size(); }

    void record_commands(Renderer& renderer) override;

    // Bring base-class getters into scope (setters below would otherwise hide them)
    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    // Re-declare fluent setters with correct return type
    ScatterSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ScatterSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ScatterSeries& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    ScatterSeries& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    ScatterSeries& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    ScatterSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // Apply a MATLAB-style format string (e.g. "ro")
    ScatterSeries& format(std::string_view fmt);

   private:
    std::vector<float> x_;
    std::vector<float> y_;
    float point_size_ = 4.0f;
};

}  // namespace spectra

#pragma once

#include <plotix/color.hpp>
#include <plotix/fwd.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace plotix {

struct SeriesStyle {
    Color  color      = colors::blue;
    float  line_width = 2.0f;
    float  point_size = 4.0f;
    float  opacity    = 1.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class Series {
public:
    virtual ~Series() = default;

    Series& label(const std::string& lbl) { label_ = lbl; return *this; }
    Series& color(const Color& c)         { color_ = c; dirty_ = true; return *this; }
    Series& visible(bool v)               { visible_ = v; return *this; }

    const std::string& label() const { return label_; }
    const Color& color() const       { return color_; }
    bool visible() const             { return visible_; }

    bool is_dirty() const    { return dirty_; }
    void clear_dirty()       { dirty_ = false; }

    virtual void record_commands(Renderer& renderer) = 0;

protected:
    std::string label_;
    Color       color_ = colors::blue;
    bool        visible_ = true;
    bool        dirty_ = true;
};

class LineSeries : public Series {
public:
    LineSeries() = default;
    LineSeries(std::span<const float> x, std::span<const float> y);

    LineSeries& set_x(std::span<const float> x);
    LineSeries& set_y(std::span<const float> y);
    void append(float x, float y);

    LineSeries& width(float w) { line_width_ = w; dirty_ = true; return *this; }
    float width() const { return line_width_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    size_t point_count() const { return x_.size(); }

    void record_commands(Renderer& renderer) override;

    // Re-declare fluent setters with correct return type
    LineSeries& label(const std::string& lbl) { Series::label(lbl); return *this; }
    LineSeries& color(const Color& c)         { Series::color(c); return *this; }

private:
    std::vector<float> x_;
    std::vector<float> y_;
    float line_width_ = 2.0f;
};

class ScatterSeries : public Series {
public:
    ScatterSeries() = default;
    ScatterSeries(std::span<const float> x, std::span<const float> y);

    ScatterSeries& set_x(std::span<const float> x);
    ScatterSeries& set_y(std::span<const float> y);
    void append(float x, float y);

    ScatterSeries& size(float s) { point_size_ = s; dirty_ = true; return *this; }
    float size() const { return point_size_; }

    std::span<const float> x_data() const { return x_; }
    std::span<const float> y_data() const { return y_; }
    size_t point_count() const { return x_.size(); }

    void record_commands(Renderer& renderer) override;

    // Re-declare fluent setters with correct return type
    ScatterSeries& label(const std::string& lbl) { Series::label(lbl); return *this; }
    ScatterSeries& color(const Color& c)         { Series::color(c); return *this; }

private:
    std::vector<float> x_;
    std::vector<float> y_;
    float point_size_ = 4.0f;
};

} // namespace plotix

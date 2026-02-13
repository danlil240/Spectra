#pragma once

#include <plotix/color.hpp>
#include <plotix/fwd.hpp>
#include <plotix/series.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace plotix {

struct AxisLimits {
    float min = 0.0f;
    float max = 1.0f;
};

struct TickResult {
    std::vector<float> positions;
    std::vector<std::string> labels;
};

class Axes {
public:
    Axes() = default;

    // Series creation — returns reference for fluent API
    LineSeries& line(std::span<const float> x, std::span<const float> y);
    LineSeries& line();

    ScatterSeries& scatter(std::span<const float> x, std::span<const float> y);
    ScatterSeries& scatter();

    // Axis configuration
    void xlim(float min, float max);
    void ylim(float min, float max);
    void title(const std::string& t);
    void xlabel(const std::string& lbl);
    void ylabel(const std::string& lbl);
    void grid(bool enabled);

    // Accessors
    AxisLimits x_limits() const;
    AxisLimits y_limits() const;
    const std::string& get_title() const  { return title_; }
    const std::string& get_xlabel() const { return xlabel_; }
    const std::string& get_ylabel() const { return ylabel_; }
    bool grid_enabled() const             { return grid_enabled_; }

    // Tick computation
    TickResult compute_x_ticks() const;
    TickResult compute_y_ticks() const;

    // Auto-fit limits to data
    void auto_fit();

    // Access series
    const std::vector<std::unique_ptr<Series>>& series() const { return series_; }

    // Viewport rect (set by layout engine)
    void set_viewport(const Rect& r) { viewport_ = r; }
    const Rect& viewport() const     { return viewport_; }

private:
    std::vector<std::unique_ptr<Series>> series_;

    std::optional<AxisLimits> xlim_;
    std::optional<AxisLimits> ylim_;

    std::string title_;
    std::string xlabel_;
    std::string ylabel_;
    bool grid_enabled_ = true;

    Rect viewport_;
};

} // namespace plotix

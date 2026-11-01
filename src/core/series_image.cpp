#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <spectra/series_image.hpp>
#include <stdexcept>

#include "contour.hpp"
#include "image_colormap.hpp"

namespace spectra
{

namespace
{

// Generations are unique across all series (not per series), so a series
// allocated at the address of a destroyed one never matches its stale GPU
// textures.
uint64_t next_generation()
{
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::vector<float> sorted_finite_unique(std::vector<float> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](float f) { return !std::isfinite(f); }), v.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

}   // namespace

// ─── ImageSeries ────────────────────────────────────────────────────────────

ImageSeries::ImageSeries()
{
    set_show_in_legend(false);
    data_generation_ = next_generation();
    lut_generation_  = next_generation();
}

ImageSeries::ImageSeries(std::span<const float> values, size_t cols, size_t rows) : ImageSeries()
{
    set_data(values, cols, rows);
}

ImageSeries& ImageSeries::set_data(std::span<const float> values, size_t cols, size_t rows)
{
    if (values.size() != cols * rows)
        throw std::invalid_argument("ImageSeries::set_data: values.size() must equal cols * rows");
    values_.assign(values.begin(), values.end());
    cols_     = cols;
    rows_     = rows;
    channels_ = 1;
    if (!extent_set_)
    {
        // Unit cells centred on integer coordinates (or nodes on integers).
        const double h = node_aligned_ ? 0.0 : 0.5;
        x0_            = -h;
        x1_            = static_cast<double>(cols) - 1.0 + h;
        y0_            = -h;
        y1_            = static_cast<double>(rows) - 1.0 + h;
    }
    recompute_data_range();
    data_generation_ = next_generation();
    dirty_           = true;
    return *this;
}

ImageSeries& ImageSeries::set_rgba(std::span<const float> pixels,
                                   size_t                 cols,
                                   size_t                 rows,
                                   int                    channels)
{
    if (channels != 3 && channels != 4)
        throw std::invalid_argument("ImageSeries::set_rgba: channels must be 3 or 4");
    const auto ch = static_cast<size_t>(channels);
    if (pixels.size() != cols * rows * ch)
        throw std::invalid_argument(
            "ImageSeries::set_rgba: pixels.size() must equal cols * rows * channels");
    // Reuse set_data for the size bookkeeping, then store RGBA.
    std::vector<float> rgba(cols * rows * 4);
    for (size_t i = 0; i < cols * rows; ++i)
    {
        for (size_t c = 0; c < 3; ++c)
            rgba[i * 4 + c] = std::clamp(pixels[i * ch + c], 0.0f, 1.0f);
        rgba[i * 4 + 3] = ch == 4 ? std::clamp(pixels[i * ch + 3], 0.0f, 1.0f) : 1.0f;
    }
    std::vector<float> dummy(cols * rows, 0.0f);
    set_data(dummy, cols, rows);
    values_   = std::move(rgba);
    channels_ = 4;
    return *this;
}

void ImageSeries::recompute_data_range()
{
    float lo  = std::numeric_limits<float>::max();
    float hi  = std::numeric_limits<float>::lowest();
    bool  any = false;
    for (float v : values_)
    {
        if (!std::isfinite(v))
            continue;
        lo  = std::min(lo, v);
        hi  = std::max(hi, v);
        any = true;
    }
    data_min_ = any ? lo : 0.0f;
    data_max_ = any ? hi : 1.0f;
}

ImageSeries& ImageSeries::extent(double x0, double x1, double y0, double y1)
{
    x0_         = x0;
    x1_         = x1;
    y0_         = y0;
    y1_         = y1;
    extent_set_ = true;
    dirty_      = true;
    return *this;
}

ImageSeries& ImageSeries::colormap(ColormapType cm)
{
    colormap_       = cm;
    lut_generation_ = next_generation();
    dirty_          = true;
    return *this;
}

ImageSeries& ImageSeries::colormap_range(float vmin, float vmax)
{
    range_min_ = vmin;
    range_max_ = vmax;
    range_set_ = true;
    dirty_     = true;
    return *this;
}

ImageSeries& ImageSeries::clear_colormap_range()
{
    range_set_ = false;
    dirty_     = true;
    return *this;
}

ImageSeries& ImageSeries::interpolation(ImageInterpolation mode)
{
    interpolation_ = mode;
    dirty_         = true;
    return *this;
}

ImageSeries& ImageSeries::origin(ImageOrigin o)
{
    origin_ = o;
    dirty_  = true;
    return *this;
}

ImageSeries& ImageSeries::node_aligned(bool on)
{
    if (node_aligned_ == on)
        return *this;
    node_aligned_ = on;
    if (!extent_set_ && !empty())
    {
        const double h = on ? 0.0 : 0.5;
        x0_            = -h;
        x1_            = static_cast<double>(cols_) - 1.0 + h;
        y0_            = -h;
        y1_            = static_cast<double>(rows_) - 1.0 + h;
    }
    dirty_ = true;
    return *this;
}

ImageSeries& ImageSeries::levels(std::vector<float> levels)
{
    levels_ = sorted_finite_unique(std::move(levels));
    if (levels_.size() < 2)
        levels_.clear();
    lut_generation_ = next_generation();
    dirty_          = true;
    return *this;
}

ImageSeries& ImageSeries::show_values(bool on, int decimals)
{
    show_values_    = on;
    value_decimals_ = std::clamp(decimals, 0, 9);
    dirty_          = true;
    return *this;
}

ImageSeries& ImageSeries::x_categories(std::vector<std::string> names)
{
    x_categories_ = std::move(names);
    dirty_        = true;
    return *this;
}

ImageSeries& ImageSeries::y_categories(std::vector<std::string> names)
{
    y_categories_ = std::move(names);
    dirty_        = true;
    return *this;
}

float ImageSeries::vmin() const
{
    if (!levels_.empty())
        return levels_.front();
    return range_set_ ? range_min_ : data_min_;
}

float ImageSeries::vmax() const
{
    if (!levels_.empty())
        return levels_.back();
    return range_set_ ? range_max_ : data_max_;
}

void ImageSeries::cell_center(size_t col, size_t row, double& x, double& y) const
{
    const size_t drow = (origin_ == ImageOrigin::Upper && rows_ > 0) ? rows_ - 1 - row : row;
    if (node_aligned_)
    {
        const double dx = cols_ > 1 ? (x1_ - x0_) / static_cast<double>(cols_ - 1) : 0.0;
        const double dy = rows_ > 1 ? (y1_ - y0_) / static_cast<double>(rows_ - 1) : 0.0;
        x               = x0_ + static_cast<double>(col) * dx;
        y               = y0_ + static_cast<double>(drow) * dy;
        return;
    }
    const double dx = cols_ > 0 ? (x1_ - x0_) / static_cast<double>(cols_) : 0.0;
    const double dy = rows_ > 0 ? (y1_ - y0_) / static_cast<double>(rows_) : 0.0;
    x               = x0_ + (static_cast<double>(col) + 0.5) * dx;
    y               = y0_ + (static_cast<double>(drow) + 0.5) * dy;
}

bool ImageSeries::cell_at(double x, double y, size_t& col, size_t& row) const
{
    if (empty() || x1_ == x0_ || y1_ == y0_)
        return false;
    const double u = (x - x0_) / (x1_ - x0_);
    const double v = (y - y0_) / (y1_ - y0_);
    if (!(u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0))
        return false;
    auto index = [this](double f, size_t n)
    {
        if (node_aligned_)
            return static_cast<size_t>(std::lround(f * static_cast<double>(n - 1)));
        return std::min(static_cast<size_t>(f * static_cast<double>(n)), n - 1);
    };
    col               = index(u, cols_);
    const size_t drow = index(v, rows_);
    row               = origin_ == ImageOrigin::Upper ? rows_ - 1 - drow : drow;
    return true;
}

std::optional<float> ImageSeries::value_at(double x, double y) const
{
    size_t col = 0;
    size_t row = 0;
    if (is_rgba() || !cell_at(x, y, col, row))
        return std::nullopt;
    return values_[row * cols_ + col];
}

Color ImageSeries::color_for_value(float v) const
{
    if (!std::isfinite(v))
        return {0.0f, 0.0f, 0.0f, 0.0f};
    const float t = image_colormap::normalized(v, vmin(), vmax());
    if (!levels_.empty() && (t < -image_colormap::BAND_EPS || t > 1.0f + image_colormap::BAND_EPS))
        return {0.0f, 0.0f, 0.0f, 0.0f};
    const auto lut = image_colormap::build_lut(colormap_, levels_);
    return image_colormap::lut_color(lut, image_colormap::lut_index(t, lut.size() / 4));
}

// ─── ContourSeries ──────────────────────────────────────────────────────────

ContourSeries::ContourSeries()
{
    set_show_in_legend(false);
}

ContourSeries::ContourSeries(std::span<const float> z, size_t cols, size_t rows) : ContourSeries()
{
    set_data(z, cols, rows);
}

ContourSeries& ContourSeries::set_data(std::span<const float> z, size_t cols, size_t rows)
{
    if (z.size() != cols * rows)
        throw std::invalid_argument("ContourSeries::set_data: z.size() must equal cols * rows");
    z_.assign(z.begin(), z.end());
    cols_ = cols;
    rows_ = rows;
    if (explicit_coords_ && (xs_.size() != cols || ys_.size() != rows))
        explicit_coords_ = false;
    reset_coordinates();
    rebuild_geometry();
    dirty_ = true;
    return *this;
}

void ContourSeries::reset_coordinates()
{
    if (explicit_coords_)
        return;
    auto fill = [](std::vector<float>& out, size_t n, double a, double b)
    {
        out.resize(n);
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<float>(
                n > 1 ? a + (b - a) * static_cast<double>(i) / static_cast<double>(n - 1) : a);
    };
    if (extent_set_)
    {
        fill(xs_, cols_, ex0_, ex1_);
        fill(ys_, rows_, ey0_, ey1_);
    }
    else
    {
        fill(xs_, cols_, 0.0, cols_ > 0 ? static_cast<double>(cols_) - 1.0 : 0.0);
        fill(ys_, rows_, 0.0, rows_ > 0 ? static_cast<double>(rows_) - 1.0 : 0.0);
    }
}

ContourSeries& ContourSeries::extent(double x0, double x1, double y0, double y1)
{
    ex0_             = x0;
    ex1_             = x1;
    ey0_             = y0;
    ey1_             = y1;
    extent_set_      = true;
    explicit_coords_ = false;
    reset_coordinates();
    rebuild_geometry();
    dirty_ = true;
    return *this;
}

ContourSeries& ContourSeries::set_coordinates(std::span<const float> x, std::span<const float> y)
{
    if (x.size() != cols_ || y.size() != rows_)
        throw std::invalid_argument(
            "ContourSeries::set_coordinates: sizes must match the grid (cols, rows)");
    xs_.assign(x.begin(), x.end());
    ys_.assign(y.begin(), y.end());
    explicit_coords_ = true;
    rebuild_geometry();
    dirty_ = true;
    return *this;
}

ContourSeries& ContourSeries::levels(int n)
{
    level_count_ = std::max(1, n);
    explicit_levels_.clear();
    rebuild_geometry();
    dirty_ = true;
    return *this;
}

ContourSeries& ContourSeries::levels(std::vector<float> values)
{
    explicit_levels_ = sorted_finite_unique(std::move(values));
    rebuild_geometry();
    dirty_ = true;
    return *this;
}

ContourSeries& ContourSeries::colormap(ColormapType cm)
{
    colormap_ = cm;
    dirty_    = true;
    return *this;
}

ContourSeries& ContourSeries::colormap_range(float vmin, float vmax)
{
    range_min_ = vmin;
    range_max_ = vmax;
    range_set_ = true;
    dirty_     = true;
    return *this;
}

float ContourSeries::vmin() const
{
    if (range_set_)
        return range_min_;
    return level_values_.empty() ? 0.0f : level_values_.front();
}

float ContourSeries::vmax() const
{
    if (range_set_)
        return range_max_;
    return level_values_.empty() ? 1.0f : level_values_.back();
}

Color ContourSeries::level_color(float level) const
{
    return image_colormap::sample(colormap_, image_colormap::normalized(level, vmin(), vmax()));
}

void ContourSeries::rebuild_geometry()
{
    line_x_.clear();
    line_y_.clear();
    level_ranges_.clear();
    level_values_.clear();
    if (cols_ < 2 || rows_ < 2)
        return;

    if (!explicit_levels_.empty())
    {
        level_values_ = explicit_levels_;
    }
    else
    {
        float lo  = std::numeric_limits<float>::max();
        float hi  = std::numeric_limits<float>::lowest();
        bool  any = false;
        for (float v : z_)
        {
            if (!std::isfinite(v))
                continue;
            lo  = std::min(lo, v);
            hi  = std::max(hi, v);
            any = true;
        }
        if (any)
            level_values_ = contour::auto_levels(lo, hi, level_count_, false);
    }

    // Fractional grid index -> node coordinate (linear between nodes).
    auto coord = [](const std::vector<float>& nodes, float f)
    {
        const auto   n  = nodes.size();
        const double fi = std::clamp(static_cast<double>(f), 0.0, static_cast<double>(n - 1));
        const auto   i0 = std::min(static_cast<size_t>(fi), n - 2);
        const double t  = fi - static_cast<double>(i0);
        return static_cast<float>(nodes[i0] + t * (nodes[i0 + 1] - nodes[i0]));
    };

    constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
    for (float level : level_values_)
    {
        LevelRange range;
        range.level       = level;
        range.first_point = line_x_.size();
        for (const auto& pl : contour::trace(z_, cols_, rows_, level))
        {
            for (size_t i = 0; i < pl.x.size(); ++i)
            {
                line_x_.push_back(coord(xs_, pl.x[i]));
                line_y_.push_back(coord(ys_, pl.y[i]));
            }
            line_x_.push_back(NaN);
            line_y_.push_back(NaN);
        }
        range.point_count = line_x_.size() - range.first_point;
        if (range.point_count > 0)
            level_ranges_.push_back(range);
    }
}

}   // namespace spectra

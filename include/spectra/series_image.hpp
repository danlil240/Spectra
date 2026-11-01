#pragma once

// Grid-valued 2D series: ImageSeries (imshow / heatmap / filled contours) and
// ContourSeries (iso-lines).  Both drive the axes colorbar
// (AxesBase::show_colorbar) when they use a colormap.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <spectra/color.hpp>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

/// @brief How an ImageSeries is sampled between its grid values.
enum class ImageInterpolation
{
    Nearest,    ///< Each cell is one flat colour (default)
    Bilinear,   ///< Values are interpolated between neighbouring samples, then colormapped
};

/// @brief Which edge of the extent row 0 of the grid is drawn at.
enum class ImageOrigin
{
    Upper,   ///< Row 0 at the top (y1), like a picture (default, as matplotlib imshow)
    Lower,   ///< Row 0 at the bottom (y0), like a z(x, y) grid
};

/// @brief A W x H grid drawn as an image in data space.
///
/// Scalar grids are mapped through a colormap on the GPU: the values are
/// uploaded once and the colormap is a separate lookup table, so changing the
/// colormap or the value range never re-uploads the grid.  RGB(A) grids
/// (set_rgba) are drawn as given.
///
/// The grid is row-major: value(col, row) = values[row * cols + col].
/// By default each cell is a unit square centred on integer coordinates, i.e.
/// the extent is [-0.5, cols - 0.5] x [-0.5, rows - 0.5]; extent() changes it.
/// NaN cells are transparent.
class ImageSeries : public Series
{
   public:
    ImageSeries();
    /// @brief Scalar grid of @p cols x @p rows values (copied).
    ImageSeries(std::span<const float> values, size_t cols, size_t rows);

    /// @brief Replace the grid with scalar values (row-major, cols x rows).
    /// Throws std::invalid_argument when values.size() != cols * rows.
    ImageSeries& set_data(std::span<const float> values, size_t cols, size_t rows);
    /// @brief Replace the grid with colours in [0, 1]: @p channels (3 = RGB,
    /// 4 = RGBA) floats per cell, row-major.  No colormap is applied.
    ImageSeries& set_rgba(std::span<const float> pixels,
                          size_t                 cols,
                          size_t                 rows,
                          int                    channels = 4);

    /// @brief Data-space rectangle covered by the grid (outer cell edges).
    ImageSeries& extent(double x0, double x1, double y0, double y1);
    /// @brief Colormap for scalar grids (default Viridis).
    ImageSeries& colormap(ColormapType cm);
    /// @brief Value range mapped onto the colormap (default: finite data range).
    ImageSeries& colormap_range(float vmin, float vmax);
    /// @brief Drop a colormap_range() and use the finite data range again.
    ImageSeries& clear_colormap_range();
    /// @brief Nearest (flat cells) or bilinear interpolation.
    ImageSeries& interpolation(ImageInterpolation mode);
    /// @brief Whether row 0 is drawn at the top (Upper, default) or the bottom.
    ImageSeries& origin(ImageOrigin o);
    /// @brief Treat the grid values as samples at the grid nodes rather than
    /// cell centres: the extent then spans from the first to the last sample
    /// (used by filled contours).  Off by default.
    ImageSeries& node_aligned(bool on);
    /// @brief Discrete colour bands between consecutive @p levels (sorted
    /// ascending, at least 2) instead of a continuous colormap: filled
    /// contours.  Values outside [levels.front(), levels.back()] are not drawn.
    /// An empty vector restores the continuous colormap.
    ImageSeries& levels(std::vector<float> levels);

    /// @brief Draw each cell's value as text in the cell centre (heatmap).
    /// Labels are skipped when cells are too small to hold them.
    ImageSeries& show_values(bool on, int decimals = 2);
    /// @brief Name each column; the x axis then shows one tick per column.
    ImageSeries& x_categories(std::vector<std::string> names);
    /// @brief Name each row; the y axis then shows one tick per row.
    ImageSeries& y_categories(std::vector<std::string> names);

    using Series::label;
    using Series::opacity;
    ImageSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ImageSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }

    // ── Queries ──
    size_t cols() const { return cols_; }
    size_t rows() const { return rows_; }
    /// @brief 1 for scalar grids, 4 for RGBA grids.
    int  channels() const { return channels_; }
    bool is_rgba() const { return channels_ == 4; }
    bool empty() const { return cols_ == 0 || rows_ == 0; }
    /// @brief Grid data: cols * rows scalars, or cols * rows * 4 RGBA floats.
    std::span<const float> data() const { return values_; }

    double x0() const { return x0_; }
    double x1() const { return x1_; }
    double y0() const { return y0_; }
    double y1() const { return y1_; }

    ColormapType                    colormap() const { return colormap_; }
    float                           vmin() const;
    float                           vmax() const;
    bool                            colormap_range_set() const { return range_set_; }
    ImageInterpolation              interpolation() const { return interpolation_; }
    ImageOrigin                     origin() const { return origin_; }
    bool                            node_aligned() const { return node_aligned_; }
    const std::vector<float>&       levels() const { return levels_; }
    bool                            show_values() const { return show_values_; }
    int                             value_decimals() const { return value_decimals_; }
    const std::vector<std::string>& x_categories() const { return x_categories_; }
    const std::vector<std::string>& y_categories() const { return y_categories_; }

    /// @brief True for a scalar grid drawn through a colormap (or levels).
    bool has_colormap() const { return !empty() && !is_rgba() && colormap_ != ColormapType::None; }

    /// @brief Data-space centre of cell (col, row).
    void cell_center(size_t col, size_t row, double& x, double& y) const;
    /// @brief Cell under data point (x, y); false when outside the extent.
    bool cell_at(double x, double y, size_t& col, size_t& row) const;
    /// @brief Scalar value under data point (x, y) (nearest cell), or nullopt
    /// outside the extent or for an RGBA grid.
    std::optional<float> value_at(double x, double y) const;

    /// @brief Colour a scalar @p v is drawn with (the same colormap lookup
    /// table entry the GPU uses).  Alpha 0 for NaN or values outside levels().
    Color color_for_value(float v) const;

    /// Bumped when the grid values change (GPU re-upload of the value texture).
    uint64_t data_generation() const { return data_generation_; }
    /// Bumped when the colormap or levels change (GPU re-upload of the lookup table only).
    uint64_t lut_generation() const { return lut_generation_; }

    size_t memory_bytes() const override { return values_.size() * sizeof(float); }

   private:
    void recompute_data_range();

    std::vector<float>       values_;
    size_t                   cols_          = 0;
    size_t                   rows_          = 0;
    int                      channels_      = 1;
    double                   x0_            = -0.5;
    double                   x1_            = 0.5;
    double                   y0_            = -0.5;
    double                   y1_            = 0.5;
    bool                     extent_set_    = false;
    ColormapType             colormap_      = ColormapType::Viridis;
    float                    range_min_     = 0.0f;
    float                    range_max_     = 1.0f;
    float                    data_min_      = 0.0f;
    float                    data_max_      = 1.0f;
    bool                     range_set_     = false;
    ImageInterpolation       interpolation_ = ImageInterpolation::Nearest;
    ImageOrigin              origin_        = ImageOrigin::Upper;
    bool                     node_aligned_  = false;
    std::vector<float>       levels_;
    bool                     show_values_    = false;
    int                      value_decimals_ = 2;
    std::vector<std::string> x_categories_;
    std::vector<std::string> y_categories_;
    uint64_t                 data_generation_ = 1;
    uint64_t                 lut_generation_  = 1;
};

/// @brief Iso-lines of a scalar grid z(x, y), computed with marching squares.
///
/// z is row-major with row 0 at the bottom: z(col, row) = z[row * cols + col]
/// is the value at node (x[col], y[row]).  Node coordinates come from extent()
/// (uniform, default 0..cols-1 x 0..rows-1) or from set_coordinates().
///
/// Lines are coloured per level through colormap() while the series has an
/// automatic colour; an explicit color() draws every level in that colour.
class ContourSeries : public Series
{
   public:
    /// One level's polylines inside x_data()/y_data().
    struct LevelRange
    {
        float  level       = 0.0f;
        size_t first_point = 0;   // index into x_data()/y_data()
        size_t point_count = 0;   // polylines separated by NaN points
    };

    ContourSeries();
    ContourSeries(std::span<const float> z, size_t cols, size_t rows);

    /// @brief Replace the grid.  Throws std::invalid_argument when
    /// z.size() != cols * rows.
    ContourSeries& set_data(std::span<const float> z, size_t cols, size_t rows);
    /// @brief Uniform node coordinates: x[0] = x0 ... x[cols-1] = x1.
    ContourSeries& extent(double x0, double x1, double y0, double y1);
    /// @brief Explicit (rectilinear) node coordinates; sizes must match cols / rows.
    ContourSeries& set_coordinates(std::span<const float> x, std::span<const float> y);
    /// @brief About @p n "nice" levels inside the data range (default 8).
    ContourSeries& levels(int n);
    /// @brief Explicit levels (any order; drawn sorted ascending).
    ContourSeries& levels(std::vector<float> values);
    /// @brief Colormap used to colour the levels (default Viridis).
    ContourSeries& colormap(ColormapType cm);
    /// @brief Value range mapped onto the colormap (default: level range).
    ContourSeries& colormap_range(float vmin, float vmax);
    /// @brief Line width in pixels (default 1.5).
    ContourSeries& width(float w)
    {
        line_width_ = w;
        dirty_      = true;
        return *this;
    }

    using Series::color;
    using Series::label;
    ContourSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ContourSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }

    float                  width() const { return line_width_; }
    size_t                 cols() const { return cols_; }
    size_t                 rows() const { return rows_; }
    std::span<const float> z_data() const { return z_; }
    /// @brief Node x coordinates (cols values).
    std::span<const float> x_coords() const { return xs_; }
    /// @brief Node y coordinates (rows values).
    std::span<const float> y_coords() const { return ys_; }

    ColormapType colormap() const { return colormap_; }
    /// @brief Levels actually contoured (ascending).
    const std::vector<float>& level_values() const { return level_values_; }
    float                     vmin() const;
    float                     vmax() const;
    /// @brief True when lines are coloured per level (colormap set, automatic colour).
    bool uses_colormap() const
    {
        return colormap_ != ColormapType::None && has_auto_color() && !level_values_.empty();
    }
    /// @brief Colour of the lines at @p level when uses_colormap().
    Color level_color(float level) const;

    /// Generated geometry: polylines with NaN breaks, grouped by level.
    std::span<const float>         x_data() const { return line_x_; }
    std::span<const float>         y_data() const { return line_y_; }
    size_t                         point_count() const { return line_x_.size(); }
    const std::vector<LevelRange>& level_ranges() const { return level_ranges_; }

    /// Recompute levels and iso-lines (called by every setter).
    void rebuild_geometry();

    size_t memory_bytes() const override
    {
        return (z_.size() + line_x_.size() + line_y_.size()) * sizeof(float);
    }

   private:
    void reset_coordinates();

    std::vector<float>      z_;
    size_t                  cols_ = 0;
    size_t                  rows_ = 0;
    std::vector<float>      xs_;
    std::vector<float>      ys_;
    bool                    explicit_coords_ = false;
    double                  ex0_ = 0.0, ex1_ = 0.0, ey0_ = 0.0, ey1_ = 0.0;
    bool                    extent_set_  = false;
    int                     level_count_ = 8;
    std::vector<float>      explicit_levels_;
    std::vector<float>      level_values_;
    ColormapType            colormap_   = ColormapType::Viridis;
    float                   range_min_  = 0.0f;
    float                   range_max_  = 1.0f;
    bool                    range_set_  = false;
    float                   line_width_ = 1.5f;
    std::vector<float>      line_x_;
    std::vector<float>      line_y_;
    std::vector<LevelRange> level_ranges_;
};

}   // namespace spectra

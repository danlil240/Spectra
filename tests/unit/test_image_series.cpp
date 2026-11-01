// ImageSeries / ContourSeries: marching squares, contour levels, colormap
// lookup tables, grid geometry, autoscale, colorbar source and SVG export.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series_image.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/colorbar_layout.hpp"
#include "core/contour.hpp"
#include "core/image_colormap.hpp"
#include "stb_image.h"

namespace spectra
{
namespace
{

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

void expect_color_near(const Color& a, const Color& b, float tol = 1.5f / 255.0f)
{
    EXPECT_NEAR(a.r, b.r, tol);
    EXPECT_NEAR(a.g, b.g, tol);
    EXPECT_NEAR(a.b, b.b, tol);
    EXPECT_NEAR(a.a, b.a, tol);
}

// Radial grid: z = distance from the centre node, in grid units.
std::vector<float> radial_grid(size_t n)
{
    std::vector<float> z(n * n);
    const float        c = static_cast<float>(n - 1) * 0.5f;
    for (size_t r = 0; r < n; ++r)
        for (size_t col = 0; col < n; ++col)
            z[r * n + col] = std::hypot(static_cast<float>(col) - c, static_cast<float>(r) - c);
    return z;
}

// ─── Marching squares ───────────────────────────────────────────────────────

TEST(MarchingSquares, FlatGridHasNoLines)
{
    std::vector<float> z(16, 1.0f);
    EXPECT_TRUE(contour::trace(z, 4, 4, 0.5f).empty());
    EXPECT_TRUE(contour::trace(z, 4, 4, 1.5f).empty());
}

TEST(MarchingSquares, TooSmallOrBadInputHasNoLines)
{
    std::vector<float> z = {0.0f, 1.0f, 2.0f};
    EXPECT_TRUE(contour::trace(z, 3, 1, 0.5f).empty());
    EXPECT_TRUE(contour::trace(z, 2, 2, 0.5f).empty());   // fewer values than cols * rows
    std::vector<float> ok = {0.0f, 1.0f, 0.0f, 1.0f};
    EXPECT_TRUE(contour::trace(ok, 2, 2, NaN).empty());
}

TEST(MarchingSquares, PeakGivesClosedDiamond)
{
    // 3 x 3 grid, 1 in the centre: the 0.5 iso-line passes the edge midpoints.
    std::vector<float> z     = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    const auto         lines = contour::trace(z, 3, 3, 0.5f);
    ASSERT_EQ(lines.size(), 1u);
    const auto& l = lines[0];
    EXPECT_TRUE(l.closed);
    ASSERT_EQ(l.x.size(), 5u);   // 4 corners + repeated start
    EXPECT_FLOAT_EQ(l.x.front(), l.x.back());
    EXPECT_FLOAT_EQ(l.y.front(), l.y.back());
    for (size_t i = 0; i + 1 < l.x.size(); ++i)
    {
        // |dx| + |dy| = 0.5 from the centre (a diamond).
        EXPECT_FLOAT_EQ(std::abs(l.x[i] - 1.0f) + std::abs(l.y[i] - 1.0f), 0.5f) << i;
    }
}

TEST(MarchingSquares, RampGivesOpenStraightLine)
{
    // z = column index: the 2.5 iso-line is the vertical line x = 2.5.
    const size_t       cols = 5, rows = 3;
    std::vector<float> z(cols * rows);
    for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < cols; ++c)
            z[r * cols + c] = static_cast<float>(c);
    const auto lines = contour::trace(z, cols, rows, 2.5f);
    ASSERT_EQ(lines.size(), 1u);
    const auto& l = lines[0];
    EXPECT_FALSE(l.closed);
    ASSERT_EQ(l.x.size(), rows);
    for (float x : l.x)
        EXPECT_FLOAT_EQ(x, 2.5f);
    EXPECT_FLOAT_EQ(std::min(l.y.front(), l.y.back()), 0.0f);
    EXPECT_FLOAT_EQ(std::max(l.y.front(), l.y.back()), 2.0f);
}

TEST(MarchingSquares, InterpolatesAlongEdges)
{
    // Row 0: 0 -> 4; the 1.0 crossing sits a quarter of the way along.
    std::vector<float> z     = {0.0f, 4.0f, 0.0f, 4.0f};
    const auto         lines = contour::trace(z, 2, 2, 1.0f);
    ASSERT_EQ(lines.size(), 1u);
    for (float x : lines[0].x)
        EXPECT_FLOAT_EQ(x, 0.25f);
}

TEST(MarchingSquares, SaddleResolvedByCentreValue)
{
    // v0 = (0,0) and v2 = (1,1) high, v1 and v3 low; centre average 0.5.
    std::vector<float> z = {1.0f, 0.0f, 0.0f, 1.0f};   // rows: [v0 v1], [v3 v2]
    // Level 0.4: centre above -> the low corners v1 and v3 are cut off.
    {
        const auto lines = contour::trace(z, 2, 2, 0.4f);
        ASSERT_EQ(lines.size(), 2u);
        for (const auto& l : lines)
        {
            ASSERT_EQ(l.x.size(), 2u);
            // Each segment stays in the corner of v1 (1, 0) or v3 (0, 1).
            const float mx = 0.5f * (l.x[0] + l.x[1]);
            const float my = 0.5f * (l.y[0] + l.y[1]);
            EXPECT_TRUE((mx > 0.5f && my < 0.5f) || (mx < 0.5f && my > 0.5f)) << mx << "," << my;
        }
    }
    // Level 0.6: centre below -> the high corners v0 and v2 are cut off.
    {
        const auto lines = contour::trace(z, 2, 2, 0.6f);
        ASSERT_EQ(lines.size(), 2u);
        for (const auto& l : lines)
        {
            const float mx = 0.5f * (l.x[0] + l.x[1]);
            const float my = 0.5f * (l.y[0] + l.y[1]);
            EXPECT_TRUE((mx < 0.5f && my < 0.5f) || (mx > 0.5f && my > 0.5f)) << mx << "," << my;
        }
    }
}

TEST(MarchingSquares, NanCellsBreakTheLoop)
{
    std::vector<float> z     = {0, 0, 0, 0, 1, 0, 0, 0, NaN};
    const auto         lines = contour::trace(z, 3, 3, 0.5f);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FALSE(lines[0].closed);   // the cell touching the NaN corner is skipped
    EXPECT_EQ(lines[0].x.size(), 4u);
}

TEST(MarchingSquares, CircleIsOneClosedLoopOnTheRadius)
{
    const size_t n     = 41;
    const auto   z     = radial_grid(n);
    const auto   lines = contour::trace(z, n, n, 10.0f);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(lines[0].closed);
    EXPECT_GT(lines[0].x.size(), 40u);
    for (size_t i = 0; i < lines[0].x.size(); ++i)
    {
        const float r = std::hypot(lines[0].x[i] - 20.0f, lines[0].y[i] - 20.0f);
        EXPECT_NEAR(r, 10.0f, 0.1f);
    }
}

// ─── Contour levels ─────────────────────────────────────────────────────────

TEST(ContourLevels, LinesStrictlyInsideRange)
{
    const auto lv = contour::auto_levels(-1.0f, 1.0f, 8, false);
    ASSERT_EQ(lv.size(), 7u);
    EXPECT_FLOAT_EQ(lv.front(), -0.75f);
    EXPECT_FLOAT_EQ(lv.back(), 0.75f);
    EXPECT_FLOAT_EQ(lv[3], 0.0f);
}

TEST(ContourLevels, FilledCoverTheRange)
{
    const auto lv = contour::auto_levels(-0.9f, 0.95f, 8, true);
    ASSERT_GE(lv.size(), 3u);
    EXPECT_LE(lv.front(), -0.9f);
    EXPECT_GE(lv.back(), 0.95f);
    EXPECT_TRUE(std::is_sorted(lv.begin(), lv.end()));
    EXPECT_LE(lv.size(), 10u);
}

TEST(ContourLevels, DegenerateRanges)
{
    EXPECT_TRUE(contour::auto_levels(2.0f, 2.0f, 8, false).empty());
    const auto flat = contour::auto_levels(2.0f, 2.0f, 8, true);
    ASSERT_EQ(flat.size(), 2u);
    EXPECT_LT(flat[0], 2.0f);
    EXPECT_GT(flat[1], 2.0f);
    EXPECT_TRUE(contour::auto_levels(NaN, 1.0f, 8, false).empty());
    EXPECT_TRUE(contour::auto_levels(0.0f, 1.0f, 0, false).empty());
    // Reversed bounds are accepted.
    EXPECT_EQ(contour::auto_levels(1.0f, -1.0f, 8, false).size(), 7u);
}

// ─── Colormap lookup tables ─────────────────────────────────────────────────

TEST(ImageLut, IndexMatchesShaderRule)
{
    EXPECT_EQ(image_colormap::lut_index(0.0f, 256), 0u);
    EXPECT_EQ(image_colormap::lut_index(-3.0f, 256), 0u);
    EXPECT_EQ(image_colormap::lut_index(NaN, 256), 0u);
    EXPECT_EQ(image_colormap::lut_index(0.5f, 256), 128u);
    EXPECT_EQ(image_colormap::lut_index(1.0f, 256), 255u);
    EXPECT_EQ(image_colormap::lut_index(7.0f, 256), 255u);
    EXPECT_EQ(image_colormap::lut_index(0.999f, 256), 255u);
    EXPECT_EQ(image_colormap::lut_index(0.25f, 4), 1u);
}

TEST(ImageLut, ContinuousSamplesEntryCentres)
{
    const auto lut = image_colormap::build_lut(ColormapType::Viridis, {});
    ASSERT_EQ(lut.size(), image_colormap::CONTINUOUS_LUT_SIZE * 4);
    const size_t n = image_colormap::CONTINUOUS_LUT_SIZE;
    for (size_t i : {size_t{0}, size_t{77}, n / 2, n - 1})
    {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        expect_color_near(image_colormap::lut_color(lut, i),
                          image_colormap::sample(ColormapType::Viridis, t));
    }
    // Viridis runs from dark purple to yellow.
    const Color lo = image_colormap::lut_color(lut, 0);
    const Color hi = image_colormap::lut_color(lut, n - 1);
    EXPECT_GT(hi.r + hi.g, lo.r + lo.g + 1.0f);
}

TEST(ImageLut, BandedLevels)
{
    const std::vector<float> levels = {0.0f, 1.0f, 2.0f, 4.0f};
    EXPECT_EQ(image_colormap::band_of(levels, 0.0f), 0);
    EXPECT_EQ(image_colormap::band_of(levels, 0.5f), 0);
    EXPECT_EQ(image_colormap::band_of(levels, 1.0f), 1);
    EXPECT_EQ(image_colormap::band_of(levels, 3.9f), 2);
    EXPECT_EQ(image_colormap::band_of(levels, 4.0f), 2);
    EXPECT_EQ(image_colormap::band_of(levels, 4.1f), -1);
    EXPECT_EQ(image_colormap::band_of(levels, -0.1f), -1);
    EXPECT_EQ(image_colormap::band_of(levels, NaN), -1);

    const auto lut = image_colormap::build_lut(ColormapType::Plasma, levels);
    ASSERT_EQ(lut.size(), image_colormap::BANDED_LUT_SIZE * 4);
    // Every entry is the flat colour of the band its centre falls in.
    const size_t n = image_colormap::BANDED_LUT_SIZE;
    for (size_t i = 0; i < n; i += 37)
    {
        const float v = 4.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const int   b = image_colormap::band_of(levels, v);
        ASSERT_GE(b, 0);
        expect_color_near(
            image_colormap::lut_color(lut, i),
            image_colormap::band_color(ColormapType::Plasma, levels, static_cast<size_t>(b)));
    }
    // Band colours sample the colormap at the band midpoints.
    expect_color_near(image_colormap::band_color(ColormapType::Plasma, levels, 2),
                      image_colormap::sample(ColormapType::Plasma, 3.0f / 4.0f),
                      1e-6f);
}

// ─── ImageSeries ────────────────────────────────────────────────────────────

TEST(ImageSeries, DefaultExtentCentresCellsOnIntegers)
{
    std::vector<float> v(6, 0.0f);
    ImageSeries        img(v, 3, 2);
    EXPECT_DOUBLE_EQ(img.x0(), -0.5);
    EXPECT_DOUBLE_EQ(img.x1(), 2.5);
    EXPECT_DOUBLE_EQ(img.y0(), -0.5);
    EXPECT_DOUBLE_EQ(img.y1(), 1.5);
    EXPECT_FALSE(img.show_in_legend());

    double x = 0, y = 0;
    img.cell_center(2, 0, x, y);   // origin Upper: row 0 at the top
    EXPECT_DOUBLE_EQ(x, 2.0);
    EXPECT_DOUBLE_EQ(y, 1.0);
    img.origin(ImageOrigin::Lower);
    img.cell_center(2, 0, x, y);
    EXPECT_DOUBLE_EQ(y, 0.0);
}

TEST(ImageSeries, CellLookupRoundTrips)
{
    std::vector<float> v(12);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<float>(i);
    ImageSeries img(v, 4, 3);
    img.extent(10.0, 18.0, -3.0, 3.0);   // 2 x 2 cells
    for (auto o : {ImageOrigin::Upper, ImageOrigin::Lower})
    {
        img.origin(o);
        for (size_t r = 0; r < 3; ++r)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                double x = 0, y = 0;
                img.cell_center(c, r, x, y);
                size_t cc = 99, rr = 99;
                ASSERT_TRUE(img.cell_at(x, y, cc, rr));
                EXPECT_EQ(cc, c);
                EXPECT_EQ(rr, r);
                EXPECT_EQ(img.value_at(x + 0.9, y - 0.9), v[r * 4 + c]);
            }
        }
    }
    size_t c = 0, r = 0;
    EXPECT_FALSE(img.cell_at(9.9, 0.0, c, r));
    EXPECT_FALSE(img.cell_at(12.0, 3.1, c, r));
    EXPECT_FALSE(img.value_at(100.0, 0.0).has_value());
    // The outer edges belong to the edge cells.
    ASSERT_TRUE(img.cell_at(18.0, 3.0, c, r));
    EXPECT_EQ(c, 3u);
}

TEST(ImageSeries, NodeAlignedSamplesSitOnTheExtentEdges)
{
    std::vector<float> v(6, 0.0f);
    ImageSeries        img;
    img.node_aligned(true).origin(ImageOrigin::Lower).set_data(v, 3, 2);
    EXPECT_DOUBLE_EQ(img.x0(), 0.0);
    EXPECT_DOUBLE_EQ(img.x1(), 2.0);
    EXPECT_DOUBLE_EQ(img.y1(), 1.0);
    double x = 0, y = 0;
    img.cell_center(2, 1, x, y);
    EXPECT_DOUBLE_EQ(x, 2.0);
    EXPECT_DOUBLE_EQ(y, 1.0);
}

TEST(ImageSeries, RejectsMismatchedSizes)
{
    std::vector<float> v(5, 0.0f);
    EXPECT_THROW(ImageSeries(v, 2, 3), std::invalid_argument);
    ImageSeries img;
    EXPECT_THROW(img.set_rgba(v, 2, 1, 3), std::invalid_argument);
    EXPECT_THROW(img.set_rgba(v, 5, 1, 1), std::invalid_argument);
}

TEST(ImageSeries, ValueRangeAndLevels)
{
    std::vector<float> v = {-2.0f, NaN, 3.0f, 1.0f};
    ImageSeries        img(v, 2, 2);
    EXPECT_FLOAT_EQ(img.vmin(), -2.0f);
    EXPECT_FLOAT_EQ(img.vmax(), 3.0f);
    img.colormap_range(0.0f, 10.0f);
    EXPECT_FLOAT_EQ(img.vmin(), 0.0f);
    EXPECT_FLOAT_EQ(img.vmax(), 10.0f);
    img.clear_colormap_range();
    EXPECT_FLOAT_EQ(img.vmax(), 3.0f);
    img.levels({5.0f, -5.0f, NaN, 0.0f, 0.0f});
    ASSERT_EQ(img.levels().size(), 3u);   // sorted, finite, unique
    EXPECT_FLOAT_EQ(img.vmin(), -5.0f);
    EXPECT_FLOAT_EQ(img.vmax(), 5.0f);
    img.levels({1.0f});   // fewer than 2 levels: continuous again
    EXPECT_TRUE(img.levels().empty());
}

TEST(ImageSeries, GenerationsSeparateDataFromColormap)
{
    std::vector<float> v = {0.0f, 1.0f};
    ImageSeries        img(v, 2, 1);
    const auto         d0 = img.data_generation();
    const auto         l0 = img.lut_generation();
    img.colormap(ColormapType::Magma);
    EXPECT_EQ(img.data_generation(), d0);
    EXPECT_NE(img.lut_generation(), l0);
    const auto l1 = img.lut_generation();
    img.colormap_range(0.0f, 2.0f);   // push constants only
    img.interpolation(ImageInterpolation::Bilinear);
    EXPECT_EQ(img.data_generation(), d0);
    EXPECT_EQ(img.lut_generation(), l1);
    img.set_data(v, 1, 2);
    EXPECT_NE(img.data_generation(), d0);
    // Generations are unique across series.
    ImageSeries other(v, 2, 1);
    EXPECT_NE(other.data_generation(), img.data_generation());
}

TEST(ImageSeries, ColorForValueMatchesLut)
{
    std::vector<float> v = {0.0f, 10.0f};
    ImageSeries        img(v, 2, 1);
    img.colormap(ColormapType::Inferno);
    const auto lut = image_colormap::build_lut(ColormapType::Inferno, {});
    expect_color_near(img.color_for_value(0.0f), image_colormap::lut_color(lut, 0), 1e-6f);
    expect_color_near(img.color_for_value(10.0f), image_colormap::lut_color(lut, 255), 1e-6f);
    expect_color_near(img.color_for_value(-5.0f), image_colormap::lut_color(lut, 0), 1e-6f);
    EXPECT_EQ(img.color_for_value(NaN).a, 0.0f);

    img.levels({0.0f, 5.0f, 10.0f});
    EXPECT_EQ(img.color_for_value(-0.5f).a, 0.0f);   // outside the bands
    EXPECT_EQ(img.color_for_value(10.5f).a, 0.0f);
    expect_color_near(img.color_for_value(2.0f),
                      image_colormap::band_color(ColormapType::Inferno, img.levels(), 0));
    expect_color_near(img.color_for_value(10.0f),
                      image_colormap::band_color(ColormapType::Inferno, img.levels(), 1));
}

TEST(ImageSeries, RgbaGrid)
{
    std::vector<float> rgb = {1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f};
    ImageSeries        img;
    img.set_rgba(rgb, 2, 1, 3);
    EXPECT_TRUE(img.is_rgba());
    EXPECT_FALSE(img.has_colormap());
    ASSERT_EQ(img.data().size(), 8u);
    EXPECT_FLOAT_EQ(img.data()[3], 1.0f);   // alpha added
    EXPECT_FLOAT_EQ(img.data()[5], 1.0f);   // clamped
    EXPECT_FALSE(img.value_at(0.0, 0.0).has_value());
}

// ─── ContourSeries ──────────────────────────────────────────────────────────

TEST(ContourSeries, LevelsAndRanges)
{
    const size_t  n = 21;
    const auto    z = radial_grid(n);   // 0 .. ~14.1
    ContourSeries ct(z, n, n);
    ct.levels(std::vector<float>{2.0f, 5.0f, 8.0f});
    ASSERT_EQ(ct.level_values().size(), 3u);
    ASSERT_EQ(ct.level_ranges().size(), 3u);
    size_t expected_first = 0;
    for (const auto& r : ct.level_ranges())
    {
        EXPECT_EQ(r.first_point, expected_first);
        expected_first += r.point_count;
        // Each level is one closed circle ending in a NaN break.
        const float last_x = ct.x_data()[r.first_point + r.point_count - 1];
        EXPECT_TRUE(std::isnan(last_x));
        for (size_t i = r.first_point; i + 1 < r.first_point + r.point_count; ++i)
        {
            const float rad = std::hypot(ct.x_data()[i] - 10.0f, ct.y_data()[i] - 10.0f);
            EXPECT_NEAR(rad, r.level, 0.15f);
        }
    }
    EXPECT_EQ(expected_first, ct.point_count());
    EXPECT_FLOAT_EQ(ct.vmin(), 2.0f);
    EXPECT_FLOAT_EQ(ct.vmax(), 8.0f);
    EXPECT_TRUE(ct.uses_colormap() == false);   // no auto colour outside an Axes
}

TEST(ContourSeries, AutoLevelsExtentAndCoordinates)
{
    const size_t  n = 11;
    const auto    z = radial_grid(n);   // 0 .. ~7.07
    ContourSeries ct(z, n, n);
    ct.levels(4);
    EXPECT_FALSE(ct.level_values().empty());
    for (float l : ct.level_values())
    {
        EXPECT_GT(l, 0.0f);
        EXPECT_LT(l, 7.08f);
    }

    ct.extent(-1.0, 1.0, 0.0, 10.0);
    for (size_t i = 0; i < ct.point_count(); ++i)
    {
        if (std::isnan(ct.x_data()[i]))
            continue;
        EXPECT_GE(ct.x_data()[i], -1.0f);
        EXPECT_LE(ct.x_data()[i], 1.0f);
        EXPECT_GE(ct.y_data()[i], 0.0f);
        EXPECT_LE(ct.y_data()[i], 10.0f);
    }

    // Rectilinear coordinates: x squashed to [0, 1] on the left half.
    std::vector<float> xs(n), ys(n);
    for (size_t i = 0; i < n; ++i)
    {
        xs[i] = static_cast<float>(i) * 0.1f;
        ys[i] = static_cast<float>(i);
    }
    ct.set_coordinates(xs, ys);
    for (size_t i = 0; i < ct.point_count(); ++i)
    {
        if (!std::isnan(ct.x_data()[i]))
        {
            EXPECT_LE(ct.x_data()[i], 1.0f + 1e-5f);
        }
    }
    std::vector<float> bad(3);
    EXPECT_THROW(ct.set_coordinates(bad, ys), std::invalid_argument);
}

// ─── Axes integration ───────────────────────────────────────────────────────

TEST(ImageAxes, ImshowFitsTightlyToTheExtent)
{
    Axes               ax;
    std::vector<float> v(4 * 3, 1.0f);
    ax.imshow(v, 4, 3);
    const auto xl = ax.x_limits();
    const auto yl = ax.y_limits();
    EXPECT_DOUBLE_EQ(xl.min, -0.5);
    EXPECT_DOUBLE_EQ(xl.max, 3.5);
    EXPECT_DOUBLE_EQ(yl.min, -0.5);
    EXPECT_DOUBLE_EQ(yl.max, 2.5);

    // Data reaching past the image keeps the usual padding on that side.
    std::vector<float> lx = {0.0f, 10.0f};
    std::vector<float> ly = {0.0f, 1.0f};
    ax.line(lx, ly);
    const auto xl2 = ax.x_limits();
    EXPECT_DOUBLE_EQ(xl2.min, -0.5);   // still sticky on the left
    EXPECT_GT(xl2.max, 10.0);          // padded beyond the line
}

TEST(ImageAxes, HeatmapCategoriesBecomeTicks)
{
    Axes                     ax;
    std::vector<float>       v = {1, 2, 3, 4, 5, 6};
    std::vector<std::string> cols{"a", "b", "c"};
    std::vector<std::string> rows{"top", "bottom"};
    auto&                    hm = ax.heatmap(v, 3, 2, cols, rows);
    EXPECT_TRUE(hm.show_values());
    const auto xt = ax.compute_x_ticks();
    ASSERT_EQ(xt.labels.size(), 3u);
    EXPECT_EQ(xt.labels[0], "a");
    EXPECT_DOUBLE_EQ(xt.positions[2], 2.0);
    const auto yt = ax.compute_y_ticks();
    ASSERT_EQ(yt.labels.size(), 2u);
    // Row 0 is drawn at the top (y = 1).
    EXPECT_EQ(yt.labels[0], "bottom");
    EXPECT_DOUBLE_EQ(yt.positions[0], 0.0);
    EXPECT_EQ(yt.labels[1], "top");
}

TEST(ImageAxes, ContourfBandsCoverTheData)
{
    Axes         ax;
    const size_t n  = 9;
    const auto   z  = radial_grid(n);
    auto&        cf = ax.contourf(z, n, n, 6);
    EXPECT_TRUE(cf.node_aligned());
    EXPECT_EQ(cf.origin(), ImageOrigin::Lower);
    EXPECT_EQ(cf.interpolation(), ImageInterpolation::Bilinear);
    ASSERT_GE(cf.levels().size(), 2u);
    EXPECT_LE(cf.levels().front(), 0.0f);
    EXPECT_GE(cf.levels().back(), *std::max_element(z.begin(), z.end()));
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 8.0);
}

TEST(ImageAxes, ContourLinesUseTheColormapByDefault)
{
    Axes         ax;
    const size_t n  = 9;
    const auto   z  = radial_grid(n);
    auto&        ct = ax.contour(z, n, n, 5);
    EXPECT_TRUE(ct.uses_colormap());
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 8.0);
    ct.color(colors::red);
    EXPECT_FALSE(ct.uses_colormap());
}

// ─── Colorbar ───────────────────────────────────────────────────────────────

TEST(ImageColorbar, FindsImageAndContourSources)
{
    {
        Axes               ax;
        std::vector<float> v = {0.0f, 2.0f, 4.0f, 8.0f};
        ax.imshow(v, 2, 2).colormap(ColormapType::Magma);
        ax.show_colorbar(true);
        EXPECT_TRUE(colorbar_layout::wants_colorbar(ax));
        const auto src = colorbar_layout::find_source(ax);
        ASSERT_TRUE(src.has_value());
        EXPECT_EQ(src->mapping, colorbar_layout::Mapping::Image);
        EXPECT_EQ(src->cmap, ColormapType::Magma);
        EXPECT_DOUBLE_EQ(src->vmin, 0.0);
        EXPECT_DOUBLE_EQ(src->vmax, 8.0);
        EXPECT_TRUE(src->levels.empty());
        expect_color_near(colorbar_layout::sample_source(*src, 0.3f),
                          image_colormap::sample(ColormapType::Magma, 0.3f),
                          1e-6f);
    }
    {
        Axes         ax;
        const size_t n   = 9;
        const auto   z   = radial_grid(n);
        auto&        cf  = ax.contourf(z, n, n, 4);
        const auto   src = colorbar_layout::find_source(ax);
        ASSERT_TRUE(src.has_value());
        EXPECT_EQ(src->levels, cf.levels());
        // The bar shows the flat band colours.
        expect_color_near(colorbar_layout::sample_source(*src, 0.01f),
                          image_colormap::band_color(cf.colormap(), cf.levels(), 0),
                          1e-6f);
    }
    {
        Axes         ax;
        const size_t n = 9;
        const auto   z = radial_grid(n);
        ax.contour(z, n, n, 4);
        const auto src = colorbar_layout::find_source(ax);
        ASSERT_TRUE(src.has_value());
        EXPECT_EQ(src->mapping, colorbar_layout::Mapping::Image);
    }
    {
        Axes               ax;
        std::vector<float> rgb(2 * 2 * 3, 0.5f);
        ax.imshow(std::vector<float>(4, 0.0f), 2, 2).set_rgba(rgb, 2, 2, 3);
        EXPECT_FALSE(colorbar_layout::find_source(ax).has_value());
    }
}

// ─── SVG export ─────────────────────────────────────────────────────────────

std::vector<uint8_t> base64_decode(std::string_view in)
{
    auto val = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    uint32_t             buf  = 0;
    int                  bits = 0;
    for (char c : in)
    {
        const int v = val(c);
        if (v < 0)
            break;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

TEST(ImageSvg, ImshowEmbedsOnePixelPerCell)
{
    Figure             fig({.width = 400, .height = 300});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> v  = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, NaN};   // 3 x 2
    auto&              im = ax.imshow(v, 3, 2);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);

    const std::string key = "xlink:href=\"data:image/png;base64,";
    const auto        at  = svg.find(key);
    ASSERT_NE(at, std::string::npos);
    EXPECT_NE(svg.find("<image class=\"image\""), std::string::npos);
    EXPECT_NE(svg.find("image-rendering:pixelated"), std::string::npos);
    const auto end = svg.find('"', at + key.size());
    const auto png =
        base64_decode(std::string_view(svg).substr(at + key.size(), end - at - key.size()));

    int      w = 0, h = 0, ch = 0;
    uint8_t* px = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &w, &h, &ch, 4);
    ASSERT_NE(px, nullptr);
    EXPECT_EQ(w, 3);
    EXPECT_EQ(h, 2);
    // Origin Upper: PNG row 0 is grid row 0.
    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            const uint8_t* p    = px + (r * 3 + c) * 4;
            const Color    want = im.color_for_value(v[static_cast<size_t>(r * 3 + c)]);
            const Color    got{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
            expect_color_near(got, want, 1.0f / 255.0f);
        }
    }
    stbi_image_free(px);
}

TEST(ImageSvg, HeatmapLabelsAndContourPaths)
{
    Figure             fig({.width = 500, .height = 400});
    auto&              ax = fig.subplot(1, 1, 1);
    std::vector<float> v  = {0.25f, 0.5f, 0.75f, 1.0f};
    ax.heatmap(v, 2, 2);
    fig.compute_layout();
    const std::string svg = SvgExporter::to_string(fig);
    EXPECT_NE(svg.find(">0.25</text>"), std::string::npos);
    EXPECT_NE(svg.find(">1.00</text>"), std::string::npos);

    Figure       fig2({.width = 500, .height = 400});
    auto&        ax2 = fig2.subplot(1, 1, 1);
    const size_t n   = 15;
    const auto   z   = radial_grid(n);
    auto&        ct  = ax2.contour(z, n, n, 4);
    fig2.compute_layout();
    const std::string svg2  = SvgExporter::to_string(fig2);
    int               paths = 0;
    for (size_t pos = svg2.find("<path class=\"contour\""); pos != std::string::npos;
         pos        = svg2.find("<path class=\"contour\"", pos + 1))
        ++paths;
    EXPECT_EQ(static_cast<size_t>(paths), ct.level_ranges().size());
}

}   // namespace
}   // namespace spectra

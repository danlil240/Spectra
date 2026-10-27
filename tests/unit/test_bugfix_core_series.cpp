// Regression tests for core-series bug fixes.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <spectra/axes.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes.hpp>
#include <spectra/series_shapes3d.hpp>
#include <vector>

#include "core/pending_series_data.hpp"

using namespace spectra;

namespace
{

constexpr float kPi = 3.14159265358979f;

// Sum of absolute triangle areas of interleaved {x, y, alpha} fill verts.
double fill_area(std::span<const float> v)
{
    double area = 0.0;
    for (size_t t = 0; t + 8 < v.size(); t += 9)
    {
        const double ax = v[t + 0], ay = v[t + 1];
        const double bx = v[t + 3], by = v[t + 4];
        const double cx = v[t + 6], cy = v[t + 7];
        area += std::abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) * 0.5;
    }
    return area;
}

}   // namespace

// ── Bug #6: pending commit with mismatched x/y lengths ─────────────────────

TEST(BugfixCoreSeries, PendingTrimWithMismatchedLengthsIsSafe)
{
    PendingSeriesData  p;
    std::vector<float> x(10), y(10);
    for (int i = 0; i < 10; ++i)
        x[i] = y[i] = static_cast<float>(i);

    std::vector<float> big(1000);
    for (int i = 0; i < 1000; ++i)
        big[i] = static_cast<float>(i);
    p.replace_x(big);
    p.trim_to_max_points(500);
    EXPECT_TRUE(p.commit(x, y));

    // Paired count was min(1000, 10) = 10 <= 500: nothing trimmed.
    EXPECT_EQ(x.size(), 1000u);
    EXPECT_EQ(y.size(), 10u);
}

TEST(BugfixCoreSeries, PendingEraseBeforeWithShortY)
{
    PendingSeriesData  p;
    std::vector<float> x(100), y(5);
    for (int i = 0; i < 100; ++i)
        x[i] = static_cast<float>(i);
    for (int i = 0; i < 5; ++i)
        y[i] = static_cast<float>(i) * 10.0f;

    p.erase_before(50.0f);
    p.commit(x, y);
    // Only the 5 paired points are considered: all are < 50 and are removed.
    EXPECT_EQ(y.size(), 0u);
    EXPECT_EQ(x.size(), 95u);
    EXPECT_FLOAT_EQ(x.front(), 5.0f);
}

TEST(BugfixCoreSeries, PendingEraseAfterKeepsPairsAligned)
{
    PendingSeriesData  p;
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {10, 11, 12, 13, 14, 15, 16};   // longer than x

    p.erase_after(1.5f);
    p.commit(x, y);
    ASSERT_EQ(x.size(), 2u);
    ASSERT_EQ(y.size(), 4u);
    EXPECT_FLOAT_EQ(y[0], 10.0f);
    EXPECT_FLOAT_EQ(y[1], 11.0f);
    // Unpaired y tail is preserved after the erased paired range.
    EXPECT_FLOAT_EQ(y[2], 15.0f);
    EXPECT_FLOAT_EQ(y[3], 16.0f);
}

TEST(BugfixCoreSeries, PendingTrimMatchedLengths)
{
    PendingSeriesData  p;
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {10, 11, 12, 13, 14};
    p.trim_to_max_points(2);
    p.commit(x, y);
    ASSERT_EQ(x.size(), 2u);
    ASSERT_EQ(y.size(), 2u);
    EXPECT_FLOAT_EQ(x[0], 3.0f);
    EXPECT_FLOAT_EQ(y[0], 13.0f);
}

// ── Bug #29: surface border normals ────────────────────────────────────────

TEST(BugfixCoreSeries, SurfaceBorderNormalsFollowSlope)
{
    // Tilted plane z = x on a 2x2 grid: every vertex is a border vertex.
    std::vector<float> xs = {0.0f, 1.0f};
    std::vector<float> ys = {0.0f, 1.0f};
    std::vector<float> zs = {0.0f, 1.0f, 0.0f, 1.0f};
    SurfaceSeries      s(xs, ys, zs);
    s.generate_mesh();
    ASSERT_TRUE(s.is_mesh_generated());
    const auto& v = s.mesh().vertices;
    ASSERT_EQ(v.size(), 4u * 6u);
    const float k = 1.0f / std::sqrt(2.0f);
    for (size_t i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(v[i * 6 + 3], -k, 1e-5f);
        EXPECT_NEAR(v[i * 6 + 4], 0.0f, 1e-5f);
        EXPECT_NEAR(v[i * 6 + 5], k, 1e-5f);
    }
}

TEST(BugfixCoreSeries, SurfaceNormalsUniformOnLargerPlane)
{
    std::vector<float> xs = {0, 1, 2, 3};
    std::vector<float> ys = {0, 1, 2, 3};
    std::vector<float> zs;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            zs.push_back(static_cast<float>(j));
    SurfaceSeries s(xs, ys, zs);
    s.generate_mesh();
    const auto& v = s.mesh().vertices;
    const float k = 1.0f / std::sqrt(2.0f);
    for (size_t i = 0; i < 16; ++i)
    {
        EXPECT_NEAR(v[i * 6 + 3], -k, 1e-5f);
        EXPECT_NEAR(v[i * 6 + 5], k, 1e-5f);
    }
}

// ── Bug #30: concave polygon fill ──────────────────────────────────────────

TEST(BugfixCoreSeries, ConcavePolygonFillCoversOnlyInterior)
{
    std::vector<float> px = {0, 3, 3, 2, 2, 1, 1, 0};
    std::vector<float> py = {0, 0, 3, 3, 1, 1, 3, 3};
    ShapeSeries        sh;
    sh.polygon(px, py);
    sh.rebuild_geometry();

    auto v = sh.fill_verts();
    ASSERT_EQ(sh.fill_vertex_count(), (px.size() - 2) * 3);
    EXPECT_NEAR(fill_area(v), 7.0, 1e-4);

    // No triangle centroid may lie in the notch (1 < x < 2, y > 1).
    for (size_t t = 0; t + 8 < v.size(); t += 9)
    {
        const float cx = (v[t] + v[t + 3] + v[t + 6]) / 3.0f;
        const float cy = (v[t + 1] + v[t + 4] + v[t + 7]) / 3.0f;
        EXPECT_FALSE(cx > 1.0f && cx < 2.0f && cy > 1.0f) << cx << "," << cy;
    }
}

TEST(BugfixCoreSeries, ClockwiseConvexPolygonFillArea)
{
    std::vector<float> px = {0, 0, 2, 2};
    std::vector<float> py = {0, 2, 2, 0};   // clockwise square
    ShapeSeries        sh;
    sh.polygon(px, py);
    sh.rebuild_geometry();
    EXPECT_NEAR(fill_area(sh.fill_verts()), 4.0, 1e-5);
}

// ── Bug #70: clear_colormap_range restores data range ──────────────────────

TEST(BugfixCoreSeries, ClearColormapRangeRestoresDataRange)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 2};
    ScatterSeries      sc(x, y);
    std::vector<float> c = {0.0f, 1.0f, 2.0f};
    sc.color_values(c);
    sc.colormap_range(0.0f, 100.0f);
    EXPECT_FLOAT_EQ(sc.colormap_max(), 100.0f);
    sc.clear_colormap_range();
    EXPECT_FALSE(sc.colormap_range_set());
    EXPECT_FLOAT_EQ(sc.colormap_min(), 0.0f);
    EXPECT_FLOAT_EQ(sc.colormap_max(), 2.0f);
}

// ── Bug #73: rounded rect with negative extents ────────────────────────────

TEST(BugfixCoreSeries, RoundedRectNegativeSizeStaysInBounds)
{
    ShapeSeries sh;
    sh.rect(0, 0, -2, 1).corner_radius(0.2f);
    sh.rebuild_geometry();
    auto xs = sh.x_data();
    auto ys = sh.y_data();
    ASSERT_FALSE(xs.empty());
    for (size_t i = 0; i < xs.size(); ++i)
    {
        if (std::isnan(xs[i]))
            continue;
        EXPECT_GE(xs[i], -2.0f - 1e-4f);
        EXPECT_LE(xs[i], 0.0f + 1e-4f);
        EXPECT_GE(ys[i], 0.0f - 1e-4f);
        EXPECT_LE(ys[i], 1.0f + 1e-4f);
    }
}

// ── Bug #74: rotation applies to line and arrow ────────────────────────────

TEST(BugfixCoreSeries, LineRotationApplied)
{
    ShapeSeries sh;
    sh.line(0, 0, 2, 0).rotation(kPi * 0.5f);
    sh.rebuild_geometry();
    auto xs = sh.x_data();
    auto ys = sh.y_data();
    ASSERT_GE(xs.size(), 2u);
    for (size_t i = 0; i < xs.size(); ++i)
    {
        if (std::isnan(xs[i]))
            continue;
        EXPECT_NEAR(xs[i], 1.0f, 1e-5f);
        EXPECT_NEAR(std::abs(ys[i]), 1.0f, 1e-5f);
    }
}

TEST(BugfixCoreSeries, ArrowRotationApplied)
{
    ShapeSeries sh;
    sh.arrow(0, 0, 2, 0).rotation(kPi * 0.5f);
    sh.rebuild_geometry();
    auto  xs   = sh.x_data();
    auto  ys   = sh.y_data();
    float maxy = -1e9f, miny = 1e9f;
    for (size_t i = 0; i < xs.size(); ++i)
    {
        if (std::isnan(xs[i]))
            continue;
        EXPECT_NEAR(xs[i], 1.0f, 0.1f);
        maxy = std::max(maxy, ys[i]);
        miny = std::min(miny, ys[i]);
    }
    // The tip is part of the solid arrowhead fill, not the outline.
    auto fv = sh.fill_verts();
    for (size_t v = 0; v < sh.fill_vertex_count(); ++v)
    {
        EXPECT_NEAR(fv[v * 3], 1.0f, 0.1f);
        maxy = std::max(maxy, fv[v * 3 + 1]);
        miny = std::min(miny, fv[v * 3 + 1]);
    }
    EXPECT_NEAR(maxy, 1.0f, 1e-4f);
    EXPECT_NEAR(miny, -1.0f, 1e-4f);
}

// ── P1-11: arrowheads are solid triangles aligned to the arrow on screen ──

TEST(BugfixCoreSeries, ArrowHeadIsSolidTriangle)
{
    ShapeSeries sh;
    sh.rect(0, 0, 1, 1).arrow(0, 0, 3, 0);
    sh.rebuild_geometry();

    // The rect's translucent fill comes first; the arrowhead is the opaque tail.
    ASSERT_EQ(sh.solid_fill_first_vertex(), 6u);
    ASSERT_EQ(sh.fill_vertex_count(), 9u);
    auto fv = sh.fill_verts();
    for (size_t v = 6; v < 9; ++v)
    {
        EXPECT_FLOAT_EQ(fv[v * 3 + 2], 1.0f);
    }
    EXPECT_FLOAT_EQ(fv[6 * 3], 3.0f);   // tip
    EXPECT_FLOAT_EQ(fv[6 * 3 + 1], 0.0f);
    // Data-unit fallback (no pixel scale): length = 0.15 * len, half-width 0.08.
    EXPECT_NEAR(fv[7 * 3], 3.0f - 0.45f, 1e-5f);
    EXPECT_NEAR(fv[8 * 3], 3.0f - 0.45f, 1e-5f);
    EXPECT_NEAR(std::abs(fv[7 * 3 + 1] - fv[8 * 3 + 1]), 0.16f, 1e-5f);
}

TEST(BugfixCoreSeries, ArrowHeadSizedInPixelsAndAlignedOnScreen)
{
    // Non-uniform data-per-pixel scale: a data-space perpendicular would look crooked.
    const float sx = 0.01f, sy = 0.02f;
    ShapeSeries sh;
    sh.arrow(0, 0, 4, 2);
    EXPECT_TRUE(sh.set_pixel_scale(sx, sy));
    EXPECT_FALSE(sh.set_pixel_scale(sx, sy));   // unchanged -> no rebuild needed
    sh.rebuild_geometry();

    ASSERT_EQ(sh.fill_vertex_count() - sh.solid_fill_first_vertex(), 3u);
    auto   fv = sh.fill_verts();
    auto   px = [&](size_t v) { return fv[v * 3] / sx; };
    auto   py = [&](size_t v) { return fv[v * 3 + 1] / sy; };
    size_t t  = sh.solid_fill_first_vertex();
    // Tip at the arrow end.
    EXPECT_NEAR(px(t), 400.0f, 1e-2f);
    EXPECT_NEAR(py(t), 100.0f, 1e-2f);

    // Screen direction of the arrow.
    float dl = std::hypot(400.0f, 100.0f);
    float nx = 400.0f / dl, ny = 100.0f / dl;

    float bx = 0.5f * (px(t + 1) + px(t + 2));
    float by = 0.5f * (py(t + 1) + py(t + 2));
    // Base midpoint sits on the shaft, 14 px (minimum head length) behind the tip.
    EXPECT_NEAR(px(t) - bx, 14.0f * nx, 1e-2f);
    EXPECT_NEAR(py(t) - by, 14.0f * ny, 1e-2f);
    // Base is perpendicular to the arrow on screen and symmetric (half-width 0.4 * 14).
    float ex = px(t + 1) - px(t + 2), ey = py(t + 1) - py(t + 2);
    EXPECT_NEAR(ex * nx + ey * ny, 0.0f, 1e-2f);
    EXPECT_NEAR(std::hypot(ex, ey), 2.0f * 5.6f, 1e-2f);

    // Thicker line -> bigger head (7 px per unit of line width).
    sh.line_width(3.0f);
    sh.rebuild_geometry();
    fv = sh.fill_verts();
    t  = sh.solid_fill_first_vertex();
    bx = 0.5f * (px(t + 1) + px(t + 2));
    by = 0.5f * (py(t + 1) + py(t + 2));
    ASSERT_EQ(sh.fill_vertex_count(), 3u);
    EXPECT_NEAR(std::hypot(px(t) - bx, py(t) - by), 21.0f, 1e-2f);

    // The shaft ends inside the head, never past its base toward the tip.
    auto xs = sh.x_data();
    auto ys = sh.y_data();
    ASSERT_EQ(xs.size(), 7u);   // shaft (2) + NaN break + closed head outline (4)
    EXPECT_TRUE(std::isnan(xs[2]));
    EXPECT_FLOAT_EQ(xs[3], fv[t * 3]);
    EXPECT_FLOAT_EQ(xs[6], fv[t * 3]);
    EXPECT_FLOAT_EQ(ys[4], fv[(t + 1) * 3 + 1]);
    EXPECT_FLOAT_EQ(ys[5], fv[(t + 2) * 3 + 1]);
    float shaft_end_along = (xs[1] / sx) * nx + (ys[1] / sy) * ny;
    float base_along      = bx * nx + by * ny;
    float tip_along       = px(t) * nx + py(t) * ny;
    EXPECT_GT(shaft_end_along, base_along);
    EXPECT_LT(shaft_end_along, tip_along);
}

TEST(BugfixCoreSeries, ArrowPixelScaleIgnoredForCustomHeadsAndOtherShapes)
{
    ShapeSeries plain;
    plain.rect(0, 0, 1, 1).line(0, 0, 1, 1);
    EXPECT_FALSE(plain.set_pixel_scale(0.01f, 0.01f));

    ShapeSeries custom;
    custom.arrow(0, 0, 2, 0).arrow_head(0.25f, 0.1f);
    EXPECT_FALSE(custom.set_pixel_scale(0.01f, 0.01f));
    custom.rebuild_geometry();
    auto   fv = custom.fill_verts();
    size_t t  = custom.solid_fill_first_vertex();
    EXPECT_NEAR(fv[(t + 1) * 3], 1.5f, 1e-5f);
    EXPECT_NEAR(std::abs(fv[(t + 1) * 3 + 1] - fv[(t + 2) * 3 + 1]), 0.2f, 1e-5f);

    ShapeSeries arrow;
    arrow.arrow(0, 0, 1, 1);
    EXPECT_FALSE(arrow.set_pixel_scale(0.0f, 1.0f));   // invalid scales rejected
    EXPECT_FALSE(arrow.set_pixel_scale(std::numeric_limits<float>::infinity(), 1.0f));
    EXPECT_TRUE(arrow.set_pixel_scale(0.5f, 1.0f));
}

// ── R00: 2D shape geometry is generated lazily (first-frame limits) ───────

TEST(BugfixCoreSeries, ShapeGeometryAvailableWithoutRebuild)
{
    ShapeSeries sh;
    sh.rect(2, 3, 4, 5).circle(10, 10, 1).text(1, 1, "label");
    // No rebuild_geometry() / render: queries must still see current geometry.
    EXPECT_GT(sh.point_count(), 0u);
    EXPECT_EQ(sh.x_data().size(), sh.point_count());
    EXPECT_EQ(sh.y_data().size(), sh.point_count());
    EXPECT_GT(sh.fill_vertex_count(), 0u);
    EXPECT_EQ(sh.solid_fill_first_vertex(), sh.fill_vertex_count());
    ASSERT_EQ(sh.text_annotations().size(), 1u);
    EXPECT_EQ(sh.text_annotations()[0].content, "label");
}

TEST(BugfixCoreSeries, FreshShapeAxesAutoscaleIncludesShapes)
{
    Axes ax;
    ax.shapes().rect(10, 20, 5, 4).circle(30, 40, 2);

    const AxisLimits xl = ax.x_limits();
    const AxisLimits yl = ax.y_limits();
    EXPECT_LE(xl.min, 10.0);
    EXPECT_GE(xl.max, 32.0);
    EXPECT_GT(xl.min, 5.0);   // not the empty-data default [-0.05, 1.05]
    EXPECT_LE(yl.min, 20.0);
    EXPECT_GE(yl.max, 42.0);
    EXPECT_GT(yl.min, 15.0);
}

TEST(BugfixCoreSeries, ShapeMutationAfterRebuildUpdatesBounds)
{
    Axes  ax;
    auto& sh = ax.shapes();
    sh.rect(0, 0, 1, 1);
    sh.rebuild_geometry();   // what the renderer's upload does on the first frame
    EXPECT_LE(ax.x_limits().max, 2.0);

    // Shape added after the first frame: limits must include it before any upload.
    sh.circle(100, 50, 5);
    EXPECT_GE(ax.x_limits().max, 105.0);
    EXPECT_GE(ax.y_limits().max, 55.0);

    // A style modifier on the last shape (rotation) also regenerates geometry.
    ShapeSeries line;
    line.line(0, 0, 10, 0);
    line.rebuild_geometry();
    float ymax_before = -1.0f;
    for (float y : line.y_data())
        if (std::isfinite(y))
            ymax_before = std::max(ymax_before, y);
    line.rotation(kPi / 2.0f);
    float ymax_after = -1.0f;
    for (float y : line.y_data())
        if (std::isfinite(y))
            ymax_after = std::max(ymax_after, y);
    EXPECT_NEAR(ymax_before, 0.0f, 1e-5f);
    EXPECT_NEAR(ymax_after, 5.0f, 1e-4f);

    // clear_shapes() leaves no geometry behind.
    sh.clear_shapes();
    EXPECT_EQ(sh.point_count(), 0u);
    EXPECT_EQ(sh.fill_vertex_count(), 0u);
}

// ── Bug #75: ShapeSeries3D segments validation ─────────────────────────────

TEST(BugfixCoreSeries, Shape3DSegmentsClampedNoNaN)
{
    ShapeSeries3D s;
    s.sphere(0, 0, 0, 1).segments(1);
    s.rebuild_geometry();
    for (float f : s.vertices())
        EXPECT_TRUE(std::isfinite(f));
    vec3 mn, mx;
    s.get_bounds(mn, mx);
    EXPECT_LE(mn.x, mx.x);
    EXPECT_LE(mn.y, mx.y);
    EXPECT_LE(mn.z, mx.z);
}

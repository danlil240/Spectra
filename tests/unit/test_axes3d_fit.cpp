// Axes3D::projection_fit_scale / projection_matrix: the 3D box with its arrows,
// tick labels and title must fit its viewport (finding P1-3).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>
#include <spectra/series3d.hpp>
#include <spectra/series_shapes3d.hpp>
#include <vector>

using namespace spectra;

namespace
{

// Screen-space pixel bounds of the 8 normalized box corners in `vp`.
struct PixelBounds
{
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
};

PixelBounds project_box(const Axes3D& ax, const Rect& vp)
{
    const mat4  pv = mat4_mul(ax.projection_matrix(vp), ax.camera().view_matrix());
    const float hs = Axes3D::box_half_size();
    PixelBounds b;
    for (int i = 0; i < 8; ++i)
    {
        const float x  = (i & 1) ? hs : -hs;
        const float y  = (i & 2) ? hs : -hs;
        const float z  = (i & 4) ? hs : -hs;
        const float cx = pv.m[0] * x + pv.m[4] * y + pv.m[8] * z + pv.m[12];
        const float cy = pv.m[1] * x + pv.m[5] * y + pv.m[9] * z + pv.m[13];
        const float cw = pv.m[3] * x + pv.m[7] * y + pv.m[11] * z + pv.m[15];
        const float sx = vp.x + (cx / cw + 1.0f) * 0.5f * vp.w;
        const float sy = vp.y + (cy / cw + 1.0f) * 0.5f * vp.h;
        b.x0           = std::min(b.x0, sx);
        b.x1           = std::max(b.x1, sx);
        b.y0           = std::min(b.y0, sy);
        b.y1           = std::max(b.y1, sy);
    }
    return b;
}

}   // namespace

TEST(Axes3DProjectionFit, NeverEnlargesTheView)
{
    Axes3D ax;
    EXPECT_GE(ax.projection_fit_scale(Rect{0, 0, 4000.0f, 3000.0f}), 1.0f);
    EXPECT_GE(ax.projection_fit_scale(Rect{0, 0, 1190.0f, 700.0f}), 1.0f);
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(Rect{0, 0, 0.0f, 0.0f}), 1.0f);
}

TEST(Axes3DProjectionFit, NarrowViewportZoomsOutMore)
{
    Axes3D      ax;
    const float wide   = ax.projection_fit_scale(Rect{0, 0, 1190.0f, 700.0f});
    const float narrow = ax.projection_fit_scale(Rect{0, 0, 560.0f, 700.0f});
    EXPECT_GT(narrow, wide);
}

// Issue #105: a full-size 3D axes (and any landscape panel, large or small)
// fills its viewport exactly as before the fit existed.
TEST(Axes3DProjectionFit, LandscapeViewportKeepsFullSize)
{
    Axes3D ax;
    ax.title("Title");   // drawn above the viewport: no longer shrinks the box
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(Rect{70, 40, 1190.0f, 860.0f}), 1.0f);
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(Rect{70, 40, 550.0f, 380.0f}), 1.0f);
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(Rect{70, 40, 180.0f, 120.0f}), 1.0f);
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(Rect{0, 0, 600.0f, 600.0f}), 1.0f);
}

TEST(Axes3DProjectionFit, TitleDoesNotShrinkTheBox)
{
    Axes3D      ax;
    const Rect  vp{0, 0, 560.0f, 700.0f};
    const float without = ax.projection_fit_scale(vp);
    ax.title("Title");
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(vp), without);
}

// Margins are proportional: the same panel at 2x DPI (twice the pixels) gets
// the same fit, and a small panel is not squeezed more than a large one.
TEST(Axes3DProjectionFit, DependsOnlyOnAspectRatio)
{
    Axes3D ax;
    for (const Rect vp : {Rect{0, 0, 560.0f, 700.0f}, Rect{0, 0, 300.0f, 900.0f}})
    {
        const float base = ax.projection_fit_scale(vp);
        EXPECT_NEAR(ax.projection_fit_scale(Rect{0, 0, vp.w * 2.0f, vp.h * 2.0f}), base, 1e-5f);
        EXPECT_NEAR(ax.projection_fit_scale(Rect{0, 0, vp.w * 0.25f, vp.h * 0.25f}), base, 1e-5f);
    }
}

TEST(Axes3DProjectionFit, IndependentOfOrientationAndZoom)
{
    Axes3D      ax;
    const Rect  vp{0, 0, 560.0f, 700.0f};
    const float base = ax.projection_fit_scale(vp);
    ax.camera().set_azimuth(130.0f).set_elevation(-70.0f).set_distance(40.0f).set_fov(20.0f);
    EXPECT_FLOAT_EQ(ax.projection_fit_scale(vp), base);
}

TEST(Axes3DProjectionFit, ProjectionMatrixScalesCameraProjection)
{
    Axes3D      ax;
    const Rect  vp{0, 0, 560.0f, 700.0f};
    const float fit  = ax.projection_fit_scale(vp);
    const mat4  cam  = ax.camera().projection_matrix(vp.w / vp.h);
    const mat4  proj = ax.projection_matrix(vp);
    EXPECT_NEAR(proj.m[0], cam.m[0] / fit, 1e-6f);
    EXPECT_NEAR(proj.m[5], cam.m[5] / fit, 1e-6f);
    EXPECT_FLOAT_EQ(proj.m[10], cam.m[10]);
    EXPECT_FLOAT_EQ(proj.m[14], cam.m[14]);
}

// The box stays inside its subplot at every orientation (default distance),
// reaching at most 4% of the half-extent into the cell margin: in a narrow
// half-figure subplot and in a full-size landscape one.
TEST(Axes3DProjectionFit, BoxFitsSubplotAtAnyOrientation)
{
    for (const Rect vp : {Rect{70.0f, 40.0f, 545.0f, 700.0f}, Rect{70.0f, 40.0f, 1190.0f, 860.0f}})
    {
        const float kMargin = -0.04f * std::min(vp.w, vp.h) * 0.5f;
        for (float az = 0.0f; az < 360.0f; az += 15.0f)
        {
            for (float el = -89.0f; el <= 89.0f; el += 89.0f / 4.0f)
            {
                Axes3D ax;
                ax.title("t");
                ax.camera().set_azimuth(az).set_elevation(el);
                const PixelBounds b = project_box(ax, vp);
                EXPECT_GE(b.x0, vp.x + kMargin) << "az " << az << " el " << el;
                EXPECT_LE(b.x1, vp.x + vp.w - kMargin) << "az " << az << " el " << el;
                EXPECT_GE(b.y0, vp.y + kMargin) << "az " << az << " el " << el;
                EXPECT_LE(b.y1, vp.y + vp.h - kMargin) << "az " << az << " el " << el;
            }
        }
    }
}

// ── S20: shape geometry must be current before the first frame's limits ────
// ShapeSeries3D generates its mesh lazily.  The renderer computes the model /
// grid from Axes3D limits *before* it uploads series, so the limits of a fresh
// (never rendered, never auto_fit) shape scene must already include the shapes.

namespace
{

struct Limits3D
{
    AxisLimits x, y, z;
};

Limits3D limits_of(const Axes3D& ax)
{
    return {ax.x_limits(), ax.y_limits(), ax.z_limits()};
}

void expect_same(const Limits3D& a, const Limits3D& b)
{
    EXPECT_DOUBLE_EQ(a.x.min, b.x.min);
    EXPECT_DOUBLE_EQ(a.x.max, b.x.max);
    EXPECT_DOUBLE_EQ(a.y.min, b.y.min);
    EXPECT_DOUBLE_EQ(a.y.max, b.y.max);
    EXPECT_DOUBLE_EQ(a.z.min, b.z.min);
    EXPECT_DOUBLE_EQ(a.z.max, b.z.max);
}

// Simulates what render_upload does for every shape series during a frame.
void simulate_upload(Axes3D& ax)
{
    for (auto& s : ax.series_mut())
        if (auto* sh = dynamic_cast<ShapeSeries3D*>(s.get()))
            sh->rebuild_geometry();
}

}   // namespace

TEST(Axes3DShapeFirstFrame, FreshShapeLimitsMatchSecondFrame)
{
    Axes3D ax;
    ax.shapes3d().box(-1.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f);
    ax.shapes3d().sphere(0.0f, 0.0f, 0.0f, 0.65f);
    ax.shapes3d().cylinder(1.5f, -0.6f, -0.7f, 1.5f, -0.6f, 0.7f, 0.35f);
    ax.shapes3d().cone(0.0f, 1.5f, -0.7f, 0.0f, 1.5f, 0.9f, 0.55f);

    // First frame: nothing has generated geometry yet.
    const Limits3D first = limits_of(ax);
    simulate_upload(ax);
    const Limits3D second = limits_of(ax);
    expect_same(first, second);

    // All solids lie within the padded bounds (box at x=-1.5, cylinder x=1.5,
    // cone apex z=0.9), not the empty-data [0,1] fallback.
    EXPECT_LT(first.x.min, -1.9);
    EXPECT_GT(first.x.max, 1.8);
    EXPECT_GT(first.y.max, 1.9);
    EXPECT_GT(first.z.max, 0.85);
    EXPECT_LT(first.z.min, -0.65);
}

TEST(Axes3DShapeFirstFrame, ArrowsAndPlaneFirstFrame)
{
    Axes3D ax;
    ax.shapes3d().arrow3d(0, 0, 0, 1.8f, 0, 0, 0.05f);
    ax.shapes3d().arrow3d(0, 0, 0, 0, 1.8f, 0, 0.05f);
    ax.shapes3d().arrow3d(0, 0, 0, 0, 0, 1.8f, 0.05f);
    ax.shapes3d().plane(0.6f, 0.6f, -0.1f, 0, 0, 1, 1.0f).opacity(0.35f);

    const Limits3D first = limits_of(ax);
    simulate_upload(ax);
    expect_same(first, limits_of(ax));
    // Plane spans x,y in [-0.4, 1.6] and z=-0.1; arrows reach 1.8.
    EXPECT_LT(first.x.min, -0.4);
    EXPECT_LT(first.z.min, -0.1);
    EXPECT_GT(first.z.max, 1.8);
}

TEST(Axes3DShapeFirstFrame, MixedSeriesIncludeShapes)
{
    Axes3D             ax;
    std::vector<float> x{0.0f, 1.0f}, y{0.0f, 1.0f}, z{0.0f, 1.0f};
    ax.scatter3d(x, y, z);
    ax.shapes3d().sphere(5.0f, 0.0f, 0.0f, 1.0f);

    const Limits3D first = limits_of(ax);
    EXPECT_GT(first.x.max, 6.0);
    simulate_upload(ax);
    expect_same(first, limits_of(ax));
}

TEST(Axes3DShapeFirstFrame, ShapesAddedAfterFirstFrameUpdateLimits)
{
    Axes3D ax;
    auto&  shapes = ax.shapes3d();
    shapes.sphere(0.0f, 0.0f, 0.0f, 1.0f);
    simulate_upload(ax);
    const Limits3D before = limits_of(ax);

    // Append without any upload: limits must reflect the new shape at once.
    shapes.box(10.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    const Limits3D after = limits_of(ax);
    EXPECT_GT(after.x.max, 11.0);
    EXPECT_GT(after.x.max, before.x.max);
    simulate_upload(ax);
    expect_same(after, limits_of(ax));

    shapes.clear_shapes();
    EXPECT_EQ(shapes.vertex_count(), 0u);
}

TEST(Axes3DShapeFirstFrame, ExplicitLimitsPreserved)
{
    Axes3D ax;
    ax.shapes3d().sphere(0.0f, 0.0f, 0.0f, 5.0f);
    ax.xlim(-1.0f, 1.0f);
    ax.ylim(-2.0f, 2.0f);
    ax.zlim(-3.0f, 3.0f);
    const Limits3D l = limits_of(ax);
    EXPECT_DOUBLE_EQ(l.x.min, -1.0);
    EXPECT_DOUBLE_EQ(l.y.max, 2.0);
    EXPECT_DOUBLE_EQ(l.z.min, -3.0);
}

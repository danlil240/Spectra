// Axes3D::projection_fit_scale / projection_matrix: the 3D box with its arrows,
// tick labels and title must fit its viewport (finding P1-3).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/math3d.hpp>

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

TEST(Axes3DProjectionFit, TitleReservesVerticalSpace)
{
    Axes3D      ax;
    const Rect  vp{0, 0, 1190.0f, 700.0f};
    const float without = ax.projection_fit_scale(vp);
    ax.title("Title");
    EXPECT_GT(ax.projection_fit_scale(vp), without);
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

// The box stays inside a half-figure subplot, with a label margin, at every
// orientation (default distance).
TEST(Axes3DProjectionFit, BoxFitsSubplotAtAnyOrientation)
{
    constexpr float kMargin = 20.0f;
    const Rect      vp{70.0f, 40.0f, 545.0f, 700.0f};
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

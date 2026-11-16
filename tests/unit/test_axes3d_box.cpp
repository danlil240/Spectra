// 3D axes box: grid panes on the faces farthest from the camera, box styles,
// and the surface colormap range tied to the data (not the axes limits).

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/series3d.hpp>
#include <vector>

#include "core/axes3d_box.hpp"
#include "core/colorbar_layout.hpp"

using namespace spectra;
using namespace spectra::axes3d_box;

namespace
{

BoxFaces faces_for(float azimuth, float elevation, bool ortho = false)
{
    Camera cam;
    cam.set_azimuth(azimuth).set_elevation(elevation);
    if (ortho)
        cam.set_projection(Camera::ProjectionMode::Orthographic);
    const mat4 view_proj = mat4_mul(cam.projection_matrix(1.5f), cam.view_matrix());
    return compute_faces(cam, view_proj);
}

int popcount(uint16_t m)
{
    int n = 0;
    for (; m; m &= static_cast<uint16_t>(m - 1))
        ++n;
    return n;
}

// Corner of the box nearest the camera (per axis: true = max).
std::array<bool, 3> near_corner(const BoxFaces& f)
{
    return {!f.back_hi[0], !f.back_hi[1], !f.back_hi[2]};
}

bool edge_touches_corner(int e, const std::array<bool, 3>& corner)
{
    const int a = e / 4;
    const int b = (a + 1) % 3;
    const int c = (a + 2) % 3;
    return ((e & 1) != 0) == corner[static_cast<size_t>(b)]
           && ((e & 2) != 0) == corner[static_cast<size_t>(c)];
}

std::vector<float> grid(int n, float lo, float hi)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = lo + (hi - lo) * static_cast<float>(i) / (n - 1);
    return v;
}

}   // namespace

// ── Far-face selection ─────────────────────────────────────────────────────

TEST(Axes3DBoxFaces, BackFaceIsOppositeTheEye)
{
    EXPECT_FALSE(back_face_hi(2.0, -1.0));   // camera on +: far wall at min
    EXPECT_TRUE(back_face_hi(-2.0, 1.0));    // camera on -: far wall at max
    // Eye in the mid plane: the view direction decides.
    EXPECT_TRUE(back_face_hi(0.0, 0.5));
    EXPECT_FALSE(back_face_hi(0.0, -0.5));
}

TEST(Axes3DBoxFaces, DefaultViewKeepsPanesAtTheMinimums)
{
    // Default camera (azimuth 45, elevation 30) sits at +x +y +z: the back
    // panes are the min faces, where the grid used to be drawn.
    const BoxFaces f = faces_for(45.0f, 30.0f);
    EXPECT_FALSE(f.back_hi[0]);
    EXPECT_FALSE(f.back_hi[1]);
    EXPECT_FALSE(f.back_hi[2]);
    EXPECT_EQ(f.up_axis, 2);
}

TEST(Axes3DBoxFaces, OrbitMovesPanesToTheFarSide)
{
    for (float az : {45.0f, 135.0f, 225.0f, 315.0f})
    {
        Camera cam;
        cam.set_azimuth(az).set_elevation(30.0f);
        const BoxFaces f = faces_for(az, 30.0f);
        // The back pane is always on the side away from the camera.
        EXPECT_EQ(f.back_hi[0], cam.position.x < 0.0) << "azimuth " << az;
        EXPECT_EQ(f.back_hi[1], cam.position.y < 0.0) << "azimuth " << az;
        EXPECT_FALSE(f.back_hi[2]) << "azimuth " << az;
    }
    // Looking from below: the floor pane moves to the top.
    EXPECT_TRUE(faces_for(45.0f, -30.0f).back_hi[2]);
}

TEST(Axes3DBoxFaces, PanesStayBehindTheBoxCentre)
{
    // A pane on a back face is farther from the camera than the box centre.
    const double hs = Axes3D::box_half_size();
    for (float az = 0.0f; az < 360.0f; az += 20.0f)
    {
        for (float el : {-60.0f, -10.0f, 15.0f, 70.0f})
        {
            Camera cam;
            cam.set_azimuth(az).set_elevation(el);
            const BoxFaces f      = faces_for(az, el);
            const double   eye[3] = {cam.position.x, cam.position.y, cam.position.z};
            const double   d0     = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(eye[i]) < 1e-3)
                    continue;   // eye in the mid plane: both faces equally far
                double p[3] = {0.0, 0.0, 0.0};
                p[i]        = f.back_hi[static_cast<size_t>(i)] ? hs : -hs;
                const double d =
                    std::sqrt((eye[0] - p[0]) * (eye[0] - p[0]) + (eye[1] - p[1]) * (eye[1] - p[1])
                              + (eye[2] - p[2]) * (eye[2] - p[2]));
                EXPECT_GT(d, d0) << "azimuth " << az << " elevation " << el << " axis " << i;
            }
        }
    }
}

TEST(Axes3DBoxFaces, OrthographicTopViewUsesViewDirection)
{
    const BoxFaces f = faces_for(0.0f, 90.0f, true);
    EXPECT_FALSE(f.back_hi[2]);   // looking down: the floor is at the back
}

TEST(Axes3DBoxFaces, UpTickCornerIsLeftmostOnScreen)
{
    Camera cam;
    cam.set_azimuth(45.0f).set_elevation(30.0f);
    const mat4     vp = mat4_mul(cam.projection_matrix(1.5f), cam.view_matrix());
    const BoxFaces f  = compute_faces(cam, vp);
    const double   hs = Axes3D::box_half_size();
    auto           sx = [&](bool x_hi, bool y_hi)
    {
        const vec4 c = mat4_mul_vec4(vp,
                                     {static_cast<float>(x_hi ? hs : -hs),
                                      static_cast<float>(y_hi ? hs : -hs),
                                      static_cast<float>(-hs),
                                      1.0f});
        return c.x / c.w;
    };
    const double chosen = sx(f.up_corner_hi[0], f.up_corner_hi[1]);
    for (int ci = 0; ci < 4; ++ci)
        EXPECT_LE(chosen, sx((ci & 1) != 0, (ci & 2) != 0) + 1e-9);
}

// ── Box styles ─────────────────────────────────────────────────────────────

TEST(Axes3DBoxStyle, FullAndNone)
{
    const BoxFaces f = faces_for(45.0f, 30.0f);
    EXPECT_EQ(edge_mask(BoxStyle::Full, f), 0x0FFF);
    EXPECT_EQ(edge_mask(BoxStyle::None, f), 0);
}

TEST(Axes3DBoxStyle, BackDropsTheEdgesAtTheNearestCorner)
{
    for (float az = 10.0f; az < 360.0f; az += 40.0f)
    {
        for (float el : {-35.0f, 25.0f, 60.0f})
        {
            const BoxFaces f    = faces_for(az, el);
            const uint16_t mask = edge_mask(BoxStyle::Back, f);
            const auto     near = near_corner(f);
            for (int e = 0; e < EDGE_COUNT; ++e)
            {
                const bool drawn   = (mask & (1u << e)) != 0;
                const bool is_tick = e == tick_edge_index(f, 0) || e == tick_edge_index(f, 1)
                                     || e == tick_edge_index(f, 2);
                const bool expected = !edge_touches_corner(e, near) || is_tick;
                EXPECT_EQ(drawn, expected)
                    << "azimuth " << az << " elevation " << el << " edge " << e;
            }
            // 9 back-pane edges, plus at most the tick edges at the near corner.
            EXPECT_GE(popcount(mask), 9);
            EXPECT_LE(popcount(mask), 12);
        }
    }
}

TEST(Axes3DBoxStyle, DefaultViewBackHasNineEdgesIncludingTicks)
{
    const BoxFaces f    = faces_for(45.0f, 30.0f);
    const uint16_t mask = edge_mask(BoxStyle::Back, f);
    EXPECT_EQ(popcount(mask), 9);
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NE(mask & (1u << tick_edge_index(f, axis)), 0) << "axis " << axis;
    // The three edges through the top-front corner (max, max, max) are gone.
    EXPECT_EQ(mask & (1u << edge_index(0, true, true)), 0);
    EXPECT_EQ(mask & (1u << edge_index(1, true, true)), 0);
    EXPECT_EQ(mask & (1u << edge_index(2, true, true)), 0);
}

TEST(Axes3DBoxStyle, TickEdgesMatchLabelPlacement)
{
    const BoxFaces f = faces_for(135.0f, 30.0f);   // camera at -x, +y
    // X ticks: bottom edge on the near (+y) side, marks pointing towards +y.
    const TickEdge tx = tick_edge(f, 0);
    EXPECT_TRUE(tx.corner_hi[1]);
    EXPECT_FALSE(tx.corner_hi[2]);
    EXPECT_EQ(tx.out_axis, 1);
    EXPECT_TRUE(tx.out_positive);
    // Y ticks: bottom edge on the near (-x) side, pointing towards -x.
    const TickEdge ty = tick_edge(f, 1);
    EXPECT_FALSE(ty.corner_hi[0]);
    EXPECT_FALSE(ty.corner_hi[2]);
    EXPECT_EQ(ty.out_axis, 0);
    EXPECT_FALSE(ty.out_positive);
}

TEST(Axes3DBoxStyle, ShowBoundingBoxMapsToStyle)
{
    Axes3D ax;
    EXPECT_EQ(ax.box_style(), BoxStyle::Back);
    EXPECT_TRUE(ax.show_bounding_box());
    ax.box_style(BoxStyle::Full);
    ax.show_bounding_box(false);
    EXPECT_EQ(ax.box_style(), BoxStyle::None);
    EXPECT_FALSE(ax.show_bounding_box());
    ax.show_bounding_box(true);   // restores the last visible style
    EXPECT_EQ(ax.box_style(), BoxStyle::Full);
}

TEST(Axes3DBoxStyle, GridPlanePositionFollowsBackFace)
{
    const double lo[3] = {-1.0, 10.0, 100.0};
    const double hi[3] = {1.0, 20.0, 200.0};
    BoxFaces     f;
    f.back_hi = {true, false, true};
    EXPECT_DOUBLE_EQ(back_plane(f, 0, lo, hi), 1.0);
    EXPECT_DOUBLE_EQ(back_plane(f, 1, lo, hi), 10.0);
    EXPECT_DOUBLE_EQ(back_plane(f, 2, lo, hi), 200.0);
}

// ── Surface colormap range ─────────────────────────────────────────────────

TEST(SurfaceColormapRange, DefaultsToDataZRange)
{
    const auto               xg = grid(3, 0.0f, 1.0f);
    const auto               yg = grid(2, 0.0f, 1.0f);
    const std::vector<float> z =
        {-2.0f, 0.5f, 7.0f, 1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f};
    SurfaceSeries s(xg, yg, z);
    EXPECT_FALSE(s.colormap_range_set());
    EXPECT_FLOAT_EQ(s.colormap_min(), -2.0f);   // NaN skipped
    EXPECT_FLOAT_EQ(s.colormap_max(), 7.0f);
}

TEST(SurfaceColormapRange, ExplicitRangeWinsAndCanBeCleared)
{
    const auto               xg = grid(2, 0.0f, 1.0f);
    const auto               yg = grid(2, 0.0f, 1.0f);
    const std::vector<float> z  = {0.0f, 1.0f, 2.0f, 3.0f};
    SurfaceSeries            s(xg, yg, z);
    s.colormap_range(-10.0f, 10.0f);
    EXPECT_TRUE(s.colormap_range_set());
    EXPECT_FLOAT_EQ(s.colormap_min(), -10.0f);
    EXPECT_FLOAT_EQ(s.colormap_max(), 10.0f);
    s.clear_colormap_range();
    EXPECT_FLOAT_EQ(s.colormap_min(), 0.0f);
    EXPECT_FLOAT_EQ(s.colormap_max(), 3.0f);
}

TEST(SurfaceColormapRange, NewDataUpdatesAutomaticRange)
{
    const auto    xg = grid(2, 0.0f, 1.0f);
    const auto    yg = grid(2, 0.0f, 1.0f);
    SurfaceSeries s(xg, yg, std::vector<float>{0.0f, 1.0f, 2.0f, 3.0f});
    s.set_data(xg, yg, std::vector<float>{5.0f, 6.0f, 7.0f, 8.0f});
    EXPECT_FLOAT_EQ(s.colormap_min(), 5.0f);
    EXPECT_FLOAT_EQ(s.colormap_max(), 8.0f);
    // No finite values: a unit range instead of NaN / inf.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    s.set_data(xg, yg, std::vector<float>{nan, nan, nan, nan});
    EXPECT_FLOAT_EQ(s.colormap_min(), 0.0f);
    EXPECT_FLOAT_EQ(s.colormap_max(), 1.0f);
}

TEST(SurfaceColormapRange, ColorbarUsesSurfaceRangeNotAxesLimits)
{
    Axes3D                   ax;
    const auto               xg = grid(3, -1.0f, 1.0f);
    const auto               yg = grid(3, -1.0f, 1.0f);
    const std::vector<float> z  = {-0.5f, 0.0f, 0.25f, 0.1f, 0.2f, 0.3f, 0.4f, 0.6f, 1.5f};
    auto&                    s  = ax.surface(xg, yg, z);
    s.colormap(ColormapType::Viridis);

    ax.zlim(-5.0, 5.0);   // zooming / panning changes the limits ...
    auto src = colorbar_layout::find_source(ax);
    ASSERT_TRUE(src.has_value());
    EXPECT_DOUBLE_EQ(src->vmin, -0.5);   // ... but not the colour mapping
    EXPECT_DOUBLE_EQ(src->vmax, 1.5);

    ax.zlim(0.0, 0.1);
    src = colorbar_layout::find_source(ax);
    ASSERT_TRUE(src.has_value());
    EXPECT_DOUBLE_EQ(src->vmin, -0.5);
    EXPECT_DOUBLE_EQ(src->vmax, 1.5);

    s.colormap_range(0.0f, 1.0f);
    src = colorbar_layout::find_source(ax);
    ASSERT_TRUE(src.has_value());
    EXPECT_DOUBLE_EQ(src->vmin, 0.0);
    EXPECT_DOUBLE_EQ(src->vmax, 1.0);
}

// 3D camera presets, view cube, projection toggle, equal aspect ("axis
// equal") and per-point colour / size of 3D scatter.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <spectra/axes3d.hpp>
#include <spectra/camera.hpp>
#include <spectra/figure.hpp>
#include <spectra/series3d.hpp>
#include <vector>

#include "core/colorbar_layout.hpp"
#include "ui/camera/view_presets.hpp"
#include "ui/commands/undo_manager.hpp"
#include "ui/input/input.hpp"
#include "ui/overlay/legend_layout.hpp"
#include "ui/overlay/view_cube.hpp"
#include "ui/workspace/figure_serializer.hpp"

using namespace spectra;

namespace
{

// Unit vector from the target to the camera.
vec3 view_dir(const Camera& cam)
{
    return vec3_normalize(cam.position - cam.target);
}

void expect_dir(const Camera& cam, vec3 expected, double tol = 2e-3)
{
    const vec3 d = view_dir(cam);
    EXPECT_NEAR(d.x, expected.x, tol);
    EXPECT_NEAR(d.y, expected.y, tol);
    EXPECT_NEAR(d.z, expected.z, tol);
}

// Screen-space right / up vectors of the camera (rows of the view matrix).
vec3 screen_right(const Camera& cam)
{
    const mat4 v = cam.view_matrix();
    return {v.m[0], v.m[4], v.m[8]};
}
vec3 screen_up(const Camera& cam)
{
    const mat4 v = cam.view_matrix();
    return {v.m[1], v.m[5], v.m[9]};
}

}   // namespace

// ─── Camera::set_view_preset ────────────────────────────────────────────────

TEST(CameraViewPreset, ZUpDirectionsAndScreenOrientation)
{
    Camera cam;   // Z-up default
    cam.set_view_preset(Camera::ViewPreset::Front);
    expect_dir(cam, {0, -1, 0});
    EXPECT_NEAR(screen_right(cam).x, 1.0, 1e-4);   // X to the right
    EXPECT_NEAR(screen_up(cam).z, 1.0, 1e-4);      // Z up

    cam.set_view_preset(Camera::ViewPreset::Back);
    expect_dir(cam, {0, 1, 0});
    cam.set_view_preset(Camera::ViewPreset::Right);
    expect_dir(cam, {1, 0, 0});
    EXPECT_NEAR(screen_right(cam).y, 1.0, 1e-4);   // +Y to the right
    cam.set_view_preset(Camera::ViewPreset::Left);
    expect_dir(cam, {-1, 0, 0});

    cam.set_view_preset(Camera::ViewPreset::Top);
    expect_dir(cam, {0, 0, 1});
    EXPECT_NEAR(screen_right(cam).x, 1.0, 1e-3);   // X right, Y up (plan view)
    EXPECT_NEAR(screen_up(cam).y, 1.0, 1e-3);

    cam.set_view_preset(Camera::ViewPreset::Bottom);
    expect_dir(cam, {0, 0, -1});
    EXPECT_NEAR(screen_right(cam).x, 1.0, 1e-3);

    cam.set_view_preset(Camera::ViewPreset::Isometric);
    const double k = 1.0 / std::sqrt(3.0);
    expect_dir(cam, {k, k, k});
}

TEST(CameraViewPreset, YUpFrontLooksAlongMinusZ)
{
    Camera cam;
    cam.set_up_axis(Camera::UpAxis::Y);
    cam.set_view_preset(Camera::ViewPreset::Front);
    expect_dir(cam, {0, 0, 1});
    EXPECT_NEAR(screen_right(cam).x, 1.0, 1e-4);
    EXPECT_NEAR(screen_up(cam).y, 1.0, 1e-4);
    cam.set_view_preset(Camera::ViewPreset::Top);
    expect_dir(cam, {0, 1, 0});
    EXPECT_NEAR(screen_right(cam).x, 1.0, 1e-3);
}

TEST(CameraViewPreset, PresetKeepsDistanceTargetAndProjection)
{
    Camera cam;
    cam.target          = {1.0f, 2.0f, 3.0f};
    cam.distance        = 7.0f;
    cam.projection_mode = Camera::ProjectionMode::Orthographic;
    cam.set_view_preset(Camera::ViewPreset::Left);
    EXPECT_FLOAT_EQ(cam.distance, 7.0f);
    EXPECT_NEAR(vec3_length(cam.position - cam.target), 7.0, 1e-4);
    EXPECT_EQ(cam.projection_mode, Camera::ProjectionMode::Orthographic);
}

TEST(CameraViewPreset, TopViewOrbitKeepsAzimuthStable)
{
    // At the pole the orbit must not flip the azimuth (a 180 deg screen spin).
    Camera cam;
    cam.set_view_preset(Camera::ViewPreset::Top);
    const float az = cam.azimuth;
    cam.orbit(0.0f, -5.0f);
    EXPECT_NEAR(std::remainder(cam.azimuth - az, 360.0f), 0.0f, 0.5f);
}

TEST(CameraViewPreset, MatchedOrthoSizeKeepsFraming)
{
    Camera cam;
    cam.distance = 10.0f;
    cam.fov      = 90.0f;
    EXPECT_NEAR(cam.perspective_matched_ortho_size(), 10.0f, 1e-4f);
}

// ─── plan_view_preset / toggled_projection ──────────────────────────────────

TEST(ViewPresetPlan, AxisViewsSwitchToOrthoIsoBackToPerspective)
{
    Camera cam;   // perspective
    auto   top = plan_view_preset(cam, Camera::ViewPreset::Top, false);
    EXPECT_EQ(top.target.projection_mode, Camera::ProjectionMode::Orthographic);
    EXPECT_TRUE(top.auto_ortho);
    EXPECT_NEAR(top.target.ortho_size, cam.perspective_matched_ortho_size(), 1e-4f);

    // From the automatic ortho, isometric returns to perspective.
    auto iso = plan_view_preset(top.target, Camera::ViewPreset::Isometric, true);
    EXPECT_EQ(iso.target.projection_mode, Camera::ProjectionMode::Perspective);
    EXPECT_FALSE(iso.auto_ortho);

    // A user-chosen ortho camera stays ortho, also for isometric.
    Camera user          = cam;
    user.projection_mode = Camera::ProjectionMode::Orthographic;
    user.ortho_size      = 3.0f;
    auto keep            = plan_view_preset(user, Camera::ViewPreset::Isometric, false);
    EXPECT_EQ(keep.target.projection_mode, Camera::ProjectionMode::Orthographic);
    EXPECT_FALSE(keep.auto_ortho);
    auto front = plan_view_preset(user, Camera::ViewPreset::Front, false);
    EXPECT_FALSE(front.auto_ortho);
    EXPECT_FLOAT_EQ(front.target.ortho_size, 3.0f);
}

TEST(ViewPresetPlan, AzimuthTakesShortestWay)
{
    Camera cam;
    cam.azimuth = 45.0f;
    cam.update_position_from_orbit();
    auto plan = plan_view_preset(cam, Camera::ViewPreset::Front, false);   // az 270
    EXPECT_NEAR(plan.target.azimuth, -90.0f, 1e-3f);
    expect_dir(plan.target, {0, -1, 0});
}

TEST(ViewPresetPlan, ToggledProjectionRoundTrip)
{
    Camera cam;
    Camera o = toggled_projection(cam);
    EXPECT_EQ(o.projection_mode, Camera::ProjectionMode::Orthographic);
    EXPECT_NEAR(o.ortho_size, cam.perspective_matched_ortho_size(), 1e-5f);
    Camera p = toggled_projection(o);
    EXPECT_EQ(p.projection_mode, Camera::ProjectionMode::Perspective);
}

TEST(ViewPresetPlan, CommandTableCoversEveryPresetOnce)
{
    EXPECT_EQ(std::size(VIEW_PRESETS), 7u);
    for (size_t i = 0; i < std::size(VIEW_PRESETS); ++i)
        for (size_t j = i + 1; j < std::size(VIEW_PRESETS); ++j)
        {
            EXPECT_NE(VIEW_PRESETS[i].preset, VIEW_PRESETS[j].preset);
            EXPECT_STRNE(VIEW_PRESETS[i].shortcut, VIEW_PRESETS[j].shortcut);
        }
}

// ─── View cube ──────────────────────────────────────────────────────────────

namespace
{
struct Cube3DFixture : ::testing::Test
{
    void SetUp() override
    {
        fig     = std::make_unique<Figure>(FigureConfig{900, 700});
        ax      = &fig->subplot3d(1, 1, 1);
        float x = 0.0f, y = 0.0f, z = 0.0f;
        ax->scatter3d(std::span(&x, 1), std::span(&y, 1), std::span(&z, 1));
        ax->auto_fit();
        fig->compute_layout();
    }
    std::unique_ptr<Figure> fig;
    Axes3D*                 ax = nullptr;
};
}   // namespace

TEST_F(Cube3DFixture, LayoutSitsInTopRightCorner)
{
    const auto  layout = view_cube::compute_layout(*ax);
    const Rect& vp     = ax->viewport();
    ASSERT_TRUE(layout.valid);
    EXPECT_NEAR(layout.box.x + layout.box.w, vp.x + vp.w - view_cube::MARGIN, 1e-3f);
    EXPECT_NEAR(layout.box.y, vp.y + view_cube::MARGIN, 1e-3f);
    // Default view (az 45, el 30): three faces visible -- top, right, back.
    EXPECT_EQ(layout.visible_count, 3);
    for (const auto& f : layout.faces)
        for (int k = 0; k < 4; ++k)
        {
            EXPECT_GE(f.x[k], layout.box.x);
            EXPECT_LE(f.x[k], layout.box.x + layout.box.w);
            EXPECT_GE(f.y[k], layout.box.y);
            EXPECT_LE(f.y[k], layout.box.y + layout.box.h);
        }
}

TEST_F(Cube3DFixture, HeadOnFaceIsHitAtCentre)
{
    ax->camera().set_view_preset(Camera::ViewPreset::Front);
    const auto layout = view_cube::compute_layout(*ax);
    const auto cx     = layout.box.x + layout.box.w * 0.5f;
    const auto cy     = layout.box.y + layout.box.h * 0.5f;
    auto       hit    = view_cube::hit_test(layout, cx, cy);
    EXPECT_EQ(hit.kind, view_cube::HitKind::Face);
    EXPECT_EQ(hit.preset, Camera::ViewPreset::Front);

    // Default camera: the top face is above the centre on screen.
    ax->camera().azimuth   = 45.0f;
    ax->camera().elevation = 30.0f;
    ax->camera().update_position_from_orbit();
    const auto  l2 = view_cube::compute_layout(*ax);
    const auto& top =
        *std::find_if(l2.faces.begin(),
                      l2.faces.end(),
                      [](const view_cube::Face& f) { return f.preset == Camera::ViewPreset::Top; });
    EXPECT_LT(top.center_y, cy);
    hit = view_cube::hit_test(l2, top.center_x, top.center_y);
    EXPECT_EQ(hit.kind, view_cube::HitKind::Face);
    EXPECT_EQ(hit.preset, Camera::ViewPreset::Top);

    // Outside the cube, nothing.
    hit = view_cube::hit_test(l2, layout.box.x - 30.0f, cy);
    EXPECT_EQ(hit.kind, view_cube::HitKind::None);
    // The strip below toggles the projection.
    hit =
        view_cube::hit_test(l2, l2.toggle.x + l2.toggle.w * 0.5f, l2.toggle.y + l2.toggle.h * 0.5f);
    EXPECT_EQ(hit.kind, view_cube::HitKind::ProjectionToggle);
}

TEST(ViewCube, HiddenInSmallViewports)
{
    Figure fig({200, 160});
    auto&  ax = fig.subplot3d(1, 1, 1);
    fig.compute_layout();
    EXPECT_FALSE(view_cube::compute_layout(ax).valid);
    EXPECT_EQ(view_cube::hit_test(ax, 100.0f, 50.0f).kind, view_cube::HitKind::None);
}

// ─── InputHandler: presets, cube clicks, undo, auto-ortho ───────────────────

TEST_F(Cube3DFixture, InputPresetIsUndoableAndOrbitLeavesAutoOrtho)
{
    InputHandler handler;
    UndoManager  undo;
    handler.set_undo_manager(&undo);
    handler.set_figure(fig.get());
    const Camera before = ax->camera();

    handler.apply_axes3d_view_preset(ax, Camera::ViewPreset::Top);
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Orthographic);
    expect_dir(ax->camera(), {0, 0, 1});
    EXPECT_TRUE(handler.axes3d_auto_ortho(ax));
    ASSERT_EQ(undo.undo_count(), 1u);

    undo.undo();
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Perspective);
    EXPECT_NEAR(ax->camera().azimuth, before.azimuth, 1e-4f);
    EXPECT_NEAR(ax->camera().elevation, before.elevation, 1e-4f);
    undo.redo();
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Orthographic);

    // Orbit drag away from the automatic ortho view returns to perspective.
    const auto& vp = ax->viewport();
    handler.set_viewport(vp.x, vp.y, vp.w, vp.h);
    const double sx = vp.x + vp.w * 0.4;
    const double sy = vp.y + vp.h * 0.6;
    handler.on_mouse_button(0, 1, 0, sx, sy);
    handler.on_mouse_move(sx + 40.0, sy + 20.0);
    handler.on_mouse_button(0, 0, 0, sx + 40.0, sy + 20.0);
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Perspective);
}

TEST_F(Cube3DFixture, ExplicitOrthoSurvivesOrbitAndPresets)
{
    InputHandler handler;
    UndoManager  undo;
    handler.set_undo_manager(&undo);
    handler.set_figure(fig.get());

    handler.toggle_axes3d_projection(ax);
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Orthographic);
    EXPECT_FALSE(handler.axes3d_auto_ortho(ax));
    handler.apply_axes3d_view_preset(ax, Camera::ViewPreset::Isometric);
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Orthographic);

    undo.undo();   // preset
    undo.undo();   // toggle
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Perspective);
}

TEST_F(Cube3DFixture, ClickOnCubeFaceSnapsInsteadOfOrbiting)
{
    InputHandler handler;
    UndoManager  undo;
    handler.set_undo_manager(&undo);
    handler.set_figure(fig.get());
    const auto& vp = ax->viewport();
    handler.set_viewport(vp.x, vp.y, vp.w, vp.h);

    const auto  layout = view_cube::compute_layout(*ax);
    const auto& top =
        *std::find_if(layout.faces.begin(),
                      layout.faces.end(),
                      [](const view_cube::Face& f) { return f.preset == Camera::ViewPreset::Top; });
    handler.on_mouse_button(0, 1, 0, top.center_x, top.center_y);
    handler.on_mouse_button(0, 0, 0, top.center_x, top.center_y);
    expect_dir(ax->camera(), {0, 0, 1});
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Orthographic);
    EXPECT_EQ(undo.undo_count(), 1u);

    // Projection toggle strip.
    handler.on_mouse_button(0,
                            1,
                            0,
                            layout.toggle.x + layout.toggle.w * 0.5f,
                            layout.toggle.y + layout.toggle.h * 0.5f);
    EXPECT_EQ(ax->camera().projection_mode, Camera::ProjectionMode::Perspective);
}

// ─── Equal aspect ───────────────────────────────────────────────────────────

TEST(Axes3DEqualAspect, WidensRangesToLargestSpanAboutCentres)
{
    Axes3D ax;
    ax.xlim(0.0, 10.0);
    ax.ylim(-1.0, 1.0);
    ax.zlim(4.0, 5.0);
    EXPECT_FALSE(ax.equal_aspect());
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 1.0);

    ax.equal_aspect(true);
    EXPECT_DOUBLE_EQ(ax.x_limits().min, 0.0);
    EXPECT_DOUBLE_EQ(ax.x_limits().max, 10.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().min, -5.0);
    EXPECT_DOUBLE_EQ(ax.y_limits().max, 5.0);
    EXPECT_DOUBLE_EQ(ax.z_limits().min, -0.5);
    EXPECT_DOUBLE_EQ(ax.z_limits().max, 9.5);

    // Same model scale on every axis: one data unit, one box length.
    const mat4 m = ax.data_to_normalized_matrix();
    EXPECT_FLOAT_EQ(m.m[0], m.m[5]);
    EXPECT_FLOAT_EQ(m.m[5], m.m[10]);

    ax.equal_aspect(false);
    EXPECT_DOUBLE_EQ(ax.z_limits().min, 4.0);
}

TEST(Axes3DEqualAspect, AppliesToAutoFitLimits)
{
    Axes3D             ax;
    std::vector<float> x{0.0f, 20.0f}, y{0.0f, 2.0f}, z{0.0f, 0.0f};
    ax.scatter3d(x, y, z);
    ax.equal_aspect(true);
    ax.auto_fit();
    const double sx = ax.x_limits().max - ax.x_limits().min;
    EXPECT_NEAR(ax.y_limits().max - ax.y_limits().min, sx, 1e-9);
    EXPECT_NEAR(ax.z_limits().max - ax.z_limits().min, sx, 1e-9);
    EXPECT_NEAR((ax.y_limits().max + ax.y_limits().min) * 0.5, 1.0, 1e-6);
}

// ─── 3D scatter per-point colour / size ─────────────────────────────────────

TEST(Scatter3DPointAttrs, ColormapNeedsValuesAndMap)
{
    std::vector<float> x{0, 1, 2}, y{0, 1, 2}, z{0, 1, 2};
    ScatterSeries3D    s(x, y, z);
    s.color(Color{0.1f, 0.2f, 0.3f, 0.5f});
    EXPECT_FALSE(s.has_colormap());
    std::vector<float> v{10.0f, 20.0f, NAN};
    s.color_values(v);
    EXPECT_FALSE(s.has_colormap());   // no colormap yet
    s.colormap(ColormapType::Viridis);
    ASSERT_TRUE(s.has_colormap());
    EXPECT_FLOAT_EQ(s.colormap_min(), 10.0f);   // NaN ignored
    EXPECT_FLOAT_EQ(s.colormap_max(), 20.0f);

    const Color lo = s.point_color(0);
    const Color hi = s.point_color(1);
    const Color e0 = SurfaceSeries::sample_colormap(ColormapType::Viridis, 0.0f);
    const Color e1 = SurfaceSeries::sample_colormap(ColormapType::Viridis, 1.0f);
    EXPECT_FLOAT_EQ(lo.r, e0.r);
    EXPECT_FLOAT_EQ(hi.g, e1.g);
    EXPECT_FLOAT_EQ(lo.a, 0.5f);                 // series alpha kept
    EXPECT_FLOAT_EQ(s.point_color(2).r, 0.1f);   // NaN -> series colour

    s.colormap_range(0.0f, 40.0f);
    const Color mid = SurfaceSeries::sample_colormap(ColormapType::Viridis, 0.5f);
    EXPECT_FLOAT_EQ(s.point_color(1).r, mid.r);
    s.clear_colormap_range();
    EXPECT_FLOAT_EQ(s.colormap_max(), 20.0f);

    // Mismatched count disables per-point colour.
    std::vector<float> short_v{1.0f};
    s.color_values(short_v);
    EXPECT_FALSE(s.has_colormap());
}

TEST(Scatter3DPointAttrs, SizesFallBackToSeriesSize)
{
    std::vector<float> x{0, 1}, y{0, 1}, z{0, 1};
    ScatterSeries3D    s(x, y, z);
    s.size(5.0f);
    EXPECT_FALSE(s.has_sizes());
    EXPECT_FLOAT_EQ(s.point_size(1), 5.0f);
    std::vector<float> sz{3.0f, 12.0f};
    s.sizes(sz);
    EXPECT_TRUE(s.has_sizes());
    EXPECT_FLOAT_EQ(s.point_size(0), 3.0f);
    EXPECT_FLOAT_EQ(s.max_point_size(), 12.0f);
    std::vector<float> wrong{1.0f, 2.0f, 3.0f};
    s.sizes(wrong);
    EXPECT_FALSE(s.has_sizes());
    EXPECT_FLOAT_EQ(s.point_size(0), 5.0f);
}

TEST(Scatter3DPointAttrs, ColorbarAndLegendFollowColormap)
{
    Axes3D             ax;
    std::vector<float> x{0, 1}, y{0, 1}, z{0, 1}, v{-2.0f, 6.0f};
    auto&              s = ax.scatter3d(x, y, z);
    EXPECT_FALSE(colorbar_layout::find_source(ax).has_value());
    s.color_values(v).colormap(ColormapType::Plasma).label("pts");
    const auto src = colorbar_layout::find_source(ax);
    ASSERT_TRUE(src.has_value());
    EXPECT_EQ(src->cmap, ColormapType::Plasma);
    EXPECT_DOUBLE_EQ(src->vmin, -2.0);
    EXPECT_DOUBLE_EQ(src->vmax, 6.0);
    // The bar samples the same colours as the points.
    const Color bar = colorbar_layout::sample(src->cmap, src->mapping, 1.0f);
    EXPECT_FLOAT_EQ(bar.r, s.point_color(1).r);
    ax.show_colorbar(true);
    EXPECT_TRUE(colorbar_layout::wants_colorbar(ax));
    const Color sw = legend_layout::swatch_color(s);
    EXPECT_FLOAT_EQ(sw.g, SurfaceSeries::sample_colormap(ColormapType::Plasma, 0.5f).g);
}

// ─── Workspace (binary figure) round trip ───────────────────────────────────

TEST(Camera3DSerialization, EqualAspectAndScatterAttrsRoundTrip)
{
    const std::string path =
        (std::filesystem::temp_directory_path() / "spectra_test_3d_camera_views.spectra").string();

    Figure src({800, 600});
    auto&  ax = src.subplot3d(1, 1, 1);
    ax.xlim(0.0, 10.0);
    ax.ylim(0.0, 2.0);
    ax.zlim(0.0, 1.0);
    ax.equal_aspect(true);
    std::vector<float> x{0, 1, 2}, y{0, 1, 2}, z{0, 1, 0}, v{1, 2, 3}, sz{4, 8, 16};
    auto&              s = ax.scatter3d(x, y, z);
    s.color_values(v).colormap(ColormapType::Jet).colormap_range(0.0f, 5.0f);
    s.sizes(sz);
    ax.scatter3d(x, y, z);   // plain one: no attributes

    ASSERT_TRUE(FigureSerializer::save(path, src));
    Figure dst({800, 600});
    ASSERT_TRUE(FigureSerializer::load(path, dst));
    std::remove(path.c_str());

    ASSERT_EQ(dst.all_axes().size(), 1u);
    auto* lax = dynamic_cast<Axes3D*>(dst.all_axes()[0].get());
    ASSERT_NE(lax, nullptr);
    EXPECT_TRUE(lax->equal_aspect());
    EXPECT_NEAR(lax->y_limits().max - lax->y_limits().min, 10.0, 1e-4);

    ASSERT_EQ(lax->series().size(), 2u);
    auto* s0 = dynamic_cast<ScatterSeries3D*>(lax->series()[0].get());
    auto* s1 = dynamic_cast<ScatterSeries3D*>(lax->series()[1].get());
    ASSERT_NE(s0, nullptr);
    ASSERT_NE(s1, nullptr);
    EXPECT_TRUE(s0->has_colormap());
    EXPECT_EQ(s0->colormap(), ColormapType::Jet);
    EXPECT_TRUE(s0->colormap_range_set());
    EXPECT_FLOAT_EQ(s0->colormap_max(), 5.0f);
    ASSERT_TRUE(s0->has_sizes());
    EXPECT_FLOAT_EQ(s0->point_size(2), 16.0f);
    EXPECT_FALSE(s1->has_colormap());
    EXPECT_FALSE(s1->has_sizes());
}

#include <cassert>
#include <cmath>
#include <iostream>
#include <spectra/math3d.hpp>

#include "../../src/core/axes3d.hpp"
#include "../../src/ui/imgui/axes3d_renderer.hpp"
#include "../../src/ui/camera/camera.hpp"

using namespace spectra;

void test_axes3d_construction()
{
    Axes3D axes;

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    assert(xlim.min == 0.0f && xlim.max == 1.0f);
    assert(ylim.min == 0.0f && ylim.max == 1.0f);
    assert(zlim.min == 0.0f && zlim.max == 1.0f);

    assert(axes.grid_enabled() == true);
    assert(axes.show_bounding_box() == true);

    std::cout << "✓ test_axes3d_construction passed\n";
}

void test_axes3d_limits()
{
    Axes3D axes;

    axes.xlim(-5.0f, 5.0f);
    axes.ylim(-10.0f, 10.0f);
    axes.zlim(0.0f, 20.0f);

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    assert(xlim.min == -5.0f && xlim.max == 5.0f);
    assert(ylim.min == -10.0f && ylim.max == 10.0f);
    assert(zlim.min == 0.0f && zlim.max == 20.0f);

    std::cout << "✓ test_axes3d_limits passed\n";
}

void test_axes3d_labels()
{
    Axes3D axes;

    axes.xlabel("X Axis");
    axes.ylabel("Y Axis");
    axes.zlabel("Z Axis");
    axes.title("3D Plot");

    assert(axes.get_xlabel() == "X Axis");
    assert(axes.get_ylabel() == "Y Axis");
    assert(axes.get_zlabel() == "Z Axis");
    assert(axes.get_title() == "3D Plot");

    std::cout << "✓ test_axes3d_labels passed\n";
}

void test_axes3d_ticks()
{
    Axes3D axes;

    axes.xlim(0.0f, 10.0f);
    axes.ylim(-5.0f, 5.0f);
    axes.zlim(0.0f, 100.0f);

    auto x_ticks = axes.compute_x_ticks();
    auto y_ticks = axes.compute_y_ticks();
    auto z_ticks = axes.compute_z_ticks();

    assert(!x_ticks.positions.empty());
    assert(!y_ticks.positions.empty());
    assert(!z_ticks.positions.empty());

    assert(x_ticks.positions.size() == x_ticks.labels.size());
    assert(y_ticks.positions.size() == y_ticks.labels.size());
    assert(z_ticks.positions.size() == z_ticks.labels.size());

    std::cout << "✓ test_axes3d_ticks passed\n";
}

void test_axes3d_camera()
{
    Axes3D axes;

    Camera& cam = axes.camera();

    cam.azimuth   = 90.0f;
    cam.elevation = 45.0f;
    cam.distance  = 10.0f;
    cam.update_position_from_orbit();

    assert(axes.camera().azimuth == 90.0f);
    assert(axes.camera().elevation == 45.0f);
    assert(axes.camera().distance == 10.0f);

    std::cout << "✓ test_axes3d_camera passed\n";
}

void test_axes3d_grid_planes()
{
    Axes3D axes;

    axes.grid_planes(Axes3D::GridPlane::XY);
    assert(axes.grid_planes() == Axes3D::GridPlane::XY);

    axes.grid_planes(Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ);
    assert(axes.grid_planes() == (Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ));

    axes.grid_planes(Axes3D::GridPlane::All);
    assert(axes.grid_planes() == Axes3D::GridPlane::All);

    std::cout << "✓ test_axes3d_grid_planes passed\n";
}

void test_axes3d_bounding_box()
{
    Axes3D axes;

    assert(axes.show_bounding_box() == true);

    axes.show_bounding_box(false);
    assert(axes.show_bounding_box() == false);

    axes.show_bounding_box(true);
    assert(axes.show_bounding_box() == true);

    std::cout << "✓ test_axes3d_bounding_box passed\n";
}

void test_axes3d_auto_fit()
{
    Axes3D axes;

    axes.auto_fit();

    auto xlim = axes.x_limits();
    auto ylim = axes.y_limits();
    auto zlim = axes.z_limits();

    assert(xlim.min == -1.0f && xlim.max == 1.0f);
    assert(ylim.min == -1.0f && ylim.max == 1.0f);
    assert(zlim.min == -1.0f && zlim.max == 1.0f);

    std::cout << "✓ test_axes3d_auto_fit passed\n";
}

void test_axes3d_viewport()
{
    Axes3D axes;

    Rect viewport{100.0f, 200.0f, 800.0f, 600.0f};
    axes.set_viewport(viewport);

    auto vp = axes.viewport();
    assert(vp.x == 100.0f);
    assert(vp.y == 200.0f);
    assert(vp.w == 800.0f);
    assert(vp.h == 600.0f);

    std::cout << "✓ test_axes3d_viewport passed\n";
}

void test_axes3d_grid_toggle()
{
    Axes3D axes;

    assert(axes.grid_enabled() == true);

    axes.grid(false);
    assert(axes.grid_enabled() == false);

    axes.set_grid_enabled(true);
    assert(axes.grid_enabled() == true);

    std::cout << "✓ test_axes3d_grid_toggle passed\n";
}

void test_axes3d_border_toggle()
{
    Axes3D axes;

    assert(axes.border_enabled() == true);

    axes.show_border(false);
    assert(axes.border_enabled() == false);

    axes.set_border_enabled(true);
    assert(axes.border_enabled() == true);

    std::cout << "✓ test_axes3d_border_toggle passed\n";
}

void test_axes3d_tick_range_edge_cases()
{
    Axes3D axes;

    axes.xlim(0.0f, 0.0f);
    auto x_ticks = axes.compute_x_ticks();
    assert(x_ticks.positions.size() == 1);
    assert(x_ticks.positions[0] == 0.0);

    axes.ylim(-1e-6f, 1e-6f);
    auto y_ticks = axes.compute_y_ticks();
    assert(!y_ticks.positions.empty());

    axes.zlim(1000.0f, 10000.0f);
    auto z_ticks = axes.compute_z_ticks();
    assert(!z_ticks.positions.empty());

    std::cout << "✓ test_axes3d_tick_range_edge_cases passed\n";
}

void test_axes3d_camera_target_update()
{
    Axes3D axes;

    axes.xlim(-10.0f, 10.0f);
    axes.ylim(-10.0f, 10.0f);
    axes.zlim(-10.0f, 10.0f);

    axes.auto_fit();

    vec3 target = axes.camera().target;
    assert(std::abs(target.x - 0.0f) < 0.1f);
    assert(std::abs(target.y - 0.0f) < 0.1f);
    assert(std::abs(target.z - 0.0f) < 0.1f);

    std::cout << "✓ test_axes3d_camera_target_update passed\n";
}

void test_axes3d_series_storage()
{
    Axes3D axes;

    assert(axes.series().empty());

    std::cout << "✓ test_axes3d_series_storage passed\n";
}

void test_axes3d_bounding_box_vertices()
{
    // Test that BoundingBoxData::generate produces 24 vertices (12 edges × 2 endpoints)
    Axes3DRenderer::BoundingBoxData bbox;
    vec3                            min_corner = {-1.0f, -2.0f, -3.0f};
    vec3                            max_corner = {1.0f, 2.0f, 3.0f};
    bbox.generate(min_corner, max_corner);

    assert(bbox.edge_vertices.size() == 24);

    // Verify all vertices are at corners (each coordinate should be min or max)
    for (const auto& v : bbox.edge_vertices)
    {
        assert(v.x == min_corner.x || v.x == max_corner.x);
        assert(v.y == min_corner.y || v.y == max_corner.y);
        assert(v.z == min_corner.z || v.z == max_corner.z);
    }

    std::cout << "✓ test_axes3d_bounding_box_vertices passed\n";
}

void test_axes3d_tick_mark_positions()
{
    // Test that TickMarkData generates correct positions
    Axes3D axes;
    axes.xlim(0.0f, 10.0f);
    axes.ylim(-5.0f, 5.0f);
    axes.zlim(0.0f, 100.0f);

    Axes3DRenderer::TickMarkData tick_data;
    vec3                         min_corner = {0.0f, -5.0f, 0.0f};
    vec3                         max_corner = {10.0f, 5.0f, 100.0f};

    tick_data.generate_x_ticks(axes, min_corner, max_corner);
    auto x_ticks = axes.compute_x_ticks();
    assert(tick_data.positions.size() == x_ticks.positions.size());
    assert(tick_data.labels.size() == x_ticks.labels.size());

    // X tick positions should be at y=y_min, z=z_min
    for (size_t i = 0; i < tick_data.positions.size(); ++i)
    {
        assert(tick_data.positions[i].x == static_cast<float>(x_ticks.positions[i]));
        assert(tick_data.positions[i].y == min_corner.y);
        assert(tick_data.positions[i].z == min_corner.z);
    }

    tick_data.generate_y_ticks(axes, min_corner, max_corner);
    auto y_ticks = axes.compute_y_ticks();
    assert(tick_data.positions.size() == y_ticks.positions.size());

    tick_data.generate_z_ticks(axes, min_corner, max_corner);
    auto z_ticks = axes.compute_z_ticks();
    assert(tick_data.positions.size() == z_ticks.positions.size());

    std::cout << "✓ test_axes3d_tick_mark_positions passed\n";
}

void test_axes3d_world_to_screen_projection()
{
    // Test that Camera MVP projection produces sensible screen coordinates
    Camera cam;
    cam.reset();
    cam.update_position_from_orbit();

    float aspect = 16.0f / 9.0f;
    mat4  proj   = cam.projection_matrix(aspect);
    mat4  view   = cam.view_matrix();
    mat4  mvp    = mat4_mul(proj, view);

    // Project the camera target (origin) — should map to roughly center of NDC
    vec3  origin = {0.0f, 0.0f, 0.0f};
    float clip_x = mvp.m[0] * origin.x + mvp.m[4] * origin.y + mvp.m[8] * origin.z + mvp.m[12];
    float clip_y = mvp.m[1] * origin.x + mvp.m[5] * origin.y + mvp.m[9] * origin.z + mvp.m[13];
    float clip_w = mvp.m[3] * origin.x + mvp.m[7] * origin.y + mvp.m[11] * origin.z + mvp.m[15];

    assert(clip_w > 0.0f);   // Should be in front of camera

    float ndc_x = clip_x / clip_w;
    float ndc_y = clip_y / clip_w;

    // Origin (camera target) should be close to NDC center (0,0)
    assert(std::abs(ndc_x) < 0.5f);
    assert(std::abs(ndc_y) < 0.5f);

    // Project a point behind the camera — should have negative or near-zero w
    vec3  behind   = cam.position + (cam.position - cam.target) * 2.0f;
    float behind_w = mvp.m[3] * behind.x + mvp.m[7] * behind.y + mvp.m[11] * behind.z + mvp.m[15];
    // Behind should have w <= 0 or very small
    assert(behind_w <= 0.1f);

    std::cout << "✓ test_axes3d_world_to_screen_projection passed\n";
}

int main()
{
    std::cout << "Running Axes3D unit tests...\n\n";

    test_axes3d_construction();
    test_axes3d_limits();
    test_axes3d_labels();
    test_axes3d_ticks();
    test_axes3d_camera();
    test_axes3d_grid_planes();
    test_axes3d_bounding_box();
    test_axes3d_auto_fit();
    test_axes3d_viewport();
    test_axes3d_grid_toggle();
    test_axes3d_border_toggle();
    test_axes3d_tick_range_edge_cases();
    test_axes3d_camera_target_update();
    test_axes3d_series_storage();
    test_axes3d_bounding_box_vertices();
    test_axes3d_tick_mark_positions();
    test_axes3d_world_to_screen_projection();

    std::cout << "\n✅ All Axes3D tests passed!\n";
    return 0;
}

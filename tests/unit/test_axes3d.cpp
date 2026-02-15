#include "../../src/core/axes3d.hpp"
#include "../../src/ui/camera.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace plotix;

void test_axes3d_construction() {
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

void test_axes3d_limits() {
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

void test_axes3d_labels() {
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

void test_axes3d_ticks() {
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

void test_axes3d_camera() {
    Axes3D axes;
    
    Camera& cam = axes.camera();
    
    cam.azimuth = 90.0f;
    cam.elevation = 45.0f;
    cam.distance = 10.0f;
    cam.update_position_from_orbit();
    
    assert(axes.camera().azimuth == 90.0f);
    assert(axes.camera().elevation == 45.0f);
    assert(axes.camera().distance == 10.0f);
    
    std::cout << "✓ test_axes3d_camera passed\n";
}

void test_axes3d_grid_planes() {
    Axes3D axes;
    
    axes.set_grid_planes(static_cast<int>(Axes3D::GridPlane::XY));
    assert(axes.grid_planes() == static_cast<int>(Axes3D::GridPlane::XY));
    
    axes.set_grid_planes(static_cast<int>(Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ));
    assert(axes.grid_planes() == (static_cast<int>(Axes3D::GridPlane::XY) | static_cast<int>(Axes3D::GridPlane::XZ)));
    
    axes.set_grid_planes(static_cast<int>(Axes3D::GridPlane::All));
    assert(axes.grid_planes() == static_cast<int>(Axes3D::GridPlane::All));
    
    std::cout << "✓ test_axes3d_grid_planes passed\n";
}

void test_axes3d_bounding_box() {
    Axes3D axes;
    
    assert(axes.show_bounding_box() == true);
    
    axes.show_bounding_box(false);
    assert(axes.show_bounding_box() == false);
    
    axes.show_bounding_box(true);
    assert(axes.show_bounding_box() == true);
    
    std::cout << "✓ test_axes3d_bounding_box passed\n";
}

void test_axes3d_auto_fit() {
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

void test_axes3d_viewport() {
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

void test_axes3d_grid_toggle() {
    Axes3D axes;
    
    assert(axes.grid_enabled() == true);
    
    axes.grid(false);
    assert(axes.grid_enabled() == false);
    
    axes.set_grid_enabled(true);
    assert(axes.grid_enabled() == true);
    
    std::cout << "✓ test_axes3d_grid_toggle passed\n";
}

void test_axes3d_border_toggle() {
    Axes3D axes;
    
    assert(axes.border_enabled() == true);
    
    axes.show_border(false);
    assert(axes.border_enabled() == false);
    
    axes.set_border_enabled(true);
    assert(axes.border_enabled() == true);
    
    std::cout << "✓ test_axes3d_border_toggle passed\n";
}

void test_axes3d_tick_range_edge_cases() {
    Axes3D axes;
    
    axes.xlim(0.0f, 0.0f);
    auto x_ticks = axes.compute_x_ticks();
    assert(x_ticks.positions.size() == 1);
    assert(x_ticks.positions[0] == 0.0f);
    
    axes.ylim(-1e-6f, 1e-6f);
    auto y_ticks = axes.compute_y_ticks();
    assert(!y_ticks.positions.empty());
    
    axes.zlim(1000.0f, 10000.0f);
    auto z_ticks = axes.compute_z_ticks();
    assert(!z_ticks.positions.empty());
    
    std::cout << "✓ test_axes3d_tick_range_edge_cases passed\n";
}

void test_axes3d_camera_target_update() {
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

void test_axes3d_series_storage() {
    Axes3D axes;
    
    assert(axes.series().empty());
    
    std::cout << "✓ test_axes3d_series_storage passed\n";
}

int main() {
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
    
    std::cout << "\n✅ All Axes3D tests passed!\n";
    return 0;
}

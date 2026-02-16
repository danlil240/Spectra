#include <plotix/plotix.hpp>
#include <cmath>
#include <vector>
#include <iostream>

using namespace plotix;

int main() {
    App app;
    auto& fig = app.figure({.width = 1600, .height = 900});
    
    // Create a single 3D axes for the animation demo
    auto& ax = fig.subplot3d(1, 1, 1);
    
    // Create a 3D spiral scatter plot
    std::vector<float> x, y, z;
    for (int i = 0; i < 200; ++i) {
        float t = static_cast<float>(i) * 0.05f;
        x.push_back(std::cos(t * 2) * t * 0.3f);
        y.push_back(std::sin(t * 2) * t * 0.3f);
        z.push_back(t * 0.2f);
    }
    
    ax.scatter3d(x, y, z)
        .color(colors::cyan)
        .size(4.0f)
        .label("Animated Spiral");
    
    ax.auto_fit();
    ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::All));
    ax.title("3D Camera Animation Demo");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.zlabel("Z");
    
    // Set initial camera position
    ax.camera().azimuth = 0.0f;
    ax.camera().elevation = 30.0f;
    ax.camera().distance = 15.0f;
    ax.camera().update_position_from_orbit();
    
    std::cout << "\n=== 3D Camera Animation Demo ===\n";
    std::cout << "Using built-in CameraAnimator for smooth camera animation!\n";
    std::cout << "Close the window to exit.\n\n";
    std::cout << "Camera Animation:\n";
    std::cout << "  - Smooth orbit: 0° → 360° azimuth over 8 seconds\n";
    std::cout << "  - Elevation oscillates: 30° → 60° → 30° over 8 seconds\n";
    std::cout << "  - Distance oscillates: 15 → 10 → 15 units over 8 seconds\n";
    std::cout << "  - FOV oscillates: 45° → 60° → 45° over 8 seconds\n";
    std::cout << "\n";
    
    fig.show();
    
    // Use the figure's animation system with direct camera parameter updates
    // This demonstrates the camera animation API without custom controllers
    fig.animate()
        .fps(60.0f)
        .duration(8.0f)
        .loop(true)
        .on_frame([&ax](Frame& frame) {
            float t = frame.elapsed_sec / 8.0f;  // Normalize to [0, 1]
            
            // Azimuth: full rotation (0° → 360°)
            ax.camera().azimuth = t * 360.0f;
            
            // Elevation: oscillate (30° → 60° → 30°)
            float elevation_phase = t * 2.0f * M_PI;
            ax.camera().elevation = 45.0f + 15.0f * std::sin(elevation_phase);
            
            // Distance: oscillate (15 → 10 → 15)
            float distance_phase = t * 2.0f * M_PI;
            ax.camera().distance = 12.5f + 2.5f * std::cos(distance_phase);
            
            // FOV: oscillates (45° → 60° → 45°)
            float fov_phase = t * 2.0f * M_PI;
            ax.camera().fov = 52.5f + 7.5f * std::sin(fov_phase);
            
            // Update camera position from orbit parameters
            ax.camera().update_position_from_orbit();
        })
        .play();
    
    app.run();
    
    return 0;
}

#include <plotix/plotix.hpp>
#include <cmath>
#include <vector>

using namespace plotix;

int main() {
    App app;
    auto& fig = app.figure({.width = 800, .height = 600});
    
    auto& ax = fig.subplot3d(1, 1, 1);
    
    // Create a grid for the surface
    const int nx = 40, ny = 40;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    
    // Define grid coordinates
    for (int i = 0; i < nx; ++i) {
        x_grid[i] = static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f;
    }
    for (int j = 0; j < ny; ++j) {
        y_grid[j] = static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f;
    }
    
    // Compute z = sin(x) * cos(y)
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            float x = x_grid[i];
            float y = y_grid[j];
            z_values[j * nx + i] = std::sin(x) * std::cos(y);
        }
    }
    
    // Create surface plot
    ax.surface(x_grid, y_grid, z_values)
        .color(colors::cyan);
    
    ax.auto_fit();
    
    ax.title("Surface: sin(x) * cos(y)");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.zlabel("Z");
    
    // Set camera for good viewing angle
    ax.camera().azimuth = 135.0f;
    ax.camera().elevation = 35.0f;
    ax.camera().distance = 6.0f;
    
    app.run();
    return 0;
}

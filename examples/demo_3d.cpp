#include <cmath>
#include <iostream>
#include <spectra/spectra.hpp>
#include <vector>

using namespace spectra;

// Global animation state
struct AnimationState
{
    float time = 0.0f;
    bool animate = true;
} g_anim;

int main()
{
    App app;
    auto& fig = app.figure({.width = 1600, .height = 1200});

    // --- Subplot 1: 3D Scatter Plot (Spiral) ---
    auto& ax1 = fig.subplot3d(2, 2, 1);

    std::vector<float> x1, y1, z1;
    for (int i = 0; i < 500; ++i)
    {
        float t = static_cast<float>(i) * 0.02f;
        x1.push_back(std::cos(t) * t);
        y1.push_back(std::sin(t) * t);
        z1.push_back(t);
    }

    ax1.scatter3d(x1, y1, z1).color(colors::cyan).size(3.0f).label("Spiral Points");

    ax1.auto_fit();

    ax1.title("3D Scatter: Spiral (Animated)");
    ax1.xlabel("X");
    ax1.ylabel("Y");
    ax1.zlabel("Z");
    ax1.camera().set_azimuth(45.0f).set_elevation(30.0f);
    ax1.grid_planes(Axes3D::GridPlane::All);

    // --- Subplot 2: 3D Line Plot (Helix) ---
    auto& ax2 = fig.subplot3d(2, 2, 2);

    std::vector<float> x2, y2, z2;
    for (int i = 0; i < 300; ++i)
    {
        float t = static_cast<float>(i) * 0.05f;
        x2.push_back(std::cos(t) * 2.0f);
        y2.push_back(std::sin(t) * 2.0f);
        z2.push_back(t * 0.2f);
    }

    ax2.line3d(x2, y2, z2).color(colors::magenta).width(3.0f).label("Helix Curve");

    ax2.auto_fit();

    ax2.title("3D Line: Helix");
    ax2.xlabel("X");
    ax2.ylabel("Y");
    ax2.zlabel("Z");
    ax2.camera().set_azimuth(60.0f).set_elevation(20.0f);
    ax2.grid_planes(Axes3D::GridPlane::All);

    // --- Subplot 3: Surface Plot (Mathematical Function) ---
    auto& ax3 = fig.subplot3d(2, 2, 3);

    const int nx = 50, ny = 50;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);

    for (int i = 0; i < nx; ++i)
    {
        x_grid[i] = static_cast<float>(i) / (nx - 1) * 6.0f - 3.0f;
    }
    for (int j = 0; j < ny; ++j)
    {
        y_grid[j] = static_cast<float>(j) / (ny - 1) * 6.0f - 3.0f;
    }

    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            float x = x_grid[i];
            float y = y_grid[j];
            float r = std::sqrt(x * x + y * y);
            z_values[j * nx + i] = std::sin(r) * std::cos(x * 0.5f) / (r + 0.5f);
        }
    }

    ax3.surface(x_grid, y_grid, z_values).color(colors::orange).label("Surface");

    ax3.auto_fit();

    ax3.title("Surface: sin(r)*cos(x/2)/(r+0.5)");
    ax3.xlabel("X");
    ax3.ylabel("Y");
    ax3.zlabel("Z");
    ax3.camera().set_azimuth(30.0f).set_elevation(45.0f);
    ax3.grid_planes(Axes3D::GridPlane::All);
    ax3.light_dir({1.0f, 1.0f, 2.0f});

    // --- Subplot 4: Multiple 3D Series (Combined) ---
    auto& ax4 = fig.subplot3d(2, 2, 4);

    // Scatter points at vertices of a cube
    std::vector<float> x_cube = {-1, 1, -1, 1, -1, 1, -1, 1};
    std::vector<float> y_cube = {-1, -1, 1, 1, -1, -1, 1, 1};
    std::vector<float> z_cube = {-1, -1, -1, -1, 1, 1, 1, 1};

    ax4.scatter3d(x_cube, y_cube, z_cube).color(colors::red).size(8.0f).label("Cube Vertices");

    // Line connecting some vertices
    std::vector<float> x_line = {-1, 1, 1, -1, -1};
    std::vector<float> y_line = {-1, -1, 1, 1, -1};
    std::vector<float> z_line = {-1, -1, -1, -1, -1};

    ax4.line3d(x_line, y_line, z_line).color(colors::blue).width(2.0f).label("Base Square");

    // Another line for top square
    std::vector<float> x_line2 = {-1, 1, 1, -1, -1};
    std::vector<float> y_line2 = {-1, -1, 1, 1, -1};
    std::vector<float> z_line2 = {1, 1, 1, 1, 1};

    ax4.line3d(x_line2, y_line2, z_line2).color(colors::green).width(2.0f).label("Top Square");

    ax4.auto_fit();

    ax4.title("Combined: Cube Wireframe");
    ax4.xlabel("X");
    ax4.ylabel("Y");
    ax4.zlabel("Z");
    ax4.camera().set_azimuth(45.0f).set_elevation(30.0f);
    ax4.grid_planes(Axes3D::GridPlane::All);
    fig.show();

    app.run();
    return 0;
}

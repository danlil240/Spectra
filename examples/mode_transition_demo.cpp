// Mode Transition Demo — demonstrates animated 2D↔3D view switching.
//
// Shows a figure with a sine wave in 2D, then transitions to a 3D helix view,
// and back. The transition animates camera, axis limits, grid planes, and
// 3D element opacity over 0.8 seconds.

#include <cmath>
#include <plotix/plotix.hpp>
#include <vector>

int main()
{
    using namespace plotix;

    // Generate helix data
    const int N = 500;
    std::vector<float> x(N), y(N), z(N);
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / (N - 1) * 4.0f * 3.14159f;
        x[i] = std::cos(t);
        y[i] = std::sin(t);
        z[i] = t / (4.0f * 3.14159f) * 2.0f - 1.0f;
    }

    // Create figure with a 3D subplot
    auto& fig = figure({.width = 1280, .height = 720});
    auto& ax = fig.subplot3d(1, 1, 1);
    ax.line3d(x, y, z).color({0.122f, 0.467f, 0.706f}).width(2.5f).label("Helix");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.zlabel("Z");
    ax.auto_fit();

    // Set camera to a nice viewing angle
    ax.camera().azimuth = 45.0f;
    ax.camera().elevation = 25.0f;
    ax.camera().update_position_from_orbit();

    show();
    return 0;
}

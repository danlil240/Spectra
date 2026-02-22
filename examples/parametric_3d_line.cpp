#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

using namespace spectra;

int main()
{
    App   app;
    auto& fig = app.figure({.width = 800, .height = 600});

    auto& ax = fig.subplot3d(1, 1, 1);

    // Generate a parametric 3D curve (trefoil knot)
    std::vector<float> x, y, z;
    for (int i = 0; i < 500; ++i)
    {
        float t = static_cast<float>(i) / 500.0f * 2.0f * M_PI;
        x.push_back(std::sin(t) + 2.0f * std::sin(2.0f * t));
        y.push_back(std::cos(t) - 2.0f * std::cos(2.0f * t));
        z.push_back(-std::sin(3.0f * t));
    }

    // Plot as 3D line
    ax.line3d(x, y, z).color(colors::red).width(2.0f);

    // Auto-fit axes to data bounds
    ax.auto_fit();

    // Set labels and title
    ax.title("3D Parametric Line - Trefoil Knot");
    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");
    ax.zlabel("Z Axis");

    // Configure camera
    ax.camera().set_azimuth(45.0f).set_elevation(30.0f).set_distance(8.0f);

    // Show all grid planes
    ax.grid_planes(Axes3D::GridPlane::All);

    app.run();
    return 0;
}

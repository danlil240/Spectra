#include <cmath>
#include <plotix/plotix.hpp>
#include <vector>

using namespace plotix;

int main()
{
    App app;
    auto& fig = app.figure({.width = 800, .height = 600});

    // Create a 3D axes
    auto& ax = fig.subplot3d(1, 1, 1);

    // Generate a 3D spiral
    std::vector<float> x, y, z;
    for (int i = 0; i < 200; ++i)
    {
        float t = static_cast<float>(i) * 0.1f;
        x.push_back(std::cos(t));
        y.push_back(std::sin(t));
        z.push_back(t * 0.1f);
    }

    // Plot as 3D scatter
    ax.scatter3d(x, y, z).color(colors::blue).size(4.0f);

    // Auto-fit axes to data bounds
    ax.auto_fit();

    // Set labels and title
    ax.title("3D Spiral Scatter Plot");
    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");
    ax.zlabel("Z Axis");

    // Configure camera (auto_fit already positions camera appropriately)
    ax.camera().azimuth = 45.0f;
    ax.camera().elevation = 30.0f;

    app.run();
    return 0;
}

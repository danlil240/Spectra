#include <plotix/app.hpp>
#include <plotix/axes3d.hpp>
#include <plotix/figure.hpp>
#include <plotix/series3d.hpp>

#include <cmath>
#include <vector>

int main() {
    using namespace plotix;

    AppConfig config;
    App app(config);

    auto& fig = app.figure();

    auto& ax = fig.subplot3d(1, 1, 1);
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.zlabel("Z");

    // Configure lighting
    ax.set_light_dir(0.5f, 0.8f, 1.0f);
    ax.set_lighting_enabled(true);

    // Generate surface data: z = sin(sqrt(x² + y²))
    const int N = 80;
    std::vector<float> x_grid(N), y_grid(N);
    std::vector<float> z_values(N * N);

    for (int i = 0; i < N; ++i) {
        x_grid[i] = -4.0f + 8.0f * static_cast<float>(i) / (N - 1);
        y_grid[i] = -4.0f + 8.0f * static_cast<float>(i) / (N - 1);
    }

    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float r = std::sqrt(x_grid[i] * x_grid[i] + y_grid[j] * y_grid[j]);
            z_values[j * N + i] = std::sin(r) * 2.0f;
        }
    }

    // Opaque surface with custom material
    auto& surf1 = ax.surface(x_grid, y_grid, z_values);
    surf1.color(Color{0.2f, 0.6f, 1.0f, 1.0f})
         .ambient(0.15f)
         .specular(0.4f)
         .shininess(48.0f)
         .colormap(ColormapType::Viridis);

    // Second surface (shifted up, semi-transparent)
    std::vector<float> z_values2(N * N);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float r = std::sqrt(x_grid[i] * x_grid[i] + y_grid[j] * y_grid[j]);
            z_values2[j * N + i] = std::cos(r) * 1.5f + 2.0f;
        }
    }

    auto& surf2 = ax.surface(x_grid, y_grid, z_values2);
    surf2.color(Color{1.0f, 0.3f, 0.2f, 0.6f})  // Semi-transparent
         .ambient(0.2f)
         .specular(0.6f)
         .shininess(64.0f);

    ax.xlim(-4.0f, 4.0f);
    ax.ylim(-4.0f, 4.0f);
    ax.zlim(-3.0f, 5.0f);

    app.run();
    return 0;
}

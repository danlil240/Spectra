// Comprehensive Subplot Demo
//
// This example demonstrates 4 different types of subplots in a single figure:
// 1. 3D Surface Plot - Mathematical surface with ripple pattern
// 2. Animated Scatter Plot - Real-time particle animation
// 3. Multiple 2D Graphs - Multiple functions with different styles
// 4. Statistical Plots - Histogram with statistical markers
//
// Usage:
//   ./comprehensive_subplot_demo
//
// The animation will run automatically. Close the window to exit.

#include <cmath>
#include <random>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    spectra::figure(1600, 1200);

    // === Subplot 1: 3D Surface Plot ===
    spectra::subplot3d(2, 2, 1);

    const int          nx = 40, ny = 40;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);

    // Create grid
    for (int i = 0; i < nx; ++i)
    {
        x_grid[i] = static_cast<float>(i) / (nx - 1) * 8.0f - 4.0f;
    }
    for (int j = 0; j < ny; ++j)
    {
        y_grid[j] = static_cast<float>(j) / (ny - 1) * 8.0f - 4.0f;
    }

    // Generate interesting surface: ripple pattern
    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            float x              = x_grid[i];
            float y              = y_grid[j];
            float r              = std::sqrt(x * x + y * y);
            z_values[j * nx + i] = std::sin(r * 2.0f) * std::exp(-r * 0.3f)
                                   + 0.3f * std::cos(x * 3.0f) * std::sin(y * 3.0f);
        }
    }

    spectra::surf(x_grid, y_grid, z_values);
    spectra::title("3D Surface: Ripple Pattern");
    spectra::xlabel("X");
    spectra::ylabel("Y");
    spectra::zlabel("Z");

    // === Subplot 2: Animated Scatter Plot ===
    spectra::subplot(2, 2, 2);

    constexpr size_t   N = 150;
    std::vector<float> x_anim(N), y_anim(N);

    // Initialize particles in a circle
    for (size_t i = 0; i < N; ++i)
    {
        float angle = static_cast<float>(i) / static_cast<float>(N) * 6.2832f;
        x_anim[i]   = std::cos(angle) * 0.8f;
        y_anim[i]   = std::sin(angle) * 0.8f;
    }

    auto& scatter_series =
        spectra::scatter(x_anim, y_anim).color(spectra::rgb(1.0f, 0.65f, 0.0f)).label("Particles");

    spectra::title("Animated Particle System");
    spectra::xlabel("X");
    spectra::ylabel("Y");
    spectra::xlim(-1.5f, 1.5f);
    spectra::ylim(-1.5f, 1.5f);
    spectra::grid(true);
    spectra::legend();

    // === Subplot 3: Multiple 2D Graphs ===
    spectra::subplot(2, 2, 3);

    // Generate data for multiple functions
    constexpr size_t   M = 200;
    std::vector<float> x_multi(M);
    std::vector<float> y1(M), y2(M), y3(M), y4(M);

    for (size_t i = 0; i < M; ++i)
    {
        x_multi[i] = static_cast<float>(i) * 0.05f;
        float x    = x_multi[i];

        y1[i] = std::sin(x) * std::exp(-x * 0.1f);   // Damped sine
        y2[i] = std::cos(x * 2.0f) * 0.7f;           // Cosine with different frequency
        y3[i] = std::sin(x * 0.5f) * 1.2f;           // Low frequency sine
        y4[i] =
            0.3f * std::sin(x * 5.0f) + 0.2f * std::cos(x * 3.0f);   // High frequency components
    }

    spectra::plot(x_multi, y1, "r-").label("Damped Sine");
    spectra::plot(x_multi, y2, "g--").label("Cosine (2x)");
    spectra::plot(x_multi, y3, "b:").label("Sine (0.5x)");
    spectra::plot(x_multi, y4, "m-.").label("Mixed Freq");

    spectra::title("Multiple 2D Functions");
    spectra::xlabel("Time (s)");
    spectra::ylabel("Amplitude");
    spectra::xlim(0.0f, 10.0f);
    spectra::ylim(-2.0f, 2.0f);
    spectra::grid(true);
    spectra::legend();

    // === Subplot 4: Statistical Plots ===
    spectra::subplot(2, 2, 4);

    // Generate statistical data
    std::mt19937                          rng(123);
    std::normal_distribution<float>       normal_dist(50.0f, 15.0f);
    std::uniform_real_distribution<float> uniform_dist(20.0f, 80.0f);
    std::exponential_distribution<float>  exp_dist(1.0f / 25.0f);

    constexpr size_t   S = 400;
    std::vector<float> normal_data(S), uniform_data(S), exp_data(S);

    for (size_t i = 0; i < S; ++i)
    {
        normal_data[i]  = normal_dist(rng);
        uniform_data[i] = uniform_dist(rng);
        exp_data[i]     = exp_dist(rng);
    }

    // Create histogram for normal distribution
    spectra::histogram(normal_data, 30)
        .color(spectra::rgb(0.3f, 0.6f, 1.0f))
        .label("Normal Distribution");

    // Add scatter points for statistics
    std::vector<float> box_x = {50.0f};
    std::vector<float> box_y = {50.0f};   // Mean
    spectra::scatter(box_x, box_y).color(spectra::colors::red).label("Mean");

    // Add percentile lines
    std::vector<float> percentile_x = {35.0f, 65.0f};
    std::vector<float> percentile_y = {35.0f, 65.0f};   // Approximate 25th and 75th percentiles
    spectra::scatter(percentile_x, percentile_y).color(spectra::colors::orange).label("Quartiles");

    spectra::title("Statistical Analysis");
    spectra::xlabel("Value");
    spectra::ylabel("Frequency / Statistics");
    spectra::xlim(0.0f, 100.0f);
    spectra::grid(true);
    spectra::legend();

    // Set up animation
    spectra::on_update(
        [&](float /* dt */, float t)
        {
            // Update animated scatter plot
            for (size_t i = 0; i < N; ++i)
            {
                float base_angle = static_cast<float>(i) / static_cast<float>(N) * 6.2832f;
                float radius     = 0.8f + 0.4f * std::sin(t * 2.0f + base_angle * 3.0f);
                float angle      = base_angle + t * 0.5f;

                x_anim[i] = radius * std::cos(angle);
                y_anim[i] = radius * std::sin(angle);
            }
            scatter_series.set_x(x_anim);
            scatter_series.set_y(y_anim);
        });

    spectra::show();

    return 0;
}

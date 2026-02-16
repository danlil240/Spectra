#include <cmath>
#include <iostream>
#include <plotix/plotix.hpp>
#include <vector>

using namespace plotix;

// Helper function for rainbow color animation
Color get_animated_color(float t)
{
    float hue = t * 360.0f;  // Rotate through full spectrum

    // Simple HSV to RGB conversion for rainbow effect
    float h = hue / 60.0f;
    float c = 1.0f;
    float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));

    float r, g, b;
    if (h < 1.0f)
    {
        r = c;
        g = x;
        b = 0;
    }
    else if (h < 2.0f)
    {
        r = x;
        g = c;
        b = 0;
    }
    else if (h < 3.0f)
    {
        r = 0;
        g = c;
        b = x;
    }
    else if (h < 4.0f)
    {
        r = 0;
        g = x;
        b = c;
    }
    else if (h < 5.0f)
    {
        r = x;
        g = 0;
        b = c;
    }
    else
    {
        r = c;
        g = 0;
        b = x;
    }

    return Color{r, g, b, 1.0f};
}

// This example shows how the CameraAnimator would be used if exposed publicly
// For now, we demonstrate the same functionality using the camera API directly

int main()
{
    App app;
    auto& fig = app.figure({.width = 1600, .height = 900});

    // Create a single 3D axes for the animation demo
    auto& ax = fig.subplot3d(1, 1, 1);

    // Create a 3D spiral scatter plot (will be animated)
    std::vector<float> x, y, z;
    x.resize(200);
    y.resize(200);
    z.resize(200);

    // Initial spiral parameters
    float base_amplitude = 0.3f;
    float base_frequency = 2.0f;
    float base_z_scale = 0.2f;

    auto& series = ax.scatter3d(x, y, z).color(colors::cyan).size(4.0f).label("Animated Spiral");

    ax.auto_fit();
    ax.grid_planes(Axes3D::GridPlane::All);
    ax.title("Camera Animation Demo (Using Camera API)");
    ax.xlabel("X");
    ax.ylabel("Y");
    ax.zlabel("Z");

    // Set initial camera position
    ax.camera().set_azimuth(0.0f).set_elevation(30.0f).set_distance(15.0f);

    std::cout << "\n=== Dual Animation Demo ===\n";
    std::cout << "Both the spiral plot AND camera are animated!\n";
    std::cout << "This demonstrates the animation capabilities of Plotix.\n\n";
    std::cout << "Camera Animation (simulating CameraAnimator::Orbit mode):\n";
    std::cout << "  - Keyframe 1 (0s): azimuth=0°, elevation=30°, distance=15\n";
    std::cout << "  - Keyframe 2 (3s): azimuth=180°, elevation=60°, distance=10\n";
    std::cout << "  - Keyframe 3 (6s): azimuth=360°, elevation=30°, distance=15\n";
    std::cout << "  - Interpolation: Linear for azimuth, EaseInOut for elevation/distance\n";
    std::cout << "\nPlot Animation:\n";
    std::cout << "  - Spiral amplitude: 0.3 → 0.5 → 0.3 over 4 seconds\n";
    std::cout << "  - Spiral frequency: 2 → 4 → 2 over 3 seconds\n";
    std::cout << "  - Z-height scale: 0.2 → 0.4 → 0.2 over 5 seconds\n";
    std::cout << "  - Color phase shift for rainbow effect\n";
    std::cout << "\n";

    fig.show();

    // Animation state
    // float time = 0.0f;  // Currently unused

    // Use the figure's animation system
    fig.animate()
        .fps(60.0f)
        .duration(6.0f)
        .loop(true)
        .on_frame(
            [&ax, &x, &y, &z, &series, base_amplitude, base_frequency, base_z_scale](Frame& frame)
            {
                float time = std::fmod(frame.elapsed_sec, 6.0f);  // Loop every 6 seconds
                float t = time / 6.0f;                            // Normalize to [0, 1]

                // === Animate Plot Data ===
                float amp_phase = t * 2.0f * M_PI;
                float amp = base_amplitude + 0.2f * std::sin(amp_phase);

                float freq_phase = t * 2.0f * M_PI;
                float freq = base_frequency + 2.0f * std::sin(freq_phase);

                float z_phase = t * (6.0f / 5.0f) * 2.0f * M_PI;
                float z_scale = base_z_scale + 0.2f * std::sin(z_phase);

                // Regenerate spiral points with new parameters
                for (int i = 0; i < 200; ++i)
                {
                    float point_t = static_cast<float>(i) * 0.05f;
                    x[i] = std::cos(point_t * freq) * point_t * amp;
                    y[i] = std::sin(point_t * freq) * point_t * amp;
                    z[i] = point_t * z_scale;
                }

                // Update series data in-place (safe for Vulkan)
                series.set_x(x).set_y(y).set_z(z).color(get_animated_color(t));

                // === Animate Camera ===
                // Simulate CameraAnimator with 3 keyframes
                Camera result;

                if (time <= 3.0f)
                {
                    // Interpolate between keyframe 1 and 2
                    float cam_t = time / 3.0f;  // [0, 1]

                    // Azimuth: linear interpolation 0° → 180°
                    result.azimuth = cam_t * 180.0f;

                    // Elevation: ease in-out 30° → 60°
                    float ease_t = cam_t * cam_t * (3.0f - 2.0f * cam_t);  // Smoothstep
                    result.elevation = 30.0f + ease_t * 30.0f;

                    // Distance: ease in-out 15 → 10
                    result.distance = 15.0f - ease_t * 5.0f;
                }
                else
                {
                    // Interpolate between keyframe 2 and 3
                    float cam_t = (time - 3.0f) / 3.0f;  // [0, 1]

                    // Azimuth: linear interpolation 180° → 360°
                    result.azimuth = 180.0f + cam_t * 180.0f;

                    // Elevation: ease in-out 60° → 30°
                    float ease_t = cam_t * cam_t * (3.0f - 2.0f * cam_t);  // Smoothstep
                    result.elevation = 60.0f - ease_t * 30.0f;

                    // Distance: ease in-out 10 → 15
                    result.distance = 10.0f + ease_t * 5.0f;
                }

                // Apply interpolated camera state
                ax.camera().azimuth = result.azimuth;
                ax.camera().elevation = result.elevation;
                ax.camera().distance = result.distance;
                ax.camera().fov = 45.0f;  // Fixed FOV
                ax.camera().update_position_from_orbit();
            })
        .play();

    app.run();

    return 0;
}

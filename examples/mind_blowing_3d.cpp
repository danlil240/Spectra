#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <random>
#include <spectra/spectra.hpp>
#include <vector>

using namespace spectra;

// Helper functions for mind-blowing effects
Color get_spectral_color(float t, float intensity = 1.0f)
{
    float hue = t * 360.0f;
    float h   = hue / 60.0f;
    float c   = intensity;
    float x   = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));

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

    return Color{r, g, b, 0.8f};
}

Color get_plasma_color(float t)
{
    float r = 0.5f + 0.5f * std::sin(t * 6.28318f + 0.0f);
    float g = 0.5f + 0.5f * std::sin(t * 6.28318f + 2.094f);
    float b = 0.5f + 0.5f * std::sin(t * 6.28318f + 4.189f);
    return Color{r, g, b, 0.9f};
}

struct ParticleSystem
{
    std::vector<float> px, py, pz;
    std::vector<float> vx, vy, vz;
    std::vector<Color> colors;
    std::mt19937       rng;

    ParticleSystem(int count) : rng(42)
    {
        px.resize(count);
        py.resize(count);
        pz.resize(count);
        vx.resize(count);
        vy.resize(count);
        vz.resize(count);
        colors.resize(count);

        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (int i = 0; i < count; ++i)
        {
            px[i]     = dist(rng) * 2.0f;
            py[i]     = dist(rng) * 2.0f;
            pz[i]     = dist(rng) * 2.0f;
            vx[i]     = dist(rng) * 0.1f;
            vy[i]     = dist(rng) * 0.1f;
            vz[i]     = dist(rng) * 0.1f;
            colors[i] = get_spectral_color(dist(rng) * 0.5f + 0.5f, 0.6f);
        }
    }

    void update(float dt, float time)
    {
        for (size_t i = 0; i < px.size(); ++i)
        {
            // Chaotic motion with time-varying forces
            float fx = std::sin(time * 2.0f + i * 0.1f) * 0.05f;
            float fy = std::cos(time * 1.5f + i * 0.15f) * 0.05f;
            float fz = std::sin(time * 3.0f + i * 0.2f) * 0.05f;

            vx[i] += fx * dt;
            vy[i] += fy * dt;
            vz[i] += fz * dt;

            // Damping
            vx[i] *= 0.98f;
            vy[i] *= 0.98f;
            vz[i] *= 0.98f;

            // Update positions
            px[i] += vx[i] * dt;
            py[i] += vy[i] * dt;
            pz[i] += vz[i] * dt;

            // Wrap around boundaries
            if (std::abs(px[i]) > 3.0f)
                px[i] = -px[i];
            if (std::abs(py[i]) > 3.0f)
                py[i] = -py[i];
            if (std::abs(pz[i]) > 3.0f)
                pz[i] = -pz[i];

            // Update colors
            colors[i] = get_plasma_color(time + i * 0.01f);
        }
    }
};

int main()
{
    App   app;
    auto& fig = app.figure({.width = 1920, .height = 1080});

    // Create the main 3D axes
    auto& ax = fig.subplot3d(1, 1, 1);

    // Initialize data structures
    const int SPIRAL_POINTS  = 500;
    const int PARTICLE_COUNT = 200;

    std::vector<float> x1(SPIRAL_POINTS), y1(SPIRAL_POINTS), z1(SPIRAL_POINTS);
    std::vector<float> x2(SPIRAL_POINTS), y2(SPIRAL_POINTS), z2(SPIRAL_POINTS);
    std::vector<float> x3(SPIRAL_POINTS), y3(SPIRAL_POINTS), z3(SPIRAL_POINTS);

    ParticleSystem particles(PARTICLE_COUNT);

    // Setup axes
    ax.auto_fit();
    ax.grid_planes(Axes3D::GridPlane::All);
    ax.title("MIND-BLOWING 3D ANIMATION");
    ax.xlabel("X Dimension");
    ax.ylabel("Y Dimension");
    ax.zlabel("Z Dimension");

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         🚀 PREPARE FOR VISUAL OVERLOAD! 🚀                    ║\n";
    std::cout << "║                                                              ║\n";
    std::cout << "║  This animation features:                                    ║\n";
    std::cout << "║  • 3 Interlocking DNA Helix Spirals                          ║\n";
    std::cout << "║  • 200 Chaotic Plasma Particles                              ║\n";
    std::cout << "║  • Dynamic Color Shifting (Spectral + Plasma)                ║\n";
    std::cout << "║  • Insane Camera Movement (Roll + Pitch + Yaw)               ║\n";
    std::cout << "║  • Morphing Geometric Parameters                             ║\n";
    std::cout << "║  • 60 FPS Smooth Animation                                   ║\n";
    std::cout << "║                                                              ║\n";
    std::cout << "║  Hold onto your seat! This is going to be EPIC!              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    fig.show();

    // EPIC ANIMATION LOOP
    fig.animate()
        .fps(60.0f)
        .duration(10.0f)
        .loop(true)
        .on_frame(
            [&](Frame& frame)
            {
                float time = std::fmod(frame.elapsed_sec, 10.0f);
                float t    = time / 10.0f;   // Normalize to [0, 1]
                float dt   = frame.dt;

                // === GENERATE MIND-BLOWING SPIRALS ===

                // Spiral 1: Primary DNA Helix
                float amp1   = 0.8f + 0.4f * std::sin(t * 4.0f * M_PI);
                float freq1  = 3.0f + 2.0f * std::sin(t * 3.0f * M_PI);
                float phase1 = t * 6.0f * M_PI;

                for (int i = 0; i < SPIRAL_POINTS; ++i)
                {
                    float point_t = static_cast<float>(i) * 0.02f;
                    float angle   = point_t * freq1 + phase1;
                    x1[i]         = std::cos(angle) * amp1 * point_t;
                    y1[i]         = std::sin(angle) * amp1 * point_t;
                    z1[i]         = point_t * 0.3f + 0.2f * std::sin(point_t * 2.0f + time * 2.0f);
                }

                // Spiral 2: Interlocking Helix (offset)
                float amp2   = 0.7f + 0.3f * std::cos(t * 5.0f * M_PI);
                float freq2  = 2.5f + 1.5f * std::cos(t * 2.0f * M_PI);
                float phase2 = t * 4.0f * M_PI + M_PI / 3.0f;

                for (int i = 0; i < SPIRAL_POINTS; ++i)
                {
                    float point_t = static_cast<float>(i) * 0.02f;
                    float angle   = point_t * freq2 + phase2;
                    x2[i]         = std::cos(angle) * amp2 * point_t * 1.2f;
                    y2[i]         = std::sin(angle) * amp2 * point_t * 1.2f;
                    z2[i] = point_t * 0.25f + 0.15f * std::cos(point_t * 3.0f + time * 3.0f);
                }

                // Spiral 3: Wild Helix
                float amp3   = 0.6f + 0.5f * std::sin(t * 6.0f * M_PI);
                float freq3  = 4.0f + 3.0f * std::sin(t * 4.0f * M_PI);
                float phase3 = t * 8.0f * M_PI + 2.0f * M_PI / 3.0f;

                for (int i = 0; i < SPIRAL_POINTS; ++i)
                {
                    float point_t = static_cast<float>(i) * 0.02f;
                    float angle   = point_t * freq3 + phase3;
                    x3[i]         = std::cos(angle) * amp3 * point_t * 0.8f;
                    y3[i]         = std::sin(angle) * amp3 * point_t * 0.8f;
                    z3[i] = point_t * 0.35f + 0.25f * std::sin(point_t * 4.0f - time * 2.0f);
                }

                // Update particle system
                particles.update(dt, time);

                // === UPDATE SERIES ===
                // clear_series() safely defers GPU resource cleanup — no Vulkan crash.
                ax.clear_series();

                ax.scatter3d(x1, y1, z1)
                    .color(get_spectral_color(t, 0.9f))
                    .size(3.0f)
                    .label("Helix Alpha");

                ax.scatter3d(x2, y2, z2)
                    .color(get_spectral_color(t + 0.33f, 0.9f))
                    .size(3.0f)
                    .label("Helix Beta");

                ax.scatter3d(x3, y3, z3)
                    .color(get_spectral_color(t + 0.67f, 0.9f))
                    .size(3.0f)
                    .label("Helix Gamma");

                ax.scatter3d(particles.px, particles.py, particles.pz)
                    .color(get_plasma_color(time))
                    .size(2.0f)
                    .label("Plasma Cloud");

                // === INSANE CAMERA MOVEMENT ===
                float cam_phase     = t * 2.0f * M_PI;
                float cam_radius    = 8.0f + 3.0f * std::sin(t * 3.0f * M_PI);
                ax.camera().azimuth = cam_phase * 180.0f / M_PI + 90.0f * std::sin(t * 4.0f * M_PI);
                ax.camera().elevation =
                    45.0f + 30.0f * std::sin(t * 2.0f * M_PI) + 15.0f * std::cos(t * 5.0f * M_PI);
                ax.camera().distance = cam_radius;
                ax.camera().fov      = 60.0f + 20.0f * std::sin(t * 6.0f * M_PI);
                ax.camera().update_position_from_orbit();
            })
        .play();

    app.run();

    return 0;
}

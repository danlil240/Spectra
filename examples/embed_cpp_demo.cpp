// embed_cpp_demo.cpp — C++ embedding example
//
// Demonstrates using the Spectra C++ embedding API to render a plot
// to a PNG file. When built with SPECTRA_USE_IMGUI, the output includes
// the full Spectra UI chrome (command bar, status bar, crosshair, legend).
//
// Build:
//   cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..
//   make embed_cpp_demo
//
// Run:
//   ./embed_cpp_demo          # writes embed_output.png

#include <spectra/embed.hpp>
#include <spectra/export.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main()
{
    // ── Create embed surface ────────────────────────────────────────────
    spectra::EmbedConfig config;
    config.width  = 1280;
    config.height = 720;
    config.theme  = "dark";

    // UI chrome options (effective when built with SPECTRA_USE_IMGUI)
    config.show_command_bar = true;
    config.show_status_bar  = true;
    config.show_nav_rail    = true;
    config.show_inspector   = false;

    spectra::EmbedSurface surface(config);
    if (!surface.is_valid())
    {
        std::fprintf(stderr, "Failed to create EmbedSurface (Vulkan available?)\n");
        return 1;
    }

    // ── Create figure and axes ──────────────────────────────────────────
    auto& fig = surface.figure();
    auto& ax  = fig.subplot(1, 1, 1);

    // Generate sample data: sine + cosine
    constexpr int N = 200;
    std::vector<float> x(N), y_sin(N), y_cos(N), y_damped(N);
    for (int i = 0; i < N; ++i)
    {
        float t     = static_cast<float>(i) / (N - 1) * 4.0f * static_cast<float>(M_PI);
        x[i]        = t;
        y_sin[i]    = std::sin(t);
        y_cos[i]    = std::cos(t);
        y_damped[i] = std::exp(-0.2f * t) * std::sin(3.0f * t);
    }

    // Add series
    ax.line(x, y_sin).label("sin(t)");
    ax.line(x, y_cos).label("cos(t)");
    ax.line(x, y_damped).label("damped");

    // Configure axes
    ax.xlabel("Time (s)");
    ax.ylabel("Amplitude");
    ax.title("Spectra Embed — C++ Demo");
    ax.grid(true);

    // ── Render to buffer ────────────────────────────────────────────────
    std::vector<uint8_t> pixels(config.width * config.height * 4);
    if (!surface.render_to_buffer(pixels.data()))
    {
        std::fprintf(stderr, "render_to_buffer failed\n");
        return 1;
    }

    // ── Save to PNG ─────────────────────────────────────────────────────
    const char* output_path = "embed_output.png";
    if (!spectra::ImageExporter::write_png(output_path, pixels.data(), config.width, config.height))
    {
        std::fprintf(stderr, "Failed to write %s\n", output_path);
        return 1;
    }

    std::printf("Saved %ux%u plot to %s\n", config.width, config.height, output_path);
    return 0;
}

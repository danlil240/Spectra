// webgpu_demo.cpp — WebGPU backend usage example.
//
// Demonstrates how to use the Spectra WebGPU backend for:
//   1. Native desktop rendering via Dawn
//   2. Browser-based rendering via Emscripten/wasm
//
// The example creates a simple line + scatter plot and renders it either
// to an on-screen GLFW window (Dawn native) or to an HTML5 canvas
// (Emscripten).  The same C++ code compiles to both targets.
//
// ── Build (native with Dawn) ─────────────────────────────────────────────────
//
//   cmake -B build -DSPECTRA_USE_WEBGPU=ON \
//         -Ddawn_DIR=/path/to/dawn/install/lib/cmake/dawn
//   cmake --build build --target webgpu_demo
//   ./build/examples/webgpu_demo
//
// ── Build (Emscripten / browser) ─────────────────────────────────────────────
//
//   source /path/to/emsdk/emsdk_env.sh
//   emcmake cmake -B build-wasm -DSPECTRA_USE_WEBGPU=ON \
//           -DSPECTRA_USE_GLFW=ON -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-wasm --target webgpu_demo
//   # Open build-wasm/examples/webgpu_demo.html in a WebGPU-capable browser

#include <cmath>
#include <cstdio>
#include <vector>

#include <spectra/spectra.hpp>

// ─── Platform-specific main loop ─────────────────────────────────────────────

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>

namespace
{
spectra::App* g_app = nullptr;

void em_main_loop()
{
    if (!g_app)
        return;

    auto result = g_app->step();
    if (result.should_exit)
        emscripten_cancel_main_loop();
}
}   // namespace
#endif   // __EMSCRIPTEN__

// ─── Data generation ─────────────────────────────────────────────────────────

static constexpr float PI = 3.14159265f;

static void generate_demo_data(std::vector<float>& x,
                               std::vector<float>& y_sin,
                               std::vector<float>& y_cos,
                               std::vector<float>& y_damped,
                               int                 n = 200)
{
    x.resize(n);
    y_sin.resize(n);
    y_cos.resize(n);
    y_damped.resize(n);

    for (int i = 0; i < n; ++i)
    {
        float t     = static_cast<float>(i) / static_cast<float>(n - 1) * 4.0f * PI;
        x[i]        = t;
        y_sin[i]    = std::sin(t);
        y_cos[i]    = std::cos(t);
        y_damped[i] = std::exp(-0.15f * t) * std::sin(2.0f * t);
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::printf("Spectra WebGPU Demo\n");
    std::printf("===================\n\n");

    // ── Generate sample data ────────────────────────────────────────────
    std::vector<float> x, y_sin, y_cos, y_damped;
    generate_demo_data(x, y_sin, y_cos, y_damped);

    // ── Create the application ──────────────────────────────────────────
    // The same App and Figure API works regardless of the rendering backend.
    // When SPECTRA_USE_WEBGPU is enabled and no Vulkan driver is found,
    // the library can fall back to the WebGPU backend automatically.
    //
    // On Emscripten, the Vulkan backend is not available, so the WebGPU
    // backend is the only option.  On native Linux/macOS/Windows, you can
    // choose either Vulkan or WebGPU (via Dawn) at build time.

    spectra::AppConfig config;
    config.headless = false;
    config.backend  = spectra::RenderBackend::WebGPU;
#ifdef __EMSCRIPTEN__
    // Emscripten: no sockets, always inproc.
    config.socket_path = "";
#endif

    spectra::App app(config);

    // ── Figure 1: Line plot ─────────────────────────────────────────────
    auto& fig1 = app.figure({.width = 800, .height = 600});
    auto& ax1  = fig1.subplot(1, 1, 1);

    ax1.line(x, y_sin).label("sin(t)").color(spectra::rgb(0.3f, 0.6f, 1.0f));
    ax1.line(x, y_cos).label("cos(t)").color(spectra::rgb(1.0f, 0.4f, 0.4f));
    ax1.line(x, y_damped).label("damped").color(spectra::rgb(0.4f, 1.0f, 0.5f));

    ax1.title("Spectra — WebGPU Backend");
    ax1.xlabel("Time (s)");
    ax1.ylabel("Amplitude");
    ax1.grid(true);

    // ── Figure 2: Scatter plot ──────────────────────────────────────────
    auto& fig2 = app.figure({.width = 640, .height = 480});
    auto& ax2  = fig2.subplot(1, 1, 1);

    // Lissajous curve as scatter data
    std::vector<float> sx(100), sy(100);
    for (int i = 0; i < 100; ++i)
    {
        float t = static_cast<float>(i) / 99.0f * 2.0f * PI;
        sx[i]   = std::sin(3.0f * t);
        sy[i]   = std::cos(2.0f * t);
    }

    ax2.scatter(sx, sy).label("Lissajous 3:2").color(spectra::rgb(1.0f, 0.7f, 0.2f)).size(6.0f);

    ax2.title("Scatter — Lissajous Curve");
    ax2.xlabel("sin(3t)");
    ax2.ylabel("cos(2t)");
    ax2.grid(true);

    // ── Run ─────────────────────────────────────────────────────────────

#ifdef __EMSCRIPTEN__
    // Emscripten: yield to the browser event loop.
    // emscripten_set_main_loop does not return until cancelled.
    g_app = &app;
    app.init_runtime();
    emscripten_set_main_loop(em_main_loop, 0 /* requestAnimationFrame */, true);
#else
    // Native (Dawn or Vulkan): blocking event loop.
    app.run();
#endif

    std::printf("Spectra WebGPU Demo — done.\n");
    return 0;
}

<p align="center">
  <h1 align="center">Plotix</h1>
  <p align="center">
    <strong>GPU-accelerated plotting for C++20</strong>
  </p>
  <p align="center">
    Real-time visualization · Animation · Streaming data · Headless export
  </p>
</p>

<p align="center">
  <a href="#features">Features</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#examples">Examples</a> •
  <a href="#building">Building</a> •
  <a href="#architecture">Architecture</a> •
  <a href="#roadmap">Roadmap</a> •
  <a href="#license">License</a>
</p>

---

## What is Plotix?

Plotix is a **C++20 GPU-accelerated plotting library** built on **Vulkan 1.2+**, designed for scientific and engineering visualization. It renders anti-aliased lines, scatter plots, and text directly on the GPU — with first-class support for animation, live-streaming data, and headless offscreen export.

```cpp
#include <plotix/plotix.hpp>

int main() {
    plotix::App app;
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax  = fig.subplot(1, 1, 1);

    std::vector<float> x = { /* ... */ };
    std::vector<float> y = { /* ... */ };

    ax.line(x, y).label("signal").color(plotix::rgb(0.2f, 0.8f, 1.0f));
    ax.title("Sensor Data");
    ax.xlabel("Time (s)");
    ax.ylabel("Amplitude");

    fig.show();
}
```

---

## Features

- **Vulkan-powered rendering** — Explicit GPU control, multi-buffered frames, async buffer uploads
- **Anti-aliased lines** — Screen-space quad expansion with SDF smoothing, resolution-independent
- **Instanced scatter plots** — SDF circle rendering with per-point GPU instancing
- **MSDF text** — Crisp text at any scale via multi-channel signed distance field atlas
- **Real-time animation** — `on_frame` callbacks, configurable FPS, frame scheduling with delta time
- **Live data streaming** — O(1) ring-buffer append with automatic sliding window
- **Keyframe animation** — Timeline-based property animation with easing functions (cubic, bounce, elastic, …)
- **Subplot layouts** — Grid-based multi-axes figures with automatic margin/tick layout
- **Headless mode** — Offscreen rendering without a window (ideal for CI, servers, batch export)
- **PNG export** — Render to image via `stb_image_write`
- **Video export** — Pipe frames to `ffmpeg` for MP4/GIF output (optional)
- **Zero-copy data** — `std::span<const float>` interfaces avoid unnecessary copies
- **Optional Eigen support** — Adapters for `Eigen::VectorXf` behind a feature flag

---

## Quick Start

### Prerequisites

| Dependency | Required | Notes |
|---|---|---|
| **C++20 compiler** | Yes | GCC 12+, Clang 15+, MSVC 2022+ |
| **CMake 3.20+** | Yes | Build system |
| **Vulkan drivers** | Yes | Usually installed with graphics drivers |
| **Vulkan SDK** | Optional | Only needed for development/debugging |

> **Note:** Most modern systems already have Vulkan drivers. The full Vulkan SDK is only needed if you want to debug or develop Vulkan features.

### Quick Install

```bash
# Clone and build
git clone https://github.com/danlil240/plotix.git
cd plotix
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

That's it! The build system will automatically fetch any missing dependencies.

### Platform-Specific Tips

**Linux:**
```bash
# Install build tools (Ubuntu/Debian)
sudo apt install build-essential cmake git

# Install Vulkan drivers if missing
sudo apt install vulkan-tools libvulkan-dev
```

**macOS:**
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install dependencies with Homebrew
brew install cmake git
```

**Windows:**
- Install Visual Studio 2022 with C++ development tools
- Install CMake from cmake.org or via Visual Studio Installer

### Optional Features

| Feature | How to enable |
|---|---|---|
| Video export | Install `ffmpeg` and rebuild with `-DPLOTIX_USE_FFMPEG=ON` |
| Eigen support | Install `eigen3` and rebuild with `-DPLOTIX_USE_EIGEN=ON` |
| Debug tools | Install Vulkan SDK and rebuild with debug flags |

### Run an Example

```bash
# Linux/macOS
./build/examples/basic_line

# Windows
./build/examples/Release/basic_line.exe
```

### Verify Installation

```bash
# Run tests to verify everything works
cd build
ctest --output-on-failure

# Check Vulkan support
vulkaninfo --summary
```

---

## Examples

### Basic Line Plot

```cpp
plotix::App app;
auto& fig = app.figure({.width = 1280, .height = 720});
auto& ax  = fig.subplot(1, 1, 1);

ax.line(x, y).label("sin(x)").color(plotix::rgb(0.2f, 0.8f, 1.0f));
ax.xlim(0.0f, 10.0f);
ax.ylim(-1.5f, 1.5f);
ax.title("Basic Line Plot");

fig.show();
```

### Live Streaming Data

```cpp
auto& line = ax.line().label("live").color(plotix::colors::cyan);

fig.animate()
   .fps(60)
   .on_frame([&](plotix::Frame& f) {
       float t = f.elapsed_seconds();
       line.append(t, sensor.read());       // O(1) ring buffer append
       ax.xlim(t - 10.0f, t);              // sliding window
   })
   .play();
```

### Animated Scatter

```cpp
auto& scatter = ax.scatter(x, y).color(plotix::rgb(1, 0.4, 0)).size(4.0f);

fig.animate()
   .fps(60)
   .on_frame([&](plotix::Frame& f) {
       float t = f.elapsed_seconds();
       for (size_t i = 0; i < x.size(); ++i)
           y[i] = std::sin(x[i] + t);
       scatter.set_y(y);
   })
   .play();
```

### Multiple Subplots

```cpp
auto& fig = app.figure({.width = 1920, .height = 1080});
auto& ax1 = fig.subplot(2, 1, 1);
auto& ax2 = fig.subplot(2, 1, 2);

ax1.line(x1, y1).label("temperature");
ax2.line(x2, y2).label("pressure");

fig.show();
```

### Headless PNG Export

```cpp
plotix::App app({.headless = true});
auto& fig = app.figure({.width = 1920, .height = 1080});
auto& ax  = fig.subplot(1, 1, 1);
ax.line(x, y);
fig.save_png("output.png");
```

### Video Recording

```cpp
fig.animate()
   .fps(60)
   .duration(10.0f)
   .on_frame([&](plotix::Frame& f) { /* update data */ })
   .record("output.mp4");  // pipes frames to ffmpeg
```

### Keyframe Property Animation

```cpp
plotix::Timeline tl;
tl.at(0.0f).set(line.color(), plotix::rgb(1,0,0));
tl.at(2.0f).set(line.color(), plotix::rgb(0,0,1));
tl.easing(plotix::ease::cubic_in_out);

fig.animate().timeline(tl).duration(4.0f).loop(true).play();
```

See the [`examples/`](examples/) directory for complete, runnable programs.

---

## Architecture

```
App
 └── Figure
      └── Axes (subplot grid)
           └── Series (LineSeries, ScatterSeries, …)

┌─────────────┐     lock-free queue     ┌──────────────┐
│  App Thread  │ ─────────────────────▶  │ Render Thread │
│  (user code) │                         │ (Vulkan/GPU)  │
└─────────────┘                         └──────────────┘
```

### Key Design Decisions

- **Vulkan 1.2+** — Explicit memory management, headless rendering without a display, async buffer uploads, multi-threaded command recording
- **Screen-space SDF lines** — Quad-expanded polylines with signed-distance-field anti-aliasing in the fragment shader; resolution-independent at any zoom level
- **MSDF text** — Pre-baked multi-channel signed distance field font atlas; crisp text rendering in a single draw call
- **Lock-free threading** — SPSC ring buffer decouples the user's app thread from the GPU render thread
- **Ring buffer streaming** — O(1) append for live data; GPU vertex buffer wraps around automatically

### Project Structure

```
plotix/
├── include/plotix/       # Public API headers
├── src/
│   ├── core/             # Figure, Axes, Series, layout, coordinate transforms
│   ├── render/           # Renderer + abstract Backend interface
│   │   └── vulkan/       # VulkanBackend, device, swapchain, pipeline, buffers
│   ├── gpu/shaders/      # GLSL 450 shaders (line, scatter, grid, text)
│   ├── text/             # MSDF font atlas + text renderer
│   ├── anim/             # Animator, Timeline, easing, frame scheduler
│   ├── ui/               # App loop, GLFW adapter, input (pan/zoom)
│   └── io/               # PNG, SVG, video export
├── examples/             # Runnable demo programs
├── tests/                # Unit tests (Google Test) + golden image tests
├── third_party/          # stb, VMA (header-only, bundled)
└── cmake/                # Shader compilation helpers
```

---

## Testing

```bash
cd build
cmake .. -DPLOTIX_BUILD_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

Tests include:
- **Unit tests** — Coordinate transforms, layout solver, easing functions, ring buffer, timeline interpolation
- **Golden image tests** — Headless renders compared pixel-by-pixel against baseline PNGs
- **Benchmarks** — 1M-point line rendering, 100K scatter throughput

---

## Roadmap

| Phase | Status | Scope |
|---|---|---|
| **Phase 1 — MVP** | 🚧 In Progress | Line/scatter plots, MSDF text, animation, streaming, PNG export, pan/zoom |
| **Phase 2 — Extended** | Planned | Keyframe timelines, video export, histograms, heatmaps, legends, SVG export |
| **Phase 3 — Polish** | Planned | ABI stability (pimpl), CMake `find_package`, Conan/vcpkg, themes, plugin API, Windows/macOS CI |

---

## Contributing

Contributions are welcome! The codebase is organized into independent modules with clear ownership boundaries — see [`agents_plan.md`](agents_plan.md) for the module decomposition.

When submitting changes:
1. Follow C++20 style with no global state and RAII throughout
2. Keep public headers in `include/plotix/` minimal (pimpl in Phase 3)
3. Add unit tests for new functionality
4. Run `ctest` before submitting

---

## License

This project is under development. License TBD.

---

<p align="center">
  Built with Vulkan · C++20 · ❤️
</p>

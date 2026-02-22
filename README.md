<p align="center">
  <p align="center">
    <img src="icons/spectra_banner.png" alt="Spectra Banner" width="600">
  </p>
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

## What is Spectra?

Spectra is a **C++20 GPU-accelerated plotting library** built on **Vulkan 1.2+**, designed for scientific and engineering visualization. It renders anti-aliased lines, scatter plots, and text directly on the GPU — with first-class support for animation, live-streaming data, and headless offscreen export.

```cpp
#include <spectra/spectra.hpp>

int main() {
    spectra::App app;
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax  = fig.subplot(1, 1, 1);

    std::vector<float> x = { /* ... */ };
    std::vector<float> y = { /* ... */ };

    ax.line(x, y).label("signal").color(spectra::rgb(0.2f, 0.8f, 1.0f));
    ax.title("Sensor Data");
    ax.xlabel("Time (s)");
    ax.ylabel("Amplitude");

    fig.show();
}
```

---

## Features

### Core Rendering
- **Vulkan-powered rendering** — Explicit GPU control, multi-buffered frames, async buffer uploads, depth buffer support
- **Anti-aliased lines** — Screen-space quad expansion with SDF smoothing, resolution-independent
- **18 marker types** — SDF-based markers (circle, square, diamond, triangles, pentagon, hexagon, star, plus, cross, filled variants)
- **Dash patterns** — GPU-accelerated dash rendering with 8 customizable dash values
- **MSDF text** — Crisp text at any scale via multi-channel signed distance field atlas
- **MATLAB-style plot API** — Format strings like `"r--o"`, `"b:*"` for quick styling

### Animation & Recording
- **Real-time animation** — `on_frame` callbacks, configurable FPS, frame scheduling with delta time
- **Live data streaming** — O(1) ring-buffer append with automatic sliding window
- **Timeline editor** — Playback controls, keyframe tracks, scrubbing, snap modes, loop modes (None/Loop/PingPong)
- **Keyframe interpolator** — 7 interpolation modes (Step, Linear, CubicBezier, Spring, EaseIn/Out, EaseInOut) with tangent control
- **Animation curve editor** — Visual curve editing with Bézier handles, multi-channel support
- **Recording export** — PNG sequences, GIF (median-cut quantization), MP4 (ffmpeg pipe)
- **Multi-pane recording** — Composite rendering with auto-grid layout

### UI & Productivity
- **Command palette** — Fuzzy search (Ctrl+K), 30+ registered commands, recent tracking
- **Configurable shortcuts** — Rebindable keybindings with persistence
- **Undo/redo system** — Full property change history with grouped operations
- **Multi-figure tabs** — Tab switching, duplication, context menus, per-figure state
- **Docking & split view** — Horizontal/vertical splits, drag-to-dock, splitter handles
- **Inspector panel** — Series statistics (min/max/mean/median/percentiles), sparkline preview, style editing
- **Workspace management** — Save/load full state (v3 format), autosave, backward compatibility

### Data Interaction
- **Hover tooltips** — Nearest-point query with series name, coordinates, color swatch
- **Crosshair overlay** — Shared across subplots with axis-intersection labels
- **Data markers** — Pin/remove persistent markers, survive zoom/pan
- **Region selection** — Shift-drag rectangular selection with statistics mini-toolbar
- **Legend interaction** — Click-to-toggle visibility, drag-to-reposition, animated opacity
- **Multi-axis linking** — Link X/Y/Both axes across subplots, synchronized zoom/pan
- **Data transforms** — 14 built-in types (Log10, Normalize, Derivative, etc.) with pipeline support

### Themes & Accessibility
- **Dark/light themes** — Smooth animated transitions between themes
- **8 colorblind-safe palettes** — Okabe-Ito, Tol Bright/Muted, IBM, Wong, Viridis, Monochrome
- **CVD simulation** — Test designs for Protanopia, Deuteranopia, Tritanopia, Achromatopsia
- **Theme export/import** — JSON-based theme customization
- **Design tokens** — Consistent spacing, typography, color system

### 3D Foundation (Phase 1 Complete)
- **3D transform pipeline** — Full MVP (Model-View-Projection) with depth buffer
- **Math library** — Header-only vec3, vec4, mat4, quat with all operations (no GLM dependency)
- **Depth testing** — Proper occlusion with configurable depth compare operations

### Export & Integration
- **Headless mode** — Offscreen rendering without a window (ideal for CI, servers, batch export)
- **PNG export** — Render to image via `stb_image_write`
- **Subplot layouts** — Grid-based multi-axes figures with automatic margin/tick layout
- **Zero-copy data** — `std::span<const float>` interfaces avoid unnecessary copies
- **Plugin-ready architecture** — C ABI for binary-compatible extensions
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
git clone https://github.com/danlil240/spectra.git
cd spectra
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
| Video export | Install `ffmpeg` and rebuild with `-DSPECTRA_USE_FFMPEG=ON` |
| Eigen support | Install `eigen3` and rebuild with `-DSPECTRA_USE_EIGEN=ON` |
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
spectra::App app;
auto& fig = app.figure({.width = 1280, .height = 720});
auto& ax  = fig.subplot(1, 1, 1);

ax.line(x, y).label("sin(x)").color(spectra::rgb(0.2f, 0.8f, 1.0f));
ax.xlim(0.0f, 10.0f);
ax.ylim(-1.5f, 1.5f);
ax.title("Basic Line Plot");

fig.show();
```

### Live Streaming Data

```cpp
auto& line = ax.line().label("live").color(spectra::colors::cyan);

fig.animate()
   .fps(60)
   .on_frame([&](spectra::Frame& f) {
       float t = f.elapsed_seconds();
       line.append(t, sensor.read());       // O(1) ring buffer append
       ax.xlim(t - 10.0f, t);              // sliding window
   })
   .play();
```

### Animated Scatter

```cpp
auto& scatter = ax.scatter(x, y).color(spectra::rgb(1, 0.4, 0)).size(4.0f);

fig.animate()
   .fps(60)
   .on_frame([&](spectra::Frame& f) {
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
spectra::App app({.headless = true});
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
   .on_frame([&](spectra::Frame& f) { /* update data */ })
   .record("output.mp4");  // pipes frames to ffmpeg
```

### MATLAB-Style Plot Customization

```cpp
// Format strings: "[color][line_style][marker]"
ax.plot(x, y1, "r--o");   // Red dashed line with circle markers
ax.plot(x, y2, "b:*");    // Blue dotted line with star markers
ax.plot(x, y3, "g-^");    // Green solid line with triangle markers

// Or use PlotStyle struct for full control
PlotStyle style;
style.line_style = LineStyle::Dashed;
style.marker_style = MarkerStyle::Diamond;
style.color = rgb(1.0f, 0.5f, 0.0f);
style.line_width = 2.5f;
style.marker_size = 8.0f;
style.opacity = 0.8f;
style.dash_pattern = {10.0f, 5.0f, 2.0f, 5.0f};  // Custom dash

ax.plot(x, y4, style);
```

### Timeline & Keyframe Animation

```cpp
// Timeline editor with playback controls
TimelineEditor timeline;
timeline.set_duration(10.0f);
timeline.set_fps(60);
timeline.add_track("Camera", Color{1,0,0});

// Keyframe interpolator with 7 modes
KeyframeInterpolator interp;
interp.add_channel("opacity", &series.opacity());
interp.add_keyframe("opacity", 0.0f, 0.0f, InterpMode::EaseIn);
interp.add_keyframe("opacity", 2.0f, 1.0f, InterpMode::CubicBezier);
interp.add_keyframe("opacity", 4.0f, 0.0f, InterpMode::EaseOut);

timeline.set_interpolator(&interp);
timeline.play();
```

See the [`examples/`](examples/) directory for complete, runnable programs.

---

## Architecture

```
App
 ├── FigureManager (multi-figure tabs, per-figure state)
 ├── CommandRegistry (30+ commands, fuzzy search)
 ├── ShortcutManager (configurable keybindings)
 ├── UndoManager (property change history)
 ├── ThemeManager (dark/light, 8 colorblind palettes)
 ├── TransitionEngine (unified animation system)
 ├── TimelineEditor (playback, keyframes, scrubbing)
 ├── DockSystem (split view, drag-to-dock)
 └── Figure
      ├── Axes (2D subplot) / Axes3D (3D subplot)
      │    ├── Camera (3D only: orbit, pan, zoom)
      │    └── Series (LineSeries, ScatterSeries, …)
      └── AxisLinkManager (synchronized zoom/pan)

┌─────────────┐     lock-free queue     ┌──────────────┐
│  App Thread  │ ─────────────────────▶  │ Render Thread │
│  (user code) │                         │ (Vulkan/GPU)  │
└─────────────┘                         └──────────────┘
```

### Key Design Decisions

- **Vulkan 1.2+** — Explicit memory management, headless rendering without a display, async buffer uploads, depth buffer for 3D
- **Screen-space SDF rendering** — Lines and markers use signed-distance-field anti-aliasing; resolution-independent at any zoom level
- **GPU-accelerated styling** — Dash patterns and 18 marker types rendered entirely in shaders via SDF
- **MSDF text** — Pre-baked multi-channel signed distance field font atlas; crisp text rendering in a single draw call
- **Lock-free threading** — SPSC ring buffer decouples the user's app thread from the GPU render thread
- **No global state** — All managers are stack-allocated and passed by pointer; thread-safe via std::mutex
- **Header-only math** — Self-contained vec3/vec4/mat4/quat library (~350 LOC); no GLM dependency
- **Backward compatibility** — Workspace v3 format loads v1/v2 files with sensible defaults

### Project Structure

```
spectra/
├── include/spectra/       # Public API headers (spectra.hpp, math3d.hpp, plot_style.hpp)
├── src/
│   ├── core/             # Figure, Axes, Series, layout, coordinate transforms
│   ├── render/           # Renderer + abstract Backend interface
│   │   └── vulkan/       # VulkanBackend, device, swapchain, pipeline, buffers, depth
│   ├── gpu/shaders/      # GLSL 450 shaders (line, scatter, grid, text) with dash/marker support
│   ├── text/             # MSDF font atlas + text renderer
│   ├── anim/             # Animator, easing, frame scheduler, transition engine
│   ├── ui/               # App, input, theme, inspector, timeline, docking, commands
│   └── io/               # PNG, GIF, MP4 export, workspace serialization
├── examples/             # Runnable demo programs (plot_styles_demo, etc.)
├── tests/
│   ├── unit/             # 700+ unit tests (Google Test)
│   ├── golden/           # Golden image tests (Phase 1/2/3)
│   └── bench/            # Performance benchmarks
├── third_party/          # stb, VMA (header-only, bundled)
├── plans/                # Architecture plans, roadmap, UI redesign spec
└── cmake/                # Shader compilation helpers
```

---

## Testing

```bash
cd build
cmake .. -DSPECTRA_BUILD_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

Tests include:
- **700+ unit tests** — All core systems, UI components, animation, serialization, edge cases
- **Golden image tests** — Headless renders compared pixel-by-pixel against baseline PNGs (Phase 1/2/3)
- **Integration tests** — Cross-component workflows (command+undo, workspace+figures, timeline+recording)
- **Benchmarks** — CommandRegistry, TransitionEngine, InspectorStats, SparklineDownsample, FigureManager, axis linking

---

## Roadmap

| Phase | Status | Scope |
|---|---|---|
| **Phase 1 — Modern Foundation** | ✅ Complete | Theme system, layout manager, inspector, animated interactions, data tooltips, transition engine |
| **Phase 2 — Power User Features** | ✅ Complete | Command palette, undo/redo, workspace v3, timeline editor, recording export, colorblind palettes, multi-figure tabs |
| **Phase 3 — Elite Differentiators** | ✅ Complete | Docking system, MATLAB-style plot API, axis linking, data transforms, shortcut persistence, plugin architecture |
| **3D Visualization** | 🚧 Phase 1 Complete | 3D transform pipeline, depth buffer, math library, camera system (in progress) |

See [`plans/ROADMAP.md`](plans/ROADMAP.md) for detailed progress tracking.

---

## Contributing

Contributions are welcome! The codebase is organized into independent modules with clear ownership boundaries — see [`agents_plan.md`](agents_plan.md) for the module decomposition.

When submitting changes:
1. Follow C++20 style with no global state and RAII throughout
2. Keep public headers in `include/spectra/` minimal (pimpl in Phase 3)
3. Add unit tests for new functionality
4. Run `ctest` before submitting

---

## License

This project is under development. License TBD.

---

<p align="center">
  Built with Vulkan · C++20 · ❤️
</p>

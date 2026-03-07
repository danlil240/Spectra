# Getting Started with Spectra

Spectra is a GPU-accelerated plotting library for C++20, built on Vulkan 1.2+.

## Prerequisites

- **C++20 compiler**: GCC 12+, Clang 15+, or MSVC 2022+
- **CMake**: 3.20+
- **Vulkan development files**: `libvulkan-dev` on Linux, LunarG Vulkan SDK on Windows, or `vulkan-headers` + `vulkan-loader` on macOS
- **glslangValidator**: `glslang-tools` on Linux or `glslang` on macOS
- **GLFW**: optional as a system package; if missing, Spectra fetches/builds it automatically for windowed builds

For released binaries, the requirements are smaller than for source builds:

- **`.deb` / `.rpm` packages**: do not manually install `-dev` packages first; use the package manager and let it resolve runtime dependencies
- **AppImage**: usually only requires a working Vulkan-capable driver/runtime on the host
- **Python wheel**: no compiler or Vulkan SDK required on the target machine, but the backend still needs a working Vulkan runtime/driver

## Building

Ubuntu/Debian source-build dependencies:

```bash
sudo apt install build-essential cmake git libvulkan-dev libglfw3-dev glslang-tools
```

If you are building a headless configuration, you can omit `libglfw3-dev` and configure with `-DSPECTRA_USE_GLFW=OFF -DSPECTRA_USE_IMGUI=OFF`.

```bash
# Clone the repository
git clone https://github.com/your-org/spectra.git
cd spectra

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `SPECTRA_USE_GLFW` | `ON` | Enable GLFW windowing adapter |
| `SPECTRA_USE_FFMPEG` | `OFF` | Enable video export via ffmpeg pipe |
| `SPECTRA_USE_EIGEN` | `OFF` | Enable Eigen vector adapters |
| `SPECTRA_BUILD_EXAMPLES` | `ON` | Build example programs |
| `SPECTRA_BUILD_TESTS` | `ON` | Build unit tests and benchmarks |
| `SPECTRA_BUILD_BENCHMARKS` | `OFF` | Build performance benchmarks |
| `SPECTRA_BUILD_GOLDEN_TESTS` | `ON` | Build golden image regression tests |

## Deployment Options

### Debian / Ubuntu package

```bash
sudo apt update
sudo apt install ./spectra_<version>_amd64.deb
```

Do not preinstall `libvulkan-dev`, `libglfw3-dev`, or other `-dev` packages for `.deb` installation. Those are source-build dependencies. The Debian package metadata is expected to pull runtime libraries automatically.

### AppImage

AppImage is the lowest-friction Linux artifact for end users. It bundles most user-space libraries, but still requires a working Vulkan-capable driver/runtime on the host system.

### Python wheel

```bash
pip install spectra-plot
```

Python wheels do not require a compiler, CMake, or Vulkan SDK on the target machine. They still require the target machine to have working Vulkan runtime support.

## Quick Start

### Windowed Line Plot

```cpp
#include <spectra/spectra.hpp>
#include <cmath>
#include <vector>

int main() {
    spectra::App app;
    auto& fig = app.figure({.width = 1280, .height = 720});
    auto& ax  = fig.subplot(1, 1, 1);

    // Generate data
    std::vector<float> x(200), y(200);
    for (size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<float>(i) * 0.05f;
        y[i] = std::sin(x[i]);
    }

    ax.line(x, y).label("sin(x)").color(spectra::rgb(0.2f, 0.8f, 1.0f));
    ax.xlim(0.0f, 10.0f);
    ax.ylim(-1.5f, 1.5f);
    ax.title("My First Plot");
    ax.xlabel("X Axis");
    ax.ylabel("Y Axis");

    app.run();
    return 0;
}
```

### Headless PNG Export

```cpp
#include <spectra/spectra.hpp>
#include <cmath>
#include <vector>

int main() {
    spectra::App app({.headless = true});
    auto& fig = app.figure({.width = 1920, .height = 1080});
    auto& ax  = fig.subplot(1, 1, 1);

    std::vector<float> x(500), y(500);
    for (size_t i = 0; i < 500; ++i) {
        x[i] = static_cast<float>(i) * 0.02f;
        y[i] = std::sin(x[i]) * std::cos(x[i] * 0.5f);
    }

    ax.line(x, y).label("signal").color(spectra::rgb(0.1f, 0.7f, 0.3f));
    ax.title("Offscreen Export");
    ax.xlabel("X");
    ax.ylabel("Y");

    fig.save_png("output.png");
    app.run();
    return 0;
}
```

### Scatter Plot

```cpp
auto& ax = fig.subplot(1, 1, 1);
ax.scatter(x, y)
    .label("data points")
    .color(spectra::rgb(1.0f, 0.4f, 0.0f))
    .size(6.0f);
```

### Multiple Subplots

```cpp
auto& fig = app.figure({.width = 1920, .height = 1080});
auto& ax1 = fig.subplot(2, 1, 1);  // 2 rows, 1 col, position 1
auto& ax2 = fig.subplot(2, 1, 2);  // 2 rows, 1 col, position 2

ax1.line(x, y1).label("temperature").color(spectra::colors::red);
ax1.title("Temperature");

ax2.line(x, y2).label("pressure").color(spectra::colors::blue);
ax2.title("Pressure");
```

### Animation

```cpp
fig.animate()
    .fps(60)
    .on_frame([&](spectra::Frame& f) {
        float t = f.elapsed_seconds();
        // Update data each frame
        for (size_t i = 0; i < N; ++i) {
            y[i] = std::sin(x[i] + t);
        }
        series.set_y(y);
    })
    .play();

app.run();
```

## Running Tests

```bash
# Build with tests
cmake -B build -DSPECTRA_BUILD_TESTS=ON
cmake --build build

# Run all unit tests
cd build && ctest --output-on-failure

# Run only golden image tests (requires Vulkan)
ctest --output-on-failure -L golden

# Update golden baselines
SPECTRA_UPDATE_BASELINES=1 ./golden_image_tests
```

## Running Benchmarks

```bash
cmake -B build -DSPECTRA_BUILD_BENCHMARKS=ON
cmake --build build
./build/bench_render
```

## Window Resizing

Spectra supports dynamic window resizing. When you resize a window:

1. The figure dimensions are automatically updated
2. The Vulkan swapchain is recreated with the new dimensions  
3. All subplot layouts are recomputed to fit the new size
4. Axes viewports are scaled proportionally

```cpp
// Create a resizable window
spectra::App app;
auto& fig = app.figure();
auto& ax = fig.subplot(1, 1, 0);

// ... add your data ...

fig.show();  // Window can be resized by the user
```

The plot will automatically scale to fill the resized window while maintaining aspect ratios and margins.

## Python API

Spectra provides a Python client that communicates with the C++ backend via IPC
(Unix domain sockets). This lets you drive plots from Python scripts, Jupyter
notebooks, or any Python application.

### Installation

```bash
# From the project root (editable install)
pip install -e python/
```

### Quick Start

```python
import spectra as sp

# Create a figure with a line plot
fig = sp.figure("My Plot")
ax = fig.subplot(1, 1, 1)
line = ax.line([0, 1, 2, 3], [0, 1, 4, 9], label="x²")

ax.set_xlabel("X")
ax.set_ylabel("Y")
ax.set_title("Quadratic")

fig.show()
sp.show()  # Block until window is closed
```

### Streaming Data

```python
import spectra as sp
import math

fig = sp.figure("Live Stream")
ax = fig.subplot(1, 1, 1)
line = ax.line([], [], label="sin(t)")
fig.show()

pacer = sp.FramePacer(fps=30)
t = 0.0
while fig.is_visible:
    t += 0.033
    x = [i * 0.1 for i in range(100)]
    y = [math.sin(xi + t) for xi in x]
    line.set_data(x, y)
    pacer.pace(sp._default_session)
```

### Backend-Driven Animation

```python
import spectra as sp
from spectra import BackendAnimator
import math

session = sp.Session()
fig = session.figure("Animated")
ax = fig.subplot(1, 1, 1)
line = ax.line([], [])

x = [i * 0.1 for i in range(100)]

def on_tick(t, dt, frame_num):
    y = [math.sin(xi + t) for xi in x]
    line.set_data(x, y)

animator = BackendAnimator(session, fig.id, fps=60)
animator.on_tick = on_tick
animator.start()
fig.show()
session.show()
animator.stop()
session.close()
```

### Batch Property Updates

```python
with ax.batch() as b:
    b.set_xlim(0, 10)
    b.set_ylim(-1, 1)
    b.set_xlabel("Time (s)")
    b.set_ylabel("Amplitude")
    b.grid(True)
```

### Session Persistence

```python
from spectra._persistence import save_session, restore_session

# Save current session
save_session(session, "my_session.json")

# Later: restore session structure (data must be re-populated)
new_session = sp.Session()
figure_ids = restore_session(new_session, "my_session.json")
```

## Architecture Overview

```
App
 └── Figure(s)
      └── Axes (subplots)
           └── Series (LineSeries, ScatterSeries)
```

- **App**: Top-level container. Owns the Vulkan backend and renderer.
- **Figure**: A single plot window/image with configurable dimensions.
- **Axes**: A subplot within a figure. Owns series, axis limits, labels, grid.
- **Series**: Data to plot. `LineSeries` for line plots, `ScatterSeries` for scatter.

All rendering is GPU-accelerated via Vulkan. Headless mode uses an offscreen
framebuffer for server-side rendering and export.

## API Reference

See the [Doxygen documentation](api/html/index.html) for the full API reference.

Generate it locally:
```bash
doxygen docs/Doxyfile
open docs/api/html/index.html
```

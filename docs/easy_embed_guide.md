# Spectra Easy Embed Guide

## Overview

The **Easy Embed API** provides one-liner offscreen rendering for Spectra plots. No windows, no daemon, no event loop — just data in → pixels out. Perfect for:

- **Web services** (plot-as-PNG endpoints)
- **Data pipelines** (batch report generation)
- **Scientific notebooks** (embed plots in documents)
- **Game engines** (render UI overlays)

## Quick Start

### C++

```cpp
#include <spectra/easy_embed.hpp>

// Basic line plot
auto img = spectra::render(x, y);

// Save to file
spectra::render(x, y, {.save_path = "plot.png"});

// Custom size and styling
auto img = spectra::render(x, y, {.width = 1920, .height = 1080, .fmt = "r--o"});

// Multiple series
auto img = spectra::render_multi({
    {x, y1, "b-", "sin(x)"},
    {x, y2, "r--", "cos(x)"},
});

// Scatter plot
auto img = spectra::render_scatter(x, y);

// Histogram
auto img = spectra::render_histogram(values, bins=30);
```

### Python

```python
import spectra.embed as spe

# Basic line plot
img = spe.render(x, y)

# Save to file
spe.render(x, y, save="plot.png")

# Custom size
img = spe.render(x, y, width=1920, height=1080)

# Multiple series
spe.render_multi([
    (x, y1, "sin"),
    (x, y2, "cos"),
], save="multi.png")

# Scatter plot
spe.scatter(x, y, save="scatter.png")

# Histogram
spe.histogram(values, bins=30, save="hist.png")
```

## Build Requirements

```bash
# Build with embed shared library
cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..

# Set library path (Python only)
export SPECTRA_EMBED_LIB=/path/to/build/libspectra_embed.so
```

## API Reference

### C++ API

#### RenderedImage
Container for rendered RGBA pixel data.

```cpp
struct RenderedImage {
    std::vector<uint8_t> data;  // RGBA pixels
    uint32_t width, height;
    
    size_t stride() const;        // width * 4
    size_t size_bytes() const;    // data.size()
    bool empty() const;
    uint8_t* pixels();            // data.data()
    const uint8_t* pixels() const;
};
```

#### RenderOptions
Configuration for rendering.

```cpp
struct RenderOptions {
    uint32_t width = 800;
    uint32_t height = 600;
    std::string fmt = "-";        // MATLAB-style format
    std::string title, xlabel, ylabel;
    std::string save_path;        // If set, saves PNG
    float dpi_scale = 1.0f;
    bool grid = true;
};
```

#### Functions

```cpp
// Single line plot
RenderedImage render(std::span<const float> x, 
                     std::span<const float> y,
                     const RenderOptions& opts = {});

// Multiple series
RenderedImage render_multi(std::initializer_list<SeriesDesc> series,
                           const RenderOptions& opts = {});

// Scatter plot
RenderedImage render_scatter(std::span<const float> x,
                             std::span<const float> y,
                             const RenderOptions& opts = {});

// Histogram
RenderedImage render_histogram(std::span<const float> values,
                               int bins = 30,
                               const RenderOptions& opts = {});

// Bar chart
RenderedImage render_bar(std::span<const float> positions,
                         std::span<const float> heights,
                         const RenderOptions& opts = {});
```

#### SeriesDesc
For multi-series plots.

```cpp
struct SeriesDesc {
    std::span<const float> x, y;
    std::string_view fmt = "-";
    std::string_view label = "";
};
```

### Python API

#### Image
Container for rendered RGBA pixel data.

```python
class Image:
    data: bytes          # RGBA pixels
    width: int
    height: int
    
    @property
    def stride(self) -> int:      # width * 4
    @property
    def size_bytes(self) -> int:  # len(data)
    
    def save(self, path: str) -> bool:  # Save as PNG
```

#### Functions

```python
# Line plot
render(x, y, *, width=800, height=600, save=None,
        title=None, xlabel=None, ylabel=None) -> Image

# Multi-series
render_multi(series_list, *, width=800, height=600, save=None,
             title=None, xlabel=None, ylabel=None) -> Image

# Scatter plot
scatter(x, y, *, width=800, height=600, save=None,
         title=None, xlabel=None, ylabel=None) -> Image

# Histogram
histogram(values, *, bins=30, width=800, height=600, save=None,
          title=None) -> Image
```

## Format Strings

MATLAB-style format strings control line style, color, and markers:

```
Colors: r, g, b, c, m, y, k, w
Line styles: -, --, :, -.
Markers: o, s, ^, v, <, >, d, p, h, *, +, x, |

Examples:
"r-"     # Red solid line
"b--o"   # Blue dashed line with circles
"g:.s"   # Green dotted line with squares
"k-."    # Black dash-dot line
```

## Examples

### Web Service Endpoint (Python)

```python
from flask import Flask, send_file
import spectra.embed as spe
import io

app = Flask(__name__)

@app.route("/plot.png")
def plot():
    x = [0, 1, 2, 3, 4, 5]
    y = [0, 1, 4, 9, 16, 25]
    img = spe.render(x, y, save="temp.png")
    return send_file("temp.png", mimetype="image/png")
```

### Batch Report Generation (C++)

```cpp
#include <spectra/easy_embed.hpp>

void generate_reports() {
    std::vector<std::string> metrics = {"cpu", "memory", "disk"};
    
    for (const auto& metric : metrics) {
        auto data = load_metric_data(metric);
        spectra::RenderOptions opts;
        opts.title = metric + " Usage";
        opts.save_path = "report_" + metric + ".png";
        spectra::render(data.timestamps, data.values, opts);
    }
}
```

### Game Engine Overlay (C++)

```cpp
void render_minimap() {
    // Player positions
    std::vector<float> x = player_x_positions;
    std::vector<float> y = player_y_positions;
    
    // Render to pixels
    spectra::RenderOptions opts;
    opts.width = 256;
    opts.height = 256;
    opts.fmt = "ro";  // Red circles
    auto img = spectra::render_scatter(x, y, opts);
    
    // Upload to GPU texture
    upload_to_texture(img.pixels(), img.width, img.height);
}
```

## Performance

The Easy Embed API is optimized for throughput:

- **GPU-accelerated** rendering via Vulkan
- **Zero-copy** when possible (direct GPU memory)
- **Batch-friendly** for large datasets
- **Thread-safe** (each render creates its own surface)

Benchmark results (RTX 3080, 50k points):
- **Render time**: ~0.2 seconds
- **Throughput**: ~250k points/second
- **Memory**: ~6 MB for 1600x900 RGBA image

## Limitations

- **No interactivity** (offscreen only)
- **No animations** (single frame)
- **Limited styling** (basic format strings)
- **Vulkan required** (GPU driver needed)

For interactive plots, use the main Spectra API with windows.

## Troubleshooting

### "undefined symbol: spectra_render_line"

Make sure the embed shared library is built and loaded:

```bash
cmake -DSPECTRA_BUILD_EMBED_SHARED=ON ..
export SPECTRA_EMBED_LIB=/path/to/build/libspectra_embed.so
```

### "Vulkan initialization failed"

Ensure a Vulkan driver is installed:

```bash
# Ubuntu/Debian
sudo apt install mesa-vulkan-drivers vulkan-tools

# Verify
vulkaninfo | grep "driverName"
```

### Blank images

Check that data ranges are reasonable and not all zeros:

```python
# Verify data
print(f"x range: {min(x)} to {max(x)}")
print(f"y range: {min(y)} to {max(y)}")
```

## See Also

- [Main Spectra Documentation](../README.md)
- [C++ Examples](../examples/)
- [Python Examples](../python/examples/)
- [API Reference](../include/spectra/easy_embed.hpp)

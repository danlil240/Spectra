# Spectra: GPU-Accelerated Plotting Engine — Development Plan

A production-grade development plan for a C++20 GPU-accelerated plotting library targeting scientific/engineering visualization with animation, real-time streaming, and export capabilities.

---

## A. Architectural Decisions

### A1. Rendering Backend: Vulkan (Option A)

**Choice: Vulkan 1.2+**

| Factor | Vulkan | OpenGL 4.5+ |
|---|---|---|
| Explicit control over GPU sync | ✅ Full | ❌ Driver-managed |
| Multi-buffering / ring buffers | ✅ Native | ⚠️ Hacks with PBOs/fences |
| Headless / offscreen | ✅ No display needed | ⚠️ Needs EGL or pbuffer |
| Shader compilation | ✅ SPIR-V offline | ⚠️ Runtime GLSL |
| Threading model | ✅ Multi-queue, explicit | ❌ Single-context bound |
| Streaming vertex updates | ✅ Staging + ring buffers | ⚠️ glBufferSubData stalls |
| macOS portability | ⚠️ MoltenVK | ✅ Native (up to 4.1) |
| Boilerplate | ❌ High | ✅ Low |

**Rationale:** Vulkan gives us explicit memory management, trivial headless rendering (critical for export/CI), true async buffer uploads for streaming data, and a threading model that maps directly onto our render-thread / app-thread architecture. The boilerplate cost is paid once in a thin abstraction layer.

**Risk:** Higher initial complexity. Mitigated by building a small `vk::Device` wrapper (~1500 LOC) covering swapchain, pipelines, buffers, and command submission.

**macOS path:** MoltenVK for Phase 2/3; the abstraction layer (`Backend` interface) leaves room for a Metal backend later without API changes.

### A2. Windowing: GLFW as Optional Dependency

**Choice: Headless core + GLFW windowing adapter (optional, behind `SPECTRA_USE_GLFW`)**

- Core library (`libspectra`) is headless-capable: renders to offscreen framebuffers. No window dependency.
- `spectra-glfw` adapter (separate CMake target) provides `spectra::app` with window creation, input, vsync.
- Trade-off: SDL2 offers audio/gamepad but is heavier; GLFW is lighter and Vulkan-native. GLFW wins for a plotting library.
- Users can bring their own surface (pass a `VkSurfaceKHR`) for embedding in Qt/ImGui/etc.

### A3. Line Rendering: Screen-Space Quad Expansion + SDF

**Strategy:** Expand each line segment into a screen-space oriented quad in the vertex shader (2 triangles per segment), compute signed distance to the line edge in the fragment shader for sub-pixel anti-aliasing.

- Miter joins computed in geometry/vertex shader for polylines (vertex shader preferred — avoids geometry shader perf penalty).
- Line width as a uniform; supports per-series width.
- Fallback: MSAA (4x) as a simpler path for Phase 1 if SDF lines slip.

**Pros:** Resolution-independent AA, no geometry shader, works at any zoom.
**Cons:** More vertices (4× point count). Acceptable for plotting workloads (typically <1M points).

### A4. Text Rendering: MSDF Atlas

- Use **multi-channel signed distance field** (MSDF) technique via pre-generated atlas.
- Ship a default font atlas (e.g., Roboto/Noto) as embedded binary data (no runtime file I/O required).
- Optional: runtime atlas generation behind `SPECTRA_USE_FREETYPE` flag using msdfgen.
- Text is batched into a single draw call per font size/atlas.

**Risk:** MSDF atlas generation tooling. Mitigated by pre-baking atlas at build time with a helper script.

### A5. Threading Model

```
┌─────────────┐      command queue       ┌──────────────┐
│  App Thread  │ ───── (lock-free) ────▶  │ Render Thread │
│  (user code) │                          │ (GPU owner)   │
└─────────────┘                          └──────────────┘
```

- **Render thread** owns all Vulkan objects, command buffers, swapchain.
- **App thread** enqueues mutation commands (set_data, set_property, add_series) into an SPSC lock-free ring buffer.
- Render thread drains queue at frame start, applies mutations, records commands, submits.
- Double-buffered (or triple) frame data: render thread reads frame N while app thread writes frame N+1.
- `spectra::app::run()` drives the render loop; user callbacks (`on_frame`) execute on the app thread.

**Failure recovery:**
- **Device lost:** Recreate logical device + all GPU resources (pipelines, buffers, atlases). Log error, re-upload data.
- **Swapchain resize:** Recreate swapchain + framebuffers on `VK_ERROR_OUT_OF_DATE_KHR`. Handled transparently.
- **OOM:** Graceful error return from data upload APIs; user can reduce dataset or enable downsampling.

---

## B. API Sketch & Core Classes

### B1. Minimal Class List

| Class | Responsibility |
|---|---|
| `App` | Owns render loop, windowing adapter, frame scheduler |
| `Figure` | Owns axes layout, framebuffer, export target |
| `Axes` | Coordinate transform, ticks, grid, legend, series container |
| `Series` (base) | Abstract renderable data series |
| `LineSeries` | Polyline rendering, streaming append |
| `ScatterSeries` | Instanced point rendering |
| `Renderer` | High-level render orchestration, draw call batching |
| `Backend` | Abstract GPU interface (impl: `VulkanBackend`) |
| `GpuBuffer` | RAII wrapper for vertex/index/uniform/SSBO buffers |
| `RingBuffer` | Multi-frame staging buffer for streaming uploads |
| `TextRenderer` | MSDF atlas management, glyph batching |
| `Animator` | Manages active animations, tween evaluation |
| `Timeline` | Keyframe sequence for a single property |
| `FrameScheduler` | Target FPS, vsync, frame pacing, delta time |
| `CommandQueue` | Lock-free SPSC queue for cross-thread mutations |
| `ImageExporter` | Offscreen render → PNG (stb_image_write) |
| `VideoExporter` | Frame pipe to ffmpeg subprocess (optional) |

### B2. API Examples

**Basic static plot:**
```cpp
spectra::app app;
auto fig = app.figure({.width = 1280, .height = 720});
auto ax  = fig.subplot(1, 1, 1);

std::vector<float> x = /* ... */;
std::vector<float> y = /* ... */;

ax.line(x, y).label("signal").color(spectra::rgb(0.2, 0.8, 1.0));
ax.xlim(0, 10);
ax.ylim(-5, 5);
ax.title("Sensor Data");
ax.xlabel("Time (s)");
ax.ylabel("Amplitude");

fig.show();  // opens window, blocks until closed
```

**Animated line/scatter over time:**
```cpp
auto scatter = ax.scatter(x, y).color(spectra::rgb(1, 0.4, 0)).size(4.0f);

fig.animate()
   .fps(60)
   .on_frame([&](spectra::frame& f) {
       float t = f.elapsed_seconds();
       for (size_t i = 0; i < x.size(); ++i)
           y[i] = std::sin(x[i] + t);
       scatter.set_y(y);
   })
   .play();
```

**Live streaming plot (append points every frame):**
```cpp
auto line = ax.line().label("live").color(spectra::colors::cyan);

fig.animate()
   .fps(60)
   .on_frame([&](spectra::frame& f) {
       float t = f.elapsed_seconds();
       line.append(t, sensor.read());       // O(1) ring buffer append
       ax.xlim(t - 10.0f, t);              // sliding window
   })
   .play();
```

**Multiple subplots updating independently:**
```cpp
auto fig = app.figure({.width = 1920, .height = 1080});
auto ax1 = fig.subplot(2, 1, 1);
auto ax2 = fig.subplot(2, 1, 2);

auto line1 = ax1.line(x1, y1).label("temperature");
auto line2 = ax2.line(x2, y2).label("pressure");

fig.animate()
   .fps(30)
   .on_frame([&](spectra::frame& f) {
       line1.set_y(temp_stream.latest());
       line2.set_y(pressure_stream.latest());
   })
   .play();
```

**Keyframe property animation:**
```cpp
auto line = ax.line(x, y);

spectra::timeline tl;
tl.at(0.0f).set(line.color(), spectra::rgb(1,0,0));
tl.at(2.0f).set(line.color(), spectra::rgb(0,0,1));
tl.at(0.0f).set(ax.xlim_prop(), {0.f, 5.f});
tl.at(2.0f).set(ax.xlim_prop(), {0.f, 20.f});
tl.easing(spectra::ease::cubic_in_out);

fig.animate().timeline(tl).duration(4.0f).loop(true).play();
```

**Offscreen export:**
```cpp
spectra::app app({.headless = true});
auto fig = app.figure({.width = 1920, .height = 1080});
auto ax  = fig.subplot(1, 1, 1);
ax.line(x, y);
fig.save_png("output.png");
```

**Video export:**
```cpp
fig.animate()
   .fps(60)
   .duration(10.0f)
   .on_frame([&](spectra::frame& f) { /* update */ })
   .record("output.mp4");  // pipes frames to ffmpeg
```

### B3. Data Interface

```cpp
// Accepts contiguous data via std::span (zero-copy when possible)
ax.line(std::span<const float> x, std::span<const float> y);

// std::vector works implicitly
std::vector<float> x, y;
ax.line(x, y);

// Optional Eigen adapter (behind SPECTRA_USE_EIGEN)
Eigen::VectorXf ex, ey;
ax.line(spectra::eigen_span(ex), spectra::eigen_span(ey));
```

---

## C. Shader Strategy & GPU Details

### C1. Shader Pipeline

- **Language:** GLSL 450 → compiled to SPIR-V offline via `glslangValidator` (CMake custom command).
- **Embedded:** SPIR-V bytecode embedded as `constexpr` arrays in headers (no runtime file I/O).
- **Pipelines:** One `VkPipeline` per visual type:
  - `line_pipeline` — quad-expanded polylines
  - `scatter_pipeline` — instanced circles/markers
  - `text_pipeline` — MSDF quad rendering
  - `grid_pipeline` — thin lines for grid/ticks
  - `heatmap_pipeline` — textured quad with colormap

### C2. Uniform / SSBO Layout

```glsl
// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 projection;       // orthographic for 2D plots
    vec2 viewport_size;
    float time;
    float _pad;
};

// Per-series push constant (minimal, fast path)
layout(push_constant) uniform SeriesPC {
    vec4 color;
    float line_width;
    float point_size;
    vec2 data_offset;      // for axis transform
};

// Vertex data via SSBO for large datasets (set 1, binding 0)
layout(std430, set = 1, binding = 0) readonly buffer VertexData {
    vec2 points[];
};
```

### C3. Anti-Aliased Lines Detail

1. Vertex shader reads consecutive point pairs from SSBO.
2. Computes screen-space perpendicular direction, extrudes ±(line_width/2 + 1px AA margin).
3. Emits 4 vertices per segment (2 triangles) with a `distance_to_edge` varying.
4. Fragment shader: `alpha = smoothstep(0.0, 1.0, line_width/2 - abs(distance_to_edge))`.
5. Miter joins: at shared vertices between segments, average the perpendicular directions; clamp miter length to avoid spikes.
6. Round caps at polyline endpoints via SDF semicircle in fragment shader.

---

## D. Directory Structure

```
spectra/
├── CMakeLists.txt                  # top-level; feature flags here
├── cmake/
│   ├── FindVulkan.cmake
│   ├── CompileShaders.cmake        # GLSL→SPIR-V custom commands
│   └── SpectraConfig.cmake.in
├── include/spectra/
│   ├── spectra.hpp                  # umbrella header
│   ├── app.hpp
│   ├── figure.hpp
│   ├── axes.hpp
│   ├── series.hpp                  # Series base + LineSeries, ScatterSeries
│   ├── color.hpp
│   ├── animator.hpp
│   ├── timeline.hpp
│   ├── frame.hpp
│   ├── export.hpp
│   └── fwd.hpp                    # forward declarations
├── src/
│   ├── core/                      # scene graph, data model, layout
│   │   ├── figure.cpp
│   │   ├── axes.cpp
│   │   ├── series.cpp
│   │   ├── layout.cpp             # subplot grid layout solver
│   │   └── transform.cpp          # data↔screen coordinate mapping
│   ├── render/
│   │   ├── renderer.cpp           # draw call orchestration, batching
│   │   ├── backend.hpp            # abstract Backend interface
│   │   └── vulkan/
│   │       ├── vk_backend.cpp     # VulkanBackend implementation
│   │       ├── vk_device.cpp      # device, queues, swapchain
│   │       ├── vk_pipeline.cpp    # pipeline cache + creation
│   │       ├── vk_buffer.cpp      # GpuBuffer, RingBuffer
│   │       └── vk_swapchain.cpp
│   ├── gpu/
│   │   ├── shaders/               # .vert/.frag GLSL source
│   │   │   ├── line.vert
│   │   │   ├── line.frag
│   │   │   ├── scatter.vert
│   │   │   ├── scatter.frag
│   │   │   ├── text.vert
│   │   │   ├── text.frag
│   │   │   ├── grid.vert
│   │   │   └── grid.frag
│   │   └── shader_spirv.hpp       # generated: embedded SPIR-V arrays
│   ├── text/
│   │   ├── text_renderer.cpp      # MSDF glyph batching + rendering
│   │   ├── font_atlas.cpp         # atlas texture management
│   │   └── embedded_font.cpp      # default font atlas binary data
│   ├── anim/
│   │   ├── animator.cpp
│   │   ├── timeline.cpp
│   │   ├── easing.cpp
│   │   └── frame_scheduler.cpp
│   ├── ui/
│   │   ├── app.cpp                # core app logic (headless)
│   │   ├── glfw_adapter.cpp       # optional: GLFW window + input
│   │   └── input.cpp              # pan/zoom logic
│   └── io/
│       ├── png_export.cpp         # stb_image_write
│       ├── video_export.cpp       # ffmpeg pipe (optional)
│       └── svg_export.cpp         # Phase 2+
├── third_party/
│   ├── stb/                       # stb_image_write (header-only)
│   └── vma/                       # Vulkan Memory Allocator (header-only)
├── examples/
│   ├── basic_line.cpp
│   ├── live_stream.cpp
│   ├── animated_scatter.cpp
│   ├── multi_subplot.cpp
│   ├── offscreen_export.cpp
│   └── video_record.cpp
├── tests/
│   ├── unit/
│   │   ├── test_transform.cpp
│   │   ├── test_layout.cpp
│   │   ├── test_timeline.cpp
│   │   ├── test_easing.cpp
│   │   └── test_ring_buffer.cpp
│   ├── golden/
│   │   ├── test_render_line.cpp
│   │   ├── test_render_scatter.cpp
│   │   ├── baseline/              # reference PNGs
│   │   └── compare.py             # pixel-diff tool
│   └── bench/
│       ├── bench_line_1m.cpp
│       └── bench_scatter_100k.cpp
└── docs/
    ├── architecture.md
    └── getting_started.md
```

---

## E. Animation System Design

### E1. Components

- **`FrameScheduler`**: Drives the render loop. Supports target FPS mode (sleep-based + spin-wait) and vsync mode. Computes `dt`, `elapsed`, frame number. Option for fixed timestep (e.g., 1/60s) with render interpolation for deterministic replay.
- **`Animator`**: Holds active `Timeline` objects. Each frame, evaluates all timelines at current time, produces property deltas, enqueues them as commands.
- **`Timeline`**: Ordered list of `Keyframe<T>` for a specific property. Supports easing functions (linear, cubic, ease-in-out, etc.). Interpolation via `lerp` / `slerp` as appropriate.
- **`frame`**: Passed to `on_frame` callback. Exposes `elapsed_seconds()`, `dt()`, `frame_number()`, `pause()`, `resume()`, `seek(float t)`.

### E2. Event Loop

```
while running:
    scheduler.begin_frame()
    drain command_queue            // apply app-thread mutations
    animator.evaluate(t)           // evaluate keyframe animations
    call on_frame(frame)           // user callback (app thread context via queue)
    renderer.build_commands()      // record Vulkan command buffers
    renderer.submit_and_present()  // submit to GPU, present
    scheduler.end_frame()          // sleep/vsync to hit target FPS
```

### E3. Streaming Data Path

- `Series::append(x, y)` writes into a CPU-side ring buffer.
- At frame start, dirty range is uploaded to GPU via staging buffer (async transfer queue if available).
- GPU vertex buffer is a ring buffer: old data is overwritten when window slides.
- `Axes::xlim()` auto-scroll mode: tracks latest data timestamp minus window width.

---

## F. Testing & CI

### F1. Test Strategy

| Layer | Tool | What |
|---|---|---|
| Unit | Google Test | Transform math, layout solver, easing functions, ring buffer, command queue |
| Golden image | Custom + headless Vulkan | Render known scenes offscreen, compare PNG against baselines (perceptual diff, threshold) |
| Perf regression | Google Benchmark | 1M-point line, 100K scatter, text rendering throughput |
| Integration | Examples as smoke tests | Build and run each example for N frames without crash |

### F2. CI Matrix

- **Linux (primary):** Ubuntu 22.04+, GCC 12+ / Clang 15+, Vulkan SDK (lavapipe for software raster in CI).
- **GPU-less fallback:** Use `VK_ICD_FILENAMES` pointing to lavapipe (Mesa's Vulkan software renderer). Golden images baselined against lavapipe output.
- **Windows/macOS:** Phase 2+ CI targets.
- **Sanitizers:** ASan + UBSan on every PR. TSan on threading tests.

---

## G. Phased Roadmap

### Phase 0 — Research & Prototypes (2–3 weeks)

- Vulkan bootstrap: device, swapchain, single-triangle render.
- Line rendering prototype: quad expansion + SDF AA in a standalone Vulkan app.
- MSDF text prototype: render "Hello World" with pre-baked atlas.
- Ring buffer prototype: append 100K points/sec, render scrolling line.
- **Exit criterion:** All 4 prototypes render correctly in a GLFW window + headless.

### Phase 1 — MVP / Shippable (6–8 weeks)

- Core API: `App`, `Figure`, `Axes`, `LineSeries`, `ScatterSeries`.
- Vulkan backend: pipelines, buffer management, swapchain, headless.
- Rendering: AA lines, instanced scatter, grid/ticks, MSDF text (labels, title, ticks).
- Layout: subplot grid, axis margins, auto-tick placement.
- Animation: `FrameScheduler`, `on_frame` callback, streaming append.
- Basic interaction: mouse pan/zoom.
- Export: offscreen PNG.
- Examples: basic_line, live_stream, offscreen_export.
- Tests: unit + golden image baseline.
- **Exit criterion:** A user can `#include <spectra/spectra.hpp>`, plot a live-updating line with labels, and export a PNG.

### Phase 2 — Interactivity + Video + More Plots (4–6 weeks)

- Keyframe animation system (`Timeline`, `Animator`, easing).
- Video export (ffmpeg pipe).
- Histogram, heatmap plot types.
- Legend rendering.
- Multi-figure / multi-window support.
- SVG export (basic: lines + text as SVG paths).
- Selection / hover tooltips (optional).
- Performance: GPU-side downsampling for >1M points.

### Phase 3 — Polish + ABI + Packaging (3–4 weeks)

- Pimpl for ABI stability on public headers.
- `pkg-config` / CMake `find_package` support.
- Conan / vcpkg recipe.
- Documentation: Doxygen API + user guide.
- Windows + macOS CI (MoltenVK).
- Theme system (dark/light, custom palettes).
- Plugin API for custom series types.

---

## H. Complexity Estimates & Key Risks

| Component | Effort | Risk | Mitigation |
|---|---|---|---|
| Vulkan bootstrap + abstraction | 2 weeks | Medium — lots of boilerplate | Use VMA for memory; keep abstraction thin |
| Anti-aliased line rendering | 1.5 weeks | **High** — miter joins are tricky | Start with MSAA fallback; SDF lines as stretch |
| MSDF text rendering | 2 weeks | **High** — atlas gen + kerning + layout | Pre-bake atlas; minimal layout engine (no wrapping) |
| Streaming ring buffer | 1 week | Low | Well-understood pattern |
| Animation/timeline | 1.5 weeks | Low | Standard keyframe interpolation |
| Frame scheduler / vsync | 0.5 weeks | Low | GLFW handles vsync; manual for headless |
| Layout engine (ticks, margins) | 1.5 weeks | Medium — lots of edge cases | Start simple: fixed margins, Wilkinson tick algorithm |
| Offscreen PNG export | 0.5 weeks | Low | `vkCmdCopyImageToBuffer` + stb_image_write |
| Video export (ffmpeg) | 1 week | Medium — process piping | Phase 2; optional dependency |
| SVG export | 2 weeks | Medium — different render path | Phase 2; can be simplified (no AA needed) |
| CI + golden image infra | 1 week | Medium — lavapipe consistency | Generous pixel thresholds; per-driver baselines |

---

## I. Concrete First 10 Tasks

1. **CMake skeleton** — top-level CMakeLists.txt with feature flags (`SPECTRA_USE_GLFW`, `SPECTRA_USE_FFMPEG`, `SPECTRA_USE_EIGEN`), directory structure, find Vulkan SDK.
2. **Vulkan device wrapper** — `VulkanBackend` init: instance, physical device selection, logical device, VMA allocator, single graphics queue.
3. **Swapchain + GLFW window** — Create GLFW window, Vulkan surface, swapchain, framebuffers, render pass. Render a clear color.
4. **Headless framebuffer** — Offscreen `VkImage` + `VkFramebuffer`, render to it, readback to CPU, write PNG via stb_image_write.
5. **Shader compilation CMake** — `CompileShaders.cmake`: glob `src/gpu/shaders/*.vert/*.frag`, compile to SPIR-V, embed as C++ header.
6. **Line rendering pipeline** — `line.vert` / `line.frag` with quad expansion + SDF AA. Render a hardcoded polyline. Validate visually.
7. **GpuBuffer + RingBuffer** — RAII buffer classes wrapping VMA allocations. Staging upload path. Unit test: upload 1M floats, readback, verify.
8. **Core data model** — `Figure`, `Axes`, `LineSeries` classes with `std::span` interfaces. No rendering yet — just data storage + coordinate transform math.
9. **Renderer integration** — Connect data model → line pipeline. Render a `LineSeries` with correct data-to-screen transform (orthographic projection + axis limits).
10. **Frame scheduler + animation loop** — `FrameScheduler` with target FPS, `on_frame` callback, `elapsed`/`dt`. Demo: animated sine wave updating every frame.

---

## J. Dependencies Summary

| Dependency | Required? | Purpose |
|---|---|---|
| Vulkan SDK (1.2+) | **Yes** | Rendering backend |
| VMA (header-only) | **Yes** (bundled) | Vulkan memory allocation |
| stb_image_write (header-only) | **Yes** (bundled) | PNG export |
| GLFW 3.3+ | Optional (`SPECTRA_USE_GLFW`) | Windowing + input |
| glslangValidator | Build-time | GLSL → SPIR-V compilation |
| Google Test | Dev only | Unit tests |
| Google Benchmark | Dev only | Perf benchmarks |
| FreeType + msdfgen | Optional (`SPECTRA_USE_FREETYPE`) | Runtime font atlas generation |
| ffmpeg (CLI) | Optional (`SPECTRA_USE_FFMPEG`) | Video export |
| Eigen | Optional (`SPECTRA_USE_EIGEN`) | Eigen::Vector adapters |

---

## K. Design Principles

- **No global state.** All state owned by `App` → `Figure` → `Axes` → `Series` hierarchy. RAII throughout.
- **Pimpl for public headers** (Phase 3). Internal headers can expose implementation.
- **No macros** except `SPECTRA_USE_*` feature flags and `SPECTRA_ASSERT` (debug only).
- **Extensibility:** New plot types inherit from `Series` base, register a pipeline + shader pair. Renderer dispatches by series type via virtual `record_commands()`.
- **Zero-copy where possible:** `std::span` interfaces avoid copies; GPU upload from user memory directly.
- **Minimal allocations in hot path:** Pre-allocated command buffers, ring buffers, arena allocators for per-frame scratch.

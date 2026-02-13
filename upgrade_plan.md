# Plotix "Next Level" Roadmap & Multi-Agent Execution Plan

A production-grade plan to evolve Plotix from a working GPU-accelerated MVP into a best-in-class interactive plotting/visualization application, grounded in a line-by-line codebase audit.

---

## A. Current State Assessment

### A1. Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                        User Code (main)                          │
│  App app; auto& fig = app.figure({...}); auto& ax = fig.subplot │
└────────────────────┬─────────────────────────────────────────────┘
                     │ app.run()
                     ▼
┌──────────────────────────────────────────────────────────────────┐
│  App::run()  (src/ui/app.cpp)                                    │
│  ┌─ FrameScheduler (sleep+spin-wait FPS targeting)               │
│  ├─ CommandQueue.drain() (lock-free SPSC, not yet wired to UI)   │
│  ├─ Animator.evaluate() (evaluates Timelines, no property bind)  │
│  ├─ on_frame callback (user mutation)                            │
│  ├─ Figure::compute_layout() → subplot grid rects               │
│  ├─ Backend::begin_frame() → VkCommandBuffer begin              │
│  ├─ Renderer::render_figure() → per-Axes:                       │
│  │   ├─ set_viewport/set_scissor                                │
│  │   ├─ upload FrameUBO (ortho projection)                      │
│  │   ├─ render_grid (placeholder — no buffer upload yet)        │
│  │   └─ per-Series: upload SSBO → bind pipeline → draw          │
│  ├─ Backend::end_frame() → submit + present                     │
│  └─ GlfwAdapter::poll_events()                                   │
└──────────────────────────────────────────────────────────────────┘

Vulkan Backend Stack:
  VulkanBackend → VkDevice (vk_device) + SwapchainContext/OffscreenContext (vk_swapchain)
               → PipelineConfig → create_graphics_pipeline (vk_pipeline)
               → GpuBuffer / RingBuffer / staging_upload (vk_buffer)

Data Model:
  App → [Figure] → [Axes] → [Series (LineSeries | ScatterSeries)]
  Series stores std::vector<float> x_, y_; dirty flag for GPU re-upload
```

### A2. Module Inventory (What Exists Today)

| Module | Files | Status | Notes |
|--------|-------|--------|-------|
| **Public API** | `include/plotix/*.hpp` (11 headers) | **Solid** | Clean interfaces, fluent API, forward decls |
| **Core data model** | `src/core/{series,axes,figure,layout,transform}.cpp` | **Functional** | Tick gen (nice-numbers), layout solver, ortho projection, auto-fit limits |
| **Vulkan backend** | `src/render/vulkan/vk_{backend,device,swapchain,pipeline,buffer}.{hpp,cpp}` | **Functional** | Instance, device, swapchain, offscreen FB, pipelines, buffers, sync, descriptor sets, readback |
| **Renderer** | `src/render/{renderer,backend}.{hpp,cpp}` | **Functional** | Per-series SSBO upload, ortho projection, line/scatter draw calls |
| **Shaders** | `src/gpu/shaders/{line,scatter,grid,text}.{vert,frag}` | **Functional (3/4)** | Line (quad-expand+SDF AA), Scatter (instanced SDF circle). Grid shader exists but not wired. Text shader exists but no atlas. |
| **Text system** | `src/text/{font_atlas,text_renderer,embedded_font}.{hpp,cpp}` | **Stub** | FontAtlas parses JSON+PNG, TextRenderer generates quads, BUT embedded font is a 1×1 white pixel placeholder. No real MSDF atlas. |
| **Animation** | `src/anim/{easing,timeline,animator,frame_scheduler}.cpp` | **Functional** | 6 easing functions, Timeline with keyframe interpolation, FrameScheduler with sleep+spin-wait. Animator evaluates but has no property binding. |
| **UI / App** | `src/ui/{app,glfw_adapter,input,command_queue}.{hpp,cpp}` | **Partially functional** | App::run() drives the loop. GlfwAdapter creates window+surface. InputHandler has pan/zoom logic BUT is never instantiated or connected in app.cpp. CommandQueue exists but only drain() is called (nothing pushes). |
| **Export** | `src/io/{png_export,video_export,svg_export}.cpp` | **PNG works; rest stub** | PNG via stb_image_write. Video via ffmpeg pipe (implemented, behind flag). SVG is a comment placeholder. |
| **Tests** | `tests/unit/test_{easing,layout,ring_buffer,transform,timeline}.cpp` | **5 test files** | Easing, layout, ring_buffer tests are thorough. test_transform and test_timeline exist (not read but registered in CMake). No golden image tests yet. |
| **Examples** | `examples/{basic_line,live_stream,animated_scatter,multi_subplot,offscreen_export,video_record}.cpp` | **6 examples** | All link against plotix. |
| **Build** | `CMakeLists.txt`, `cmake/{CompileShaders,EmbedShaders}.cmake` | **Solid** | C++20, feature flags, FetchContent for GLFW/GTest, shader compilation |

### A3. Pain Points & Refactor Candidates

| # | Issue | Severity | Location |
|---|-------|----------|----------|
| P1 | **Grid rendering is a no-op** — `render_grid()` computes ticks but never uploads vertices or draws | High | `renderer.cpp:170-196` |
| P2 | **Text rendering not connected** — TextRenderer/FontAtlas exist but Renderer never calls them; no real MSDF atlas embedded | High | `renderer.cpp:167` (TODO comment), `embedded_font.cpp` (1×1 pixel) |
| P3 | **InputHandler never instantiated** — `input.cpp` has working pan/zoom code but `app.cpp` never creates or wires an InputHandler | High | `app.cpp` (no mention of InputHandler) |
| P4 | **CommandQueue unused** — created in `app.cpp:66` but nothing pushes commands; mutations happen on same thread anyway | Medium | `app.cpp:66, 102` |
| P5 | **GlfwAdapter leaked via raw `new`** — `app.cpp:77` uses `new GlfwAdapter()` with manual `delete` | Medium | `app.cpp:77,165` |
| P6 | **Debug fprintf scattered** — `static bool once` debug prints in renderer and backend | Low | `renderer.cpp:223-225`, `vk_backend.cpp:505-530` |
| P7 | **No legend rendering** | High | Nowhere in codebase |
| P8 | **No axis border/frame** — only grid lines, no box around plot area | Medium | — |
| P9 | **`record_commands()` virtual is unused** — Series has a virtual method that does nothing; renderer dispatches by dynamic_cast instead | Low | `series.cpp:35-38,68-70` |
| P10 | **VMA not actually used** — `vk_buffer.cpp` manually calls `vkAllocateMemory`; the bundled `vk_mem_alloc.h` header is never included | Medium | `vk_buffer.cpp`, `third_party/vma/` |
| P11 | **Single-figure limitation** — `App::run()` only drives `figures_[0]` | Medium | `app.cpp:64` |
| P12 | **No window resize handling** — swapchain not recreated on resize | Medium | `app.cpp` (no framebuffer_size_callback wiring) |
| P13 | **Animator has no property binding** — evaluates timelines but results are discarded (no `set_property()` callback) | Medium | `animator.cpp:21-29` |
| P14 | **STB_IMAGE_IMPLEMENTATION in library source** — `font_atlas.cpp:9` defines the stb impl; will ODR-violate if user also includes stb | Low | `font_atlas.cpp` |

### A4. Quick Wins vs Deep Work

**Quick wins (< 1 day each):**
- Wire InputHandler into App::run() (P3)
- Replace raw `new GlfwAdapter` with `std::unique_ptr` (P5)
- Remove debug fprintf statements (P6)
- Upload grid vertices and issue draw call (P1) — tick positions already computed
- Handle window resize → recreate_swapchain (P12)
- Draw axis border rectangle (P8)

**Deep work (multi-day):**
- Integrate real MSDF font atlas and wire text rendering end-to-end (P2)
- Legend rendering (P7)
- Multi-figure/multi-window support (P11)
- Property binding for animator (P13)
- Full interaction system: box zoom, reset view, hover, keyboard shortcuts
- Data tools: decimation, smoothing, scatter↔line toggle
- SVG/PDF export
- Theme/style system

---

## B. Proposed Architecture Upgrades

### B1. Data Model Enhancements

```
Figure
  ├── config: FigureConfig (width, height, title, theme)
  ├── axes_: vector<unique_ptr<Axes>>     [exists]
  ├── legend_: optional<LegendConfig>     [NEW]
  └── style_: FigureStyle                 [NEW]

Axes
  ├── series_: vector<unique_ptr<Series>> [exists]
  ├── xlim_, ylim_: optional<AxisLimits>  [exists]
  ├── title_, xlabel_, ylabel_            [exists]
  ├── grid_enabled_                       [exists]
  ├── x_axis_style_: AxisStyle            [NEW: line color/width, tick length, label font size]
  ├── y_axis_style_: AxisStyle            [NEW]
  ├── show_border_: bool                  [NEW: draw box around plot area]
  └── autoscale_mode_: enum {Manual,AutoFit,AutoFollow}  [NEW]

Series
  ├── label_, color_, dirty_              [exists]
  ├── visible_: bool                      [NEW: toggle series visibility]
  ├── style_: SeriesStyle                 [NEW: per-series line dash, marker shape]
  └── max_points_: optional<size_t>       [NEW: ring-buffer limit for streaming]

NEW: PropertySystem
  ├── PropertyRef<T>: typed reference to an animatable property
  └── Binds Timeline evaluation → actual property mutation
```

### B2. Rendering Pipeline Organization

```
Renderer::render_figure(fig)
  │
  ├── 1. Clear background (existing)
  ├── 2. Per-Axes:
  │     ├── a. Set viewport + scissor (existing)
  │     ├── b. Upload FrameUBO with ortho projection (existing)
  │     ├── c. Draw axis border rectangle [NEW]
  │     ├── d. Draw grid lines (FIX: upload vertices)
  │     ├── e. Draw each Series (existing)
  │     ├── f. Draw tick labels via TextRenderer [NEW]
  │     ├── g. Draw axis labels (xlabel/ylabel) [NEW]
  │     └── h. Draw title [NEW]
  │
  ├── 3. Draw legend [NEW]
  └── 4. (Future) Draw UI overlays: cursor readout, selection box
```

**Text rendering integration plan:**
1. Bake a real Roboto MSDF atlas at build time (msdf-atlas-gen → PNG + JSON)
2. At init, FontAtlas::load_embedded() → TextRenderer::init()
3. Create a VkImage texture from atlas pixels, upload via staging buffer
4. Renderer holds TextRenderer*, calls generate_quads() for each text element
5. Upload text vertex buffer, bind text pipeline + atlas texture descriptor, draw

### B3. Input & Interaction System

```
EventRouter [NEW]
  ├── Dispatches GLFW callbacks to appropriate handler
  ├── Hit-tests mouse position against Axes viewports
  ├── Routes to active InputHandler for that Axes
  └── Manages gesture state machine

Gesture State Machine:
  Idle → Pan (left-drag) | BoxZoom (right-drag) | Scroll (scroll wheel)
  BoxZoom → draws rubber-band rect → sets xlim/ylim on release

Keyboard Shortcuts [NEW]:
  'r' → reset view (auto_fit)
  'a' → autoscale toggle
  'g' → toggle grid
  'l' → toggle legend
  Ctrl+S → save PNG
  Escape → cancel box zoom

Cursor Readout [NEW]:
  - On hover: display (x,y) coordinates near cursor
  - Requires text rendering to be functional
```

### B4. UI Layer Approach

**Decision: No external UI framework in Phase 1-2.** All rendering done through Vulkan shaders (text via MSDF, UI elements via grid/line pipelines). This keeps the dependency tree minimal.

**Phase 3 option:** If a style editor / docking panel is desired, evaluate:
- **Dear ImGui** (lowest friction — single header, Vulkan backend exists, overlay mode)
- **Qt6** (heavyweight, but best for a full application with docking)

Recommendation: Add Dear ImGui as optional (`PLOTIX_USE_IMGUI`) in Phase 3 for a debug/style panel overlay.

---

## C. Detailed Feature Backlog (Prioritized)

### Phase 1: Feature-Complete Core Plotting

| # | Feature | Complexity | Risk | Dependencies | Test Approach |
|---|---------|-----------|------|-------------|---------------|
| F1 | **Wire InputHandler (pan/zoom)** | S | Low | None | Manual test: drag to pan, scroll to zoom. Unit test: screen_to_data mapping. |
| F2 | **Grid line rendering** | S | Low | None (shader exists) | Golden image: grid lines match tick positions |
| F3 | **Axis border (box frame)** | S | Low | Grid pipeline | Golden image |
| F4 | **Real MSDF font atlas** | L | **High** | msdf-atlas-gen build tool | Visual inspection; measure_text unit tests |
| F5 | **Tick label rendering** | M | Med | F4 (text) | Golden image: tick labels positioned correctly |
| F6 | **Axis labels (xlabel/ylabel)** | M | Med | F4 (text) | Golden image |
| F7 | **Title rendering** | S | Low | F4 (text) | Golden image |
| F8 | **Legend rendering** | M | Med | F4 (text) | Golden image: legend box with colored lines + labels |
| F9 | **Window resize handling** | S | Med | Swapchain recreate (exists) | Manual test: resize window, no crash |
| F10 | **Headless PNG export** (fix pipeline) | S | Low | F2-F7 for visual completeness | Golden image regression suite |
| F11 | **Autoscale / reset view** | S | Low | F1 | Keyboard 'r' resets to auto_fit |
| F12 | **Box zoom** | M | Med | F1 | Manual test: right-drag selects region |
| F13 | **Cursor readout (hover tooltip)** | M | Med | F4 (text) | Manual test: hover shows coordinates |
| F14 | **Remove debug prints, fix GlfwAdapter leak** | S | Low | None | Code review |
| F15 | **Golden image test infrastructure** | M | Med | Headless render (exists) | CI: headless render → pixel diff |

### Phase 2: Pro UX + Data Tools

| # | Feature | Complexity | Risk | Dependencies | Test Approach |
|---|---------|-----------|------|-------------|---------------|
| F16 | **Theme system** (dark/light/custom palettes) | M | Low | Color, style structs | Golden images with different themes |
| F17 | **Per-series styling** (dash patterns, marker shapes) | L | Med | New shader variants or uniforms | Golden images |
| F18 | **Series visibility toggle** | S | Low | Series.visible_ flag | Unit test |
| F19 | **Colormap support** | M | Med | New color utilities | Unit test: colormap interpolation |
| F20 | **Data decimation / downsampling** (Largest-Triangle-Three-Buckets) | M | Med | None | Perf benchmark: 1M pts at 60fps; visual fidelity test |
| F21 | **Smoothing / moving average filter** | S | Low | None | Unit test: known input/output |
| F22 | **Scatter↔line toggle** | S | Low | Series type switch | Manual test |
| F23 | **SVG export** | L | Med | Figure/Axes/Series traversal | Validate SVG output against reference |
| F24 | **Multi-figure windows** | M | Med | Swapchain per window | Manual test |
| F25 | **Selection / hover highlight** | M | Med | Hit-testing per-point | Manual test |
| F26 | **Configurable keyboard shortcuts** | M | Low | Event router | Unit test: key binding map |
| F27 | **Style preset system** (matplotlib-like rc params) | M | Low | Theme system (F16) | Unit test |

### Phase 3: Elite Features

| # | Feature | Complexity | Risk | Dependencies | Test Approach |
|---|---------|-----------|------|-------------|---------------|
| F28 | **Animation editor / timeline UI** | XL | High | Dear ImGui or custom | Manual test |
| F29 | **Video export recording** (wire into app loop) | M | Med | ffmpeg pipe (exists) | Automated: record 60 frames, check MP4 |
| F30 | **PDF export** | L | High | Vector graphics traversal | Visual comparison |
| F31 | **Plugin architecture** (custom series types) | L | Med | Clean Series interface | API test: register + render custom type |
| F32 | **Histogram / bar chart** | M | Med | New pipeline or reuse grid | Golden image |
| F33 | **Heatmap with colormap** | L | Med | New pipeline (heatmap_pipeline exists as enum) | Golden image |
| F34 | **Pimpl ABI stability** | M | Low | All public headers | ABI compatibility test |
| F35 | **Docking / panel layout** (Dear ImGui overlay) | L | High | ImGui integration | Manual test |
| F36 | **Conan/vcpkg packaging** | M | Low | Stable API | Package build test |

---

## D. Multi-Agent Execution Plan (6 Agents)

### Agent 1: Core Plotting Model + Text Integration
**Owns:** `include/plotix/`, `src/core/`, `src/text/`

**Contract:** Public headers are the shared contract. No other agent modifies `include/plotix/`. Any interface additions go through Agent 1.

**Deliverables:**
1. Bake a real Roboto MSDF atlas (msdf-atlas-gen) → replace `embedded_font.cpp` placeholder
2. Add `LegendConfig`, `AxisStyle`, `SeriesStyle` structs to public headers
3. Add `Series::visible()` toggle
4. Add `Axes::show_border()`, `Axes::autoscale_mode()`
5. Improve tick algorithm: handle edge cases (negative ranges, very small ranges, log scale prep)
6. Wire FontAtlas::load_embedded() into Renderer init path

**Files touched:** `include/plotix/{axes,figure,series}.hpp`, `src/core/{axes,series,figure}.cpp`, `src/text/embedded_font.cpp`, new `src/text/atlas_data/` (baked atlas files)

**Tests:** `test_tick_generation` (edge cases), `test_series_visibility`, `test_font_atlas_load`

**PR boundaries:** PR1: real font atlas. PR2: style structs + visibility. PR3: tick improvements.

### Agent 2: Rendering Upgrades (Grid, Text, Legend, Batching)
**Owns:** `src/render/`

**Contract:** Uses `Backend` abstract interface. Must call `TextRenderer::generate_quads()` for text. Must not modify public headers (request additions from Agent 1).

**Deliverables:**
1. **Fix grid rendering** — upload grid line vertices to a buffer, draw with grid pipeline
2. **Wire text rendering** — integrate TextRenderer into Renderer; create VkImage texture for atlas
3. **Render tick labels** — position text at tick positions, transform from data space
4. **Render xlabel, ylabel, title** — centered text positioning
5. **Render legend** — box with colored line samples + label text
6. **Draw axis border** — 4 lines around plot area
7. **Remove debug fprintf**, clean up `static bool once` patterns
8. **Implement `bind_texture()`** in VulkanBackend (currently a no-op)
9. **VMA integration** — replace manual `vkAllocateMemory` with VMA (optional, can defer)

**Files touched:** `src/render/{renderer.hpp,renderer.cpp}`, `src/render/vulkan/vk_backend.cpp`, potentially `src/render/vulkan/vk_buffer.cpp`

**Tests:** Golden images: grid, tick labels, title, legend. Headless render comparison.

**PR boundaries:** PR1: grid + axis border. PR2: text rendering (tick labels, axis labels, title). PR3: legend. PR4: cleanup.

### Agent 3: Input/Interaction + Shortcuts + UX Behaviors
**Owns:** `src/ui/`

**Contract:** Uses `Axes::xlim()`, `Axes::ylim()`, `Axes::auto_fit()`. Uses `GlfwAdapter::set_callbacks()`. Must not modify rendering code.

**Deliverables:**
1. **Wire InputHandler into App::run()** — create InputHandler, set_callbacks on GlfwAdapter, route events
2. **Hit-test mouse position** → find which Axes the cursor is over
3. **Box zoom** — right-drag draws rectangle, releases sets limits
4. **Reset view** — 'r' key calls `auto_fit()` on active Axes
5. **Window resize handling** — `on_resize` callback → `recreate_swapchain()`
6. **Replace raw `new GlfwAdapter` with `unique_ptr`**
7. **Cursor readout overlay** (depends on Agent 2 text rendering being available)
8. **Keyboard shortcut map** — configurable bindings (initially hardcoded)

**Files touched:** `src/ui/{app.cpp,input.hpp,input.cpp,glfw_adapter.hpp,glfw_adapter.cpp}`

**Tests:** Unit: `screen_to_data` mapping with known values. Integration: headless pan/zoom simulation (programmatic).

**PR boundaries:** PR1: wire input + pan/zoom + resize. PR2: box zoom + reset. PR3: keyboard shortcuts + cursor readout.

### Agent 4: Export Pipeline (PNG/SVG/Video) + Headless
**Owns:** `src/io/`

**Contract:** Uses `Backend::readback_framebuffer()` for pixel data. Uses `Figure`/`Axes`/`Series` read-only for SVG traversal.

**Deliverables:**
1. **SVG export** — traverse Figure→Axes→Series, emit `<svg>`, `<line>`, `<circle>`, `<text>` elements
2. **Wire video recording into App::run()** — when `AnimationBuilder::record()` is called, create VideoExporter, pipe frames
3. **Fix STB_IMAGE_IMPLEMENTATION ODR issue** — move to a single compilation unit
4. **Multi-resolution PNG export** — `save_png("out.png", {.width=3840, .height=2160})` renders at custom resolution
5. **Headless batch mode** — render multiple figures sequentially without window

**Files touched:** `src/io/{svg_export.cpp,video_export.cpp,png_export.cpp}`

**Tests:** SVG: parse output XML, verify element count matches series. Video: record 10 frames, verify file exists and is valid. PNG: golden image at multiple resolutions.

**PR boundaries:** PR1: SVG export. PR2: video recording wiring. PR3: multi-resolution + batch.

### Agent 5: Data Tools + Performance
**Owns:** new `src/data/` directory

**Contract:** Provides free functions/classes that operate on `std::span<const float>`. Does not modify Series directly — provides processed data that user feeds back.

**Deliverables:**
1. **LTTB decimation** (Largest-Triangle-Three-Buckets) — reduce N points to M representative points
2. **Moving average / exponential smoothing filter**
3. **Min-max decimation** — for each pixel column, keep min+max (GPU-friendly)
4. **Resampling** — uniform resampling of irregular data
5. **Performance benchmark harness** — 1M-point line rendering, 100K scatter, measure FPS
6. **GPU-side LOD** — compute visible range, only upload/draw points in [xlim.min, xlim.max] (optimization)

**Files touched:** new `src/data/{decimation.hpp,decimation.cpp,filters.hpp,filters.cpp}`, `tests/bench/`

**Tests:** Unit: LTTB with known input → verify output size + key points preserved. Benchmark: 1M points target ≥60 FPS.

**PR boundaries:** PR1: LTTB + moving average. PR2: min-max + resampling. PR3: benchmark harness + GPU LOD.

### Agent 6: Testing/CI/Golden Images
**Owns:** `tests/`, `docs/`, CI config

**Contract:** Read-only access to all source. Creates test infrastructure that other agents' tests plug into.

**Deliverables:**
1. **Golden image test framework** — headless render → PNG → pixel diff against baseline (tolerance threshold)
2. **Baseline images** — render known scenes, store in `tests/golden/baseline/`
3. **CI pipeline config** (GitHub Actions) — build, run unit tests, run golden tests with lavapipe
4. **Sanitizer runs** — ASan + UBSan in CI
5. **Performance regression tracking** — store benchmark results, alert on regressions
6. **API documentation** — Doxygen config, getting started guide
7. **Add missing tests**: test_command_queue (real SPSC queue from `src/ui/`), test_series_data (set_x/set_y/append)

**Files touched:** `tests/golden/`, `tests/bench/`, `.github/workflows/`, `docs/`

**Tests:** Meta: verify the test framework itself catches intentional pixel diffs.

**PR boundaries:** PR1: golden image framework + 5 baseline scenes. PR2: CI pipeline. PR3: benchmarks + docs.

### Agent Dependency Graph

```
Time ──────────────────────────────────────────────────────▶

Agent 1: [MSDF atlas]────────[style structs]───[tick improvements]──▶
              │ (atlas ready)
              ▼
Agent 2: ····[grid+border]──[text rendering]──[legend]──[cleanup]───▶
                                   │
Agent 3: [wire input]──[box zoom]──[resize]──[cursor readout]───────▶
                                                  ↑ needs text
Agent 4: [SVG export]──[video wiring]──[multi-res PNG]──────────────▶

Agent 5: [LTTB decimation]──[filters]──[benchmarks]──[GPU LOD]─────▶

Agent 6: [golden framework]──[baselines]──[CI pipeline]──[docs]─────▶
```

**Critical path:** Agent 1 (MSDF atlas) → Agent 2 (text rendering) → everything that displays text.

**Parallel from day 1:** Agents 3, 4, 5, 6 can start immediately on non-text work.

### Conflict Avoidance Rules

1. **Only Agent 1 modifies `include/plotix/`** — other agents request additions via shared TODO list
2. **Only Agent 2 modifies `src/render/`**
3. **Only Agent 3 modifies `src/ui/`**
4. Each agent owns their `src/` subdirectory exclusively
5. If Agent 3 needs a new method on Axes (e.g., `set_xlim_animated()`), they document the need and Agent 1 adds it
6. Examples (`examples/`) can be modified by any agent adding demos for their feature, but coordinated to avoid conflicts

---

## E. Phased Roadmap with Acceptance Criteria

### Phase 1: Feature-Complete Core Plotting (3 weeks)

**Exit criteria:**
- [ ] A user can `#include <plotix/plotix.hpp>`, create a line plot with title, axis labels, tick labels, grid, and legend — all rendered correctly
- [ ] Mouse pan/zoom works in windowed mode
- [ ] Headless PNG export produces a complete plot (not just data — includes text, grid, legend)
- [ ] Golden image tests pass for: single line plot, scatter plot, multi-subplot, empty axes
- [ ] Window resize does not crash
- [ ] All existing unit tests still pass
- [ ] No debug fprintf in release build

**Demo scenarios:**
1. `basic_line` example opens window with titled, labeled, gridded line plot with legend; user can pan/zoom
2. `offscreen_export` produces a PNG that matches golden baseline within 1% pixel tolerance
3. `multi_subplot` renders 2×1 grid with independent axes, each with tick labels

### Phase 2: Pro UX + Data Tools (3 weeks)

**Exit criteria:**
- [ ] Box zoom: right-drag selects a rectangle, release zooms to that region
- [ ] Reset view: 'r' key returns to auto-fit
- [ ] Theme system: `fig.set_theme(plotix::themes::dark)` changes all colors
- [ ] Per-series styling: dash patterns, marker shapes (at least circle, square, triangle)
- [ ] Series visibility toggle: click legend entry to hide/show series
- [ ] SVG export produces valid SVG for line + scatter plots
- [ ] LTTB decimation: 1M points decimated to 2000, renders at 60fps
- [ ] Video recording: `fig.animate().record("out.mp4")` produces valid MP4
- [ ] Performance: 1M-point line plot maintains ≥ 30 FPS with pan/zoom on discrete GPU

**Demo scenarios:**
1. Live data stream at 1M points with LTTB, smooth 60fps, dark theme
2. Export publication-quality SVG from headless
3. Record 10-second animated scatter to MP4

### Phase 3: Elite Features (3 weeks)

**Exit criteria:**
- [ ] Histogram / bar chart type works
- [ ] Heatmap with colormap works
- [ ] Plugin API: user can register a custom `Series` subclass with custom shader
- [ ] Dear ImGui overlay for style editing (behind `PLOTIX_USE_IMGUI`)
- [ ] Pimpl on all public headers — ABI stable across minor versions
- [ ] `find_package(plotix)` works from an install
- [ ] CI runs on Linux (GCC + Clang), optionally Windows
- [ ] API documentation published (Doxygen + user guide)

**Demo scenarios:**
1. Custom heatmap plot with jet colormap, axis labels, exported as PNG
2. ImGui panel: adjust line width, color, toggle grid — see changes live
3. Plugin demo: user-defined "waterfall" plot type

---

## F. Engineering Quality Requirements

### F1. No Global State
**Current status:** ✅ No global state. All state owned by App → Figure → Axes → Series hierarchy. RAII throughout.
**Keep it this way.** Any new singletons (e.g., shader cache, font cache) must be owned by App or Backend.

### F2. Thread Safety
**Model:** Single-threaded for Phase 1-2. CommandQueue exists for future dual-thread model.
- **Render thread = App thread** (same thread runs user callbacks + Vulkan commands)
- **Phase 3:** Split into app thread (user code, input) + render thread (Vulkan). CommandQueue bridges them.
- **Rule:** All Series/Axes mutation must happen before `Renderer::render_figure()`. Currently enforced by sequential execution in `App::run()`.

### F3. Deterministic Input Handling
- Input events processed once per frame via `glfwPollEvents()` at a fixed point in the loop
- `FrameScheduler` already supports fixed timestep for deterministic replay
- **New requirement:** Record input events for replay in golden image tests

### F4. Test Strategy

| Layer | Tool | Coverage Target |
|-------|------|----------------|
| Unit | Google Test | Transform math, layout solver, easing, ring buffer, tick generation, decimation, filters, color utilities |
| Golden image | Headless Vulkan + pixel diff | Line plot, scatter plot, grid, tick labels, legend, multi-subplot, themes |
| Performance | Google Benchmark | 1M-line FPS, 100K-scatter FPS, text rendering throughput, LTTB decimation time |
| Integration | Examples as smoke tests | Each example runs for 10 frames without crash |
| Sanitizers | ASan + UBSan in CI | All tests pass under sanitizers |

### F5. Performance Benchmarks

| Metric | Target | Current Estimate |
|--------|--------|-----------------|
| 1M-point line, 60fps | ≥ 30 FPS | Likely achievable (SSBO, quad expansion) |
| 100K scatter, 60fps | ≥ 60 FPS | Instanced rendering should be fast |
| Text rendering (1000 glyphs) | < 1ms | Single draw call with MSDF atlas |
| Headless PNG export (1920×1080) | < 500ms | Vulkan readback + stb_write |
| LTTB 1M→2000 points | < 10ms | O(n) algorithm |

---

## G. First 2-Week Sprint Plan

### Week 1: Foundation Fixes + Text

| Day | Task | Agent | Definition of Done | Merge Point |
|-----|------|-------|--------------------|-------------|
| D1 | Bake real Roboto MSDF atlas, replace embedded_font.cpp | A1 | FontAtlas::load_embedded() returns true, atlas is >1 KB | ✅ Merge: atlas data |
| D1 | Wire InputHandler into App::run(), connect GLFW callbacks | A3 | Pan/zoom works in basic_line example | ✅ Merge: input wiring |
| D1 | Set up golden image test framework (headless render → diff) | A6 | Framework runs, produces PNG from headless, diff script works | ✅ Merge: test framework |
| D2 | Fix grid rendering — upload vertices, issue draw call | A2 | Grid lines visible in basic_line at correct tick positions | ✅ Merge: grid rendering |
| D2 | Draw axis border (box around plot area) | A2 | 4 lines visible around each subplot | Merge with grid PR |
| D2 | Handle window resize → recreate_swapchain | A3 | Resize window, no crash, render continues | ✅ Merge: resize |
| D2 | Begin SVG export implementation | A4 | SVG header + line elements emitted | — |
| D3 | Implement bind_texture() in VulkanBackend | A2 | Create VkImage from atlas, bind to descriptor set | ✅ Merge: texture support |
| D3 | Wire TextRenderer into Renderer — render tick labels | A2 | Tick labels visible at correct positions | — |
| D3 | LTTB decimation algorithm | A5 | Unit test: 1000 pts → 100 pts, key points preserved | ✅ Merge: decimation |
| D4 | Render xlabel, ylabel, title via TextRenderer | A2 | Title + axis labels visible in basic_line | ✅ Merge: all text rendering |
| D5 | Render legend (box + colored lines + labels) | A2 | Legend visible with series labels | ✅ Merge: legend |
| D5 | Remove debug prints, fix GlfwAdapter leak | A3 | No stderr output in release, valgrind clean | ✅ Merge: cleanup |
| D5 | Generate 5 golden image baselines | A6 | Baselines in tests/golden/baseline/ | ✅ Merge: baselines |

### Week 2: Interaction + Export + Polish

| Day | Task | Agent | Definition of Done | Merge Point |
|-----|------|-------|--------------------|-------------|
| D6 | Box zoom (right-drag → region select → zoom) | A3 | Right-drag selects, release zooms, golden test passes | — |
| D6 | Reset view ('r' key → auto_fit) | A3 | Press 'r', view resets to data extent | ✅ Merge: box zoom + reset |
| D6 | Complete SVG export (line + scatter + text) | A4 | SVG opens in browser, matches plot visually | ✅ Merge: SVG export |
| D7 | Wire video recording into App::run() | A4 | animated_scatter records 3-second MP4 | ✅ Merge: video recording |
| D7 | Moving average + exponential smoothing | A5 | Unit tests pass, demo with noisy data | ✅ Merge: filters |
| D7 | Add style structs (AxisStyle, SeriesStyle, FigureStyle) | A1 | Structs in headers, defaults match current look | ✅ Merge: style structs |
| D8 | Series visibility toggle | A1 | series.visible(false) hides it from render | ✅ Merge: visibility |
| D8 | Performance benchmark harness | A5 | bench_line_1m and bench_scatter_100k run, report FPS | ✅ Merge: benchmarks |
| D8 | Cursor coordinate readout on hover | A3 | Hover shows (x,y) near cursor | ✅ Merge: readout |
| D9 | CI pipeline (GitHub Actions: build + test + golden) | A6 | CI green on push to main | ✅ Merge: CI |
| D9 | Integration test: all examples run 10 frames headless | A6 | No crashes, no Vulkan validation errors | Merge with CI |
| D10 | **Integration checkpoint** — all agents merge, resolve conflicts | ALL | Project builds, all tests pass, basic_line demo is complete | ✅ RELEASE: Phase 1 alpha |

### Definition of Done Checklist (per task)

- [ ] Code compiles with `-Wall -Wextra -Werror` (or equivalent)
- [ ] No new compiler warnings
- [ ] Existing unit tests still pass (`ctest`)
- [ ] New functionality has at least one test (unit or golden image)
- [ ] No Vulkan validation layer errors in debug build
- [ ] No memory leaks under ASan
- [ ] Code follows existing style (no global state, RAII, C++20)
- [ ] PR is scoped to a single logical change

---

## H. What Is NOT In Phase 1 (Explicit Exclusions)

- ❌ Theme system / dark mode
- ❌ Dash patterns / marker shapes
- ❌ Histogram, heatmap, bar chart
- ❌ Animation timeline editor
- ❌ Dear ImGui integration
- ❌ PDF export
- ❌ Plugin API
- ❌ Pimpl / ABI stability
- ❌ Conan / vcpkg packaging
- ❌ Multi-window support
- ❌ Docking panels
- ❌ Log-scale axes
- ❌ Colormap support
- ❌ GPU-side decimation
- ❌ Configurable keyboard shortcuts (hardcoded is fine)

These are all Phase 2+ features and will not be started until Phase 1 exit criteria are met.

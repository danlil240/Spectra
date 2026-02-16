# Spectra: Multi-Agent Task Decomposition

Strategy for splitting the Spectra development plan across parallel Windsurf Cascade sessions to maximize throughput and minimize conflicts.

---

## Principles

1. **Non-overlapping file ownership** — each agent owns specific directories/files. No two agents write the same file.
2. **Interface-first** — Agent 1 (lead) establishes shared headers/interfaces first. Other agents start after those exist (~15 min head start).
3. **Copy the plan** — every agent gets `plan.md` as context. Each agent also gets a focused prompt below.
4. **Dependency order** — some agents can start immediately in parallel; others need a thin interface to exist first.

---

## Agent Layout (5 agents)

### Agent 1 — "Scaffold + Vulkan Core" (START FIRST)

**Owns:** `CMakeLists.txt`, `cmake/`, `include/spectra/`, `src/render/`, `third_party/`, `src/gpu/shaders/`

**Prompt to paste:**
```
Read /home/daniel/projects/spectra/plan.md for full context.

You are Agent 1 — Scaffold + Vulkan Core. You own these directories exclusively:
- CMakeLists.txt (top-level)
- cmake/
- include/spectra/ (ALL public headers)
- src/render/ (renderer + vulkan backend)
- third_party/ (stb, vma)

Your tasks IN ORDER:
1. Create the full directory structure from plan.md section D.
2. Create CMakeLists.txt with C++20, feature flags (PLOTIX_USE_GLFW, PLOTIX_USE_FFMPEG, PLOTIX_USE_EIGEN), find Vulkan SDK, subdirectories.
3. Create cmake/CompileShaders.cmake — find glslangValidator, add custom command to compile src/gpu/shaders/*.vert/*.frag to SPIR-V, generate src/gpu/shader_spirv.hpp with embedded constexpr arrays.
4. Create ALL public headers in include/spectra/ with forward declarations, class skeletons, and the interfaces other agents depend on:
   - fwd.hpp (forward decls for all core types)
   - color.hpp (rgb struct, colors namespace)
   - series.hpp (Series base class with virtual record_commands(), LineSeries, ScatterSeries)
   - axes.hpp (Axes class skeleton)
   - figure.hpp (Figure class skeleton)
   - app.hpp (App class skeleton)
   - frame.hpp (frame struct: elapsed_seconds, dt, frame_number)
   - animator.hpp, timeline.hpp (skeletons)
   - export.hpp (ImageExporter, VideoExporter skeletons)
   - spectra.hpp (umbrella include)
5. Create src/render/backend.hpp — abstract Backend interface (init, create_pipeline, create_buffer, begin_frame, end_frame, submit).
6. Implement src/render/vulkan/vk_backend.cpp — VulkanBackend: instance creation, physical device selection, logical device, VMA allocator init, single graphics queue.
7. Implement src/render/vulkan/vk_device.cpp — device helpers, queue family selection.
8. Implement src/render/vulkan/vk_swapchain.cpp — swapchain creation/recreation, framebuffers, render pass.
9. Implement src/render/vulkan/vk_pipeline.cpp — pipeline creation for line/scatter/grid/text (use SPIR-V from shader_spirv.hpp). Define descriptor set layouts, push constant ranges per plan section C2.
10. Implement src/render/vulkan/vk_buffer.cpp — GpuBuffer (RAII VMA allocation), RingBuffer (multi-frame staging), staging upload helper.
11. Implement src/render/renderer.cpp — Renderer class: owns pipelines, orchestrates draw calls, batches by series type.
12. Download/add third_party/stb/stb_image_write.h and third_party/vma/vk_mem_alloc.h (or add as git submodule / FetchContent).

CRITICAL: Create ALL include/spectra/ headers FIRST (tasks 1-5) before moving to implementation. Other agents depend on these interfaces. Keep implementations minimal but compilable.

Do NOT touch: src/core/, src/text/, src/anim/, src/ui/, src/io/, examples/, tests/
```

---

### Agent 2 — "Core Data Model + Layout"

**Owns:** `src/core/`

**Prompt to paste:**
```
Read /home/daniel/projects/spectra/plan.md for full context.

You are Agent 2 — Core Data Model + Layout. You own src/core/ exclusively.

Wait until include/spectra/ headers exist (Agent 1 creates them). Then implement:

1. src/core/series.cpp — Series base class, LineSeries (stores x/y data as std::vector<float>, supports set_x/set_y/append via std::span, tracks dirty flag for GPU upload), ScatterSeries (same + point size).
2. src/core/axes.cpp — Axes class: holds vector of Series, xlim/ylim (auto or manual), title/xlabel/ylabel strings, tick generation (Wilkinson algorithm or simple linear), grid enable/disable.
3. src/core/figure.cpp — Figure class: holds grid of Axes via subplot(rows, cols, index), manages layout rectangles, width/height.
4. src/core/layout.cpp — Subplot grid layout solver: given figure size and subplot grid, compute pixel rectangles for each Axes. Account for margins (fixed for now: 60px left, 40px right, 50px bottom, 40px top).
5. src/core/transform.cpp — Data-to-screen coordinate mapping: given axis limits + viewport rect, produce an orthographic projection matrix (glm-style mat4, or manual). Provide data_to_ndc() and ndc_to_screen() helpers.

Design notes:
- Use std::span<const float> for all data input interfaces.
- Series tracks a "dirty" flag — set on any data mutation, cleared after GPU upload.
- Axes auto-computes limits from data if xlim/ylim not explicitly set.
- Tick algorithm: simple approach — divide range into ~5-10 nice intervals (multiples of 1, 2, 5 × 10^n).

Do NOT touch any other directories. Implement against the interfaces in include/spectra/.
```

---

### Agent 3 — "Shaders + Text Rendering"

**Owns:** `src/gpu/shaders/`, `src/text/`

**Prompt to paste:**
```
Read /home/daniel/projects/spectra/plan.md for full context.

You are Agent 3 — Shaders + Text Rendering. You own:
- src/gpu/shaders/ (all .vert/.frag files)
- src/text/

Your tasks:

SHADERS (can start immediately):
1. src/gpu/shaders/line.vert — Reads point pairs from SSBO (vec2 points[]). Uses gl_VertexIndex to determine which of 4 quad vertices this is. Computes screen-space perpendicular, extrudes by line_width/2 + 1px AA margin. Outputs distance_to_edge varying. Uses FrameUBO (projection, viewport_size) and SeriesPC (color, line_width, data_offset) per plan section C2.
2. src/gpu/shaders/line.frag — Computes alpha via smoothstep on distance_to_edge for AA. Outputs color with alpha.
3. src/gpu/shaders/scatter.vert — Instanced rendering: each instance is a point (vec2 from SSBO). Expands to a screen-space quad of size point_size. Outputs local UV for SDF circle.
4. src/gpu/shaders/scatter.frag — SDF circle: discard fragments outside radius, smoothstep AA at edge.
5. src/gpu/shaders/grid.vert — Simple line rendering for grid lines and ticks. Takes start/end positions as vertex attributes.
6. src/gpu/shaders/grid.frag — Solid color output with configurable alpha.
7. src/gpu/shaders/text.vert — Textured quad per glyph. Takes position + UV from vertex buffer. Applies projection.
8. src/gpu/shaders/text.frag — MSDF sampling: compute median of RGB channels, smoothstep for crisp edges. Output text color with computed alpha.

All shaders use GLSL 450 (#version 450). Use layout qualifiers exactly as specified in plan section C2.

TEXT RENDERING:
9. src/text/font_atlas.cpp — FontAtlas class: loads a pre-baked MSDF atlas (PNG + JSON glyph metrics). Stores glyph UV rects, advances, bearings. Provides lookup by character.
10. src/text/text_renderer.cpp — TextRenderer class: takes a string + position + font size, generates a batch of textured quads (vertex buffer with pos + UV per glyph). Provides measure_text() for layout. Renders all text in one draw call per atlas.
11. src/text/embedded_font.cpp — For now, create a placeholder that defines the embedded font data arrays (can be filled with real atlas data later). Define the expected format: atlas PNG as uint8_t[], glyph metrics as a constexpr struct array.

Do NOT touch any other directories.
```

---

### Agent 4 — "Animation + UI + App"

**Owns:** `src/anim/`, `src/ui/`

**Prompt to paste:**
```
Read /home/daniel/projects/spectra/plan.md for full context.

You are Agent 4 — Animation + UI + App. You own:
- src/anim/
- src/ui/

Wait until include/spectra/ headers exist (Agent 1 creates them). Then implement:

ANIMATION:
1. src/anim/easing.cpp — Easing functions: linear, ease_in, ease_out, ease_in_out (cubic), bounce, elastic. Each takes float t in [0,1], returns float. Namespace spectra::ease.
2. src/anim/timeline.cpp — Timeline class: ordered vector of Keyframe<T> (time, value, easing). evaluate(float t) interpolates between surrounding keyframes. Supports float, vec2, vec4 (color) types.
3. src/anim/animator.cpp — Animator class: holds active Timelines, evaluates all at current time, applies property changes. Supports add/remove timeline, pause/resume.
4. src/anim/frame_scheduler.cpp — FrameScheduler: target FPS mode (sleep + spin-wait for precision) and vsync mode. Tracks elapsed time, dt, frame number. begin_frame()/end_frame() bracket. Fixed timestep option with accumulator for deterministic replay.

UI:
5. src/ui/app.cpp — App class (headless core): creates Backend (VulkanBackend), owns Renderer, FrameScheduler. figure() creates Figure. run() drives the main loop: drain commands → evaluate animations → user callback → render → present. Headless mode: no window, render to offscreen.
6. src/ui/glfw_adapter.cpp — GlfwAdapter (behind PLOTIX_USE_GLFW): GLFW window creation, Vulkan surface creation, input polling, swapchain integration. Wraps glfwPollEvents in the frame loop.
7. src/ui/input.cpp — Input handler: mouse pan (click-drag translates axis limits), mouse scroll zoom (scale axis limits around cursor position). Maps GLFW callbacks to axis limit mutations.

Key design:
- App::animate() returns an AnimationBuilder (fluent API): .fps(), .duration(), .on_frame(), .play(), .record().
- on_frame callback receives a spectra::frame& with elapsed_seconds(), dt(), frame_number(), pause(), resume(), seek().
- CommandQueue (lock-free SPSC ring buffer) for thread-safe mutations from app thread to render thread. Implement as a simple fixed-size circular buffer with atomic head/tail.

Do NOT touch any other directories.
```

---

### Agent 5 — "Export + Examples + Tests"

**Owns:** `src/io/`, `examples/`, `tests/`

**Prompt to paste:**
```
Read /home/daniel/projects/spectra/plan.md for full context.

You are Agent 5 — Export + Examples + Tests. You own:
- src/io/
- examples/
- tests/

Wait until include/spectra/ headers exist (Agent 1 creates them). Then implement:

EXPORT:
1. src/io/png_export.cpp — ImageExporter: takes raw RGBA pixel data (from Vulkan readback), writes PNG via stb_image_write. figure.save_png("path") interface.
2. src/io/video_export.cpp — VideoExporter (behind PLOTIX_USE_FFMPEG): opens ffmpeg subprocess via popen(), pipes raw RGBA frames to stdin. Supports MP4/GIF output. Configurable fps, codec. figure.animate().record("output.mp4") interface.
3. src/io/svg_export.cpp — Placeholder/stub for Phase 2. Define the interface but implement as TODO.

EXAMPLES (each should be a standalone main() that demonstrates the API):
4. examples/basic_line.cpp — Static line plot with labels, title, axis limits. fig.show().
5. examples/live_stream.cpp — Animated plot appending points every frame with sliding window.
6. examples/animated_scatter.cpp — Scatter plot with positions updating via on_frame.
7. examples/multi_subplot.cpp — 2×1 subplot grid with independent series.
8. examples/offscreen_export.cpp — Headless mode, render to PNG.
9. examples/video_record.cpp — Record animated plot to MP4 (requires ffmpeg).

TESTS:
10. tests/unit/test_transform.cpp — Test data-to-screen coordinate mapping with known inputs/outputs.
11. tests/unit/test_layout.cpp — Test subplot grid layout solver: verify pixel rectangles.
12. tests/unit/test_timeline.cpp — Test keyframe interpolation with various easings.
13. tests/unit/test_easing.cpp — Test easing functions at t=0, t=0.5, t=1.
14. tests/unit/test_ring_buffer.cpp — Test SPSC ring buffer: single-threaded correctness, wrap-around.
15. Add CMakeLists.txt in tests/ and examples/ for building (link against spectra, GTest, etc.)

Do NOT touch any other directories. Use the interfaces from include/spectra/.
```

---

## Execution Order & Timing

```
Time ──────────────────────────────────────────────────▶

Agent 1: [CMake + headers + interfaces]──────[Vulkan impl]──────────────▶
              │ (headers ready ~15min)
              ▼
Agent 2: ····[core data model + layout]─────────────────────────────────▶
Agent 3: [shaders (no deps)]───────────[text rendering]─────────────────▶
Agent 4: ····[anim + easing]───────────[app + ui + glfw]────────────────▶
Agent 5: ····[export stubs]────────────[examples + tests]───────────────▶
```

- **Agent 1** starts immediately. Creates directory structure + all public headers first.
- **Agent 3** can start shaders immediately (no code dependencies, just GLSL files).
- **Agents 2, 4, 5** wait ~15 min for Agent 1 to create `include/spectra/` headers, then start.
- All agents work in parallel on non-overlapping directories.

## How to Launch Each Agent

1. Open 5 Cascade chat sessions in Windsurf.
2. In each session, paste the corresponding prompt block above.
3. Start Agent 1 and Agent 3 first (no dependencies).
4. Start Agents 2, 4, 5 once you see Agent 1 has created the `include/spectra/` headers.

## Conflict Avoidance Rules

- **Only Agent 1 touches `CMakeLists.txt` (top-level) and `include/spectra/`.**
- Each agent owns their `src/` subdirectories exclusively.
- If an agent needs to modify a public header (e.g., add a method), they should note it and you manually merge, or instruct Agent 1 to add it.
- Examples and tests (Agent 5) only `#include` public headers — no internal deps.

## After All Agents Finish

Run a final integration session:
```
All agents have completed their work. Read plan.md for context.
Please:
1. Verify the project builds: mkdir build && cd build && cmake .. && make
2. Fix any compilation errors from interface mismatches between agents.
3. Run the unit tests.
4. Try running examples/basic_line.cpp.
5. Report what works and what needs fixing.
```

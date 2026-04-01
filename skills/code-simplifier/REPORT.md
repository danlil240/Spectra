# Code Simplifier — Report

Tracks completed and in-progress simplification work.

---

## Completed Simplifications

### [2026-03-09] Deduplicate 3D Series Setters, Centroid, and Bounds

- **Files:** `src/core/series3d.cpp`
- **Risk:** low
- **Change:** Extracted three file-local anonymous-namespace helpers (`assign_coord`, `compute_centroid_xyz`, `get_bounds_xyz`) to replace six identical setter bodies and four identical algorithm bodies across `LineSeries3D` and `ScatterSeries3D`. Public API and behavior unchanged.
- **Verification:** Build + `ctest --test-dir build -LE gpu`
- **Status:** Done

### [2026-03-09] Deduplicate Logger Sink Formatting

- **Files:** `src/core/logger.cpp`
- **Risk:** low
- **Change:** Extracted `format_log_entry()` static helper in the `sinks` namespace. Both `console_sink()` and `file_sink()` now call it instead of repeating the timestamp+level+category+message+optional-file conditional block.
- **Verification:** Build + `ctest --test-dir build -LE gpu`
- **Status:** Done

### [2026-03-09] Deduplicate DataTransform apply_* Functions

- **Files:** `src/math/data_transform.cpp`
- **Risk:** low
- **Change:** Added two template helpers in the existing anonymous namespace: `apply_positive_log` (shared between `apply_log10` and `apply_ln`) and `apply_elementwise_y` (shared between `apply_abs`, `apply_negate`, `apply_scale`, `apply_offset`, `apply_clamp`). Reduced 7 near-identical ~10-line function bodies to single-line calls.
- **Verification:** Build + `ctest --test-dir build -LE gpu`
- **Status:** Done

<!--
Use this format for each entry:

### [YYYY-MM-DD] <Title>

- **Files:** `src/path/file.cpp`
- **Risk:** low / med / high
- **Change:** Brief description of what was simplified
- **Verification:** Commands and steps used to validate
- **Status:** Done
-->

---

### [2025-07-25] MR-1: Split renderer.cpp (3,338 → 735 lines)

- **Files:** `src/render/renderer.cpp`, `src/render/render_2d.cpp`, `src/render/render_3d.cpp`, `src/render/render_geometry.cpp`, `src/render/render_upload.cpp`
- **Risk:** med
- **Change:** Extracted 2D rendering (598 lines), 3D rendering (560 lines), geometry helpers (1,142 lines), and GPU upload (426 lines) into focused translation units. Core renderer.cpp retained orchestration logic only (735 lines). All methods remain `Renderer` members via separate TUs including the same header.
- **Verification:** Build + `ctest --test-dir build -LE gpu` (111/112 pass — 1 pre-existing ROS2 SEGFAULT)
- **Status:** Done

### [2025-07-25] MR-2: Split vk_backend.cpp (2,932 → 1,299 lines)

- **Files:** `src/render/vulkan/vk_backend.cpp`, `src/render/vulkan/vk_texture.cpp`, `src/render/vulkan/vk_frame.cpp`, `src/render/vulkan/vk_capture.cpp`, `src/render/vulkan/vk_multi_window.cpp`
- **Risk:** med
- **Change:** Extracted texture create/destroy (370 lines), frame lifecycle + draw commands + queries (485 lines), framebuffer readback/capture (453 lines), and multi-window context management (397 lines) into focused translation units. Core vk_backend.cpp retained init/shutdown, surface, swapchain, pipelines, buffers, and descriptor management (1,299 lines).
- **Verification:** Build + `ctest --test-dir build -LE gpu` (111/112 pass — 1 pre-existing ROS2 SEGFAULT)
- **Status:** Done

## In Progress

_None._

---

## Candidate Backlog

| File + Symbol | Why Complex | Proposed Change | Risk | Est. Diff |
|---------------|-------------|-----------------|------|-----------|
| `src/ui/overlay/` | Not yet analyzed — 16 files | Analyze next session for repeated ImGui widget patterns | — | — |
| `src/ui/commands/` | Not yet analyzed — 14 files | Analyze next session for command registration boilerplate | — | — |

<!--
| File + Symbol | Why Complex | Proposed Change | Risk | Est. Diff |
|---------------|-------------|-----------------|------|-----------|
| example       | example     | example         | low  | ~20 lines |
-->

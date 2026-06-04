# Code Simplifier — Exploration Tracker

Persistent record of which modules have been analyzed for simplification candidates.
The agent reads this file at the start of each session to resume where it left off.

**Last updated:** 2026-03-09
**Sessions completed:** 2

---

## Status Key

- `not-started` — Not yet analyzed
- `in-progress` — Partially analyzed (notes below)
- `analyzed` — Fully scanned, candidates logged in REPORT.md backlog
- `clean` — Analyzed, no actionable candidates found
- `simplified` — Candidates found and simplifications applied

---

## Module Sweep

| # | Module | Files | Status | Session | Notes |
|---|--------|-------|--------|---------|-------|
| 1 | `src/core/` | 12 | simplified | 1 | Applied low-risk format-string dedup in `series.cpp`; backlog remains in `series3d.cpp` and `logger.cpp` |
| 2 | `src/render/` | 5 | not-started | — | Renderer abstraction, backend interface |
| 3 | `src/render/vulkan/` | 11 | not-started | — | VulkanBackend: device, swapchain, pipeline, buffers. **High-risk area** |
| 4 | `src/anim/` | 5 | clean | 2 | animator.cpp is minimal (25 lines). easing.cpp has 7 distinct math functions — no dedup opportunity. frame_scheduler.cpp: timing logic, no simplification candidates. |
| 5 | `src/data/` | 4 | clean | 2 | lttb and min_max_decimate are distinct algorithms. filter functions each stand alone. No actionable duplication. |
| 6 | `src/math/` | 2 | simplified | 2 | Extracted `apply_positive_log` and `apply_elementwise_y` helpers in data_transform.cpp — simplified apply_log10, apply_ln, apply_abs, apply_negate, apply_scale, apply_offset, apply_clamp. |
| 7 | `src/io/` | 5 | not-started | — | PNG/SVG/MP4 export |
| 8 | `src/ipc/` | 6 | not-started | — | Binary IPC protocol. **Avoid unless explicit** |
| 9 | `src/daemon/` | 8 | not-started | — | Multi-process backend daemon |
| 10 | `src/agent/` | 1 | not-started | — | Multi-process window agent |
| 11 | `src/embed/` | 2 | not-started | — | Embedding support |
| 12 | `src/platform/window_system/` | 3 | not-started | — | Platform windowing |
| 13 | `src/ui/app/` | 12 | not-started | — | App lifecycle, runtime mode split |
| 14 | `src/ui/overlay/` | 16 | not-started | — | Legend, crosshair, inspector, data interaction |
| 15 | `src/ui/animation/` | 16 | not-started | — | Timeline editor, curve editor, keyframe UI |
| 16 | `src/ui/commands/` | 14 | not-started | — | Command registry, palette, shortcuts, clipboard |
| 17 | `src/ui/input/` | 9 | not-started | — | Input handling, mouse, keyboard |
| 18 | `src/ui/figures/` | 8 | not-started | — | Tab bar, figure management |
| 19 | `src/ui/data/` | 7 | not-started | — | UI data layer |
| 20 | `src/ui/imgui/` | 6 | not-started | — | ImGui integration, status bar, nav rail, menu |
| 21 | `src/ui/workspace/` | 6 | not-started | — | Workspace save/load, serialization |
| 22 | `src/ui/docking/` | 4 | not-started | — | Split view, docking |
| 23 | `src/ui/window/` | 5 | not-started | — | Window management |
| 24 | `src/ui/theme/` | 5 | not-started | — | Themes, design tokens |
| 25 | `src/ui/panel/` | 2 | not-started | — | Detachable panels |
| 26 | `src/ui/layout/` | 2 | not-started | — | Layout management |
| 27 | `src/ui/camera/` | 2 | not-started | — | Camera UI controls |
| 28 | `src/gpu/shaders/` | — | not-started | — | GLSL shaders (separate skill, low priority) |
| 29 | `src/adapters/px4/` | 11 | not-started | — | PX4 adapter |
| 30 | `src/adapters/px4/messages/` | 5 | not-started | — | PX4 message types |
| 31 | `src/adapters/px4/ui/` | 4 | not-started | — | PX4 UI |
| 32 | `src/adapters/qt/` | 4 | not-started | — | Qt adapter |
| 33 | `src/adapters/ros2/` | 39 | not-started | — | ROS2 adapter core |
| 34 | `src/adapters/ros2/ui/` | 32 | not-started | — | ROS2 UI |
| 35 | `src/adapters/ros2/display/` | 21 | not-started | — | ROS2 display plugins |
| 36 | `src/adapters/ros2/messages/` | 7 | not-started | — | ROS2 message types |
| 37 | `src/adapters/ros2/scene/` | 6 | not-started | — | ROS2 scene graph |
| 38 | `src/adapters/ros2/tf/` | 2 | not-started | — | ROS2 TF transforms |
| 39 | `src/adapters/ros2/urdf/` | 2 | not-started | — | ROS2 URDF parsing |
| 40 | `include/spectra/` | — | not-started | — | Public API headers |

---

## Sweep Order (Recommended)

Priority is based on: file count (more files = more opportunity), risk level, and centrality.

### Tier 1 — High value, lower risk
1. `src/core/` — Central data model, likely duplication in Series/Axes helpers
2. `src/ui/overlay/` — 16 files, repeated ImGui widget patterns
3. `src/ui/commands/` — 14 files, command registration boilerplate
4. `src/ui/animation/` — 16 files, timeline/keyframe UI patterns
5. `src/anim/` — Easing functions, potential duplication
6. `src/io/` — Export format boilerplate
7. `src/data/` — Filter/decimation patterns
8. `src/math/` — Small, pure functions, easy wins

### Tier 2 — Medium value, moderate risk
9. `src/ui/app/` — App lifecycle, be careful with mode split
10. `src/ui/input/` — Input handling patterns
11. `src/ui/figures/` — Tab/figure management
12. `src/ui/imgui/` — ImGui integration
13. `src/ui/data/` — UI data layer
14. `src/ui/workspace/` — Serialization patterns
15. `src/render/` — Renderer abstraction
16. `src/ui/docking/` — Split view logic
17. `src/ui/window/` — Window management
18. `src/ui/theme/` — Theme definitions
19. `src/ui/panel/` — Detachable panels
20. `src/ui/layout/` — Layout
21. `src/ui/camera/` — Camera controls

### Tier 3 — Higher risk or specialized
22. `src/render/vulkan/` — **High-risk.** Requires validation-layer testing
23. `src/ipc/` — **Protocol compatibility.** Avoid unless requested
24. `src/daemon/` — Multi-process daemon
25. `src/agent/` — Window agent
26. `src/embed/` — Embedding
27. `src/platform/window_system/` — Platform layer

### Tier 4 — Adapters (large but domain-specific)
28. `src/adapters/ros2/` — ROS2 adapter (39+32+21+7+6+2+2 = 109 files total)
29. `src/adapters/px4/` — PX4 adapter (11+5+4 = 20 files)
30. `src/adapters/qt/` — Qt adapter (4 files)

### Tier 5 — Headers & shaders
31. `include/spectra/` — Public headers (API stability critical)
32. `src/gpu/shaders/` — GLSL (separate skill)

---

## Session Log

Record each analysis session here so progress is visible across conversations.

<!--
### Session N — YYYY-MM-DD

**Modules covered:** src/core/, src/anim/
**Candidates found:** 3
**Candidates applied:** 1
**Notes:** Found duplicated axis limit calculation in Axes and Axes3D. Extracted to shared helper.
-->

### Session 2 — 2026-03-09

**Modules covered:** src/core/ (backlog), src/anim/, src/data/, src/math/
**Candidates found:** 3 (data_transform.cpp log/elementwise patterns) + 2 backlog
**Candidates applied:** 5 (series3d setters/centroid/bounds, logger sinks, data_transform apply_*)
**Notes:** Applied remaining backlog from Session 1. Analyzed anim, data, math. anim and data are clean. Extracted two template helpers in data_transform.cpp eliminating 7 near-identical function bodies.

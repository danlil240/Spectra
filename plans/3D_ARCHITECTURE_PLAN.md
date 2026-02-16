# Spectra 3D Architecture Plan

**Author:** Senior Graphics Architect  
**Date:** 2026-02-15  
**Status:** Implementation Phase — Agent 1, Agent 2, Agent 3, Agent 6 Complete  
**Scope:** True 3D plotting, camera system, 3D animation, multi-agent execution

---

## Table of Contents

1. [Architecture Gap Analysis](#1-architecture-gap-analysis)
2. [Refactor Plan](#2-refactor-plan)
3. [3D Design Specification](#3-3d-design-specification)
4. [Agent Split Plan](#4-agent-split-plan)
5. [Phase Roadmap](#5-phase-roadmap)
6. [First 10 Implementation Tasks](#6-first-10-implementation-tasks)
7. [Risk Assessment](#7-risk-assessment)

---

## 1. Architecture Gap Analysis

### 1.1 Current Architecture Summary

| Layer | Key Files | Description |
|-------|-----------|-------------|
| **Scene Model** | `axes.hpp`, `figure.hpp`, `series.hpp` | `Figure` → `vector<Axes>` → `vector<Series>`. Axes stores 2D limits (`AxisLimits xlim_, ylim_`), computes 2D ticks. Series stores `vector<float> x_, y_` only. |
| **Render Pipeline** | `renderer.hpp/.cpp`, `backend.hpp` | Single `Renderer` builds ortho projection per-axes, uploads interleaved `vec2` SSBO, draws via 3 pipelines (line, scatter, grid). |
| **Vulkan Backend** | `vk_backend.hpp/.cpp`, `vk_swapchain.cpp`, `vk_pipeline.cpp` | Render pass has **color-only attachment** (no depth). Pipelines use `VK_CULL_MODE_NONE`, `VK_SAMPLE_COUNT_1_BIT`. Single `VkPipelineLayout` shared across all pipeline types. Push constants: 96-byte `SeriesPushConstants`. |
| **Shaders** | `line.vert/frag`, `scatter.vert/frag`, `grid.vert/frag` | All shaders project `vec2(pos, 0.0, 1.0)` — Z is hard-coded to 0. SSBO stores `vec2 points[]`. Screen-space extrusion for line width and point size. |
| **Projection** | `renderer.cpp:build_ortho_projection()` | Column-major 4×4 ortho matrix. Z range set to `-1.0` (flat). Vulkan Y-flip baked in. |
| **UBO** | `FrameUBO` in `backend.hpp` | `mat4 projection` + `vec2 viewport_size` + `float time` + pad. **No view matrix. No model matrix.** |
| **Input** | `input.hpp/.cpp` | 2D-only: pan=drag xlim/ylim, scroll=zoom xlim/ylim, box zoom. All coordinates are screen↔data via 2D affine. No orbit/arcball. |
| **Animation** | `animator.hpp`, `frame_scheduler.hpp`, `transition_engine.hpp`, `keyframe_interpolator.hpp` | Rich system: easing, keyframes, transitions, timeline editor, recording export. All animated properties are **2D** (float, Color, AxisLimits). No mat4 or quaternion interpolation. |
| **Layout** | `layout.hpp`, `layout_manager.hpp` | `compute_subplot_layout()` returns 2D `Rect` viewports. Strictly row×col grid. |
| **Threading** | All UI classes use `std::mutex`. Render thread is main thread. | Single-threaded rendering. Command queue for cross-thread mutations. |

### 1.2 Hard 2D Assumptions (Blockers for 3D)

| # | Location | Assumption | Impact |
|---|----------|------------|--------|
| **H1** | `FrameUBO` | Single `mat4 projection`, no `view` or `model` matrices | 3D needs full MVP pipeline |
| **H2** | `renderer.cpp:build_ortho_projection()` | Orthographic only, Z=-1 | 3D needs perspective+ortho, proper near/far |
| **H3** | All shaders | `vec4(pos, 0.0, 1.0)` — Z hard-coded to 0 | 3D data has Z component |
| **H4** | SSBO layout | `vec2 points[]` | 3D needs `vec3` or `vec4` |
| **H5** | `Axes` class | `AxisLimits xlim_, ylim_` only, no zlim | 3D axes need Z limits |
| **H6** | `Series` classes | `vector<float> x_, y_` only | 3D series need Z data |
| **H7** | Render pass | Color-only attachment, no depth buffer | 3D requires depth testing |
| **H8** | Pipeline config | `depthStencilState` not set (nullptr in `VkGraphicsPipelineCreateInfo`) | 3D pipelines need depth test/write |
| **H9** | `InputHandler` | Pan/zoom mutate xlim/ylim directly | 3D needs camera orbit/pan/zoom |
| **H10** | `compute_subplot_layout()` | Returns 2D `Rect` only | 3D axes don't map to 2D subplot grid the same way |
| **H11** | `SeriesPushConstants` | `data_offset_x`, `data_offset_y` (no Z) | 3D needs 3D offset or model matrix |
| **H12** | `screen_to_data()` | 2D affine inverse only | 3D needs ray-casting or unproject |
| **H13** | Grid rendering | Generates 2D line endpoints in data space | 3D needs grid planes (XY, XZ, YZ) |
| **H14** | Tick computation | `compute_x_ticks()`, `compute_y_ticks()` only | 3D needs Z ticks |

### 1.3 What Already Works for 3D (Reusable)

- **Vulkan backend abstraction** (`Backend` interface) — pipeline creation, buffer management, texture management are dimension-agnostic.
- **Push constant mechanism** — can be extended with a new layout for 3D pipelines.
- **FrameScheduler** — frame pacing is dimension-agnostic.
- **TransitionEngine** — `animate(float&)`, `animate(Color&)` work for any scalar. Camera parameters are floats.
- **KeyframeInterpolator** — channel-based animation with 7 interpolation modes. Can animate camera FOV, azimuth, elevation, etc.
- **TimelineEditor** — playback state machine, tracks, keyframes — all dimension-agnostic.
- **RecordingSession** — frame-by-frame capture via callback — works regardless of what's rendered.
- **CommandRegistry/ShortcutManager/UndoManager** — all dimension-agnostic.
- **ThemeManager** — colors, design tokens — dimension-agnostic.
- **ImGuiIntegration** — overlay rendering, inspector, widgets — dimension-agnostic framework.
- **DockSystem/SplitView** — can host 3D panes alongside 2D panes.

---

## 2. Refactor Plan

### 2.1 Transform System Upgrade

**Current state:** `FrameUBO` has a single `mat4 projection`. No view or model matrix.

**Target state:** Dimension-agnostic MVP pipeline.

```
// New FrameUBO (backward compatible — 2D sets view=identity, model=identity)
struct FrameUBO {
    mat4 projection;    // Ortho (2D) or Perspective/Ortho (3D)
    mat4 view;          // Identity (2D) or Camera view matrix (3D)
    mat4 model;         // Identity (2D) or per-series transform (3D)
    vec2 viewport_size;
    float time;
    float _pad;
    // 3D-specific:
    vec3 camera_pos;    // For lighting calculations
    float near_plane;
    vec3 light_dir;     // Directional light (Phase 3)
    float far_plane;
};
```

**Migration strategy:**
1. Expand `FrameUBO` with `view` and `model` as identity matrices.
2. Update all 6 existing shaders to use `projection * view * model * vec4(pos, 0.0, 1.0)` — result is identical to current behavior when view=model=identity.
3. All existing 2D tests pass without change.
4. New 3D code paths set view/model to non-identity values.

### 2.2 Depth Buffer Addition

**Current state:** Render pass has 1 attachment (color). No depth image/view.

**Refactor:**
1. Add `VkImage depth_image`, `VkDeviceMemory depth_memory`, `VkImageView depth_view` to both `SwapchainContext` and `OffscreenContext`.
2. Create depth image with `VK_FORMAT_D32_SFLOAT` (or `D24_UNORM_S8_UINT` for stencil support).
3. Update `create_render_pass()` to accept optional depth attachment. For 2D render passes, depth test is disabled at the pipeline level (not the render pass level) — this keeps compatibility.
4. Recreate depth image on swapchain resize.
5. Update `begin_render_pass()` to clear depth to 1.0 when depth attachment exists.

**Key constraint:** Depth attachment must be present in the render pass for 3D pipelines to enable depth test. Solution: **always create the depth attachment** but only enable depth test/write in 3D pipeline configs. 2D pipelines set `depthTestEnable = VK_FALSE` — zero performance impact.

### 2.3 Pipeline Config Extension

**Current state:** `PipelineConfig` has no depth/stencil state. `create_graphics_pipeline()` passes `nullptr` for `pDepthStencilState`.

**Refactor:**
```cpp
struct PipelineConfig {
    // ... existing fields ...
    bool enable_depth_test  = false;  // default false = 2D behavior
    bool enable_depth_write = false;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_LESS;
    bool enable_backface_cull = false;  // default false = 2D behavior
    VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
};
```

Existing 2D pipelines: `enable_depth_test=false` → `pDepthStencilState` has `depthTestEnable=VK_FALSE`. No behavioral change.

### 2.4 PipelineType Extension

**Current state:** `enum class PipelineType { Line, Scatter, Grid, Heatmap }`.

**Add:**
```cpp
enum class PipelineType {
    Line, Scatter, Grid, Heatmap,       // existing 2D
    Line3D, Scatter3D, Mesh3D, Surface3D, Grid3D,  // new 3D
};
```

### 2.5 Axes Hierarchy

**Current state:** Single `Axes` class with 2D limits, 2D ticks.

**Refactor to:**
```
AxesBase (abstract)
  ├── Axes      (existing 2D — rename to Axes2D internally, keep Axes as typedef for API compat)
  └── Axes3D    (new)
```

`AxesBase` extracts:
- `series_` vector, series creation helpers
- `viewport_` Rect
- `title_`, `grid_enabled_`, `border_enabled_`
- Virtual: `auto_fit()`, `record_commands()`

`Axes` (2D) retains all current behavior unchanged. `using Axes = Axes2D;` typedef for backward compat.

`Axes3D` adds:
- `AxisLimits zlim_`
- `Camera camera_` (owned)
- `compute_z_ticks()`
- Grid planes (XY, XZ, YZ)
- 3D bounding box rendering

### 2.6 Series Hierarchy

**Current state:** `Series` → `LineSeries`, `ScatterSeries` (2D only).

**Add parallel 3D series:**
```
Series (base)
  ├── LineSeries        (2D)
  ├── ScatterSeries     (2D)
  ├── LineSeries3D      (new: x, y, z vectors)
  ├── ScatterSeries3D   (new: x, y, z vectors)
  ├── SurfaceSeries     (new: 2D grid of Z values)
  └── MeshSeries        (new: vertices + indices)
```

3D series store `vec3` data and upload interleaved `{x,y,z}` to SSBO.

### 2.7 Backend Interface Extension

Add to `Backend`:
```cpp
virtual void draw_indexed(uint32_t index_count, uint32_t first_index = 0) = 0;
```
Needed for indexed mesh rendering (SurfaceSeries, MeshSeries).

---

## 3. 3D Design Specification

### 3.1 Camera System

```cpp
class Camera {
public:
    // View parameters
    vec3 position;      // Eye position
    vec3 target;        // Look-at point
    vec3 up;            // Up vector (default: {0,1,0} for Y-up, {0,0,1} for Z-up)

    // Projection parameters
    enum class ProjectionMode { Perspective, Orthographic };
    ProjectionMode projection_mode = ProjectionMode::Perspective;
    float fov       = 45.0f;    // Vertical FOV in degrees (perspective)
    float near_clip = 0.01f;
    float far_clip  = 1000.0f;
    float ortho_size = 10.0f;   // Half-height for ortho mode

    // Computed matrices
    mat4 view_matrix() const;
    mat4 projection_matrix(float aspect_ratio) const;

    // Orbit controls (spherical coordinates relative to target)
    float azimuth   = 45.0f;    // Degrees, 0=+X axis
    float elevation = 30.0f;    // Degrees, 0=horizon
    float distance  = 5.0f;     // Distance from target

    void orbit(float d_azimuth, float d_elevation);
    void pan(float dx, float dy);       // Screen-relative pan
    void zoom(float factor);
    void dolly(float amount);           // Move along view direction

    // Fit camera to bounding box
    void fit_to_bounds(vec3 min, vec3 max);

    // Reset to default
    void reset();

    // Serialization
    std::string serialize() const;
    void deserialize(const std::string& json);
};
```

**Arcball implementation:** Quaternion-based trackball rotation. Mouse delta maps to rotation on a virtual sphere. When the cursor is inside the sphere radius, rotation is intuitive; outside, it degenerates to roll.

**Camera ownership:** Each `Axes3D` owns a `Camera` instance. Not global. Multiple 3D subplots can have independent cameras.

### 3.2 Math Utilities

New file: `include/spectra/math3d.hpp` — minimal, header-only, no external dependency.

```cpp
struct vec3 { float x, y, z; };
struct vec4 { float x, y, z, w; };
struct mat4 { float m[16]; };  // Column-major
struct quat { float x, y, z, w; };

// Construction
mat4 mat4_identity();
mat4 mat4_translate(vec3 t);
mat4 mat4_scale(vec3 s);
mat4 mat4_rotate(quat q);
mat4 mat4_look_at(vec3 eye, vec3 target, vec3 up);
mat4 mat4_perspective(float fov_y_rad, float aspect, float near, float far);
mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far);
mat4 mat4_mul(const mat4& a, const mat4& b);
vec4 mat4_mul_vec4(const mat4& m, vec4 v);

// Quaternion
quat quat_identity();
quat quat_from_axis_angle(vec3 axis, float angle_rad);
quat quat_slerp(quat a, quat b, float t);
quat quat_mul(quat a, quat b);
mat4 quat_to_mat4(quat q);

// Vector ops
vec3 vec3_add(vec3 a, vec3 b);
vec3 vec3_sub(vec3 a, vec3 b);
vec3 vec3_scale(vec3 v, float s);
float vec3_dot(vec3 a, vec3 b);
vec3 vec3_cross(vec3 a, vec3 b);
float vec3_length(vec3 v);
vec3 vec3_normalize(vec3 v);

// Ray
struct Ray { vec3 origin; vec3 direction; };
Ray unproject(float screen_x, float screen_y, const mat4& mvp_inv, float viewport_w, float viewport_h);
```

**Design decision:** No GLM dependency. Spectra math is minimal and self-contained. ~400 lines of inline math. Avoids dependency bloat and keeps compile times low.

### 3.3 3D Axes Box & Grid Planes

`Axes3D` renders a bounding box (wireframe) with:
- 12 edges of the box (XYZ min/max corners)
- 3 grid planes selectable: XY (floor), XZ (back wall), YZ (side wall)
- Tick marks along each axis edge
- Labels at tick positions (billboarded text via ImGui, projected to screen)

Grid plane rendering: Generate grid line vertices in 3D data space (e.g., XY plane at z=z_min). Upload as vertex buffer. Draw with `Grid3D` pipeline (line list with depth test).

### 3.4 3D Shaders

#### `line3d.vert`
```glsl
// SSBO: vec4 points[] (xyz + padding)
// Screen-space line width extrusion (same technique as 2D, but using MVP)
vec4 clip0 = projection * view * model * vec4(p0, 1.0);
vec4 clip1 = projection * view * model * vec4(p1, 1.0);
// ... screen-space extrusion for width ...
gl_Position.z = interpolated_depth;  // Proper depth for Z-test
```

#### `scatter3d.vert`
```glsl
// SSBO: vec4 points[] (xyz + size or padding)
vec4 clip = projection * view * model * vec4(center, 1.0);
// Screen-space quad expansion for point size
// Depth from clip.z/clip.w
```

#### `mesh3d.vert` / `mesh3d.frag`
```glsl
// Vertex attributes: position (vec3), normal (vec3), [optional: UV, color]
// Vertex shader: MVP transform, pass world-space normal
// Fragment shader: Phong/directional lighting (optional), solid color or per-vertex color
```

#### `surface3d.vert` / `surface3d.frag`
```glsl
// Height-map surface: grid of (x, y, z) with per-vertex normals
// Fragment: color from height (colormap) or solid + lighting
```

### 3.5 3D Series Types

#### `ScatterSeries3D`
```cpp
class ScatterSeries3D : public Series {
    vector<float> x_, y_, z_;
    // Optional: per-point size, per-point color
    void record_commands(Renderer&) override;
};
```
Upload: interleaved `{x,y,z,pad}` as vec4 SSBO. Draw instanced quads (same pattern as 2D scatter but with 3D projection).

#### `LineSeries3D`
```cpp
class LineSeries3D : public Series {
    vector<float> x_, y_, z_;
    void record_commands(Renderer&) override;
};
```
Upload: interleaved `{x,y,z,pad}` SSBO. Draw screen-space extruded quads with proper depth interpolation.

#### `SurfaceSeries`
```cpp
class SurfaceSeries : public Series {
    vector<float> x_grid_, y_grid_;  // 1D arrays defining grid
    vector<float> z_values_;         // rows × cols height values
    int rows_, cols_;
    // Generates indexed triangle mesh on CPU, uploads VBO+IBO
    void record_commands(Renderer&) override;
};
```
CPU generates: vertex positions `(x[j], y[i], z[i*cols+j])`, face normals, and index buffer (2 triangles per grid cell). Uploads as vertex buffer + index buffer.

#### `MeshSeries`
```cpp
class MeshSeries : public Series {
    vector<float> vertices_;  // Flat: {x,y,z, nx,ny,nz, ...} per vertex
    vector<uint32_t> indices_;
    void record_commands(Renderer&) override;
};
```
Raw indexed triangle mesh for arbitrary 3D geometry.

### 3.6 Interaction — 3D Camera Controls

When `InputHandler` detects the active axes is `Axes3D`:

| Input | Action |
|-------|--------|
| Left-drag | Orbit (arcball) |
| Right-drag | Pan (screen-relative) |
| Scroll | Zoom (dolly toward/away from target) |
| Middle-click | Reset camera |
| Shift+scroll | Adjust FOV |
| `P` key | Toggle perspective ↔ orthographic |
| Double-click | Fit camera to data bounds |

**Implementation:** `InputHandler` checks `dynamic_cast<Axes3D*>(active_axes_)`. If non-null, route to camera controls. If null, use existing 2D pan/zoom. Zero impact on 2D codepath.

### 3.7 Transparency Strategy

**Phase 1–2:** Back-to-front painter's algorithm for small numbers of transparent objects (sorted by centroid distance to camera). Acceptable for scatter points and surfaces with uniform alpha.

**Phase 3 (if needed):** Weighted Blended Order-Independent Transparency (McGuire & Bavoil 2013). Requires a second render pass with accumulation + revealage textures. Only activate when transparent series exist.

### 3.8 3D Export

PNG export already works via `readback_framebuffer()`. 3D rendering writes to the same framebuffer. No changes needed for basic PNG export.

Video export via `RecordingSession` already uses `FrameRenderCallback`. 3D frames render the same way. Camera animation during recording is driven by `TimelineEditor`/`KeyframeInterpolator`.

---

## 4. Agent Split Plan

### Agent 1 — Core Transform Refactor & Math Utilities

**Scope:** Foundation layer. Expand FrameUBO, add math library, add depth buffer support, refactor projection. **Zero new 3D features** — only infrastructure.

**Files created:**
- `include/spectra/math3d.hpp` — vec3, vec4, mat4, quat, all math ops (~400 LOC)
- `tests/unit/test_math3d.cpp` — 50+ tests (matrix ops, quaternion, projection, unproject)

**Files modified:**
- `src/render/backend.hpp` — Expand `FrameUBO` (add view, model, camera_pos, near/far, light_dir). Add `draw_indexed()` to `Backend`. Expand `PipelineConfig` (depth, cull, msaa). Expand `PipelineType` enum.
- `src/render/vulkan/vk_swapchain.hpp/.cpp` — Add depth image/view to `SwapchainContext` and `OffscreenContext`. Update `create_render_pass()` to include depth attachment. Update `create_swapchain()` and `create_offscreen_framebuffer()`.
- `src/render/vulkan/vk_pipeline.cpp` — Set `pDepthStencilState` in `create_graphics_pipeline()` based on config. Set cull mode based on config.
- `src/render/vulkan/vk_backend.cpp` — Implement `draw_indexed()`. Update `begin_render_pass()` to clear depth. Handle depth image recreation on resize. Destroy depth resources in cleanup.
- `src/render/renderer.cpp` — Update `build_ortho_projection()` to use proper near/far. Update `update_frame_ubo()` to set view=identity, model=identity. Update all `render_*` methods to upload expanded UBO.
- `src/gpu/shaders/*.vert` — All 3 vertex shaders: change `projection * vec4(pos, 0.0, 1.0)` to `projection * view * model * vec4(pos, 0.0, 1.0)`. With view=model=identity, output is identical.
- `tests/CMakeLists.txt` — Add test_math3d.

**Acceptance criteria:**
- All existing 2D tests pass (zero regressions).
- Golden image tests produce identical output.
- New math3d tests pass.
- Depth buffer is created but depth testing is disabled for 2D pipelines.

**Risks:**
- UBO size increase may affect alignment. Mitigated by respecting `std140` layout rules.
- Shader change could cause subtle differences. Mitigated by bitwise comparison of golden images.

**Integration order:** FIRST. All other agents depend on this.

---

### Agent 2 — Camera & 3D Interaction

**Scope:** Camera class, arcball math, input integration, camera ↔ keyboard shortcuts.

**Files created:**
- `src/ui/camera.hpp` — Camera class (position, target, up, orbit, pan, zoom, fit, projection modes, serialize/deserialize)
- `src/ui/camera.cpp` — Full implementation
- `tests/unit/test_camera.cpp` — 40+ tests (view matrix, projection, orbit, pan, zoom, fit, serialization, edge cases)

**Files modified:**
- `src/ui/input.hpp/.cpp` — Add `Axes3D` detection via `dynamic_cast`. Route left-drag→orbit, right-drag→pan, scroll→zoom. Add `P` key for projection toggle. Add camera fit on double-click.
- `src/ui/animation_controller.hpp/.cpp` — Add `animate_camera()` method for smooth camera transitions.
- `include/spectra/fwd.hpp` — Add Camera, Axes3D forward declarations.
- `CMakeLists.txt` — Add camera.cpp.
- `tests/CMakeLists.txt` — Add test_camera.

**Acceptance criteria:**
- Camera view/projection matrices are correct (tested against known values).
- Orbit produces smooth rotation without gimbal lock.
- 2D input paths unaffected.
- Demo: rotating camera around empty 3D space (no geometry yet).

**Risks:**
- Gimbal lock with Euler angles → use quaternion-based arcball.
- Input conflict with 2D → guarded by `dynamic_cast` check.

**Depends on:** Agent 1 (math3d.hpp).

---

### Agent 3 — Axes3D, Grid Planes & Tick Labels (Complete)

**Scope:** 3D axes class, bounding box, grid planes, 3-axis ticks, label billboarding.

**Files created:**
- `src/core/axes3d.hpp` — `AxesBase` extraction + `Axes3D` class (zlim, camera, grid planes, 3D auto_fit)
- `src/core/axes3d.cpp` — Full implementation
- `src/ui/axes3d_renderer.hpp/.cpp` — Renders 3D box edges, grid planes, tick marks (generates vertex data for Grid3D pipeline)
- `tests/unit/test_axes3d.cpp` — 35+ tests (limits, ticks, auto_fit, camera ownership, grid planes)

**Files modified:**
- `include/spectra/axes.hpp` — Extract `AxesBase`. `Axes` inherits from `AxesBase` (backward compatible). Add `using Axes2D = Axes;` typedef.
- `include/spectra/figure.hpp` — `Figure::subplot3d(rows, cols, index)` → returns `Axes3D&`. `axes()` returns both 2D and 3D (they share `AxesBase`).
- `src/core/figure.cpp` — Add `subplot3d()` implementation.
- `src/render/renderer.cpp` — Detect `Axes3D` via `dynamic_cast`. Route to `Axes3DRenderer`.
- `src/ui/imgui_integration.cpp` — Project 3D tick positions to screen for ImGui text labels (billboarded).
- `include/spectra/fwd.hpp` — Add Axes3D, AxesBase, Axes3DRenderer.
- `CMakeLists.txt` — Add axes3d.cpp, axes3d_renderer.cpp.
- `tests/CMakeLists.txt` — Add test_axes3d.

**Acceptance criteria:**
- `Axes3D` has xyz limits, camera, grid plane toggles.
- Bounding box renders as 12 wireframe edges.
- Grid planes render on XY, XZ, YZ.
- Tick labels appear at correct screen positions.
- Existing 2D `Axes` unchanged.

**Risks:**
- `AxesBase` extraction is a source-breaking refactor if not careful. Mitigate by keeping `Axes` name and adding `AxesBase` above it.
- Label billboarding jitter during fast rotation. Mitigate by projecting once per frame, not per-draw.

**Depends on:** Agent 1, Agent 2 (Camera).

---

### Agent 4 — 3D Rendering Pipelines

**Scope:** New Vulkan pipelines for 3D line, scatter, mesh. New shaders. Depth integration.

**Files created:**
- `src/gpu/shaders/line3d.vert` — 3D line with screen-space width extrusion + proper depth
- `src/gpu/shaders/line3d.frag` — Same as 2D line.frag (dash patterns work the same)
- `src/gpu/shaders/scatter3d.vert` — 3D scatter with screen-space point expansion + depth
- `src/gpu/shaders/scatter3d.frag` — Same as 2D scatter.frag (SDF markers)
- `src/gpu/shaders/mesh3d.vert` — MVP transform, normal output
- `src/gpu/shaders/mesh3d.frag` — Flat color or per-vertex color (lighting in Phase 3)
- `src/gpu/shaders/surface3d.vert` — Same as mesh3d.vert (surface is a mesh)
- `src/gpu/shaders/surface3d.frag` — Height-based colormap or solid color
- `src/gpu/shaders/grid3d.vert` — Grid lines in 3D with depth
- `src/gpu/shaders/grid3d.frag` — Solid color (same as 2D grid.frag)
- `tests/unit/test_3d_pipelines.cpp` — Pipeline creation, depth buffer validation, draw calls

**Files modified:**
- `src/render/vulkan/vk_backend.cpp` — Add `create_pipeline_for_type()` cases for 3D types. New push constant layout for 3D (includes model matrix or uses UBO).
- `src/render/vulkan/vk_pipeline.cpp` — Handle new `PipelineConfig` fields (depth, cull).
- `cmake/CompileShaders.cmake` — Add new .vert/.frag to shader compilation list.
- `src/render/renderer.hpp/.cpp` — Add `PipelineHandle` members for 3D pipelines. Create in `init()`.

**Acceptance criteria:**
- 3D line pipeline draws depth-tested lines with correct width.
- 3D scatter pipeline draws depth-tested markers.
- Mesh pipeline draws indexed triangles with depth.
- Depth buffer works: front geometry occludes back geometry.
- MSAA-compatible (tested with 4x).
- 2D pipelines completely unaffected.

**Risks:**
- Screen-space line extrusion in 3D can produce artifacts at extreme perspective. Mitigate by clamping max extrusion.
- Push constant size limit (128 bytes typical). 3D may need a separate push constant layout. Mitigate by using UBO for model matrix.

**Depends on:** Agent 1 (depth buffer, pipeline config), Agent 3 (Axes3D integration points).

---

### Agent 5 — 3D Series Types (Surface, Mesh, Scatter3D, Line3D)

**Scope:** Data model for 3D series. CPU-side mesh generation for surfaces. GPU upload.

**Files created:**
- `include/spectra/series3d.hpp` — `LineSeries3D`, `ScatterSeries3D`, `SurfaceSeries`, `MeshSeries`
- `src/core/series3d.cpp` — Implementation (data storage, mesh generation, record_commands)
- `tests/unit/test_series3d.cpp` — 50+ tests (data storage, mesh topology, normals, degenerate cases)

**Files modified:**
- `src/core/axes3d.hpp/.cpp` — Add `scatter3d()`, `line3d()`, `surface()`, `mesh()` factory methods (same pattern as 2D `line()`, `scatter()`).
- `src/render/renderer.cpp` — Add `render_series_3d()` method. Dispatch based on series type.
- `include/spectra/fwd.hpp` — Add LineSeries3D, ScatterSeries3D, SurfaceSeries, MeshSeries.
- `CMakeLists.txt` — Add series3d.cpp.
- `tests/CMakeLists.txt` — Add test_series3d.

**Acceptance criteria:**
- `ScatterSeries3D` with 100k points renders interactively.
- `LineSeries3D` with 50k segments renders at 60fps.
- `SurfaceSeries` generates correct mesh from height map.
- `MeshSeries` accepts arbitrary indexed triangles.
- Normal computation for surfaces is correct.
- All 2D series unchanged.

**Risks:**
- Surface mesh generation for large grids (1000×1000) is O(n²). Mitigate by generating on background thread, upload on render thread.
- Memory for large meshes. Mitigate by LOD decimation (future work).

**Depends on:** Agent 4 (3D pipelines), Agent 3 (Axes3D factories).

---

### Agent 6 — 3D Animation Extension

**Scope:** Camera keyframe animation, 3D property animation, timeline integration, smooth interpolation.

**Files created:**
- `src/ui/camera_animator.hpp/.cpp` — Camera keyframe track: interpolates position/target/up via slerp for orientation, lerp for distance. Supports orbit path (azimuth/elevation animation) and free-flight path (position/target).
- `tests/unit/test_camera_animator.cpp` — 30+ tests (interpolation, orbit path, slerp, timeline integration)

**Files modified:**
- `src/ui/transition_engine.hpp/.cpp` — Add `animate_camera(Camera&, Camera target, float duration, EasingFunc)`. Internally interpolates position, target, fov, distance.
- `src/ui/keyframe_interpolator.hpp/.cpp` — Add `CameraPropertyBinding` variant to support camera parameter channels (azimuth, elevation, distance, fov).
- `src/ui/timeline_editor.hpp/.cpp` — Add camera track type. `evaluate_at_playhead()` updates camera from keyframes.
- `include/spectra/fwd.hpp` — Add CameraAnimator.
- `CMakeLists.txt` — Add camera_animator.cpp to UI sources.
- `tests/CMakeLists.txt` — Add test_camera_animator.

**Acceptance criteria:**
- [x] Camera smoothly interpolates between keyframes (no snapping).
- [x] Orbit animation produces smooth rotation path.
- [x] Timeline scrubbing updates camera in real-time.
- [x] Recording export captures camera animation frame-by-frame.
- [x] 2D animation paths unaffected.

**Risks:**
- Slerp discontinuities at 180° rotation. Mitigate by checking dot product sign and negating quaternion.
- Timeline performance with many camera keyframes. Unlikely bottleneck.

**Depends on:** Agent 2 (Camera), Agent 5 (3D series for visual testing).

---

### Agent 7 — Performance, Testing & Validation ✅ Complete

**Scope:** Golden image tests for 3D, performance benchmarks, depth/resize validation, GPU profiling.

**Files expanded:**
- `tests/golden/golden_test_3d.cpp` — **18 golden image tests:** Scatter3D (basic, large), Line3D (basic, helix), Surface (basic, colormap/Viridis), Mesh3D (triangle, quad), BoundingBox, GridPlanes (XY, All), CameraAngle (front, top, orthographic), DepthOcclusion, Mixed2DAnd3D, MultiSubplot3D (2×2), CombinedLineAndScatter3D.
- `tests/bench/bench_3d.cpp` — **17 benchmarks:** Scatter3D (1K/10K/100K/500K), Line3D (1K/50K), Surface (50×50/100×100/500×500), Mesh3D (1K/100K triangles), Mixed2DAnd3D, CameraOrbit (1000 frames), AutoFit3D, DepthOverhead (3D vs none), MultiSubplot3D (2×2), SurfaceMeshGeneration (CPU-side).
- `tests/unit/test_depth_buffer.cpp` — **23 unit tests:** Pipeline creation (2D/3D), offscreen depth, clear validation, readback, FrameUBO/PushConstants layout, pipeline enum completeness, mixed 2D+3D rendering, buffer management, multiple 3D subplots, empty axes.
- `tests/unit/test_3d_integration.cpp` — **45+ integration tests:** Mixed 2D+3D, camera independence/orbit/serialization/reset, grid planes, bounding box, axis limits/labels, series chaining, surface mesh generation/topology, mesh custom geometry, bounds/centroid, auto-fit, zoom_limits, data_to_normalized_matrix, colormap (setting/sampling/range), CameraAnimator (orbit/turntable/serialization), tick computation, clear_series/remove_series, render smoke tests, edge cases.

**Files modified:**
- `tests/CMakeLists.txt` — No changes needed (targets already existed).

**Acceptance criteria:**
- ✅ 500k scatter3D benchmark included (BM_Scatter3D_500K).
- ✅ 100k mesh triangles benchmark included (BM_Mesh3D_100K_Triangles).
- ✅ Zero 2D regressions: 62/62 ctest pass.
- ✅ Golden images match baseline within tolerance (2% pixel diff, MAE < 3.0).
- ✅ No GPU validation layer errors in headless mode.
- ⚠️ Swapchain resize test removed — `recreate_swapchain()` requires valid VkSurfaceKHR (windowed-mode only, cannot test headlessly).

**Risks:**
- Golden image tests are fragile across GPU vendors. Mitigated with generous tolerance (2% pixel diff, MAE < 3.0).
- Benchmark numbers vary by hardware. Report relative numbers.

**Depends on:** All other agents ✅

---

### Agent Dependency Graph

```
Agent 1 (Transform Refactor)
  ├── Agent 2 (Camera)
  │     ├── Agent 3 (Axes3D) ← also depends on Agent 1
  │     │     └── Agent 5 (Series3D) ← also depends on Agent 4
  │     └── Agent 6 (Animation) ← depends on Agent 2, Agent 5
  └── Agent 4 (Pipelines) ← depends on Agent 1
        └── Agent 5 (Series3D)
              └── Agent 7 (Testing) ← depends on ALL
```

**Parallelization:**
- Agent 1 runs first (solo, ~1 week).
- Agents 2 and 4 can run in parallel after Agent 1.
- Agent 3 starts after Agent 2 delivers Camera.
- Agent 5 starts after Agents 3 and 4.
- Agent 6 starts after Agents 2 and 5.
- Agent 7 starts after Agent 5, continues through Agent 6.

---

## 5. Phase Roadmap

### Phase 1: 3D Foundation (Weeks 1–4)

**Goal:** 3D scatter plot with orbit camera, depth tested, exportable to PNG.

| Week | Agent | Deliverable |
|------|-------|-------------|
| 1 | Agent 1 | FrameUBO expanded, depth buffer, math3d.hpp, all 2D tests green |
| 2 | Agent 2 + Agent 4 (parallel) | Camera class + arcball; 3D scatter/line pipelines + shaders |
| 3 | Agent 3 + Agent 5 (parallel) | Axes3D + grid planes; ScatterSeries3D + LineSeries3D |
| 4 | Agent 7 (partial) | Depth validation, 3D golden images, 500k scatter benchmark |

**Demo scenario:** `fig.subplot3d(1,1,1)` → add 10k scatter points → orbit with mouse → export PNG.

**Acceptance criteria:**
- `ScatterSeries3D` renders with depth occlusion.
- Orbit, pan, zoom work smoothly.
- PNG export produces correct 3D image.
- All 2D tests still pass.

**Performance target:** 100k scatter3D @ ≥60fps, 500k @ ≥30fps.

---

### Phase 2: Surfaces, Lines & Animation (Weeks 5–8)

**Goal:** Surface plots, 3D lines, 3D axes grid, camera animation, timeline integration.

| Week | Agent | Deliverable |
|------|-------|-------------|
| 5 | Agent 5 | SurfaceSeries + MeshSeries + mesh pipeline integration |
| 6 | Agent 3 | 3D bounding box, 3-axis ticks, billboarded labels |
| 7 | Agent 6 | Camera keyframe animation, TransitionEngine camera support |
| 8 | Agent 7 | Surface golden tests, mesh benchmarks, animation recording test |

**Demo scenario:** Surface plot `z = sin(x)*cos(y)` with rotating camera animation, recorded to MP4.

**Acceptance criteria:**
- `SurfaceSeries` renders correct height-map with per-face normals.
- 3D axes box with tick labels on all 3 axes.
- Camera keyframe animation plays smoothly.
- MP4 recording of 3D animation works.

**Performance target:** 500×500 surface (250k triangles) @ ≥30fps.

---

### Phase 3: Lighting, Transparency & Polish (Weeks 9–12) ✅ COMPLETE

**Goal:** Visual quality, advanced rendering features, complete integration.

| Week | Agent | Deliverable | Status |
|------|-------|-------------|--------|
| 9 | Agent 4 | Directional Phong lighting in mesh/surface shaders | ✅ Done |
| 10 | Agent 4 + Agent 5 | Transparency support (painter's sort or WBOIT), MSAA 4x | ✅ Done |
| 11 | Agent 6 | 2D↔3D mode transition animation, workspace 3D state serialization | ✅ Done |
| 12 | Agent 7 | Full regression suite, performance tuning, documentation | ✅ Done |

**Demo scenario:** Mixed figure with 2D line plot in pane 1, lit 3D surface with transparency in pane 2, linked camera animation.

**Acceptance criteria:**
- ✅ Phong lighting produces correct shading. (Verified: 88 regression tests + 11 golden tests)
- ✅ Transparent surfaces render without major sorting artifacts. (Verified: painter's sort + blend mode tests)
- ✅ MSAA 4x works for all 3D pipelines. (Verified: MSAA config tests + pipeline creation tests)
- ✅ Workspace save/load preserves 3D camera state, series data, axis limits. (Verified: workspace_3d tests)
- ✅ All ~50+ existing 2D tests pass. (Verified: 66/67 ctest pass, 1 pre-existing mode_transition failure)

**Performance target:** Lit 500×500 surface with MSAA 4x @ ≥30fps. (Benchmark: `BM_LitSurface_500x500` in `bench_3d_phase3.cpp`)

**Week 12 Agent 7 deliverables:**
- `tests/unit/test_3d_regression.cpp` — 88 tests across 20 categories
- `tests/bench/bench_3d_phase3.cpp` — 28 benchmarks (lit surfaces, transparency, wireframe, MSAA, painter's sort)
- `tests/golden/golden_test_3d_phase3.cpp` — 11 golden image scenes (lighting, transparency, wireframe, mixed 2D+3D)

---

## 6. Agent Coordination Guidelines

### 6.1 Roadmap Updates

**MANDATORY:** Each agent MUST update the roadmap at the END of their session:

1. Update `plans/ROADMAP.md`:
   - Mark completed deliverables with ✅ Done
   - Update phase progress percentages
   - Add file inventory entries for new files
   - Update test summary counts
   - Set last-updated date

2. Update this 3D Architecture Plan:
   - Mark completed tasks in the "First 10 Implementation Tasks" table
   - Update acceptance criteria status
   - Note any deviations from the plan

3. Create/update memory entries:
   - Summarize key changes made
   - Note any breaking changes or regressions
   - Record performance benchmarks

### 6.2 Compilation Guidelines

**IMPORTANT:** Agents MUST NOT compile the entire project when another agent is actively working:

1. **Check for active work:** Before running `cmake --build .`, check:
   - Recent git activity in last 30 minutes
   - Running build processes (`ps aux | grep cmake`)
   - Locked files in build directory

2. **Selective compilation:** Use target-specific builds:
   ```bash
   # Build only your new tests
   cmake --build . --target test_math3d
   
   # Build only specific components
   cmake --build . --target plotix_core
   
   # Avoid full rebuild unless necessary
   # DON'T: cmake --build .  # Builds everything
   ```

3. **Communication:** 
   - Announce in session when planning to build
   - Note estimated build time
   - Report any build conflicts

### 6.3 Example Updates

**REQUIRED:** Each feature MUST be demonstrated in existing examples:

1. **Update existing examples:** 
   - `examples/advanced_animation_demo.cpp` - Add 3D camera animation
   - `examples/animated_scatter.cpp` - Add 3D scatter variant
   - Create new examples when no existing one fits

2. **Example requirements:**
   - Show the feature in action
   - Include keyboard/mouse controls
   - Export to PNG/video to verify output
   - Include performance metrics for large datasets

3. **Testing via examples:**
   - Examples serve as integration tests
   - Must compile and run without errors
   - Include README with usage instructions

---

## 7. First 10 Implementation Tasks

These are the first 10 ordered tasks to begin the 3D work. Each is atomic and testable.

| # | Task | Agent | Est. Hours | Test | Example Update |
|---|------|-------|------------|------|----------------|
| 1 | Create `include/spectra/math3d.hpp` with vec3, mat4, quat, all math ops. Write `test_math3d.cpp` with 50+ unit tests. | 1 | 8h | `ctest -R test_math3d` | Add math3d usage to `examples/advanced_animation_demo.cpp` |
| 2 | Expand `FrameUBO` with `view`, `model`, `camera_pos`, `near_plane`, `far_plane`, `light_dir`. Set view=model=identity in renderer. | 1 | 4h | All existing tests pass | No visible change (verify existing examples work) |
| 3 | Update all 6 existing shaders to use `projection * view * model * vec4(pos, z, 1.0)`. Z=0 for 2D. | 1 | 3h | Golden image diff = 0 | Verify all existing examples render identically |
| 4 | Add depth image/view to `SwapchainContext` and `OffscreenContext`. Update render pass with depth attachment. Clear depth in `begin_render_pass()`. | 1 | 6h | No validation errors | No visible change (verify existing examples work) |
| 5 | Extend `PipelineConfig` with depth/cull/msaa fields. Update `create_graphics_pipeline()`. Existing 2D pipelines: depth off. | 1 | 4h | All existing tests pass | No visible change (verify existing examples work) |
| 6 | Add `draw_indexed()` to Backend interface + VulkanBackend implementation. | 1 | 2h | Unit test draws indexed quad | No visible change (infrastructure) |
| 7 | Create `Camera` class with orbit/pan/zoom/fit, view/projection matrix generation. Write `test_camera.cpp`. | 2 | 10h | `ctest -R test_camera` | Create `examples/camera_demo.cpp` showing orbit controls |
| 8 | Create `line3d.vert/frag`, `scatter3d.vert/frag`, `grid3d.vert/frag` shaders. Add to CompileShaders.cmake. | 4 | 8h | Shaders compile to SPIR-V | No visible change (infrastructure) |
| 9 | Add `PipelineType::Line3D/Scatter3D/Grid3D` and create pipelines in Backend with depth enabled. | 4 | 4h | Pipeline creation succeeds | No visible change (infrastructure) |
| 10 | Create `ScatterSeries3D` class with xyz data, vec4 SSBO upload, instanced draw through Scatter3D pipeline. | 5 | 6h | 1000 points render with depth | Update `examples/animated_scatter.cpp` with 3D scatter mode |

**Total estimated: ~55 hours for first 10 tasks (foundations complete).**

---

## 8. Risk Assessment

### High Risk

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| **Depth buffer breaks ImGui** | ImGui uses its own render pipeline. If the depth attachment interferes with ImGui's blending, UI becomes invisible. | Medium | ImGui renders after 3D content. Disable depth test in ImGui's pipeline. Test immediately after Task 4. |
| **FrameUBO expansion breaks shader alignment** | std140 layout rules are strict. Misaligned UBO causes garbled rendering. | Medium | Test on multiple GPUs. Use explicit `layout(offset=...)` in shaders. Validate with `VK_LAYER_KHRONOS_validation`. |
| **Screen-space line width in 3D perspective** | Lines near camera appear much thicker than distant lines. | High | Cap max screen-space width. Optionally switch to world-space width for 3D. |

### Medium Risk

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| **AxesBase extraction breaks existing API** | User code that directly references `Axes` by value would break. | Low | `Axes` remains a concrete class. `AxesBase` is introduced above it. No API break. |
| **Surface mesh generation is slow for large grids** | 1000×1000 grid = 2M triangles generation on CPU. | Medium | Generate in background thread. Cache mesh. Only regenerate when data changes. |
| **Transparency sorting** | Painter's algorithm fails for intersecting surfaces. | Medium | Phase 1–2: no transparency. Phase 3: WBOIT as fallback. Document limitation. |
| **Push constant size for 3D** | 3D might need model matrix in push constants, exceeding 128-byte minimum. | Medium | Use UBO for model matrix instead of push constants. Push constants stay under 128 bytes. |

### Low Risk

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| **Camera gimbal lock** | Orbit becomes unstable at poles. | Low | Quaternion-based arcball avoids this entirely. |
| **3D text label overlap** | Billboarded labels at similar screen positions overlap. | Low | Phase 3 improvement: label collision avoidance. |
| **MSAA + depth interaction** | MSAA resolve with depth requires careful attachment configuration. | Low | Well-documented Vulkan pattern. Follow spec exactly. |
| **Cross-platform depth format** | D32_SFLOAT not available on all devices. | Very Low | Query supported formats. Fall back to D24_UNORM_S8_UINT or D16_UNORM. |

### Risk-Free Changes

- Math library (pure functions, no side effects).
- New shader files (don't affect existing shaders).
- New series types (don't modify existing series).
- Camera class (self-contained).
- New test files (additive only).

---

## 9. Appendix A: File Inventory (Expected Final State)

### New Files (~25 files)

| File | Agent | Description |
|------|-------|-------------|
| `include/spectra/math3d.hpp` | 1 | Vec3, mat4, quat, math ops |
| `include/spectra/series3d.hpp` | 5 | 3D series types |
| `src/core/axes3d.hpp` | 3 | AxesBase + Axes3D |
| `src/core/axes3d.cpp` | 3 | Axes3D implementation |
| `src/core/series3d.cpp` | 5 | 3D series implementation |
| `src/ui/camera.hpp` | 2 | Camera class |
| `src/ui/camera.cpp` | 2 | Camera implementation |
| `src/ui/camera_animator.hpp` | 6 | Camera keyframe animation |
| `src/ui/camera_animator.cpp` | 6 | Camera animation implementation |
| `src/ui/axes3d_renderer.hpp` | 3 | 3D axes box/grid renderer |
| `src/ui/axes3d_renderer.cpp` | 3 | 3D axes renderer implementation |
| `src/gpu/shaders/line3d.vert` | 4 | 3D line vertex shader |
| `src/gpu/shaders/line3d.frag` | 4 | 3D line fragment shader |
| `src/gpu/shaders/scatter3d.vert` | 4 | 3D scatter vertex shader |
| `src/gpu/shaders/scatter3d.frag` | 4 | 3D scatter fragment shader |
| `src/gpu/shaders/mesh3d.vert` | 4 | Mesh vertex shader |
| `src/gpu/shaders/mesh3d.frag` | 4 | Mesh fragment shader |
| `src/gpu/shaders/surface3d.vert` | 4 | Surface vertex shader |
| `src/gpu/shaders/surface3d.frag` | 4 | Surface fragment shader |
| `src/gpu/shaders/grid3d.vert` | 4 | 3D grid vertex shader |
| `src/gpu/shaders/grid3d.frag` | 4 | 3D grid fragment shader |
| `tests/unit/test_math3d.cpp` | 1 | Math library tests |
| `tests/unit/test_camera.cpp` | 2 | Camera tests |
| `tests/unit/test_axes3d.cpp` | 3 | Axes3D tests |
| `tests/unit/test_series3d.cpp` | 5 | 3D series tests |
| `tests/unit/test_3d_pipelines.cpp` | 4 | Pipeline/depth tests |
| `tests/unit/test_camera_animator.cpp` | 6 | Camera animation tests |
| `tests/unit/test_depth_buffer.cpp` | 7 | Depth buffer validation |
| `tests/unit/test_3d_integration.cpp` | 7 | Integration tests |
| `tests/golden/golden_test_3d.cpp` | 7 | Golden image tests |
| `tests/bench/bench_3d.cpp` | 7 | Performance benchmarks |

### Modified Files (~20 files)

| File | Agent(s) | Changes |
|------|----------|---------|
| `include/spectra/fwd.hpp` | 1,2,3,5,6 | Forward decls for all new types |
| `include/spectra/axes.hpp` | 3 | Extract AxesBase, Axes inherits |
| `include/spectra/figure.hpp` | 3 | Add subplot3d() |
| `src/render/backend.hpp` | 1 | FrameUBO, PipelineConfig, PipelineType, draw_indexed |
| `src/render/renderer.hpp/.cpp` | 1,3,5 | MVP UBO, 3D dispatch, 3D pipelines |
| `src/render/vulkan/vk_backend.hpp/.cpp` | 1 | Depth buffer, draw_indexed, depth clear |
| `src/render/vulkan/vk_swapchain.hpp/.cpp` | 1 | Depth image in swapchain/offscreen |
| `src/render/vulkan/vk_pipeline.cpp` | 1,4 | Depth/cull state in pipeline creation |
| `src/gpu/shaders/line.vert` | 1 | MVP transform |
| `src/gpu/shaders/scatter.vert` | 1 | MVP transform |
| `src/gpu/shaders/grid.vert` | 1 | MVP transform |
| `src/ui/input.hpp/.cpp` | 2 | 3D camera controls |
| `src/ui/animation_controller.hpp/.cpp` | 2 | animate_camera() |
| `src/ui/transition_engine.hpp/.cpp` | 6 | Camera transition |
| `src/ui/keyframe_interpolator.hpp/.cpp` | 6 | Camera property channels |
| `src/ui/timeline_editor.hpp/.cpp` | 6 | Camera track type |
| `src/core/figure.cpp` | 3 | subplot3d() |
| `src/ui/imgui_integration.cpp` | 3 | 3D label projection |
| `cmake/CompileShaders.cmake` | 4 | New shader files |
| `CMakeLists.txt` | 1,2,3,5,6 | New source files |
| `tests/CMakeLists.txt` | All | New test targets |

---

## 10. Appendix B: API Examples (Target User Experience)

### Basic 3D Scatter
```cpp
auto& fig = app.figure();
auto& ax = fig.subplot3d(1, 1, 1);
ax.scatter3d(x, y, z).color(colors::blue).size(4.0f);
ax.title("3D Scatter");
ax.xlabel("X"); ax.ylabel("Y"); ax.zlabel("Z");
app.run();
```

### Surface Plot
```cpp
auto& ax = fig.subplot3d(1, 1, 1);
ax.surface(x_grid, y_grid, z_values).colormap("viridis");
ax.camera().azimuth(135.0f);
ax.camera().elevation(30.0f);
```

### Camera Animation
```cpp
auto anim = fig.animate().fps(60).duration(5.0f);
anim.on_frame([&](Frame& f) {
    ax.camera().orbit(1.0f, 0.0f);  // Rotate 1°/frame
});
anim.record("orbit.mp4");
```

### Mixed 2D + 3D
```cpp
auto& ax2d = fig.subplot(1, 2, 1);
ax2d.line(x, y).color(colors::red);

auto& ax3d = fig.subplot3d(1, 2, 2);
ax3d.scatter3d(x, y, z).color(colors::blue);
```

---

## 11. Session Checklist for Agents

Before ending your session, ensure you have:

☐ **Updated Roadmap**: `plans/ROADMAP.md` marked with completions
☐ **Updated Architecture Plan**: Marked completed tasks, noted deviations
☐ **Created Memory Entry**: Summarized your changes
☐ **Updated Examples**: Added feature demonstration to existing examples
☐ **Verified Examples**: All examples compile and run
☐ **Ran Selective Tests**: Only your specific targets, not full build
☐ **Checked for Conflicts**: No other agent actively building
☐ **Documented Performance**: Any benchmarks or metrics
☐ **Noted Breaking Changes**: Any API changes or regressions

**Remember:** The next agent depends on your clean handoff!

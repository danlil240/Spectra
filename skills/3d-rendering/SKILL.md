---
name: 3d-rendering
description: Develop, extend, or debug 3D rendering features in Spectra. Use when adding new 3D series types (mesh, surface, point cloud, volume), modifying camera behavior, implementing lighting models, adding colormaps, working on depth buffer or transparency, extending the 3D axes system, or fixing 3D visual artifacts. Covers the full 3D pipeline from data model through math transforms to GPU rendering.
---

# 3D Rendering Development

Add or modify 3D rendering capabilities: series types, camera, lighting, colormaps, depth, and the Axes3D system.

---

## Required Context

Before starting any task, read:
- `CLAUDE.md` — architecture overview and 3D status
- `plans/3D_ARCHITECTURE_PLAN.md` — 3D phase roadmap and design decisions
- `src/core/axes3d.cpp` / `axes3d.hpp` — 3D axes data model
- `src/core/series3d.cpp` — 3D series data (positions, normals, colors)
- `include/spectra/axes3d.hpp` — public 3D axes API
- `include/spectra/series3d.hpp` — public 3D series API
- `include/spectra/math3d.hpp` — math primitives (vec3, mat4, quaternion)
- `src/ui/camera/camera.cpp` — camera system (orbit, pan, zoom)
- `src/ui/imgui/axes3d_renderer.cpp` — ImGui integration for 3D rendering
- `src/gpu/shaders/` — 3D GLSL shaders (line3d, scatter3d, mesh3d, surface3d, etc.)

---

## 3D Architecture Overview

```
User API (C++ / Python)
  → Series3D data (positions, normals, colors, indices)
    → Axes3D (transform stack: model → view → projection)
      → Camera (orbit/arcball, pan, zoom, FOV)
        → Renderer (GPU buffer upload, draw calls)
          → 3D Shaders (MVP transform, Blinn-Phong lighting, colormaps)
            → Depth buffer + MSAA → Framebuffer → Swapchain
```

### Transform pipeline

```
Model space → [Model matrix] → World space
  → [View matrix (camera)] → View/Eye space
    → [Projection matrix (perspective/ortho)] → Clip space
      → [Viewport transform] → Screen space
```

The camera produces the view matrix. Projection is either perspective (default) or orthographic.

---

## Workflow

### 1. Adding a new 3D series type

**Step 1 — Data model** in `src/core/series3d.cpp`:
- Define the series type enum value
- Specify what data it needs (positions, normals, indices, UVs, colors)
- Add validation for required data fields

**Step 2 — Public API** in `include/spectra/series3d.hpp`:
- Add the factory method on `Axes3D` (e.g., `add_volume(...)`)
- Use `std::span<const float>` for data input

**Step 3 — Shaders** (see `skills/vulkan-shader-dev/SKILL.md`):
- Create `src/gpu/shaders/<type>3d.vert` and `<type>3d.frag`
- Apply MVP transform in vertex shader
- Implement lighting in fragment shader (follow `mesh3d.frag` Blinn-Phong pattern)
- Register in `cmake/CompileShaders.cmake`

**Step 4 — Pipeline** in `src/render/vulkan/vk_backend.cpp`:
- Create pipeline with depth test enabled:
  ```cpp
  config.enable_depth_test  = true;
  config.enable_depth_write = true;
  config.depth_compare_op   = VK_COMPARE_OP_LESS;
  ```

**Step 5 — Renderer** in `src/render/renderer.cpp`:
- Add `draw_<type>3d()` method
- Upload vertex/index data
- Set push constants (color, opacity, lighting params)
- Issue draw call

**Step 6 — ImGui overlay** in `src/ui/imgui/axes3d_renderer.cpp`:
- Add interaction UI if needed (inspector, tooltips for 3D data)

### 2. Modifying camera behavior

Camera lives in `src/ui/camera/camera.cpp`:
- **Orbit**: mouse drag rotates around a focus point
- **Pan**: middle-click translates the view
- **Zoom**: scroll changes distance from focus (perspective) or scale (orthographic)
- **FOV**: field of view for perspective projection

The camera produces a `mat4` view matrix consumed by the renderer. Key methods:
- `orbit(delta_x, delta_y)` — arc-ball rotation
- `pan(delta_x, delta_y)` — translate focus point
- `zoom(delta)` — change distance
- `view_matrix()` — compute the 4x4 view transform
- `projection_matrix(aspect)` — perspective or orthographic

### 3. Implementing lighting

3D shaders use **Blinn-Phong** lighting by default:
- Ambient + diffuse + specular components
- Light position in view space (moves with camera)
- Normal vectors required for lit surfaces/meshes

Fragment shader pattern (see `mesh3d.frag`):
```glsl
vec3 light_dir = normalize(light_pos - frag_pos);
float diff = max(dot(normal, light_dir), 0.0);
vec3 reflect_dir = reflect(-light_dir, normal);
vec3 view_dir = normalize(-frag_pos); // camera at origin in view space
float spec = pow(max(dot(view_dir, reflect_dir), 0.0), shininess);
vec3 result = ambient + diff * diffuse_color + spec * specular_color;
```

### 4. Adding colormaps

Colormaps map scalar values to colors. Supported types:
`Viridis`, `Plasma`, `Inferno`, `Magma`, `Jet`, `Coolwarm`, `Grayscale`

Colormap lookup happens in the fragment shader:
- Pass normalized scalar (0.0–1.0) as a varying
- Sample from a 1D colormap texture or use analytical approximation
- See `surface3d.frag` for the existing implementation

### 5. Build and validate

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run 3D-specific tests
ctest --test-dir build -R "3d|axes3d|camera|series3d|depth" --output-on-failure

# Run 3D golden tests
ctest --test-dir build -R "golden.*3d" --output-on-failure

# Visual validation with examples
./build/examples/axes3d_demo
./build/examples/demo_3d
./build/examples/lit_surface_demo
```

---

## 3D Pipeline Inventory

| Pipeline | Shader files | Topology | Depth | Lighting | Use case |
|----------|-------------|----------|-------|----------|----------|
| line3d | `line3d.vert/frag` | Triangle list (expanded) | Yes | No | 3D line plots |
| scatter3d | `scatter3d.vert/frag` | Triangle list (sprites) | Yes | No | 3D scatter plots |
| mesh3d | `mesh3d.vert/frag` | Triangle list | Yes | Blinn-Phong | Triangulated meshes |
| surface3d | `surface3d.vert/frag` | Triangle list (grid) | Yes | Blinn-Phong | Surface plots z=f(x,y) |
| pointcloud | `pointcloud.vert/frag` | Point list | Yes | No | Dense point clouds |
| arrow3d | `arrow3d.vert/frag` | Triangle list | Yes | Blinn-Phong | Vector field arrows |
| marker3d | `marker3d.vert/frag` | Triangle list (billboards) | Yes | No | 3D markers |
| grid3d | `grid3d.vert/frag` | Line list | Yes | No | 3D grid planes |
| image3d | `image3d.vert/frag` | Triangle list (quad) | Yes | No | Textured image planes |

---

## Issue-to-File Map

| Issue type | Primary file(s) |
|---|---|
| 3D axes model / transforms | `src/core/axes3d.cpp`, `include/spectra/axes3d.hpp` |
| 3D series data | `src/core/series3d.cpp`, `include/spectra/series3d.hpp` |
| Camera orbit / pan / zoom | `src/ui/camera/camera.cpp`, `camera.hpp` |
| Camera animation | `src/ui/animation/camera_animator.cpp` |
| Perspective / orthographic projection | `src/ui/camera/camera.cpp` |
| 3D shaders (GLSL) | `src/gpu/shaders/*3d.vert`, `*3d.frag` |
| Depth buffer / Z-fighting | `src/render/vulkan/vk_swapchain.cpp` (depth attachment) |
| Blinn-Phong lighting | Fragment shaders (`mesh3d.frag`, `surface3d.frag`) |
| Colormaps | `surface3d.frag`, series3d colormap config |
| 3D ImGui overlay | `src/ui/imgui/axes3d_renderer.cpp` |
| 3D pipeline creation | `src/render/vulkan/vk_backend.cpp` |
| 3D draw calls | `src/render/renderer.cpp` |
| Math primitives (vec3, mat4) | `include/spectra/math3d.hpp` |
| 3D unit tests | `tests/unit/test_axes3d.cpp`, `test_camera.cpp`, `test_series3d.cpp` |
| 3D golden tests | `tests/golden/golden_test_3d.cpp`, `golden_test_3d_phase3.cpp` |
| 3D benchmarks | `tests/bench/bench_3d.cpp` |

---

## Common Pitfalls

1. **Missing depth test** — 3D pipelines must enable depth test and depth write in `PipelineConfig`. Without it, surfaces render in submission order, not depth order.
2. **Normal vector calculation** — Lit surfaces need correct normals. For programmatic meshes, compute face normals via cross product of edges. Incorrectly oriented normals cause dark or inverted surfaces.
3. **Camera near/far planes** — Too-tight near/far range causes Z-fighting. Too wide range wastes depth precision. Default near=0.1, far=1000.0.
4. **Projection aspect ratio** — Must match the viewport aspect ratio, otherwise geometry appears stretched. Update on resize.
5. **Winding order** — Backface culling requires consistent CCW winding. Inconsistent winding causes missing faces.
6. **Transparency with depth** — Alpha-blended 3D objects need order-dependent rendering (back-to-front sort) or order-independent transparency (OIT). Depth writes with blending cause artifacts.

---

## Guardrails

- Always enable depth test for 3D pipelines — never render 3D without depth.
- Never modify `math3d.hpp` primitives without running the full `test_math3d` suite.
- Always validate camera matrix correctness — a bad view matrix causes invisible geometry.
- Never change the 3D transform pipeline order (model → view → projection) without updating all 3D shaders.
- Add golden tests for any new 3D visual output.
- Follow existing Blinn-Phong lighting pattern for new lit surfaces.
- Keep GPU resource destruction deferred to the render thread.

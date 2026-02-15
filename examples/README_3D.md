# Plotix 3D Examples

This directory contains examples demonstrating the 3D plotting capabilities of Plotix.

## Building 3D Examples

```bash
# Configure with CMake
cmake -B build -S .

# Build all examples
cmake --build build

# Run a specific 3D example
./build/examples/simple_3d_scatter
./build/examples/surface_3d
./build/examples/parametric_3d_line
./build/examples/demo_3d
```

---

## Examples Overview

### 1. `simple_3d_scatter.cpp` — Introduction to 3D Scatter Plots

**What it demonstrates:**
- Creating a 3D axes with `fig.subplot3d()`
- Plotting 3D scatter points with `scatter3d(x, y, z)`
- Setting axis labels (`xlabel`, `ylabel`, `zlabel`)
- Configuring camera position (azimuth, elevation, distance)

**Output:** A blue spiral of scatter points in 3D space.

**Key API:**
```cpp
auto& ax = fig.subplot3d(1, 1, 1);
ax.scatter3d(x, y, z).color(colors::blue).size(4.0f);
ax.camera().azimuth = 45.0f;
ax.camera().elevation = 30.0f;
```

**Interaction:**
- **Left-drag:** Orbit camera around the plot
- **Right-drag:** Pan the view
- **Scroll:** Zoom in/out
- **P key:** Toggle perspective ↔ orthographic projection

---

### 2. `surface_3d.cpp` — Surface Plots from Grid Data

**What it demonstrates:**
- Creating a 2D grid of X and Y coordinates
- Computing Z values as a function: `z = sin(x) * cos(y)`
- Rendering a 3D surface mesh with `surface(x_grid, y_grid, z_values)`
- Camera positioning for optimal viewing

**Output:** A smooth mathematical surface with automatic mesh generation.

**Key API:**
```cpp
std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
// ... fill grid and compute z values ...
ax.surface(x_grid, y_grid, z_values).color(colors::cyan);
```

**Technical details:**
- Surface automatically generates indexed triangle mesh
- Per-vertex normals computed for smooth shading (when lighting enabled)
- Grid size: 40×40 = 1,600 vertices, ~3,200 triangles

---

### 3. `parametric_3d_line.cpp` — Parametric 3D Curves

**What it demonstrates:**
- Plotting parametric 3D curves with `line3d(x, y, z)`
- Rendering complex mathematical curves (trefoil knot)
- Configuring grid planes (`GridPlane::All`)
- Line width and color customization

**Output:** A magenta trefoil knot curve in 3D.

**Key API:**
```cpp
ax.line3d(x, y, z).color(colors::magenta).width(3.0f);
ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::All));
```

**Mathematical curve:**
```
x(t) = sin(t) + 2*sin(2t)
y(t) = cos(t) - 2*cos(2t)
z(t) = -sin(3t)
```

---

### 4. `demo_3d.cpp` — Comprehensive 3D Feature Showcase

**What it demonstrates:**
- Multiple 3D subplots in a 2×2 grid
- All 3D series types: scatter, line, surface
- Combining multiple series in one axes
- Different grid plane configurations
- Camera control for each subplot

**Subplots:**
1. **Top-left:** Spiral scatter (500 points)
2. **Top-right:** Helix line curve (300 segments)
3. **Bottom-left:** Mathematical surface (50×50 grid)
4. **Bottom-right:** Cube wireframe (combined scatter + lines)

**Key API:**
```cpp
auto& ax1 = fig.subplot3d(2, 2, 1);  // Row 2, Col 2, Index 1
auto& ax2 = fig.subplot3d(2, 2, 2);
// ... each subplot has independent camera and series
```

---

## 3D API Reference

### Creating 3D Axes

```cpp
auto& ax = fig.subplot3d(rows, cols, index);
```

### 3D Series Types

#### Scatter3D
```cpp
ax.scatter3d(x, y, z)
    .color(colors::blue)
    .size(4.0f)
    .label("Points")
    .opacity(0.8f);
```

#### Line3D
```cpp
ax.line3d(x, y, z)
    .color(colors::red)
    .width(2.5f)
    .label("Curve");
```

#### Surface
```cpp
ax.surface(x_grid, y_grid, z_values)
    .color(colors::orange)
    .label("Surface");
```

#### Mesh (Custom Geometry)
```cpp
std::vector<float> vertices = {x, y, z, nx, ny, nz, ...};  // 6 floats per vertex
std::vector<uint32_t> indices = {0, 1, 2, ...};
ax.mesh(vertices, indices).color(colors::green);
```

### Camera Control

```cpp
// Camera position (spherical coordinates)
ax.camera().azimuth = 45.0f;      // Rotation around Z axis (degrees)
ax.camera().elevation = 30.0f;    // Angle above XY plane (degrees)
ax.camera().distance = 5.0f;      // Distance from target

// Projection settings
ax.camera().projection_mode = Camera::ProjectionMode::Perspective;
ax.camera().fov = 45.0f;          // Field of view (degrees)
ax.camera().near_clip = 0.1f;
ax.camera().far_clip = 100.0f;

// Programmatic camera movement
ax.camera().orbit(delta_azimuth, delta_elevation);
ax.camera().pan(dx, dy);
ax.camera().zoom(factor);
ax.camera().fit_to_bounds(min_vec3, max_vec3);
ax.camera().reset();
```

### Axis Configuration

```cpp
// Axis limits
ax.xlim(-1.0f, 1.0f);
ax.ylim(-2.0f, 2.0f);
ax.zlim(-3.0f, 3.0f);

// Axis labels
ax.xlabel("X Axis");
ax.ylabel("Y Axis");
ax.zlabel("Z Axis");

// Grid planes
ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::XY));
ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::All));
ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::None));

// Bounding box
ax.show_bounding_box(true);

// Auto-fit to data
ax.auto_fit();
```

### Grid Plane Options

```cpp
Axes3D::GridPlane::None   // No grid
Axes3D::GridPlane::XY     // Floor grid
Axes3D::GridPlane::XZ     // Back wall grid
Axes3D::GridPlane::YZ     // Side wall grid
Axes3D::GridPlane::All    // All three planes
```

Combine with bitwise OR:
```cpp
ax.set_grid_planes(static_cast<int>(
    Axes3D::GridPlane::XY | Axes3D::GridPlane::XZ
));
```

---

## Interactive Controls

When running 3D examples, you can interact with the plot:

| Input | Action |
|-------|--------|
| **Left-drag** | Orbit camera (arcball rotation) |
| **Right-drag** | Pan view (screen-relative) |
| **Scroll** | Zoom in/out (dolly) |
| **Middle-click** | Reset camera to default |
| **Shift+Scroll** | Adjust field of view |
| **P key** | Toggle Perspective ↔ Orthographic |
| **Double-click** | Auto-fit camera to data bounds |

---

## Performance Notes

Based on benchmarks (see `tests/bench/bench_3d.cpp`):

| Dataset Size | Target Performance |
|--------------|-------------------|
| 1K scatter points | 60+ fps |
| 10K scatter points | 60+ fps |
| 100K scatter points | 60+ fps |
| 500K scatter points | 30+ fps |
| 50K line segments | 60+ fps |
| 50×50 surface | 60+ fps |
| 100×100 surface | 60+ fps |
| 500×500 surface | 30+ fps |

Performance is GPU-accelerated via Vulkan. Actual fps depends on hardware.

---

## Mixed 2D + 3D Figures

You can combine 2D and 3D plots in the same figure:

```cpp
auto& ax2d = fig.subplot(2, 1, 1);      // 2D axes
ax2d.line(x, y).color(colors::blue);

auto& ax3d = fig.subplot3d(2, 1, 2);    // 3D axes
ax3d.scatter3d(x, y, z).color(colors::red);
```

Each axes type maintains its own rendering pipeline. No performance impact.

---

## Export and Recording

3D plots support the same export features as 2D:

```cpp
// PNG export
fig.save("output.png");

// Video recording (requires animation)
auto anim = fig.animate().fps(60).duration(5.0f);
anim.on_frame([&](Frame& f) {
    ax.camera().orbit(1.0f, 0.0f);  // Rotate 1° per frame
});
anim.record("orbit.mp4");
```

---

## Advanced Topics

### Camera Animation

```cpp
// Smooth camera transition
ax.camera().azimuth = 0.0f;
// ... later ...
transition_engine.animate_camera(
    ax.camera(), 
    target_camera, 
    2.0f,  // duration
    easing::ease_in_out_cubic
);
```

### Custom Mesh Generation

```cpp
// Create custom geometry
std::vector<float> vertices;
std::vector<uint32_t> indices;

// Add vertices: {x, y, z, nx, ny, nz} per vertex
vertices.insert(vertices.end(), {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f});
// ... add more vertices ...

// Add triangle indices
indices.insert(indices.end(), {0, 1, 2});

ax.mesh(vertices, indices);
```

### Depth Testing and Occlusion

3D rendering uses depth testing automatically. Objects closer to the camera occlude objects behind them. This is handled by the GPU depth buffer.

---

## Troubleshooting

**Q: Plot appears flat/2D**  
A: Check that you're using `subplot3d()` not `subplot()`. Verify camera distance > 0.

**Q: Camera doesn't respond to mouse**  
A: Ensure the window has focus. Check that you're left-dragging (not right-dragging).

**Q: Surface looks faceted/blocky**  
A: Increase grid resolution (nx, ny). Lighting (Phase 3) will add smooth shading.

**Q: Performance is slow**  
A: Reduce point count, use decimation, or lower surface resolution. Check GPU drivers.

---

## Next Steps

- Explore camera animation with `KeyframeInterpolator`
- Try custom colormaps for surfaces (coming in Phase 2)
- Add lighting for realistic surface rendering (Phase 3)
- Combine with 2D plots for multi-view analysis

For more information, see:
- `plans/3D_ARCHITECTURE_PLAN.md` — Full 3D design specification
- `tests/golden/golden_test_3d.cpp` — Visual regression tests
- `tests/bench/bench_3d.cpp` — Performance benchmarks

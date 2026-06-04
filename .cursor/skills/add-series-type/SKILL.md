---
name: add-series-type
description: >-
  Adds a new Spectra plot series type end-to-end — core model, shaders,
  Vulkan pipeline, renderer draw path, and tests. Use for bar charts, heatmaps,
  contours, or new 2D/3D series in src/core/ and src/render/.
---

# Add Series Type

1. Public header in `include/spectra/` if API-facing
2. Implementation in `src/core/` (`Series` / `Series3D`)
3. Rendering:
   - Shaders: [add-shader](../add-shader/SKILL.md)
   - Pipeline: `src/render/vulkan/vk_pipeline.cpp`
   - Draw: `src/render/renderer.cpp`
4. Wire `Axes` / `Axes3D` factory (e.g. `ax.bar(...)`)
5. Dirty flag for incremental GPU upload
6. `tests/unit/` + golden if visual
7. `examples/` demo

## Conventions

- Input: `std::span<const float>`
- Builder-style config (`.color()`, `.label()`, …)
- Match existing `LineSeries` / `ScatterSeries` patterns

3D series: [3d-rendering](../3d-rendering/SKILL.md).

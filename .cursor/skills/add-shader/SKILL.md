---
name: add-shader
description: >-
  Adds GLSL 450 vertex/fragment shaders to Spectra, SPIR-V compile at configure
  time, pipeline registration, and renderer draw integration. Use for new
  visual primitives, SDF line/marker shaders, or 3D lighting in src/gpu/shaders/.
---

# Add Shader

1. `src/gpu/shaders/<name>.vert` and `<name>.frag` (`#version 450`)
2. `cmake/CompileShaders.cmake` — add to `SHADER_SOURCES`
3. `src/render/vulkan/vk_pipeline.cpp` — pipeline + `PipelineConfig`
4. `src/render/renderer.cpp` — draw method + push constants
5. Re-**configure** CMake (SPIR-V is configure-time, not build-only)

## Conventions

- Push constants for MVP, color, per-draw uniforms
- SDF anti-aliasing in fragment shaders for 2D primitives
- 3D: enable depth test/write; see [3d-rendering](../3d-rendering/SKILL.md)

Validate: [build-and-test](../build-and-test/SKILL.md), then [graphical-change-workflow](../graphical-change-workflow/SKILL.md).

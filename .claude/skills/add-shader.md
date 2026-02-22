# Add a New Shader

Create a new GLSL 450 shader pair and integrate it into the build.

## Steps

1. Create vertex shader: `src/gpu/shaders/<name>.vert`
2. Create fragment shader: `src/gpu/shaders/<name>.frag`
3. Add compilation entries in `cmake/CompileShaders.cmake`
4. Register the pipeline in `src/render/vulkan/vk_pipeline.cpp`
5. Add draw call support in `src/render/renderer.cpp`
6. Build to verify SPIR-V compilation succeeds

## Conventions

- Target GLSL 450 (`#version 450`)
- Use push constants for per-draw uniforms (MVP matrix, color, etc.)
- Use SDF-based anti-aliasing for edges in fragment shaders
- Follow existing shader naming: `<type>.vert` / `<type>.frag`
- Shaders compile to SPIR-V and are embedded as byte arrays

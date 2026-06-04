---
name: debug-vulkan
description: >-
  Diagnoses Spectra Vulkan rendering failures — validation layers, swapchain
  OUT_OF_DATE, buffer uploads, pipeline/SPIR-V mismatches, and lavapipe software
  rendering. Use for GPU errors, blank frames, validation spam, resize glitches,
  or shader pipeline issues in src/render/vulkan/.
---

# Debug Vulkan

## Validation layers

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/examples/basic_line
```

Key files: `src/render/vulkan/vk_backend.cpp`, `vk_swapchain.cpp`, `vk_buffer.cpp`, `vk_pipeline.cpp`, `cmake/CompileShaders.cmake`.

## Common issues

| Symptom | Check |
|---------|--------|
| Validation errors | Extensions, sync, image layouts |
| Present / resize | `vk_swapchain.cpp`, framebuffer size, recreate path |
| Upload / corruption | VMA flags, staging barriers, vertex counts |
| Pipeline mismatch | SPIR-V rebuild (re-configure CMake), push constants, vertex bindings |

## Diagnostics

```bash
vulkaninfo --summary
ls /usr/share/vulkan/icd.d/
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./build/examples/basic_line
```

## Live capture

Run with validation, then [spectra-mcp](../spectra-mcp/SKILL.md): `get_state`, `capture_window` to `/tmp/debug_snap.png`.

Resize/sync deep dives: see legacy notes in `skills/` or `plans/archive/`; prefer no `vkDeviceWaitIdle()` in the frame loop.

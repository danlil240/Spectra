---
name: add-example
description: >-
  Creates a new runnable Spectra example in examples/ with CMake target and
  build wiring. Use when demonstrating a feature, adding a minimal repro, or
  extending the example gallery.
---

# Add Example

1. `examples/<name>.cpp` — follow existing patterns
2. `examples/CMakeLists.txt`: `add_executable` + `target_link_libraries(... spectra)`
3. Include `<spectra/spectra.hpp>` or `<spectra/easy.hpp>`
4. One feature per example; inline data unless demonstrating import
5. `./build/examples/<name>`

Names: `animated_scatter.cpp`, `live_stream.cpp`. Link `spectra` in CMake.

Visual check: [graphical-change-workflow](../graphical-change-workflow/SKILL.md).

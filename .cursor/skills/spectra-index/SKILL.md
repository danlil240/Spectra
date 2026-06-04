---
name: spectra-index
description: >-
  Router for all Spectra project Cursor skills. Use at the start of any Spectra
  task to pick the right skill — rendering, Vulkan, Python IPC, CMake, QA agents,
  simplification, or visual validation. Invoke when unsure which Spectra workflow
  applies, when onboarding to this repo, or when the user says "use Spectra skills".
---

# Spectra Skills Index

Spectra is a GPU-accelerated C++20 plotting library (Vulkan, ImGui, multi-process IPC). Project skills live in `.cursor/skills/`. Read `CLAUDE.md` and `BUILD_ENVIRONMENT.md` for build conventions.

## Development skills

| Skill | Use when |
|-------|----------|
| [build-and-test](build-and-test/SKILL.md) | Build, run `ctest`, verify a change compiles |
| [build-system](build-system/SKILL.md) | CMake targets, feature flags, shaders in build |
| [debug-vulkan](debug-vulkan/SKILL.md) | Validation errors, swapchain, pipeline, GPU hangs |
| [add-shader](add-shader/SKILL.md) | New GLSL/SPIR-V shader pair |
| [add-series-type](add-series-type/SKILL.md) | New 2D/3D plot series type |
| [add-command](add-command/SKILL.md) | Command palette + shortcuts |
| [add-test](add-test/SKILL.md) | Unit, golden, or benchmark tests |
| [add-example](add-example/SKILL.md) | New `examples/` demo |
| [3d-rendering](3d-rendering/SKILL.md) | Axes3D, camera, 3D shaders, depth, lighting |
| [data-pipeline](data-pipeline/SKILL.md) | Decimation, filters, transforms, streaming |
| [ipc-protocol-dev](ipc-protocol-dev/SKILL.md) | Binary IPC messages, codec, daemon routing |
| [python-bindings](python-bindings/SKILL.md) | `python/spectra/` API and codec mirror |
| [code-simplifier](code-simplifier/SKILL.md) | Safe refactors without behavior/API change |
| [graphical-change-workflow](graphical-change-workflow/SKILL.md) | Any visual/rendering change — build, plot, screenshot |
| [spectra-mcp](spectra-mcp/SKILL.md) | Live app automation at `http://127.0.0.1:8765/mcp` |

## QA skills (require display + `SPECTRA_BUILD_QA_AGENT=ON`)

| Skill | Use when |
|-------|----------|
| [qa-designer-agent](qa-designer-agent/SKILL.md) | Visual design review, `plans/QA_design_review.md` |
| [qa-performance-agent](qa-performance-agent/SKILL.md) | Stress/fuzz, `plans/QA_results.md` |
| [qa-regression-agent](qa-regression-agent/SKILL.md) | Golden image tests, baseline updates |
| [qa-api-agent](qa-api-agent/SKILL.md) | Python/C++ API and IPC contract tests |
| [qa-accessibility-agent](qa-accessibility-agent/SKILL.md) | WCAG contrast, colorblind palettes, keyboard nav |
| [qa-memory-agent](qa-memory-agent/SKILL.md) | Leaks, ASan, Valgrind, VMA budget |
| [qa-ros-performance-agent](qa-ros-performance-agent/SKILL.md) | ROS2 adapter QA with `spectra_ros_qa_agent` |

## Quick routing

- **Changed pixels or shaders** → `graphical-change-workflow`, then domain skill (`3d-rendering`, etc.)
- **Python script broke** → `python-bindings` + `qa-api-agent`
- **IPC framing / daemon** → `ipc-protocol-dev`
- **CMake / link error** → `build-system`
- **Simplify without redesign** → `code-simplifier`

Legacy copies remain in `skills/` for Windsurf/Claude; **prefer `.cursor/skills/` in Cursor**.

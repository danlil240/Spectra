---
name: spectra-index
description: >-
  Router for all Spectra project Cursor skills. Use at the start of any Spectra
  task to pick the right skill — rendering, Vulkan, Python IPC, CMake, QA agents,
  simplification, or visual validation. Invoke when unsure which Spectra workflow
  applies, when onboarding to this repo, or when the user says "use Spectra skills".
---

# Spectra Skills Index

Spectra: GPU C++20 plotting (Vulkan, ImGui, IPC). `CLAUDE.md` + `BUILD_ENVIRONMENT.md` for build rules.

## Workflow & repo

| Skill | Use when |
|-------|----------|
| [spectra-dev](spectra-dev/SKILL.md) | Full feature cycle (plan → build → QA) |
| [spectra-implementation](spectra-implementation/SKILL.md) | Plan, design, or write C++/GLSL/Python |
| [build-and-test](build-and-test/SKILL.md) | Compile, ctest, smoke |
| [git-manager](git-manager/SKILL.md) | Commits, branches, PRs, releases |
| [github-ci](github-ci/SKILL.md) | Actions workflows, CI failures |
| [qa-orchestrator](qa-orchestrator/SKILL.md) | Multi-domain QA sweep |

## Development

| Skill | Use when |
|-------|----------|
| [build-system](build-system/SKILL.md) | CMake targets, flags, shaders in build |
| [debug-vulkan](debug-vulkan/SKILL.md) | Validation, swapchain, pipeline errors |
| [add-shader](add-shader/SKILL.md) | New GLSL/SPIR-V |
| [add-series-type](add-series-type/SKILL.md) | New 2D/3D series |
| [add-command](add-command/SKILL.md) | Command palette + shortcuts |
| [add-test](add-test/SKILL.md) | Unit, golden, benchmark |
| [add-example](add-example/SKILL.md) | `examples/` demo |
| [3d-rendering](3d-rendering/SKILL.md) | Axes3D, camera, 3D shaders |
| [data-pipeline](data-pipeline/SKILL.md) | Decimation, filters, streaming |
| [ipc-protocol-dev](ipc-protocol-dev/SKILL.md) | IPC messages, daemon |
| [python-bindings](python-bindings/SKILL.md) | `python/spectra/` |
| [code-simplifier](code-simplifier/SKILL.md) | Safe refactors, no API change |
| [graphical-change-workflow](graphical-change-workflow/SKILL.md) | Any pixel/shader/UI change |
| [spectra-mcp](spectra-mcp/SKILL.md) | Live app `http://127.0.0.1:8765/mcp` |

## QA (`SPECTRA_BUILD_QA_AGENT=ON`)

| Skill | Use when |
|-------|----------|
| [qa-designer-agent](qa-designer-agent/SKILL.md) | Visual design review |
| [qa-performance-agent](qa-performance-agent/SKILL.md) | Stress/fuzz, crashes |
| [qa-regression-agent](qa-regression-agent/SKILL.md) | Golden + unit gate |
| [qa-api-agent](qa-api-agent/SKILL.md) | Python/IPC/public API |
| [qa-accessibility-agent](qa-accessibility-agent/SKILL.md) | WCAG, colorblind, keyboard |
| [qa-memory-agent](qa-memory-agent/SKILL.md) | ASan, leaks, VMA |
| [qa-ros-performance-agent](qa-ros-performance-agent/SKILL.md) | ROS2 adapter |

## Quick routing

- Pixels/shaders → `graphical-change-workflow` + domain skill
- Python/IPC break → `python-bindings` + `qa-api-agent`
- CI red → `github-ci` (+ root fix in code if needed)
- Commit/PR/release → `git-manager`
- Simplify only → `code-simplifier`

Converted from `.github/agents/*.agent.md` (2026-06).

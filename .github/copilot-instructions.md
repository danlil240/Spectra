---
applyTo: "**"
---

# Spectra Project Guidelines

Use this file for repo-wide defaults only. Keep task-specific workflows in skill files and deeper project background in `CLAUDE.md`, `FORMAT.md`, and `README.md`.

## Core Expectations

- Keep edits minimal and aligned with existing Spectra patterns; avoid architectural rewrites unless the task explicitly requires them.
- Preserve both runtime modes. Do not assume a primary window, a single process, or single-window ownership semantics.
- For specialized QA or workflow tasks, read the matching `SKILL.md` before planning changes.

## Build And Test

- Preferred build flow is CMake with Ninja: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` then `cmake --build build -j$(nproc)`.
- Unit and integration validation: `ctest --test-dir build -LE gpu --output-on-failure`.
- GPU-only validation: `ctest --test-dir build -L gpu --output-on-failure`.
- Golden image validation: `ctest --test-dir build -L golden -j1 --output-on-failure`.
- Formatting: `./format_project.sh` or `./format_project.sh --check`.
- Re-run CMake configure when changing feature flags, embedded assets, or shaders. Shader compilation and font embedding happen during configure, not only during build.

## Architecture

- Public API headers belong in `include/spectra/`; keep them minimal and keep implementation details in `src/`.
- Core data model lives in `src/core/`; rendering abstractions live in `src/render/`; Vulkan-specific ownership and synchronization live in `src/render/vulkan/`.
- App lifecycle and runtime mode split live under `src/ui/app/`, with separate in-process and multi-process paths.
- Multi-process changes must preserve explicit IPC versioning, ordering, and request correlation.
- All ownership should remain explicit. Do not introduce new global state or hidden singleton-style ownership.

## Conventions

- Follow `.clang-format`: Allman braces, 4-space indent, 100-column limit, sorted includes.
- Naming stays consistent with the codebase: `PascalCase` types, `snake_case` functions and variables, trailing `_` for members.
- Prefer RAII, `std::unique_ptr` for ownership, and `std::span`/`std::string_view` in public APIs where appropriate.
- Do not use exceptions in the render backend; preserve return-code based error handling.
- GPU resource destruction must remain deferred to the render thread. Avoid app-thread teardown of Vulkan resources.
- UI and windowing changes must preserve peer-equivalent window behavior and avoid introducing primary-window special cases.

## Verification Expectations

- Add or update tests for behavior changes when practical.
- Structural rendering, resize, animation, or multi-window changes should be validated with targeted manual steps in addition to automated tests.
- If a request touches rendering behavior, avoid speculative fixes. Measure or inspect the relevant synchronization, frame-loop, or swapchain path first.

## Skill Routing

When a request in this workspace matches one of the skill domains below, read the referenced `SKILL.md` before planning changes or running validation. Treat that file as the authoritative workflow for required context, commands, verification, and any mandatory report updates.

- Accessibility audits, contrast issues, keyboard navigation, and high-contrast theme work: `skills/qa-accessibility-agent/SKILL.md`
- Python API, C++ easy API, IPC protocol, serialization, and public API compatibility: `skills/qa-api-agent/SKILL.md`
- Visual QA, design review screenshots, UI polish, theme tuning, and screenshot-driven triage: `skills/qa-designer-agent/SKILL.md`
- Memory leaks, RSS growth, ASan/LSan runs, Valgrind, and Vulkan/VMA memory tracking: `skills/qa-memory-agent/SKILL.md`
- Performance QA, fuzzing, crash reproduction, frame-time regressions, and stress scenarios: `skills/qa-performance-agent/SKILL.md`
- Golden image failures, rendering regressions, baseline updates, and pixel-diff validation: `skills/qa-regression-agent/SKILL.md`
- ROS2 QA, spectra-ros workflows, ROS session validation, diagnostics, TF, and bag playback: `skills/qa-ros-performance-agent/SKILL.md`
- General Spectra engine guidance for Vulkan diagnostics, synchronization, resize stability, animation determinism, multi-window behavior, memory discipline, and architectural boundaries: `.windsurf/skills/spectra-skills/SKILL.md`

## Operating Rules

- If a mapped skill applies, do not skip it. Read the skill file first and follow its required-context section before editing code.
- Prefer the narrowest matching skill. If multiple skills apply, use the most task-specific skill first, then consult the broader Spectra engine skill if needed.
- Follow any verification gates in the selected skill before updating related `REPORT.md` or plan files.
- Keep edits minimal and aligned with the existing Spectra conventions documented in `CLAUDE.md`.
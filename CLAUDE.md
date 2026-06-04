# Spectra — Agent Guide

GPU-accelerated **C++20** plotting (Vulkan 1.2+): 2D/3D charts, animation, ImGui UI, headless export, workspace/plugins. Optional: **ROS2**, **PX4/ULog**, **Qt** embed, **C FFI**, experimental **WebGPU** (2D only).

| Doc | Use when |
|-----|----------|
| [`CODEBASE_MAP.md`](CODEBASE_MAP.md) | Finding files, subsystems, counts — **read before editing code** |
| [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md) | Packages, CMake flags, ROS2/Qt/WebGPU, sanitizers, golden/lavapipe — **do not guess deps** |

## Outputs & runtimes

| Artifact | Path |
|----------|------|
| Main library | `build/libspectra.a` |
| Headless core | `build/libspectra-core.a` |
| App (inproc) | `build/spectra` |
| Multiproc | `build/spectra-backend`, `build/spectra-window` |
| Tests | `build/tests/unit_*` |
| Examples | `build/examples/*` |

**Runtime:** `SPECTRA_RUNTIME_MODE` — `multiproc` (default) or `inproc`. IPC: TLV (legacy) + FlatBuffers.

## Build & verify (summary)

Use the **canonical configure** in [`BUILD_ENVIRONMENT.md` §2](BUILD_ENVIRONMENT.md#2-standard-build-all-core-features) (ROS2 off for default agent work). Shorthand: `make build`.

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)   # default verification
./build/tests/unit_<name>                                        # single test binary
```

GPU / golden / xvfb / lavapipe: [`BUILD_ENVIRONMENT.md` §3](BUILD_ENVIRONMENT.md#3-running-tests). ROS2 / Qt / WebGPU: §4–6.

## Where to change (common tasks)

| Goal | Start here |
|------|------------|
| Public API | `include/spectra/` only — keep minimal |
| Data model | `src/core/` (Figure → Axes → Series) |
| Draw pipeline | `src/render/`, `src/render/vulkan/` |
| Shaders | `src/gpu/shaders/` (GLSL → SPIR-V via `cmake/CompileShaders.cmake`) |
| UI / ImGui | `src/ui/` (`imgui/`, `figures/`, `commands/`, `workspace/`) |
| IPC / multiproc | `src/ipc/`, `src/daemon/`, `src/agent/` |
| ROS2 / PX4 / Qt | `src/adapters/ros2/`, `src/adapters/px4/`, `src/adapters/qt/` |
| Tests | `tests/unit/`, baselines in `tests/golden/baseline/` |
| Demos | `examples/`, `python/examples/` |

## Rules agents must not break

**Architecture**

- No global singletons — managers live on `App`, passed by pointer.
- RAII; prefer stack + `std::unique_ptr` for ownership.
- Zero-copy data APIs: `std::span<const float>`, `std::string_view` in public headers.
- Render backend: **no exceptions** — return codes only.
- Threading: SPSC ring buffer app ↔ render; `std::mutex` elsewhere when shared.
- **Deferred GPU cleanup** — never destroy GPU resources on the app thread; queue for render thread.

**API surface**

- Implementation stays in `src/`; `include/spectra/` is the stable public surface.
- Series format strings: MATLAB-style (`"r--o"` = red dashed line, circle markers).

**Style**

- `.clang-format`: Google base, C++20, Allman braces, 4 spaces, 100 columns.
- Format: `clang-format -i <file>` or `make format` (`format_project.sh`).
- Naming: `PascalCase` types, `snake_case` functions/vars, trailing `_` members, `UPPER_SNAKE` macros.

**Testing**

- Add Google Test coverage for new behavior (`tests/unit/`).
- Label `gpu` in CMake — excluded from sanitizer CI (`ctest -LE gpu`).
- Golden tests: headless PNG vs `tests/golden/baseline/`.

## Patterns & stack notes

- **Hierarchy:** App → Figure → Axes / Axes3D → Series.
- **Backends:** `Backend` strategy — `VulkanBackend` (production), WebGPU experimental.
- **Registries:** commands, shortcuts, figures, series types, transforms.
- **Events / streaming:** `include/spectra/event_bus.hpp`, `include/spectra/topic.hpp`.
- **Rendering:** queued draw commands → Vulkan; SDF lines/markers; MSDF text atlas.
- **Workspace:** v3 (compatible with v1/v2); runtime plugins for series, sources, overlays, transforms.

## CMake flags (agent-relevant)

Defaults are in `CMakeLists.txt`. Commonly toggled:

| Flag | Default | When to change |
|------|---------|----------------|
| `SPECTRA_USE_ROS2` | ON | OFF for standard build; ON only with sourced ROS2 (see BUILD_ENV §4) |
| `SPECTRA_USE_QT` | OFF | Qt6 embed — BUILD_ENV §5 |
| `SPECTRA_USE_WEBGPU` | OFF | Dawn/wgpu-native — BUILD_ENV §6 |
| `SPECTRA_BUILD_BENCHMARKS` | OFF | Perf work — `tests/bench/` |
| `SPECTRA_BUILD_EMBED_SHARED` | OFF | C FFI `libspectra_embed` |
| `SPECTRA_RUNTIME_MODE` | multiproc | `inproc` for single-process debugging |

Full flag table: search `option(SPECTRA_` in `CMakeLists.txt` or BUILD_ENV.

## Commits

Conventional commits: `feat|fix|refactor|test|docs|ci|perf|chore: description`

Example: `fix: remove hardcoded lavapipe ICD path, discover dynamically`

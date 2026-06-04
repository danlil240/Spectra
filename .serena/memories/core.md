# Spectra — Core

GPU-accelerated C++20 plotting/visualization (Vulkan 1.2+). v0.2.2. ImGui UI, headless export, 3D, animation, workspace v3, plugin API. Optional: ROS2, PX4/ULog, Qt embed, C FFI (`libspectra_embed`), experimental WebGPU (2D only).

## Source map (entry points)

| Path | Role |
|------|------|
| `include/spectra/` | Public API only — keep minimal |
| `src/core/` | Figure → Axes → Series data model, layout, transforms |
| `src/render/` | `Backend` abstraction; `vulkan/`, `webgpu/` |
| `src/gpu/shaders/` | GLSL 450 → SPIR-V at build (`cmake/CompileShaders.cmake`) |
| `src/ui/` | App lifecycle, ImGui, figures/tabs, input, overlays, commands, docking, workspace |
| `src/app/` | Standalone `main`, inproc topic server |
| `src/ipc/`, `src/daemon/`, `src/agent/` | Multiproc IPC, backend daemon, window agents |
| `src/adapters/` | qt, ros2, px4 |
| `src/embed/` | EmbedSurface, C FFI |
| `python/` | IPC Python package |
| `examples/` | Runnable demos |
| `tests/unit/`, `tests/golden/`, `tests/bench/` | GTest, visual regression, benchmarks |
| `plans/` | Architecture/roadmaps (not runtime) |

## Runtime modes

- **inproc:** single binary (`build/spectra`)
- **multiproc (default `SPECTRA_RUNTIME_MODE`):** `spectra-backend` + `spectra-window` over IPC (TLV legacy + FlatBuffers)

## Render/data flow (invariants)

- Renderer queues draw commands → `VulkanBackend` executes on GPU
- SDF lines/markers; MSDF text atlas
- **No exceptions in render backend** — error codes only
- **Deferred GPU teardown:** never destroy GPU resources on app thread; queue for render thread
- App-thread ↔ render-thread: SPSC ring buffer; other sharing via explicit `std::mutex`

## Object hierarchy

`App` → `Figure` → `Axes` / `Axes3D` → `Series`. Managers are **instance members of App** (no global singletons).

## Agent docs (full detail)

- Build/env: repo `BUILD_ENVIRONMENT.md` (authoritative; do not guess deps)
- Overview: `CLAUDE.md`, `CODEBASE_MAP.md`

## Related memories

- Stack/versions: `mem:tech_stack`
- Commands: `mem:suggested_commands`
- Style/architecture: `mem:conventions`
- Done criteria: `mem:task_completion`
- Memory rules: `mem:memory_maintenance`
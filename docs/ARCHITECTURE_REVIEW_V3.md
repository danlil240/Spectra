# Spectra Architecture Review — V3

> **Date**: 2026-04-04 (metrics refreshed 2026-06-04)
> **Baseline**: V2 review (2026-04-03)
> **Scope**: Current repo reality after removing duplicated `WindowManager` implementations from the build path
> **Metrics source**: `python tools/architecture_metrics.py --format json`

---

## What the project is

Spectra is a C++20 scientific plotting and visualization application with a strong Vulkan-first architecture, a large Dear ImGui UI layer, Unix-socket automation, Python bindings, and a growing multi-process runtime. It already supports serious 2D and 3D workflows on the Vulkan path.

WebGPU exists, but the honest description today is **2D/experimental** rather than “full second backend.” The backend and WGSL shaders are real, but the 3D pipeline set is not implemented yet, so docs and user messaging should stay conservative.

Current scripted scale:

- `src/` C/C++: 155,625 lines
- `tests/` C/C++: 90,995 lines
- `python/`: 19,404 lines

---

## Repo Reality Snapshot

### Verified file sizes

| File | Lines | Notes |
|---|---:|---|
| `src/ui/window/window_manager.cpp` | 221 | Orchestration/helpers only |
| `src/ui/window/window_lifecycle.cpp` | 817 | Window creation/destruction + UI bootstrap |
| `src/ui/window/window_figure_ops.cpp` | 668 | Detach/move/preview logic |
| `src/ui/window/window_glfw_callbacks.cpp` | 555 | GLFW routing/callback glue |
| `src/ui/app/register_commands.cpp` | 62 | Table-driven command registration (delegates to `commands_*`) |
| `src/ui/automation/automation_server.cpp` | 453 | Handler-map dispatch + `list_methods` catalog |
| `src/render/render_geometry.cpp` | 1,202 | Geometry/text/grid/border rendering mix |
| `src/ui/imgui/imgui_integration.cpp` | 1,324 | Large UI dispatcher/orchestrator |
| `src/ipc/codec.cpp` | 720 | Legacy TLV codec |
| `src/ipc/codec_fb.cpp` | 1,412 | FlatBuffers codec |
| `src/render/webgpu/wgpu_backend.cpp` | 1,547 | Experimental WebGPU backend |
| `src/ui/app/window_ui_context.hpp` | 146 | Central per-window UI bundle |

### Verified architecture signals

- `WindowManager` split is now real in the repo/build: the duplicate cross-file method definitions are gone.
- `ui::theme()` is the bigger theming debt than direct singleton calls:
  - `ui::theme()` references: 27
  - `ThemeManager::instance()` references: 10
- Command registration is table-driven: `register_commands.cpp` builds a `CommandContext` from `CommandBindings` and batch-registers per-category descriptor tables under `src/ui/app/commands/`.
- `AutomationServer::execute()` dispatches via a handler map populated from `src/ui/automation/handlers/`; pre-flight context and parameter validation lives in `automation_dispatch.cpp`. Use the `list_methods` automation call to introspect handler metadata.
- WebGPU WGSL footprint: 5 shader files / 595 total lines.

---

## Main subsystems

| Subsystem | Location | Responsibility |
|---|---|---|
| Core data model | `src/core/`, `include/spectra/` | Figures, 2D/3D axes, series types, styles, chunked series |
| Vulkan rendering | `src/render/`, `src/gpu/shaders/` | Production renderer/backend path |
| WebGPU rendering | `src/render/webgpu/`, `src/gpu/shaders/wgsl/` | Experimental backend, currently best described as 2D-focused |
| App lifecycle | `src/ui/app/` | `App`, `SessionRuntime`, `WindowRuntime`, `WindowUIContext`, command registration |
| Windowing/platform | `src/ui/window/`, `src/platform/` | Window lifecycle, input routing, surface hosting |
| IPC / multiproc | `src/ipc/`, `src/daemon/`, `src/agent/` | 40-byte framing header, TLV + FlatBuffers payload handling, daemon/agent transport |
| Automation / MCP | `src/ui/automation/` | JSON automation endpoint + MCP adapter |
| Plugins / workspace | `src/ui/workspace/` | Plugin API, plugin guard, workspace serialization |

---

## What is working well

1. **Vulkan-first architecture is strong.** The production rendering path is modular, large, and believable for serious plotting workloads.
2. **The WindowManager split is finally honest.** `window_manager.cpp` is now a small orchestration/helper file instead of quietly re-implementing lifecycle and figure-op code.
3. **SessionRuntime / WindowRuntime / WindowUIContext is the right seam.** The app already has the beginnings of a per-window runtime model rather than one giant UI loop.
4. **IPC migration is partway to the right place.** Both C++ and Python already auto-detect payload format on reads.
5. **The codebase has enough structure to support table-driven cleanup.** `CommandBindings` already exists in `src/ui/app/register_commands.hpp`, so the command layer has a natural descriptor anchor instead of needing a second parallel context object.

---

## Current concerns

1. **`WindowUIContext` is the next real architecture project.** The struct itself is small, but the initialization and ownership story is spread across `src/ui/window/window_lifecycle.cpp` and `src/ui/app/app_step.cpp`, with different startup paths for windowed and headless operation. That is the bigger architectural payoff now.
2. **Theming debt is broader than singleton cleanup.** Chasing a handful of remaining `ThemeManager::instance()` calls would be cosmetic. The deeper issue is the remaining `ui::theme()` references that bypass explicit context and make per-window or contextual theming harder.
3. **IPC is in transition, not “done.”** The repo already supports auto-detecting FlatBuffers vs TLV on reads, but writes are still mixed and the legacy TLV codec is still large and active.
4. **WebGPU should stay explicitly experimental in docs.** The source itself says 3D pipeline types are not implemented, so public messaging should not imply Vulkan parity.

---

## Recommended next architecture work

### 1. Window initialization / lifecycle architecture

Make `WindowUIContext` the next major project.

Recommended direction:

- Introduce a `WindowInitializationContext` or builder shared by both windowed startup and headless startup.
- Centralize construction order for `WindowUIContext` members and callback wiring.
- Make “create first window,” “create secondary window,” “create panel/preview window,” and “headless UI bootstrap” reuse the same dependency-assembly path where possible.

Why this comes first:

- It removes the biggest extension risk in the current UI stack.
- It pays off across runtime startup, testing, detached windows, and headless automation.
- It is a deeper improvement than another surface-level source split.

### 2. Table-driven command and automation registration — done

Implemented as of 2026-06:

- **Commands:** `CommandBindings` → `CommandContext` → per-category `CommandDescriptor` tables in `src/ui/app/commands/`, registered via `register_descriptors()`.
- **Automation:** handler group factories in `src/ui/automation/handlers/`, map dispatch in `AutomationServer::execute()`, pre-flight validation in `automation_dispatch.cpp`, introspection via `list_methods`.

### 3. Theming, IPC migration, and WebGPU messaging

These should advance in parallel, but with clearer framing:

- **Theming:** target elimination or contextualization of `ui::theme()` usage, not just the remaining direct `instance()` calls.
- **IPC:** move to **FB-by-default writes, auto-detect on reads, remove dead TLV last**.
- **WebGPU:** document it as **2D/experimental** until the 3D pipeline family actually lands.

---

## Assessment

Spectra still looks like a strong modular monolith with a credible Vulkan production path, a large but understandable UI stack, and a lot of serious capability already present. The main correction from the prior review is not that the architecture is weak; it is that a few areas were being described too optimistically or with stale metrics.

The codebase is in a good position to keep improving, but the next wins should be architectural assembly and registration patterns, not more mechanical file shuffling.

---

## Appendix: Scripted metrics command

Use this to regenerate the numbers in this review:

```bash
python tools/architecture_metrics.py --format markdown
```

Use this to guard against `WindowManager` duplication regressions:

```bash
python tools/architecture_metrics.py --fail-on-window-manager-duplicates
```

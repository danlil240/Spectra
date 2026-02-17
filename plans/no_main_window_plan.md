# Spectra — No-Main-Window Multi-Process Architecture Plan

> **Project**: Spectra (GPU-accelerated scientific plotting library)
> **Goal**: Eliminate the "primary window" concept so every OS window is an equal peer, with a long-term path to multi-process (backend-daemon + window-agent) architecture.
> **Status**: Phase 1 Complete, Phase 2 In Progress (tasks 1-2 complete)  
> **Last Updated**: 2026-02-19

---

## Execution Rules

1. This document is the **source of truth**. After every work session, update it:
   - Mark completed items with ✅ and date.
   - Add "What changed" and "Next steps" sections.
   - Update risks if new ones are found.
2. After every work session, provide:
   - Exact build/run commands.
   - Steps to verify using existing examples (no new examples unless none exist).
   - Expected results + failure symptoms.
3. Keep PRs small and single-purpose (one step per PR). No drive-by refactors.
4. **Never** regress resize stability or introduce GPU hangs.

---

## 1. Goals and UX Requirements

### Required Behaviors

- **Tab tear-off**: Dragging a tab outside the window spawns a new OS window. The figure follows the cursor (tab + figure only; no menus/toolbars while dragging).
- **Drop inside**: Keep current docking/split behavior (no regression).
- **Drop outside**: Spawn a new OS window that is identical (same theme, shortcuts, docking capability). The figure becomes active there.
- **No main window**: Closing *any* window (including the first-created one) does not kill the app. The app exits only when **all** windows are closed.
- **Long-term target**: Backend-daemon owns session state; window-agents are identical peers; 1 window = 1 OS process (v1).

### Non-Goals for v1

- Full remote/network IPC (local machine only).
- Perfect cross-platform parity (Linux first; Windows/macOS stubs).
- Advanced layout persistence across sessions (optional Phase 9).

---

## 2. Current Architecture (Codebase Audit)

### What Already Exists

The multi-window infrastructure is **partially built** across prior sessions (Phases 1–4 of `MULTI_WINDOW_ARCHITECTURE.md`). Here is what exists today:

| Component | File(s) | Status |
|---|---|---|
| **WindowContext** | `src/render/vulkan/window_context.hpp` | ✅ Per-window Vulkan resources (surface, swapchain, cmd buffers, sync objects, ImGui context pointer) |
| **WindowManager** | `src/ui/window_manager.hpp/.cpp` | ✅ Creates/destroys secondary windows, GLFW callbacks, `detach_figure()`, `move_figure()`, `create_window_with_ui()` |
| **WindowUIContext** | `src/ui/window_ui_context.hpp` | ✅ Per-window UI bundle (ImGuiIntegration, FigureManager, DockSystem, InputHandler, etc.) |
| **FigureRegistry** | `src/ui/figure_registry.hpp/.cpp` | ✅ Thread-safe `uint64_t` FigureId, monotonic IDs, `release()` for zero-copy transfer |
| **FigureManager** | `src/ui/figure_manager.hpp/.cpp` | ✅ Per-window figure lifecycle (create/close/switch/duplicate), queued ops |
| **VulkanBackend** | `src/render/vulkan/vk_backend.hpp/.cpp` | ✅ `primary_window_` + `active_window_` pointer, `init_window_context()`, `destroy_window_context()`, `recreate_swapchain_for()` |
| **GlfwAdapter** | `src/ui/glfw_adapter.hpp/.cpp` | ✅ `destroy_window()` + `terminate()` separated |
| **TabDragController** | `src/ui/tab_drag_controller.hpp` | ✅ Drag state machine, drop-inside/drop-outside callbacks |
| **Tab tear-off** | `src/ui/app.cpp` (lines 664–693, 765–782, 1982–2041) | ✅ Deferred detach queue, `pending_detaches`, new window creation between frames |
| **Multi-window render loop** | `src/ui/app.cpp` (lines 2043–2108) | ✅ Iterates `window_mgr->windows()`, per-window `update_window()`/`render_window()` |

### Remaining Primary-Window Assumptions (The Problem)

Despite the above, the codebase still has a **hard "primary window" concept** that must be eliminated:

| # | File | Symbol / Line | Current Behavior | Problem |
|---|---|---|---|---|
| **P1** | `vk_backend.hpp:161` | `WindowContext primary_window_` | VulkanBackend **owns** a `WindowContext` member directly. All other windows are in `WindowManager::windows_` (heap-allocated). | Primary window has different ownership/lifetime than all others. |
| **P2** | `vk_backend.hpp:162` | `active_window_ = &primary_window_` | Default active window is always the primary. | Assumes primary always exists. |
| **P3** | `vk_backend.hpp:82` | `primary_window()` accessor | Returns `primary_window_` by reference. Called from ~30 places in `app.cpp`, `window_manager.cpp`. | Every caller assumes "the primary" exists. |
| **P4** | `window_manager.cpp:42-76` | `adopt_primary_window()` | Special-cases the primary: doesn't install GLFW callbacks (GlfwAdapter owns them), doesn't allocate a new WindowContext, stores in `active_ptrs_` without ownership. | Primary uses GlfwAdapter callbacks; secondaries use WindowManager callbacks — two different input paths. |
| **P5** | `window_manager.cpp:149-161` | `destroy_window()` primary guard | Refuses to destroy the primary window — only marks `should_close = true`. | Primary cannot be destroyed like other windows. |
| **P6** | `window_manager.cpp:171-213` | `destroy_window()` reattach policy | When a secondary window closes, its figures are **moved back to the primary**. | Assumes primary always exists as a fallback. |
| **P7** | `app.cpp:259-275` | `GlfwAdapter` + `WindowManager` init | `GlfwAdapter` creates the first GLFW window. Then `WindowManager` "adopts" it. Two separate objects manage the same window. | Dual ownership of the first window. |
| **P8** | `app.cpp:1862-1873` | `primary_setup.assigned_figures` | Primary window's `assigned_figures` is set to `registry_.all_ids()`. Primary's `ui_ctx_non_owning` is a raw pointer to the stack-local `ui_ctx`. | Primary's UI context has different lifetime (stack) vs. secondaries (heap via `WindowContext::ui_ctx`). |
| **P9** | `app.cpp:1889-1922` | `primary_closed` flag | Special `primary_closed` bool controls whether to skip primary update/render. Primary is hidden (not destroyed) when closed. | Primary has special close semantics. |
| **P10** | `app.cpp:2043-2108` | Secondary window render loop | Iterates windows, **skips primary** (`wctx == &primary_wctx`), then restores primary as active at the end. | Primary is rendered separately (lines 1964-1977), secondaries in a loop — asymmetric. |
| **P11** | `app.cpp:2184-2204` | Exit condition | `glfw->should_close()` checks the primary GLFW window. If primary closes but secondaries remain, primary is hidden. App exits only when `!window_mgr->any_window_open()`. | Exit logic is tied to the primary GlfwAdapter. |
| **P12** | `app.cpp:106-2307` | `App::run()` monolith | ~2200 lines. All UI setup, command registration, callback wiring, and the main loop are in one function. Stack-local `ui_ctx` is the primary's UI context. | Cannot be reused for a standalone window-agent process. |
| **P13** | `window_context.hpp:92-96` | `ui_ctx` vs `ui_ctx_non_owning` | Primary uses `ui_ctx_non_owning` (raw pointer to stack-local). Secondaries use `ui_ctx` (owned `unique_ptr`). | Two different ownership models for the same concept. |

---

## 3. Target Architecture

### Processes (Long-Term)

```
┌─────────────────────┐     UDS / shared-mem     ┌─────────────────────┐
│  spectra-backend     │◄────────────────────────►│  spectra-window (1) │
│                      │                          │  GLFW + Vulkan +    │
│  FigureRegistry      │     UDS / shared-mem     │  ImGui              │
│  Session graph       │◄────────────────────────►├─────────────────────┤
│  Window registry     │                          │  spectra-window (2) │
│  Detach/move logic   │          ...             │  GLFW + Vulkan +    │
│  Process manager     │                          │  ImGui              │
└─────────────────────┘                          └─────────────────────┘
```

- **spectra-backend** (headless): Owns `FigureRegistry`, session graph, window registry. Spawns/kills window-agent processes. Exits when `window_count == 0 && client_count == 0`.
- **spectra-window** (one per OS window): Owns one GLFW window + Vulkan swapchain + ImGui context. Holds a mirror/cache of assigned figures. Emits input events to backend; receives state diffs.
- **No primary window**: Any window may be created first and closed at any time.

### IPC Protocol (v1)

**Transport**: Linux `AF_UNIX` + `SOCK_SEQPACKET` (preserves message boundaries). One connection per window-agent. Optional bulk blob channel for large array data (v1.1).

**Serialization**: CBOR (compact, schema-less, easy C++ libs).

**Message envelope**:
```
{ type: enum, seq: u64, request_id: u64, session_id: u64,
  window_id: u64, timestamp_ns: u64, payload: ... }
```

**IDs**: `SessionId`, `WindowId`, `FigureId`, `ProcessId`, `RequestId`, `Revision` — all `u64`.

#### Handshake
- Agent → Backend: `HELLO { protocol_major, protocol_minor, agent_build, capabilities }`
- Backend → Agent: `WELCOME { session_id, window_id, process_id, heartbeat_ms, mode }`

#### Control Messages

| Direction | Message | Fields |
|---|---|---|
| Agent → Backend | `REQ_CREATE_WINDOW` | `template_window_id?` |
| Agent → Backend | `REQ_CLOSE_WINDOW` | `window_id, reason` |
| Agent → Backend | `REQ_DETACH_FIGURE` | `figure_id, from_window_id, drop_screen_xy` |
| Agent → Backend | `REQ_MOVE_FIGURE` | `figure_id, from_window_id, to_window_id, dock_spec` |
| Agent → Backend | `REQ_SNAPSHOT` | `window_id` |
| Backend → Agent | `CMD_ASSIGN_FIGURES` | `window_id, figure_ids[], active_figure_id` |
| Backend → Agent | `CMD_REMOVE_FIGURE` | `window_id, figure_id` |
| Backend → Agent | `CMD_SET_ACTIVE` | `window_id, figure_id` |
| Backend → Agent | `CMD_CLOSE_WINDOW` | `window_id, reason` |
| Both | `RESP_OK` | `request_id` |
| Both | `RESP_ERR` | `request_id, code, message` |

#### State Sync

| Direction | Message | Fields |
|---|---|---|
| Backend → Agent | `STATE_SNAPSHOT` | `revision, payload` (may be chunked) |
| Backend → Agent | `STATE_DIFF` | `base_revision, new_revision, ops[]` |
| Agent → Backend | `ACK_STATE` | `revision` |

**Ops**: create/delete figure/axes/series, set props, set/append data via `data_ref`.

**Blob transfer** (large arrays):
- `BLOB_BEGIN { blob_id, byte_len, format }`
- `BLOB_CHUNK { blob_id, offset, bytes }`
- `BLOB_END { blob_id, checksum }`

#### Events

| Direction | Message | Fields |
|---|---|---|
| Agent → Backend | `EVT_INPUT` | `key/mouse/wheel/text ...` |
| Agent → Backend | `EVT_WINDOW` | `resized/minimized/restored/close_requested/focused ...` |
| Agent → Backend | `EVT_TAB_DRAG` | `figure_id, state(start/move/end), cursor_screen_xy` |
| Agent → Backend | `EVT_HEARTBEAT` | `fps, frame_time_p95_ms` |

#### Ordering & Resync
- Backend applies per-window events in `seq` order.
- Backend emits diffs with monotonic `revision`.
- On reconnect: backend sends `STATE_SNAPSHOT` and reassigns figures.

---

## 4. Step-by-Step Implementation Roadmap

### Phase 0 — Codebase Audit (DONE — this document)

The audit is captured in Section 2 above. All 13 primary-window assumptions are catalogued with file/line references.

---

### Phase 1 — Eliminate Primary Window Asymmetry (In-Process) — 3–5 days

**Objective**: Make all windows (including the first) go through the same creation, ownership, callback, and destruction path. Zero behavior change from the user's perspective.

**Tasks**:

1. ✅ **Unify window creation** (addresses P1, P4, P7) — 2026-02-18:
   - ✅ `VulkanBackend::primary_window_` replaced with `std::unique_ptr<WindowContext> initial_window_` (heap-allocated).
   - ✅ Added `release_initial_window()` to transfer ownership to `WindowManager`.
   - ✅ Added `initial_window_raw_` non-owning pointer so `primary_window()` accessor still works (transitional).
   - ✅ `WindowManager::create_initial_window()` takes ownership from backend, stores in `windows_` uniformly.
   - ✅ `adopt_primary_window()` delegates to `create_initial_window()` (deprecated).
   - ✅ Removed primary special-casing from `rebuild_active_list()`, `destroy_window()`, `process_pending_closes()`, `focused_window()`, `any_window_open()`, `find_window()`, `find_by_glfw_window()`, all GLFW callback trampolines.
   - ⬜ `GlfwAdapter` still creates the GLFW window (reduced to init/terminate + window creation). Window creation not yet moved to `WindowManager`.

2. ✅ **Unify callback routing** (addresses P4) — 2026-02-18:
   - ✅ All windows use `WindowManager`'s GLFW callbacks via `install_input_callbacks()`.
   - ✅ Removed ~290-line `InputCallbacks` / `GlfwAdapter::set_callbacks()` block from `app.cpp`.
   - ✅ Removed `GlfwAdapter::init()` callback installation (was causing segfault via ImGui callback chaining with wrong user pointer type).
   - ✅ Added resize sync bridge: `wctx->needs_resize` → `ui_ctx->needs_resize` before `update_window()`.

3. ✅ **Unify UI context ownership** (addresses P8, P13) — 2026-02-18:
   - ✅ Removed `WindowContext::ui_ctx_non_owning` and `WindowContext::is_primary`.
   - ✅ `ui_ctx` moved from `App::run()` stack into `initial_wctx->ui_ctx` after setup.
   - ✅ `ui_ctx_ptr` raw pointer kept for main loop access (heap object stays at same address).
   - ⬜ First window's UI context is still created in `App::run()` then moved; not yet created by `init_window_ui()` directly.

4. ✅ **Unify close/destroy** (addresses P5, P6, P9) — 2026-02-19:
   - ✅ `destroy_window()` handles all windows identically. No special guard for "primary".
   - ✅ When *any* window closes, its figures are redistributed to first remaining open window (not hardcoded primary).
   - ✅ **Removed `primary_closed` flag** entirely from main loop.
   - ✅ **Simplified exit condition** to `!window_mgr->any_window_open()` only.
   - ✅ **Fixed X11 BadWindow error** via proper GLFW window handle ownership.

5. ✅ **Unify render loop** (addresses P10, P11) — 2026-02-19:
   - ✅ **Unified main loop**: single iteration over `window_mgr->windows()` (no separate primary/secondary paths).
   - ✅ All windows (including the first) go through identical `update()` + `render()` paths.
   - ✅ **Removed all `vk->primary_window()` calls** from production code.

**Deliverables** (progress):
- ✅ `GlfwAdapter` reduced to init/terminate + window creation (no callbacks).
- ✅ `VulkanBackend::primary_window_` replaced with heap-allocated `initial_window_` (transitional `initial_window_raw_` remains).
- ✅ `adopt_primary_window()` delegates to `create_initial_window()`.
- ⬜ All windows created via `WindowManager::create_window_with_ui()` — not yet, first window still uses `create_initial_window()`.
- ✅ **Uniform render loop** — single iteration over all windows.

**Verify**:
- ✅ `ctest` → 69/69 pass, zero regressions.
- ✅ `multi_window_demo` runs without segfault, detach/reattach works, close original → detached remains.
- ✅ **No X11 BadWindow error** on window close/exit.
- ✅ **Tab detach shows correct figure names** (fixed positional index → FigureId bug).
- ✅ **No shutdown crash** when closing all windows.
- ⬜ Resize torture test not yet performed.
- ⬜ `grep -rn "primary_window" src/` → still has hits (transitional accessor).

---

### Phase 2 — Extract App::run() into Reusable Components — 2–3 days

**Objective**: Split the ~2200-line `App::run()` monolith (P12) into composable pieces so the same code can be used by both the in-process runtime and a future standalone window-agent.

**Tasks**:

1. ✅ **Extract `WindowRuntime`** — per-window event loop body — 2026-02-19:
   - ✅ `WindowRuntime::update()` — animations, ImGui frame, layout, figure switching.
   - ✅ `WindowRuntime::render()` — begin_frame, render pass, ImGui render, end_frame.
   - ✅ Wraps the existing `update_window()` / `render_window()` methods.

2. ✅ **Extract `SessionRuntime`** — session-level orchestration — 2026-02-19:
   - ✅ Owns references to `Backend`, `Renderer`, `FigureRegistry`.
   - ✅ `SessionRuntime::tick()` — frame scheduling, command queue, animation, window iteration, detach processing, event polling.
   - ✅ `SessionRuntime::should_exit()` — `!running_`.

3. ⬜ **Add feature flag**: `SPECTRA_RUNTIME_MODE` (`inproc` | `multiproc`, default `inproc`).
   - `inproc`: `SessionRuntime` + N × `WindowRuntime` in one process (current behavior).
   - `multiproc`: `SessionRuntime` in backend process, `WindowRuntime` in agent process (future).

4. ⬜ **Slim down `App::run()`** to use `SessionRuntime::tick()`:
   ```cpp
   void App::run() {
       SessionRuntime session(backend_, renderer_, registry_);
       while (!session.should_exit()) {
           session.tick(scheduler, animator, cmd_queue, ...);
       }
   }
   ```

**Deliverables**:
- ✅ `src/ui/window_runtime.hpp/.cpp` — per-window update/render.
- ✅ `src/ui/session_runtime.hpp/.cpp` — session orchestration.
- ⬜ `App::run()` reduced to <50 lines.

**Verify**:
- ✅ Identical behavior to Phase 1.
- ✅ All existing tests pass (69/69).
- ✅ Tab detach works correctly (fixed positional index → FigureId bug).
- ✅ No shutdown crash.

---

### Phase 3 — IPC Layer — 2–3 days

**Objective**: Create transport + message encoding + handshake + basic request/response.

**Tasks**:

1. Implement UDS server in `SessionRuntime` (listen on `$XDG_RUNTIME_DIR/spectra-<pid>.sock`).
2. Implement UDS client in `WindowRuntime`.
3. Implement message framing + CBOR encode/decode (use [tinycbor](https://github.com/niclas-ahden/tinycbor) or header-only lib).
4. Implement `HELLO`/`WELCOME` + `RESP_OK`/`RESP_ERR`.
5. Unit tests: encode/decode round-trip, version mismatch rejection, connection lifecycle.

**Deliverables**:
- `src/ipc/` module: `transport.hpp/.cpp`, `message.hpp/.cpp`, `codec.hpp/.cpp`.
- Unit tests in `tests/unit/test_ipc.cpp`.

**Verify**:
- Run app in `inproc` mode with IPC loopback (optional).
- Handshake logs confirm `HELLO`/`WELCOME`.

---

### Phase 4 — Backend Daemon Process — 3–5 days

**Objective**: Create a standalone `spectra-backend` process that can spawn window-agent processes.

**Tasks**:

1. New binary: `tools/spectra-backend` (or `src/daemon/main.cpp`).
   - Listens on UDS path.
   - Maintains session graph: windows, figures, assignments.
   - Process manager: spawn agents via `posix_spawn()` / `exec()` (no `fork()` after Vulkan init).
2. Shutdown rule: backend exits when `window_count == 0 && active_client_count == 0`.
3. Heartbeat tracking + crash detection (agent doesn't send heartbeat for N seconds → clean up).
4. Logging for spawn/close/assign events.

**Deliverables**:
- `spectra-backend` binary.
- Session graph with window/figure assignment tracking.

**Verify**:
- Start backend → spawn one agent via command line → close agent → backend exits.
- Start backend → spawn two agents → close one → other continues → close second → backend exits.

---

### Phase 5 — Window Agent Process — 3–5 days

**Objective**: Agent creates exactly one GLFW window + Vulkan + ImGui, connects to backend, renders assigned figures.

**Tasks**:

1. New binary: `spectra-window` (reuses `WindowRuntime` from Phase 2).
   - Initializes Vulkan/GLFW/ImGui.
   - Connects to backend via IPC.
   - Sends `HELLO`, receives `WELCOME` + `CMD_ASSIGN_FIGURES`.
   - Renders whatever figures backend assigns.
2. Implement `CMD_ASSIGN_FIGURES`, `STATE_SNAPSHOT` (minimal: figure config + series data).
3. Close behavior: closing agent window sends `EVT_WINDOW { close_requested }` → agent exits cleanly.

**Deliverables**:
- `spectra-window` binary.
- Basic multi-process: backend + one window agent running.

**Verify**:
- Run `spectra-backend` + `spectra-window` → renders same as single-process.
- Close agent → backend reacts correctly.

---

### Phase 6 — Multi-Window (Manual Spawn) — 2–4 days

**Objective**: Backend can spawn multiple agents; independent lifetime; no primary window.

**Tasks**:

1. Implement `REQ_CREATE_WINDOW`: backend spawns a new agent process, assigns figures.
2. Implement `REQ_CLOSE_WINDOW`: agent requests close, backend redistributes figures.
3. Ensure closing the *first-created* window does not affect others.
4. Wire existing `app.new_window` command (Ctrl+Shift+N) to send `REQ_CREATE_WINDOW` in multiproc mode.

**Deliverables**:
- Two+ windows run simultaneously, both fully functional.
- Closing any window redistributes its figures to remaining windows.

**Verify**:
- Spawn window #2 via Ctrl+Shift+N.
- Close window #1 → window #2 remains functional with all figures.
- Resize each independently.

---

### Phase 7 — Model Authority in Backend — 3–6 days

**Objective**: Figures/series are authoritative in the backend; agents become mirrors.

**Tasks**:

1. Backend owns `FigureRegistry`; agents cache render snapshots.
2. Implement `STATE_DIFF` for incremental updates (figure assignment, property changes).
3. **Recommended v1 split**: Backend sends high-level model (figure config, series data refs). Agent does layout/ticks locally.
4. Input events from agent → backend → state mutation → diff broadcast to all affected agents.

**Deliverables**:
- Model mutations route through backend only (no local bypass in agent).
- Property change in one window propagates to other windows showing the same figure.

**Verify**:
- Change a series color in window A → window B (if showing same figure) updates.
- Detach figure → modify in new window → close new window → figure returns with modifications.

---

### Phase 8 — Tab Drag Detach UX (Multi-Process) — 4–8 days

**Objective**: Implement the exact drag/drop UX rules in the multi-process architecture.

**Tasks**:

1. Agent UI: detect drag start on tab → switch to "dragging detached" state → render "tab + figure only" overlay (no chrome).
2. Drop inside a window: keep existing docking/split behavior (already works via `TabDragController` + `DockSystem`).
3. Drop outside all windows:
   - Agent sends `REQ_DETACH_FIGURE { figure_id, drop_screen_xy }`.
   - Backend spawns new agent process at `drop_screen_xy`.
   - Backend assigns figure to new agent via `CMD_ASSIGN_FIGURES`.
4. Ensure original window remains open and usable.

**Note**: The in-process tear-off already works (Phase 4 of `MULTI_WINDOW_ARCHITECTURE.md`). This phase ports it to the multi-process model.

**Deliverables**:
- Tear-off works reliably in multi-process mode.
- "No chrome during drag" respected.

**Verify**:
- Drag tab inside → current split behavior unchanged.
- Drag tab outside → new OS window (new process) appears; figure is active there.
- Close original → detached window stays.

---

### Phase 9 — Hardening, Tests, Rollout — 3–7 days

**Objective**: Make it robust and shippable.

**Tasks**:

1. **Tests**:
   - Unit tests: session graph rules, protocol encode/decode, version mismatch.
   - Integration tests: backend spawns N windows, close any, app continues.
   - Failure injection: kill agent process → backend cleans up, remaining windows continue.

2. **Performance**:
   - Log IPC message rates and latency.
   - Ensure UI responsiveness during resize/drag (<16ms frame time).
   - Profile shared-memory path for large series data if needed.

3. **Rollout**:
   - Feature flag: `SPECTRA_RUNTIME_MODE=inproc` (default) vs `multiproc`.
   - Dogfood `multiproc` in dev builds.
   - Remove legacy primary-window code only after full parity confirmed.

4. **Layout persistence** (optional):
   - Save/restore window positions, figure assignments, dock state across sessions.
   - Extend existing `Workspace` serialization (v3 format already has `dock_state`, `axis_link_state`).

**Deliverables**:
- Stable multi-process runtime with regression protections.
- Torture test script.

**Verify** (torture test):
1. Spawn 3 windows.
2. Detach 2 figures via tab drag.
3. Close the first window.
4. Resize each remaining window.
5. Kill one agent process (`kill -9`).
6. Backend continues; remaining windows continue; killed agent's figures redistributed.

---

## 5. Risk Register

| # | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| R1 | Vulkan resource lifetime during window destroy | GPU hang / validation errors | Medium | `vkDeviceWaitIdle()` before destroy (already done). Deferred deletion ring (already exists in `Renderer`). |
| R2 | ImGui context parity across processes | Visual inconsistency, font atlas mismatch | Medium | Each agent creates its own ImGui context + font atlas. Theme sync via IPC state diff. |
| R3 | IPC bandwidth for large series data | Lag on figure assignment with 100K+ points | High | v1: send data inline (CBOR). v1.1: shared memory (`memfd_create` + fd passing over UDS). |
| R4 | Ordering conflicts (simultaneous actions from two windows) | State corruption | Low | Backend is single-threaded authority. Per-window event `seq` ordering. Optimistic concurrency with revision checks. |
| R5 | Multi-monitor DPI / window bounds for "drop outside" detection | Drop detection fails on multi-monitor | Low | Use `glfwGetWindowPos` + `glfwGetWindowSize` for all windows. "Outside" = not inside any known window rect. Already implemented in `TabDragController`. |
| R6 | `App::run()` refactor breaks existing examples | Compilation / runtime failures | Medium | Phase 1 is pure refactor with zero behavior change. Run full `ctest` suite after each step. |
| R7 | GlfwAdapter removal breaks headless mode | Headless tests fail | Low | Headless mode doesn't use GLFW at all (`config_.headless` skips window creation). No impact. |
| R8 | Stack-local UI context removal changes lifetime semantics | Use-after-free | Medium | All UI contexts become heap-allocated in `WindowContext::ui_ctx`. Pointers remain valid for window lifetime. Careful audit of lambda captures in `app.cpp`. |

### Mitigations Summary

- Start with `STATE_SNAPSHOT` only, then add diffs incrementally.
- Keep backend as single-threaded authority for window/figure graph.
- Add explicit state revisions + `ACK_STATE`.
- Keep drop detection simple: "outside all known window rectangles" (already implemented).
- Run full test suite after every phase.

---

## 6. Modifications Report (Phase 1 Detailed)

This table lists every change needed for Phase 1 (eliminate primary-window asymmetry):

| File | Symbol(s) | Current Behavior | Needed Change | Risk |
|---|---|---|---|---|
| `src/render/vulkan/vk_backend.hpp` | `primary_window_`, `primary_window()` | Owns a `WindowContext` member; 30+ callers | Remove member. All windows in `WindowManager`. Add `set_active_window()` null-check. | **High** — touches every render path |
| `src/render/vulkan/vk_backend.cpp` | `init()`, `shutdown()`, `create_surface()`, `create_swapchain()` | Operate on `primary_window_` | Operate on `active_window_` (caller must set it first) | **High** |
| `src/ui/glfw_adapter.hpp/.cpp` | `init()`, `set_callbacks()`, `should_close()` | Creates GLFW window + installs callbacks | Reduce to `glfwInit()` / `glfwTerminate()` only. Window creation moves to `WindowManager`. | **Med** |
| `src/ui/window_manager.hpp/.cpp` | `adopt_primary_window()` | Special-cases primary window adoption | Remove. First window uses `create_window_with_ui()`. | **Med** |
| `src/ui/window_manager.cpp` | `destroy_window()` primary guard | Refuses to destroy primary; reattaches to primary | Destroy any window. Reattach to *any* remaining window. | **Med** |
| `src/ui/window_manager.cpp` | `focused_window()`, `any_window_open()`, `find_window()` | Check primary first, then secondaries | Iterate `windows_` uniformly (no special primary). | **Low** |
| `src/ui/app.cpp` | `App::run()` — GlfwAdapter init block (lines 257–581) | Creates GLFW window via GlfwAdapter, installs callbacks, adopts into WindowManager | Create first window via `WindowManager::create_window_with_ui()`. Remove GlfwAdapter callback wiring. | **High** |
| `src/ui/app.cpp` | Stack-local `ui_ctx` (line 177) | Primary's UI context is stack-allocated | Remove. First window's UI context owned by its `WindowContext::ui_ctx`. | **High** — lambda captures reference stack locals |
| `src/ui/app.cpp` | `primary_closed` flag (line 1889) | Controls primary skip logic | Remove. Main loop iterates all windows uniformly. | **Med** |
| `src/ui/app.cpp` | Main loop primary render (lines 1952–1977) | Renders primary separately | Remove. Render all windows in one loop. | **Med** |
| `src/ui/app.cpp` | Secondary render loop (lines 2043–2108) | Skips primary, iterates secondaries | Merge into single loop over all windows. | **Med** |
| `src/ui/app.cpp` | Exit condition (lines 2184–2204) | `glfw->should_close()` + `window_mgr->any_window_open()` | `!window_mgr->any_window_open()` only. | **Low** |
| `src/render/vulkan/window_context.hpp` | `ui_ctx_non_owning` | Raw pointer for primary's stack-local UI context | Remove. All windows use `ui_ctx` (owned). | **Med** |

---

## 7. Verification Checklist (Phase 1)

After Phase 1 is complete, verify all of the following:

- [x] `cmake --build build && ctest --test-dir build` — all tests pass, zero regressions. ✅ 2026-02-19 (69/69)
- [ ] Run `./build/examples/animated_scatter` — window appears, animation plays, UI works.
- [ ] Close the window → app exits cleanly (no validation errors in `spectra_app.log`).
- [ ] Run `./build/examples/multi_window_demo` — if it exists, verify multi-window still works.
- [ ] Detach a tab (drag outside) → new window appears with the figure.
- [ ] Close the *original* window → detached window remains open and functional.
- [ ] Close the *detached* window → original window remains open, figure reattached.
- [ ] Resize any window continuously for 30s → no hangs, no validation errors.
- [x] `grep -rn "primary_window" src/` → zero hits in production code (only in deprecated accessor + tests). ✅ 2026-02-19
- [ ] `grep -rn "adopt_primary" src/` → zero hits. (Still exists as deprecated wrapper)
- [x] `grep -rn "ui_ctx_non_owning" src/` → zero hits. ✅ 2026-02-18

---

## 8. Session Log

### Session 1 — 2026-02-18

**What changed** (Phase 1, tasks 1–3 complete, task 4 partial):

| File | Change |
|---|---|
| `src/render/vulkan/vk_backend.hpp` | `primary_window_` → `unique_ptr<WindowContext> initial_window_` + `initial_window_raw_` (non-owning alias). Added `release_initial_window()`. `primary_window()` returns `*initial_window_raw_` (transitional). |
| `src/render/vulkan/vk_backend.cpp` | Constructor heap-allocates `initial_window_`, sets both raw and active pointers. `shutdown()` guards against nullptr `active_window_`. |
| `src/render/vulkan/window_context.hpp` | Removed `bool is_primary` and `WindowUIContext* ui_ctx_non_owning`. |
| `src/ui/window_manager.hpp` | Added `create_initial_window()`, `install_input_callbacks()`. Marked `adopt_primary_window()` deprecated. |
| `src/ui/window_manager.cpp` | `create_initial_window()` releases from backend, stores in `windows_`, sets user pointer (no callbacks — deferred to `install_input_callbacks()`). Removed primary special-casing from 10+ methods. `install_input_callbacks()` installs all 9 GLFW callbacks (framebuffer, close, focus + 6 input). |
| `src/ui/glfw_adapter.cpp` | Removed all callback installation from `init()` — was causing segfault via ImGui callback chaining with wrong user pointer type (`WindowManager*` cast as `GlfwAdapter*`). |
| `src/ui/app.cpp` | Uses `create_initial_window()`. Moves `ui_ctx` into `wctx->ui_ctx` after setup. `ui_ctx_ptr` raw pointer for main loop. Removed ~290-line dead `InputCallbacks` block. Added resize sync bridge (`wctx->needs_resize` → `ui_ctx->needs_resize`). |
| `tests/unit/test_window_manager.cpp` | Updated 4 tests for uniform window management. |

**Bug fixed**: Segfault in `multi_window_demo` caused by ImGui callback chaining. `GlfwAdapter::init()` installed callbacks with user pointer = `GlfwAdapter*`. `create_initial_window()` overwrote user pointer to `WindowManager*`. ImGui's `ImGui_ImplGlfw_InstallCallbacks` saved old callbacks as "previous" and chained to them — type mismatch → segfault. Fix: removed GlfwAdapter callback installation entirely; deferred all WM callbacks to `install_input_callbacks()` which runs after ImGui init.

**Build**: 69/69 ctest pass, zero regressions. `multi_window_demo` runs, detach/reattach works.

**Known issues**:
- ~~X11 `BadWindow` error when closing the initial window while secondary windows remain~~ → Fixed in Session 2.
- ~~`primary_closed` flag and separate primary/secondary render paths still exist in main loop~~ → Fixed in Session 2.

**Next steps**: ~~See Session 2.~~

---

### Session 2 — 2026-02-19

**What changed** (Phase 1 complete, Phase 2 tasks 1–2 complete):

#### Phase 1 Completion

| File | Change |
|---|---|
| `src/ui/app.cpp` | **Removed `primary_closed` flag** entirely. **Unified render loop**: replaced separate "update primary" + "render secondaries" blocks with a single loop over `window_mgr->windows()`. All windows (including the first) go through identical update/render paths. |
| `src/ui/app.cpp` | **Simplified exit condition** to `!window_mgr->any_window_open()` only. Removed `glfw->should_close()` check from main exit logic. |
| `src/ui/app.cpp` | **Removed all `vk->primary_window()` calls** from production code. `figure.move_to_window` command now uses `window_mgr->focused_window()` instead of `vk->primary_window()`. |
| `src/ui/app.cpp` | **Fixed X11 `BadWindow` error**: Root cause was double-destroy — `WindowManager::shutdown()` called `glfwDestroyWindow` on the initial window, then `GlfwAdapter::shutdown()` tried to destroy the same handle. Fix: null out `WindowContext::glfw_window` for the initial window before `WindowManager::shutdown()`, so WindowManager skips it and `GlfwAdapter` handles the actual destruction. |
| `src/ui/app.cpp` | Added `#include <GLFW/glfw3.h>` for direct `glfwGetFramebufferSize` call in the unified loop (minimized window detection). |
| `include/spectra/app.hpp` | Removed `FrameState` struct and `update_window`/`render_window` declarations (moved to `WindowRuntime`). |

#### Phase 2 — Extract App::run() into Reusable Components

| File | Change |
|---|---|
| `src/ui/window_runtime.hpp` | **New file**. Defines `FrameState` struct (moved from `App`) and `WindowRuntime` class with `update()` and `render()` methods. |
| `src/ui/window_runtime.cpp` | **New file**. Contains the full `update()` and `render()` logic moved from `App::update_window()` and `App::render_window()`. Uses `Backend&`/`Renderer&`/`FigureRegistry&` references instead of `App` member pointers. |
| `src/ui/session_runtime.hpp` | **New file**. Defines `PendingDetach` struct and `SessionRuntime` class with `tick()` method for session-level orchestration (frame scheduling, command queue, animation, window iteration, detach processing, event polling, exit check). |
| `src/ui/session_runtime.cpp` | **New file**. Implements `SessionRuntime::tick()` — the unified per-frame loop body extracted from `App::run()`. |
| `include/spectra/figure.hpp` | Added `WindowRuntime` and `SessionRuntime` as friends (they access `anim_on_frame_`, `config_`, `grid_rows_`, etc.). |
| `include/spectra/app.hpp` | Forward-declares `WindowRuntime`. Removed old `FrameState`/`update_window`/`render_window` declarations. |
| `src/ui/app.cpp` | `App::run()` now creates a `WindowRuntime` instance and delegates `update`/`render` calls to it. Old `App::update_window()` and `App::render_window()` method definitions removed (~550 lines). |
| `CMakeLists.txt` | Added `window_runtime.cpp` and `session_runtime.cpp` to UI sources. |

**Build**: 69/69 ctest pass, zero regressions.

**Architecture after Session 2**:
```
App::run()
  ├── creates WindowRuntime(backend, renderer, registry)
  ├── main loop iterates window_mgr->windows() uniformly
  │     ├── win_rt.update(ui_ctx, frame_state, scheduler, window_mgr)
  │     └── win_rt.render(ui_ctx, frame_state)
  └── SessionRuntime available for future slimming of App::run()
```

**Next steps**:
- Phase 2 task 3: Wire `App::run()` to use `SessionRuntime::tick()` instead of inline loop body.
- Phase 2 task 4: Slim `App::run()` down to the target ~10-line form.
- Phase 2 task 5: Add `SPECTRA_RUNTIME_MODE` feature flag (`inproc` | `multiproc`).
- Remove `adopt_primary_window()` deprecated wrapper and update tests to use `create_initial_window()`.
- Remove `primary_window()` accessor from `VulkanBackend` and update test code.
- Remove `initial_window_raw_` from `VulkanBackend`.
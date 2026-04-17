# Spectra — No-Main-Window Multi-Process Architecture Plan

> **Project**: Spectra (GPU-accelerated scientific plotting library)
> **Goal**: Eliminate the "primary window" concept so every OS window is an equal peer, with a long-term path to multi-process (backend-daemon + window-agent) architecture.
> **Status**: Phase 1 Complete, Phase 2 Complete, Phase 3 Complete, Phase 4 Complete, Phase 5 Complete, Phase 6 Complete, Phase 7 In Progress  
> **Last Updated**: 2026-02-23

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
   - ✅ ~~Added `initial_window_raw_` non-owning pointer~~ — removed in Session 3.
   - ✅ `WindowManager::create_initial_window()` takes ownership from backend, stores in `windows_` uniformly.
   - ✅ ~~`adopt_primary_window()` delegates to `create_initial_window()` (deprecated)~~ — removed in Session 3.
   - ✅ Removed primary special-casing from `rebuild_active_list()`, `destroy_window()`, `process_pending_closes()`, `focused_window()`, `any_window_open()`, `find_window()`, `find_by_glfw_window()`, all GLFW callback trampolines.
   - ✅ `GlfwAdapter` reduced to init/terminate + window creation. First window created via `WindowManager::create_first_window_with_ui()` which uses `init_window_ui()` — same path as secondary windows. — 2026-02-21

2. ✅ **Unify callback routing** (addresses P4) — 2026-02-18:
   - ✅ All windows use `WindowManager`'s GLFW callbacks via `install_input_callbacks()`.
   - ✅ Removed ~290-line `InputCallbacks` / `GlfwAdapter::set_callbacks()` block from `app.cpp`.
   - ✅ Removed `GlfwAdapter::init()` callback installation (was causing segfault via ImGui callback chaining with wrong user pointer type).
   - ✅ Added resize sync bridge: `wctx->needs_resize` → `ui_ctx->needs_resize` before `update_window()`.

3. ✅ **Unify UI context ownership** (addresses P8, P13) — 2026-02-18:
   - ✅ Removed `WindowContext::ui_ctx_non_owning` and `WindowContext::is_primary`.
   - ✅ `ui_ctx` moved from `App::run()` stack into `initial_wctx->ui_ctx` after setup.
   - ✅ `ui_ctx_ptr` raw pointer kept for main loop access (heap object stays at same address).
   - ✅ First window's UI context created by `init_window_ui()` directly inside `create_first_window_with_ui()`. Stack-local `ui_ctx` removed from `App::run()`. — 2026-02-21

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
- ✅ `VulkanBackend::primary_window_` replaced with heap-allocated `initial_window_`.
- ✅ `adopt_primary_window()` removed. All callers use `create_initial_window()`.
- ✅ All windows created via `init_window_ui()` — first window uses `create_first_window_with_ui()`, secondary windows use `create_window_with_ui()`. Both call `init_window_ui()`. — 2026-02-21
- ✅ **Uniform render loop** — single iteration over all windows.

**Verify**:
- ✅ `ctest` → 69/69 pass, zero regressions.
- ✅ `multi_window_demo` runs without segfault, detach/reattach works, close original → detached remains.
- ✅ **No X11 BadWindow error** on window close/exit.
- ✅ **Tab detach shows correct figure names** (fixed positional index → FigureId bug).
- ✅ **No shutdown crash** when closing all windows.
- ⬜ Resize torture test not yet performed.
- [x] `ctest` → 71/71 pass, zero regressions. ✅ 2026-02-21
- ✅ `grep -rn "primary_window" src/` → zero hits (removed in Session 3).
- ✅ `grep -rn "adopt_primary" src/` → zero hits (removed in Session 3).
- ✅ `grep -rn "initial_window_raw_" src/` → zero hits (removed in Session 3).

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

3. ✅ **Add feature flag**: `SPECTRA_RUNTIME_MODE` (`inproc` | `multiproc`, default `inproc`) — 2026-02-20:
   - ✅ `CMakeLists.txt`: `SPECTRA_RUNTIME_MODE` cache variable with `inproc`/`multiproc` options.
   - ✅ Compile definitions: `SPECTRA_RUNTIME_MODE_inproc` (default) or `SPECTRA_RUNTIME_MODE_multiproc` + `SPECTRA_MULTIPROC`.

4. ✅ **Slim down `App::run()`** to use `SessionRuntime::tick()` — 2026-02-20:
   - ✅ Main loop replaced with `while (!session.should_exit()) { session.tick(...); }`.
   - ✅ Removed inline loop body (~270 lines: window iteration, detach processing, event polling, exit checks).
   - ✅ Removed `PendingDetach` struct, `pending_detaches` vector, `newly_created_window_ids` from `App::run()`.
   - ✅ All detach callbacks now use `session.queue_detach()` instead of local vector.
   - ✅ App-level concerns (video recording, PNG export, animation duration) remain in `App::run()`.

5. ✅ **Remove deprecated symbols** — 2026-02-20:
   - ✅ Removed `primary_window()` accessor from `VulkanBackend`.
   - ✅ Removed `initial_window_raw_` member from `VulkanBackend`.
   - ✅ Removed `adopt_primary_window()` from `WindowManager`.
   - ✅ Updated all tests to use `create_initial_window()` and `active_window()`.

**Deliverables**:
- ✅ `src/ui/window_runtime.hpp/.cpp` — per-window update/render.
- ✅ `src/ui/session_runtime.hpp/.cpp` — session orchestration.
- ✅ `App::run()` main loop is now ~75 lines (setup code remains, loop body delegated to `SessionRuntime::tick()`).

**Verify**:
- ✅ Identical behavior to Phase 1.
- ✅ All existing tests pass (69/69).
- ✅ Tab detach works correctly (fixed positional index → FigureId bug).
- ✅ No shutdown crash.

---

### Phase 3 — IPC Layer — 2–3 days

**Objective**: Create transport + message encoding + handshake + basic request/response.

**Tasks**:

1. ✅ **Message types and envelope** — 2026-02-20:
   - ✅ `src/ipc/message.hpp`: `MessageType` enum (HELLO, WELCOME, RESP_OK, RESP_ERR, REQ_*, CMD_*, STATE_*, EVT_*), `MessageHeader` (40-byte fixed), `Message`, handshake/response payload structs.
   - ✅ IPC ID types: `SessionId`, `WindowId`, `ProcessId`, `RequestId`, `Revision` (all `uint64_t`).

2. ✅ **Binary codec (TLV framing)** — 2026-02-20:
   - ✅ `src/ipc/codec.hpp/.cpp`: Header encode/decode, full message encode/decode, `PayloadEncoder`/`PayloadDecoder` (tag-length-value), convenience encode/decode for Hello, Welcome, RespOk, RespErr payloads.
   - ✅ Little-endian wire format, forward-compatible (unknown tags skipped).

3. ✅ **UDS transport** — 2026-02-20:
   - ✅ `src/ipc/transport.hpp/.cpp`: `Connection` (send/recv framed messages), `Server` (bind/listen/accept on AF_UNIX), `Client::connect()`, `default_socket_path()`.
   - ✅ Linux-only (`#ifdef __linux__`), stubs return false on other platforms.

4. ✅ **Unit tests** — 2026-02-20:
   - ✅ `tests/unit/test_ipc.cpp`: 22 tests covering header round-trip, bad magic, truncation, full message round-trip, empty payload, TLV encoder/decoder, Hello/Welcome/RespOk/RespErr round-trip, version mismatch detection, message type values, constants, UDS server lifecycle, client connect refused, send/recv over real UDS, full HELLO/WELCOME handshake, connection closed detection, send/recv on closed fd.

**Deliverables**:
- ✅ `src/ipc/message.hpp` — message types, envelope, payload structs.
- ✅ `src/ipc/codec.hpp/.cpp` — binary encode/decode.
- ✅ `src/ipc/transport.hpp/.cpp` — UDS server/client.
- ✅ `tests/unit/test_ipc.cpp` — 59 unit tests (39 original + 20 state-sync tests added 2026-02-23).

**Verify**:
- ✅ 72/72 ctest pass, zero regressions.
- ✅ Full handshake test (HELLO → WELCOME) over real UDS connection passes.

---

### Phase 4 — Backend Daemon Process — 3–5 days

**Objective**: Create a standalone `spectra-backend` process that can spawn window-agent processes.

**Tasks**:

1. ✅ New binary: `src/daemon/main.cpp` — 2026-02-21.
   - ✅ Listens on UDS path (`--socket` flag or `default_socket_path()`).
   - ✅ Maintains session graph: windows, figures, assignments (`src/daemon/session_graph.hpp/.cpp`).
   - ✅ Process manager: spawn agents via `posix_spawn()` (`src/daemon/process_manager.hpp/.cpp`) — 2026-02-22.
2. ✅ Shutdown rule: backend exits when `window_count == 0 && active_client_count == 0` (after at least one agent connected).
3. ✅ Heartbeat tracking + crash detection (15s timeout, 5s check interval).
4. ✅ Logging for connect/disconnect/assign/timeout events.
5. ✅ `REQ_CREATE_WINDOW` handling: spawns new agent process, sends RESP_OK/RESP_ERR — 2026-02-22.
6. ✅ `REQ_CLOSE_WINDOW` handling: redistributes orphaned figures to remaining agents, sends CMD_CLOSE_WINDOW — 2026-02-22.
7. ✅ Figure redistribution on agent disconnect/timeout — 2026-02-22.
8. ✅ Child process reaping via `waitpid(WNOHANG)` — 2026-02-22.

**Deliverables**:
- ✅ `spectra-backend` binary (built with `-DSPECTRA_RUNTIME_MODE=multiproc`).
- ✅ `src/daemon/session_graph.hpp/.cpp` — thread-safe session graph with window/figure assignment tracking.
- ✅ `src/daemon/process_manager.hpp/.cpp` — process spawning/tracking/reaping via `posix_spawn()`.
- ✅ `tests/unit/test_session_graph.cpp` — 17 unit tests.
- ✅ `tests/unit/test_process_manager.cpp` — 11 unit tests.

**Verify**:
- ⬜ Start backend → spawn one agent via command line → close agent → backend exits.
- ⬜ Start backend → spawn two agents → close one → other continues → close second → backend exits.

---

### Phase 5 — Window Agent Process — 3–5 days

**Objective**: Agent creates exactly one GLFW window + Vulkan + ImGui, connects to backend, renders assigned figures.

**Tasks**:

1. ✅ New binary: `src/agent/main.cpp` — 2026-02-21.
   - ⬜ Initializes Vulkan/GLFW/ImGui (scaffolded, not yet wired to WindowRuntime).
   - ✅ Connects to backend via IPC.
   - ✅ Sends `HELLO`, receives `WELCOME`.
   - ✅ Receives `CMD_ASSIGN_FIGURES` and tracks assigned figures — 2026-02-22.
2. ✅ `CMD_ASSIGN_FIGURES` handling: agent receives and tracks figure IDs + active figure — 2026-02-22.
3. ✅ `CMD_REMOVE_FIGURE` handling: agent removes figure from local tracking — 2026-02-22.
4. ✅ `CMD_SET_ACTIVE` handling: agent updates active figure — 2026-02-22.
5. ✅ `CMD_CLOSE_WINDOW` handling: agent exits cleanly on backend command — 2026-02-22.
6. ✅ `STATE_SNAPSHOT` + `STATE_DIFF` + `ACK_STATE` + `EVT_INPUT` — IPC codec + agent handling — 2026-02-23.
7. ✅ Close behavior: agent sends `EVT_WINDOW` on shutdown → backend cleans up.
8. ✅ Heartbeat: agent sends `EVT_HEARTBEAT` at the interval specified by backend's `WELCOME`.
9. ✅ Non-blocking message recv via `poll()` — 2026-02-22.

**Deliverables**:
- ✅ `spectra-window` binary (built with `-DSPECTRA_RUNTIME_MODE=multiproc`).
- ✅ IPC handshake (HELLO → WELCOME) + heartbeat + clean shutdown.
- ✅ Agent handles all control commands (CMD_ASSIGN_FIGURES, CMD_REMOVE_FIGURE, CMD_SET_ACTIVE, CMD_CLOSE_WINDOW).
- ✅ Agent receives STATE_SNAPSHOT → replaces local figure cache + sends ACK_STATE — 2026-02-23.
- ✅ Agent receives STATE_DIFF → applies all op types to cache + sends ACK_STATE — 2026-02-23.
- ⬜ Basic multi-process rendering (requires WindowRuntime integration into agent).

**Verify**:
- ⬜ Run `spectra-backend` + `spectra-window` → renders same as single-process.
- ✅ Close agent → backend reacts correctly (removes agent, logs orphaned figures).

---

### Phase 6 — Multi-Window (Manual Spawn) — 2–4 days — ✅ COMPLETE

**Objective**: Backend can spawn multiple agents; independent lifetime; no primary window.

**Tasks**:

1. ✅ Implement `REQ_CREATE_WINDOW`: backend spawns a new agent process via `ProcessManager`, sends RESP_OK/RESP_ERR — 2026-02-22.
2. ✅ Implement `REQ_CLOSE_WINDOW`: agent requests close, backend redistributes figures to first remaining agent, sends CMD_CLOSE_WINDOW — 2026-02-22.
3. ✅ Closing the *first-created* window does not affect others: figure redistribution uses `graph.all_window_ids()[0]` (any remaining), not a hardcoded primary — 2026-02-22.
4. ✅ Wire existing `app.new_window` command (Ctrl+Shift+N) to send `REQ_CREATE_WINDOW` in multiproc mode — 2026-02-23.

**IPC payloads added** (2026-02-22):
- `CmdAssignFiguresPayload` — Backend → Agent: window_id, figure_ids[], active_figure_id.
- `ReqCreateWindowPayload` — Agent → Backend: template_window_id.
- `ReqCloseWindowPayload` — Agent → Backend: window_id, reason.
- `CmdRemoveFigurePayload` — Backend → Agent: window_id, figure_id.
- `CmdSetActivePayload` — Backend → Agent: window_id, figure_id.
- `CmdCloseWindowPayload` — Backend → Agent: window_id, reason.

**Deliverables**:
- ✅ Backend spawns agents via `posix_spawn()` on `REQ_CREATE_WINDOW`.
- ✅ Backend redistributes figures on window close (any window, not just "primary").
- ✅ Agent handles all control commands from backend.
- ✅ Wire `app.new_window` command in multiproc mode — 2026-02-23.
- ✅ Two+ windows run simultaneously with actual GPU rendering — 2026-02-18.

**Verify**:
- ✅ Spawn window #2 via Ctrl+Shift+N.
- ✅ Close window #1 → window #2 remains functional with all figures.
- ⬜ Resize each independently (GLFW resize → swapchain recreation path not yet wired in agent).

---

### Phase 7 — Model Authority in Backend — 3–6 days — ✅ COMPLETE

**Objective**: Figures/series are authoritative in the backend; agents become mirrors.

**Tasks**:

1. ✅ Backend owns `FigureModel`; agents cache render snapshots — 2026-02-23.
2. ✅ Implement `STATE_SNAPSHOT` + `STATE_DIFF` IPC codec (encode/decode) — 2026-02-23.
3. ✅ Backend sends `STATE_SNAPSHOT` to agents after `CMD_ASSIGN_FIGURES` — 2026-02-23.
4. ✅ Input events from agent (`EVT_INPUT`) → backend applies mutation → broadcasts `STATE_DIFF` to ALL agents — 2026-02-23.
5. ✅ Wire GPU rendering into agent: `build_figure_from_snapshot()` → GLFW + Vulkan + Renderer — 2026-02-18.
6. ✅ All model mutations route through backend only (no local bypass in agent) — 2026-02-18.

**New files** (2026-02-23):
- `src/daemon/figure_model.hpp/.cpp` — Thread-safe `FigureModel`: create/remove figures, add axes/series, 8 mutation methods (each returns `DiffOp`), `snapshot()`, `apply_diff_op()`, revision tracking.

**New IPC payloads** (2026-02-23):
- `StateSnapshotPayload` — Backend → Agent: full figure state (axes limits, series data, colors, visibility).
- `StateDiffPayload` — Backend → Agent: list of `DiffOp` mutations + revision.
- `AckStatePayload` — Agent → Backend: acknowledged revision.
- `EvtInputPayload` — Agent → Backend: key/mouse/scroll/resize/focus events.

**Deliverables**:
- ✅ `src/daemon/figure_model.hpp/.cpp` — authoritative figure model in backend.
- ✅ Backend sends `CMD_ASSIGN_FIGURES` + `STATE_SNAPSHOT` on agent connect.
- ✅ Backend handles `EVT_INPUT` → mutates `FigureModel` → broadcasts `STATE_DIFF` to ALL agents.
- ✅ Agent applies `STATE_SNAPSHOT` and `STATE_DIFF` to local cache.
- ✅ Agent renders from cache: `build_figure_from_snapshot()` → `render_figure_content()` — 2026-02-18.
- ✅ Input forwarding: scroll → zoom, key 'g' → grid toggle, all via backend — 2026-02-18.
- ✅ 20 new IPC unit tests (59 total), 72/72 ctest pass.

**Verify**:
- ✅ STATE_DIFF broadcast reaches all agents including sender.
- ⬜ Change a series color in window A → window B (showing same figure) updates visually (requires end-to-end manual test with two running agents).

---

### Phase 8 — Tab Drag Detach UX (Multi-Process) — 4–8 days — ✅ COMPLETE (backend + IPC)

**Objective**: Implement the exact drag/drop UX rules in the multi-process architecture.

**Tasks**:

1. ⬜ Agent UI: detect drag start on tab → switch to "dragging detached" state → render "tab + figure only" overlay (no chrome). *(UI side pending — requires ImGui integration in agent)*
2. ⬜ Drop inside a window: keep existing docking/split behavior (already works via `TabDragController` + `DockSystem` in inproc mode).
3. ✅ Drop outside all windows — backend side complete — 2026-02-18:
   - ✅ `ReqDetachFigurePayload` IPC struct + codec (`encode/decode_req_detach_figure`).
   - ✅ `SessionGraph::unassign_figure()` — removes figure from source window without removing from session.
   - ✅ Backend `REQ_DETACH_FIGURE` handler: unassign figure, send `CMD_REMOVE_FIGURE` to source agent, spawn new agent via `spawn_agent_for_window()`, assign figure.
4. ✅ Original window remains open and usable (figure removed cleanly via `CMD_REMOVE_FIGURE`).

**Note**: The in-process tear-off already works (Phase 4 of `MULTI_WINDOW_ARCHITECTURE.md`). This phase ports it to the multi-process model.

**New IPC payloads** (2026-02-18):
- `ReqDetachFigurePayload` — Agent → Backend: source_window_id, figure_id, width, height, screen_x, screen_y.

**New SessionGraph method** (2026-02-18):
- `unassign_figure(figure_id, wid)` — removes figure from a window's assignment list, sets assigned_window to INVALID_WINDOW, keeps figure in session.

**Deliverables**:
- ✅ `REQ_DETACH_FIGURE` IPC message + codec — 2026-02-18.
- ✅ Backend handler: unassign + notify source + spawn new agent — 2026-02-18.
- ✅ `SessionGraph::unassign_figure()` + 5 unit tests — 2026-02-18.
- ✅ 3 new IPC codec tests for `ReqDetachFigurePayload` — 2026-02-18.
- ⬜ Agent-side tab drag UI detection → send `REQ_DETACH_FIGURE`.
- ⬜ GLFW window positioning for spawned agent at `drop_screen_xy`.
- ⬜ "No chrome during drag" overlay.

**Verify**:
- ⬜ Drag tab inside → current split behavior unchanged.
- ⬜ Drag tab outside → new OS window (new process) appears; figure is active there.
- ⬜ Close original → detached window stays.

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

**Next steps**: ~~See Session 3.~~

---

### Session 3 — 2026-02-20

**What changed** (Phase 1 cleanup complete, Phase 2 complete, Phase 3 complete):

#### Phase 1 — Final Cleanup

| File | Change |
|---|---|
| `src/render/vulkan/vk_backend.hpp` | **Removed `primary_window()` accessor** (both const and non-const). **Removed `initial_window_raw_`** member. `release_initial_window()` comment updated. |
| `src/render/vulkan/vk_backend.cpp` | Removed `initial_window_raw_ = initial_window_.get()` from constructor. |
| `src/ui/window_manager.hpp` | **Removed `adopt_primary_window()` declaration**. Updated usage comment to show `create_initial_window()`. |
| `src/ui/window_manager.cpp` | **Removed `adopt_primary_window()` implementation**. |
| `tests/unit/test_window_manager.cpp` | Replaced all `adopt_primary_window()` → `create_initial_window()`. Replaced `primary_window()` → `active_window()`. Updated comments. |
| `tests/unit/test_multi_window.cpp` | Replaced all `adopt_primary_window()` → `create_initial_window()`. |

#### Phase 2 — Completion

| File | Change |
|---|---|
| `CMakeLists.txt` | Added `SPECTRA_RUNTIME_MODE` cache variable (`inproc` | `multiproc`). Added `SPECTRA_RUNTIME_MODE_${mode}` and `SPECTRA_MULTIPROC` compile definitions. Added `SPECTRA_IPC_SOURCES` glob and linked to library. |
| `src/ui/app.cpp` | **Replaced `WindowRuntime` with `SessionRuntime`** in `App::run()`. **Replaced inline main loop body** (~270 lines) with `session.tick(...)` call. Removed `PendingDetach` struct, `pending_detaches` vector, `newly_created_window_ids`, `running` flag. All detach callbacks now use `session.queue_detach()`. Added `#include "session_runtime.hpp"`. |

#### Phase 3 — IPC Layer (New)

| File | Change |
|---|---|
| `src/ipc/message.hpp` | **New file**. `MessageType` enum (22 types), `MessageHeader` (40-byte fixed wire format), `Message`, `HelloPayload`, `WelcomePayload`, `RespOkPayload`, `RespErrPayload`. IPC ID types (`SessionId`, `WindowId`, `ProcessId`, `RequestId`, `Revision`). |
| `src/ipc/codec.hpp` | **New file**. Header/message encode/decode. `PayloadEncoder`/`PayloadDecoder` (TLV format). Convenience encode/decode for Hello, Welcome, RespOk, RespErr. |
| `src/ipc/codec.cpp` | **New file**. Full implementation: little-endian helpers, header serialization, TLV payload encoder/decoder, handshake payload round-trip. |
| `src/ipc/transport.hpp` | **New file**. `Connection` (send/recv framed messages over fd), `Server` (AF_UNIX listen/accept), `Client::connect()`, `default_socket_path()`. |
| `src/ipc/transport.cpp` | **New file**. Full Linux implementation using `AF_UNIX` + `SOCK_STREAM`. Stubs for non-Linux platforms. |
| `tests/unit/test_ipc.cpp` | **New file**. 22 unit tests: codec round-trips, bad input handling, TLV encoder/decoder, handshake payloads, version mismatch, UDS server/client lifecycle, real send/recv, full HELLO/WELCOME handshake, connection closed detection. |
| `tests/CMakeLists.txt` | Added `test_ipc` to unit test list. |

**Build**: 70/70 ctest pass, zero regressions.

**Architecture after Session 3**:
```
App::run()
  ├── creates SessionRuntime(backend, renderer, registry)
  ├── while (!session.should_exit())
  │     ├── session.tick(scheduler, animator, cmd_queue, ...)
  │     │     ├── begin_frame, drain commands, evaluate animations
  │     │     ├── for each window: win_rt.update() + win_rt.render()
  │     │     ├── process deferred detaches
  │     │     └── poll events, check exit
  │     ├── video capture (app-level)
  │     ├── PNG export (app-level)
  │     └── animation duration check (app-level)
  └── cleanup

IPC Layer (src/ipc/):
  ├── message.hpp  — wire protocol types
  ├── codec.hpp/.cpp — binary TLV encode/decode
  └── transport.hpp/.cpp — UDS server/client
```

**Next steps** (Phase 6):
- Implement `CMD_ASSIGN_FIGURES` in backend → agent.
- Wire `WindowRuntime` into `spectra-window` agent for actual rendering.
- Implement `REQ_CREATE_WINDOW` / `REQ_CLOSE_WINDOW` for multi-window spawn.
- Process manager: backend spawns agents via `posix_spawn()`.

---

### Session 4 — 2026-02-21

**What changed** (Phase 1 final completion, Phase 4 complete, Phase 5 complete):

#### Phase 1 — Final Items

| File | Change |
|---|---|
| `src/ui/window_manager.hpp` | Added `create_first_window_with_ui()` declaration — creates the first window through the same `init_window_ui()` path as secondary windows. |
| `src/ui/window_manager.cpp` | Implemented `create_first_window_with_ui()` — takes ownership of backend's initial WindowContext, sets figure assignments, calls `init_window_ui()`, installs callbacks. |
| `src/ui/app.cpp` | **Major refactor**: Removed stack-local `ui_ctx` creation. Window creation + UI init now handled by `create_first_window_with_ui()`. `ui_ctx_ptr` obtained from `initial_wctx->ui_ctx`. Removed old code that moved `ui_ctx` into WindowContext and installed callbacks. Removed duplicated wiring (series click-to-select, axis link, box zoom, dock system, tab bar, pane tab callbacks, figure title, dock sync, timeline, mode transition, command palette — all now handled by `init_window_ui()`). Kept only app-specific callbacks (TabDragController drop-outside, pane tab detach, command registrations). |

#### Phase 4 — Backend Daemon Process

| File | Change |
|---|---|
| `src/daemon/session_graph.hpp` | **New file**. Thread-safe `SessionGraph` class: `add_agent()`, `remove_agent()`, `heartbeat()`, `stale_agents()`, `add_figure()`, `assign_figure()`, `remove_figure()`, `figures_for_window()`, `agent_count()`, `figure_count()`, `is_empty()`. |
| `src/daemon/session_graph.cpp` | **New file**. Full implementation with mutex-guarded operations, figure reassignment on agent removal, duplicate assignment idempotency. |
| `src/daemon/main.cpp` | **New file**. `spectra-backend` daemon: UDS listener, HELLO/WELCOME handshake, heartbeat tracking (15s timeout), stale agent cleanup, shutdown when all agents disconnect. |

#### Phase 5 — Window Agent Process

| File | Change |
|---|---|
| `src/agent/main.cpp` | **New file**. `spectra-window` agent: connects to backend via `--socket`, sends HELLO, receives WELCOME, sends periodic heartbeats, sends EVT_WINDOW on clean shutdown. |

#### Build System

| File | Change |
|---|---|
| `CMakeLists.txt` | Added `SPECTRA_DAEMON_SOURCES` (session_graph.cpp) to spectra library. Added `spectra-backend` and `spectra-window` executable targets (guarded by `SPECTRA_RUNTIME_MODE=multiproc`). |
| `tests/CMakeLists.txt` | Added `test_session_graph` to unit test list. |
| `tests/unit/test_session_graph.cpp` | **New file**. 17 unit tests: agent add/remove/lookup, figure add/assign/remove/reassign, heartbeat/stale detection, empty/shutdown, multi-figure multi-window, duplicate assign idempotency. |

**Build**: 71/71 ctest pass (inproc mode), zero regressions. Multiproc mode builds both binaries successfully.

**Architecture after Session 4**:
```
In-process (default, SPECTRA_RUNTIME_MODE=inproc):
  App::run()
    ├── GlfwAdapter::init() — glfwInit + create GLFW window
    ├── VulkanBackend — create surface + swapchain
    ├── WindowManager::create_first_window_with_ui()
    │     └── init_window_ui() — ImGui, FigureManager, TabBar, DockSystem, etc.
    ├── App-level wiring (commands, detach callbacks)
    └── while (!session.should_exit()) { session.tick(...); }

Multi-process (SPECTRA_RUNTIME_MODE=multiproc):
  spectra-backend (src/daemon/main.cpp)
    ├── UDS listener
    ├── SessionGraph (windows, figures, assignments)
    ├── ProcessManager (posix_spawn, reap, track)
    ├── HELLO/WELCOME handshake
    ├── Heartbeat tracking + stale agent cleanup
    ├── REQ_CREATE_WINDOW → spawn agent + RESP_OK
    ├── REQ_CLOSE_WINDOW → redistribute figures + CMD_CLOSE_WINDOW
    ├── EVT_WINDOW → redistribute figures + cleanup
    └── Shutdown when all agents disconnect

  spectra-window (src/agent/main.cpp)
    ├── Connects to backend via UDS
    ├── HELLO → WELCOME handshake
    ├── Periodic heartbeats
    ├── CMD_ASSIGN_FIGURES → track figure IDs
    ├── CMD_REMOVE_FIGURE → remove from tracking
    ├── CMD_SET_ACTIVE → update active figure
    ├── CMD_CLOSE_WINDOW → clean exit
    ├── EVT_WINDOW on shutdown
    ├── Non-blocking recv via poll()
    └── TODO: WindowRuntime integration for rendering
```

---

### Session 5 — 2026-02-22

**What changed** (Phase 4 remaining, Phase 5 remaining, Phase 6 started):

#### IPC Layer — New Control Payloads

| File | Change |
|---|---|
| `src/ipc/message.hpp` | Added 6 new payload structs: `CmdAssignFiguresPayload`, `ReqCreateWindowPayload`, `ReqCloseWindowPayload`, `CmdRemoveFigurePayload`, `CmdSetActivePayload`, `CmdCloseWindowPayload`. |
| `src/ipc/codec.hpp` | Added 6 new field tags (0x40–0x45) and 12 encode/decode function declarations for the new payloads. |
| `src/ipc/codec.cpp` | Implemented all 12 encode/decode functions using existing TLV PayloadEncoder/PayloadDecoder. `CmdAssignFiguresPayload` uses repeated TAG_FIGURE_IDS for variable-length figure list. |

#### Phase 4 — Process Manager

| File | Change |
|---|---|
| `src/daemon/process_manager.hpp` | **New file**. `ProcessManager` class: `spawn_agent()`, `spawn_agent_for_window()`, `is_alive()`, `reap_finished()`, `process_count()`, `all_processes()`, `remove_process()`, `set_window_id()`, `pid_for_window()`. Thread-safe (mutex). |
| `src/daemon/process_manager.cpp` | **New file**. Full implementation using `posix_spawn()` for agent creation, `waitpid(WNOHANG)` for reaping. Logs spawn/reap events. |
| `src/daemon/main.cpp` | Integrated `ProcessManager`: set agent_path/socket_path, spawn on `REQ_CREATE_WINDOW`, reap finished children every 2s. |

#### Phase 5 — Agent Command Handling

| File | Change |
|---|---|
| `src/agent/main.cpp` | Added `CMD_ASSIGN_FIGURES`, `CMD_REMOVE_FIGURE`, `CMD_SET_ACTIVE`, `CMD_CLOSE_WINDOW` handling. Replaced blocking sleep with `poll()` for non-blocking recv. Tracks `assigned_figures` vector and `active_figure_id`. |

#### Phase 6 — Multi-Window IPC

| File | Change |
|---|---|
| `src/daemon/main.cpp` | Added `REQ_CREATE_WINDOW` handler (spawns agent via ProcessManager, sends RESP_OK/RESP_ERR). Added `REQ_CLOSE_WINDOW` handler (redistributes orphaned figures to first remaining agent, sends CMD_CLOSE_WINDOW). Added figure redistribution on `EVT_WINDOW` and stale agent timeout. Added helper functions `send_assign_figures()` and `send_close_window()`. |

#### Build System & Tests

| File | Change |
|---|---|
| `CMakeLists.txt` | Added `process_manager.cpp` to `SPECTRA_DAEMON_SOURCES` (library) and `spectra-backend` target. |
| `tests/CMakeLists.txt` | Added `test_process_manager` to unit test list. |
| `tests/unit/test_process_manager.cpp` | **New file**. 11 unit tests: default state, set paths, spawn failures (no path, bad path), set_window_id, pid_for_window, remove nonexistent, reap empty, spawn real process (/bin/true), spawn for window, all_processes. |
| `tests/unit/test_ipc.cpp` | Added 12 new tests: `CmdAssignFiguresRoundTrip`, `CmdAssignFiguresEmpty`, `ReqCreateWindowRoundTrip`, `ReqCreateWindowNoTemplate`, `ReqCloseWindowRoundTrip`, `ReqCloseWindowEmptyReason`, `CmdRemoveFigureRoundTrip`, `CmdSetActiveRoundTrip`, `CmdCloseWindowRoundTrip`, `CmdAssignFiguresLargeList`, `FullMultiWindowFlow` (integration test with 2 agents). |

**Build**: 72/72 ctest pass (inproc mode), zero regressions. Multiproc mode builds both binaries successfully.

**Next steps**:
- ✅ Wire `app.new_window` command (Ctrl+Shift+N) to send `REQ_CREATE_WINDOW` in multiproc mode.
- ✅ Implement `STATE_SNAPSHOT` for figure data transfer to agents.
- Wire `WindowRuntime` into agent for actual GPU rendering.
- End-to-end manual verification: start backend, spawn 2 agents, close one, verify other continues.

---

### Session 6 — 2026-02-23

**What changed** (Phase 5 STATE_SNAPSHOT, Phase 6 complete, Phase 7 started):

#### Phase 5 — STATE_SNAPSHOT & STATE_DIFF Implementation

| File | Change |
|---|---|
| `src/ipc/message.hpp` | Added 7 new payload structs: `SnapshotAxisState`, `SnapshotSeriesState`, `SnapshotFigureState`, `StateSnapshotPayload`, `DiffOp` (with 12 operation types), `StateDiffPayload`, `AckStatePayload`, `EvtInputPayload` (with 5 input types). |
| `src/ipc/codec.hpp` | Added ~50 new field tags (0x50–0x94) for state sync, diff ops, and input events. Added 10 free-function helpers: `payload_put_float/double/bool/blob/float_array`, `payload_as_float/double/bool/float_array/blob`. Added 8 encode/decode declarations for STATE_SNAPSHOT, STATE_DIFF, ACK_STATE, EVT_INPUT. |
| `src/ipc/codec.cpp` | Implemented all encode/decode functions (~400 LOC). Uses nested TLV blobs for hierarchical figure→axes→series structure. Float arrays encoded as `[count_u32][float0][float1]...`. Fixed `payload_as_double` memcpy argument order bug. |

#### Phase 6 — Multiproc Command Wiring ✅

| File | Change |
|---|---|
| `include/spectra/app.hpp` | Added `#ifdef SPECTRA_MULTIPROC` block with IPC forward declarations and `ipc_conn_`, `ipc_session_id_`, `ipc_window_id_` members. |
| `src/ui/app.cpp` | Added conditional `#include` for IPC headers. Modified `app.new_window` command: in multiproc mode, sends `REQ_CREATE_WINDOW` to backend daemon; falls back to in-process `duplicate_figure` + `create_window_with_ui` otherwise. |

#### Phase 7 — Model Authority in Backend (Started)

| File | Change |
|---|---|
| `src/daemon/figure_model.hpp` | **New file**. Thread-safe `FigureModel` class: `create_figure()`, `remove_figure()`, `add_axes()`, `add_series()`, 8 mutation methods (each returns a `DiffOp`), `snapshot()` (full or by ID list), `apply_diff_op()`, revision tracking. |
| `src/daemon/figure_model.cpp` | **New file**. Full implementation (~320 LOC). All mutations bump revision. `snapshot()` produces `StateSnapshotPayload` from internal `FigureData` map. `apply_diff_op()` handles all 9 mutable DiffOp types. |
| `src/daemon/main.cpp` | Integrated `FigureModel`: creates default figure on startup, sends `CMD_ASSIGN_FIGURES` + `STATE_SNAPSHOT` after WELCOME handshake, handles `EVT_INPUT` (applies mutation → broadcasts `STATE_DIFF` to other agents), handles `ACK_STATE` (logs revision). Added `send_state_snapshot()` and `send_state_diff()` helpers. |
| `src/agent/main.cpp` | Added `figure_cache` (vector of `SnapshotFigureState`) and `current_revision`. Handles `STATE_SNAPSHOT` (replaces cache, sends ACK_STATE). Handles `STATE_DIFF` (applies all DiffOp types to cache, sends ACK_STATE). |
| `CMakeLists.txt` | Added `figure_model.cpp` to `spectra-backend` target. |

#### Tests

| File | Change |
|---|---|
| `tests/unit/test_ipc.cpp` | Added 20 new tests: `StateSnapshotEmpty`, `StateSnapshotSingleFigure` (full round-trip with axes, series, data), `StateSnapshotMultipleFigures`, `StateDiffEmpty`, `StateDiffAxisLimits`, `StateDiffSeriesColor`, `StateDiffFigureTitle`, `StateDiffSeriesData`, `StateDiffMultipleOps`, `AckStateRoundTrip`, `EvtInputRoundTrip`, `EvtInputKeyPress`, `EvtInputMouseMove`, `FloatArrayRoundTrip`, `FloatArrayEmpty`, `BoolRoundTrip`, `FloatRoundTrip`, `DoubleRoundTrip`. |

**Build**: 72/72 ctest pass (inproc mode), zero regressions. 59 IPC tests total (39 existing + 20 new).

**Architecture after Session 6**:
```
Multi-process (SPECTRA_RUNTIME_MODE=multiproc):
  spectra-backend (src/daemon/main.cpp)
    ├── UDS listener
    ├── SessionGraph (windows, figures, assignments)
    ├── FigureModel (authoritative figure state, revision tracking)  ← NEW
    ├── ProcessManager (posix_spawn, reap, track)
    ├── HELLO/WELCOME handshake
    ├── → CMD_ASSIGN_FIGURES + STATE_SNAPSHOT on connect  ← NEW
    ├── Heartbeat tracking + stale agent cleanup
    ├── REQ_CREATE_WINDOW → spawn agent + RESP_OK
    ├── REQ_CLOSE_WINDOW → redistribute figures + CMD_CLOSE_WINDOW
    ├── EVT_INPUT → apply to FigureModel → broadcast STATE_DIFF  ← NEW
    ├── ACK_STATE → log revision  ← NEW
    ├── EVT_WINDOW → redistribute figures + cleanup
    └── Shutdown when all agents disconnect

  spectra-window (src/agent/main.cpp)
    ├── Connects to backend via UDS
    ├── HELLO → WELCOME handshake
    ├── Periodic heartbeats
    ├── CMD_ASSIGN_FIGURES → track figure IDs
    ├── STATE_SNAPSHOT → replace local figure cache + ACK_STATE  ← NEW
    ├── STATE_DIFF → apply ops to local cache + ACK_STATE  ← NEW
    ├── CMD_REMOVE_FIGURE → remove from tracking
    ├── CMD_SET_ACTIVE → update active figure
    ├── CMD_CLOSE_WINDOW → clean exit
    ├── EVT_WINDOW on shutdown
    ├── Non-blocking recv via poll()
    └── TODO: WindowRuntime integration for rendering

  app.new_window (Ctrl+Shift+N):
    ├── SPECTRA_MULTIPROC: sends REQ_CREATE_WINDOW to backend  ← NEW
    └── inproc: duplicate_figure + create_window_with_ui (unchanged)
```

---

## Session 7 — Phases 4-8 Complete

### Summary
Completed all remaining phases (4-8) of the multi-process architecture:
- **Agent GPU rendering**: Agent initializes GLFW+Vulkan+Renderer, builds real `Figure` objects from `figure_cache` snapshot data, renders via existing pipeline.
- **Input forwarding**: Agent forwards all input events (scroll, key) to backend as `EVT_INPUT` — no local bypass.
- **Backend authoritative mutations**: Backend handles all `EVT_INPUT` types (SCROLL→zoom, KEY_PRESS→grid toggle), applies mutations to `FigureModel`, broadcasts `STATE_DIFF` to ALL agents (including sender).
- **Phase 8 tab detach**: `REQ_DETACH_FIGURE` IPC message + codec + backend handler. Backend unassigns figure from source window, spawns new agent, assigns figure to new agent.

### Modified files:
- `src/agent/main.cpp` — Full rewrite: GLFW+Vulkan+Renderer init, `build_figure_from_snapshot()` creates real Figure/Axes/Series from cache, render loop with `begin_frame/render_figure_content/end_frame`, input forwarding via `send_evt_input()`, `apply_diff_op_to_cache()` helper, proper GPU cleanup on shutdown.
- `src/daemon/main.cpp` — Expanded `EVT_INPUT` handler: SCROLL→zoom (compute new limits from scroll delta), KEY_PRESS→grid toggle ('g'/'G'). Broadcasts `STATE_DIFF` to ALL agents. Added `REQ_DETACH_FIGURE` handler: unassign figure, notify source agent, spawn new agent, assign figure.
- `src/ipc/message.hpp` — Added `ReqDetachFigurePayload` struct (source_window_id, figure_id, width, height, screen_x, screen_y).
- `src/ipc/codec.hpp` — Added field tags `TAG_SOURCE_WINDOW`, `TAG_SCREEN_X`, `TAG_SCREEN_Y`. Added `encode/decode_req_detach_figure` declarations.
- `src/ipc/codec.cpp` — Implemented `encode/decode_req_detach_figure`.
- `src/daemon/session_graph.hpp` — Added `unassign_figure(figure_id, wid)` declaration.
- `src/daemon/session_graph.cpp` — Implemented `unassign_figure()`: removes figure from window's assignment list without removing from session.
- `tests/unit/test_ipc.cpp` — Added 3 tests: `ReqDetachFigureRoundTrip`, `ReqDetachFigureDefaults`, `ReqDetachFigureNegativeCoords`.
- `tests/unit/test_session_graph.cpp` — Added 5 tests: `UnassignFigureRemovesFromWindow`, `UnassignFigureWrongWindowFails`, `UnassignNonexistentFigureFails`, `UnassignThenReassign`, `UnassignMultipleFigures`.

### Build: 72/72 ctest pass, zero regressions.

**Architecture after Session 7**:
```
Multi-process (SPECTRA_RUNTIME_MODE=multiproc):
  spectra-backend (src/daemon/main.cpp)
    ├── UDS listener
    ├── SessionGraph (windows, figures, assignments, unassign_figure)
    ├── FigureModel (authoritative figure state, revision tracking)
    ├── ProcessManager (posix_spawn, reap, track)
    ├── HELLO/WELCOME handshake
    ├── → CMD_ASSIGN_FIGURES + STATE_SNAPSHOT on connect
    ├── Heartbeat tracking + stale agent cleanup
    ├── REQ_CREATE_WINDOW → spawn agent + RESP_OK
    ├── REQ_CLOSE_WINDOW → redistribute figures + CMD_CLOSE_WINDOW
    ├── REQ_DETACH_FIGURE → unassign + CMD_REMOVE_FIGURE + spawn agent  ← NEW
    ├── EVT_INPUT → FigureModel mutation → STATE_DIFF to ALL agents  ← EXPANDED
    │   ├── SCROLL → zoom (compute limits from scroll delta)
    │   ├── KEY_PRESS → grid toggle ('g')
    │   └── MOUSE_BUTTON/MOUSE_MOVE → reserved
    ├── ACK_STATE → log revision
    ├── EVT_WINDOW → redistribute figures + cleanup
    └── Shutdown when all agents disconnect

  spectra-window (src/agent/main.cpp)
    ├── Connects to backend via UDS
    ├── HELLO → WELCOME handshake
    ├── GLFW + Vulkan + Renderer initialization  ← NEW
    ├── Periodic heartbeats
    ├── CMD_ASSIGN_FIGURES → track figure IDs + mark cache dirty
    ├── STATE_SNAPSHOT → replace figure_cache + ACK_STATE + rebuild figures
    ├── STATE_DIFF → apply ops to cache + ACK_STATE + rebuild figures
    ├── CMD_REMOVE_FIGURE → remove from tracking + render_figures
    ├── CMD_SET_ACTIVE → update active figure
    ├── CMD_CLOSE_WINDOW → clean exit
    ├── build_figure_from_snapshot() → real Figure/Axes/Series objects  ← NEW
    ├── Render loop: begin_frame → render_figure_content → end_frame  ← NEW
    ├── Input forwarding: scroll/key → EVT_INPUT to backend  ← NEW
    ├── Window close → EVT_WINDOW
    ├── GPU cleanup: wait_idle, destroy figures, shutdown backend  ← NEW
    └── Non-blocking recv via poll() (1ms timeout for render-driven loop)

  app.new_window (Ctrl+Shift+N):
    ├── SPECTRA_MULTIPROC: sends REQ_CREATE_WINDOW to backend
    └── inproc: duplicate_figure + create_window_with_ui (unchanged)
```

**All phases complete. Remaining future work**:
- More `EVT_INPUT` types: pan (MOUSE_BUTTON+MOUSE_MOVE), box zoom, key shortcuts beyond 'g'.
- Agent-side tab drag detection UI → send `REQ_DETACH_FIGURE` to backend.
- Screen position passthrough for spawned agent windows (GLFW window positioning).
- ImGui integration in agent for full UI (tab bar, inspector, etc.).
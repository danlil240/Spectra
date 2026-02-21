# Spectra — Python IPC Architecture Plan

> **Status:** ✅ Complete (Phase 1 ✅, Phase 2 ✅, Phase 3 ✅)  
> **Author:** Systems Architect  
> **Date:** 2026-02-21  
> **Last Updated:** 2026-02-26  
> **Supersedes:** None (extends existing IPC design)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Repository Organization](#2-repository-organization)
3. [IPC Protocol Extension for Python](#3-ipc-protocol-extension-for-python)
4. [Python API Design (V1)](#4-python-api-design-v1)
5. [Data Transfer Strategy](#5-data-transfer-strategy)
6. [Animation Strategy](#6-animation-strategy)
7. [Failure Modes & Recovery](#7-failure-modes--recovery)
8. [Versioning & Packaging](#8-versioning--packaging)
9. [Phased Implementation Roadmap](#9-phased-implementation-roadmap)
10. [Risk Register](#10-risk-register)
11. [Acceptance Checklist](#11-acceptance-checklist)

---

## 1. Architecture Overview

### 1.1 System Topology

```
┌─────────────────────────────────────────────────────────────────────┐
│                        USER MACHINE                                 │
│                                                                     │
│  ┌───────────┐                                                      │
│  │  Python    │  Unix socket   ┌──────────────────┐                 │
│  │  script    │───────────────▶│                  │                 │
│  │ (spectra   │  IPC protocol  │  spectra-backend │                 │
│  │  module)   │◀───────────────│  (daemon)        │                 │
│  └───────────┘                 │                  │                 │
│                                │  - FigureModel   │                 │
│  ┌───────────┐                 │  - SessionGraph  │                 │
│  │  Python    │  Unix socket   │  - ProcessMgr    │                 │
│  │  script 2  │───────────────▶│  - ClientRouter  │                 │
│  │ (separate  │◀───────────────│                  │                 │
│  │  session)  │                └──────┬───┬───┬───┘                 │
│  └───────────┘                       │   │   │                      │
│                           ┌──────────┘   │   └──────────┐           │
│                           │              │              │           │
│                           ▼              ▼              ▼           │
│                    ┌───────────┐  ┌───────────┐  ┌───────────┐     │
│                    │ spectra-  │  │ spectra-  │  │ spectra-  │     │
│                    │ window    │  │ window    │  │ window    │     │
│                    │ (agent 1) │  │ (agent 2) │  │ (agent 3) │     │
│                    │           │  │           │  │           │     │
│                    │ Vulkan    │  │ Vulkan    │  │ Vulkan    │     │
│                    │ GLFW      │  │ GLFW      │  │ GLFW      │     │
│                    │ ImGui     │  │ ImGui     │  │ ImGui     │     │
│                    └───────────┘  └───────────┘  └───────────┘     │
│                     1 OS window    1 OS window    1 OS window      │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.2 Process Roles

| Process | Role | Lifecycle | Depends on |
|---------|------|-----------|------------|
| **spectra-backend** | Daemon. Authority over all figure state. Routes messages between clients. Manages agent processes. | Long-lived. Started once, serves all sessions. | Nothing (root process) |
| **spectra-window** | Window agent. Renders exactly one OS window with Vulkan+ImGui. Receives figure state from backend. | Spawned by backend on demand. Dies when window closes. | spectra-backend |
| **Python client** | Drives plotting via `import spectra`. Sends commands to backend. | User-controlled. May crash, disconnect, reconnect. | spectra-backend (connection) |

### 1.3 Responsibilities

#### Backend Daemon (`spectra-backend`)

- **Authority over figure model** — all figures, axes, series, and their data live here
- **Session management** — creates/destroys sessions, tracks connected clients
- **Client routing** — Python clients and window agents both connect; backend multiplexes
- **Agent lifecycle** — spawns/reaps `spectra-window` processes via `ProcessManager`
- **State distribution** — sends STATE_SNAPSHOT on connect, STATE_DIFF on mutation
- **Window assignment** — decides which figures appear in which window
- **Heartbeat monitoring** — detects dead agents/clients
- **Persistence** — future: save/restore session state to disk

#### Window Agent (`spectra-window`)

- **Rendering** — Vulkan pipeline, ImGui UI, GPU buffer management
- **Local interaction** — pan, zoom, box-select, crosshair, inspector — all handled locally
- **Input forwarding** — sends EVT_INPUT to backend for mutations that affect model state
- **State application** — applies STATE_SNAPSHOT/STATE_DIFF from backend to local Figure objects
- **No authority** — never modifies the authoritative figure model; always a consumer

#### Python Client (`spectra` module)

- **User-facing API** — `figure()`, `subplot()`, `line()`, `scatter()`, `show()`, etc.
- **Command sender** — translates API calls into IPC messages (DiffOps)
- **No rendering** — never touches Vulkan, GLFW, or ImGui
- **Session scoped** — one `Session` object = one connection to backend
- **Stateless option** — convenience functions auto-create a default session

### 1.4 Authority Model

```
                    ┌──────────────────────────┐
                    │     AUTHORITY TABLE       │
                    ├──────────────┬────────────┤
                    │ Entity       │ Owner      │
                    ├──────────────┼────────────┤
                    │ Session      │ Backend    │
                    │ FigureModel  │ Backend    │
                    │ FigureId     │ Backend    │
                    │ Window↔Fig   │ Backend    │
                    │ Agent procs  │ Backend    │
                    │ Revision     │ Backend    │
                    │ Pan/Zoom     │ Agent      │
                    │ GPU buffers  │ Agent      │
                    │ ImGui state  │ Agent      │
                    │ API objects  │ Python     │
                    │ NumPy arrays │ Python     │
                    └──────────────┴────────────┘
```

**Key rule:** Python and agents are *proposal-makers*. The backend is the *decision-maker*. All mutations flow through the backend's `FigureModel` before being broadcast.

### 1.5 Lifecycle Flow

```
STARTUP:
  1. User runs: spectra-backend (or auto-launched by Python)
  2. Backend binds Unix socket, enters poll loop

PYTHON CONNECTS:
  3. Python: Session() → connect to socket
  4. Python → Backend: HELLO (protocol_version, client_type="python")
  5. Backend → Python: WELCOME (session_id, capabilities)
  6. Backend → Python: STATE_SNAPSHOT (empty or restored session)

FIGURE CREATION:
  7. Python: fig = s.figure()
  8. Python → Backend: REQ_CREATE_FIGURE (title, size)
  9. Backend: FigureModel.create_figure() → figure_id
  10. Backend: spawn spectra-window agent
  11. Agent connects, handshakes
  12. Backend → Agent: CMD_ASSIGN_FIGURES + STATE_SNAPSHOT
  13. Agent: creates local Figure, initializes GPU, renders
  14. Backend → Python: RESP_OK (figure_id)

DATA UPDATE:
  15. Python: ax.line(x, y)
  16. Python → Backend: STATE_DIFF (SET_SERIES_DATA with blob_ref)
  17. Backend: FigureModel.apply_diff_op()
  18. Backend → Agent: STATE_DIFF (forward)
  19. Agent: applies diff, re-uploads GPU data

WINDOW CLOSE:
  20. User clicks X on agent window
  21. Agent → Backend: REQ_CLOSE_WINDOW
  22. Backend: removes agent from SessionGraph
  23. Backend: figure remains in FigureModel (Python still references it)
  24. Backend → Python: EVT_WINDOW_CLOSED (window_id)

PYTHON DISCONNECT:
  25. Python process exits or crashes
  26. Backend: detects broken socket via poll/EPOLLHUP
  27. Backend: keeps figures alive for reconnect_timeout (default 30s)
  28. If Python reconnects within timeout: resend STATE_SNAPSHOT
  29. If timeout expires: tear down session, close agent windows

SHUTDOWN:
  30. Backend receives SIGTERM, or all sessions expired + no agents
  31. Backend → all Agents: CMD_CLOSE_WINDOW
  32. Backend: waits for agent processes, unlinks socket, exits
```

### 1.6 Client Classification

The backend must distinguish between two client types on the same socket:

| Client Type | Identified By | Capabilities |
|-------------|---------------|-------------|
| **python** | `HELLO.client_type = "python"` | Create/destroy figures, set data, control animation |
| **agent** | `HELLO.client_type = "agent"` | Receive figure state, forward input events |

Both use the same wire protocol. The backend's `ClientRouter` dispatches based on client type.

---

## 2. Repository Organization

### 2.1 Target Layout

```
spectra/
├── CMakeLists.txt                  # Root build (C++ engine)
├── pyproject.toml                  # Python package build
├── include/
│   └── spectra/                    # PUBLIC C++ API headers
│       ├── spectra.hpp             #   Convenience include-all
│       ├── figure.hpp              #   Figure, FigureConfig
│       ├── axes.hpp                #   Axes (2D)
│       ├── axes3d.hpp              #   Axes3D
│       ├── series.hpp              #   LineSeries, ScatterSeries
│       ├── series3d.hpp            #   3D series types
│       ├── camera.hpp              #   Camera
│       ├── color.hpp               #   Color struct
│       ├── fwd.hpp                 #   Forward declarations
│       ├── math3d.hpp              #   Vec3, Mat4, Quat
│       ├── plot_style.hpp          #   LineStyle, MarkerType, etc.
│       ├── animator.hpp            #   AnimationController
│       ├── export.hpp              #   PNG/SVG export
│       ├── frame.hpp               #   Frame timing
│       ├── logger.hpp              #   Logging
│       └── easy.hpp                #   Easy-mode MATLAB-style API
├── src/
│   ├── core/                       # Plot model (no GPU, no UI, no IPC)
│   │   ├── axes.cpp
│   │   ├── axes3d.cpp
│   │   ├── figure.cpp
│   │   ├── series.cpp
│   │   ├── series3d.cpp
│   │   └── ...
│   ├── render/                     # Vulkan rendering (no UI, no IPC)
│   │   ├── renderer.hpp
│   │   ├── renderer.cpp
│   │   ├── backend.hpp             #   Abstract backend interface
│   │   └── vulkan/
│   │       ├── vk_backend.hpp
│   │       ├── vk_backend.cpp
│   │       ├── vk_pipeline.hpp
│   │       ├── vk_pipeline.cpp
│   │       ├── vk_swapchain.hpp
│   │       ├── vk_swapchain.cpp
│   │       └── window_context.hpp
│   ├── gpu/                        # Shaders
│   │   └── shaders/
│   │       ├── line.vert / .frag
│   │       ├── scatter.vert / .frag
│   │       ├── grid.vert / .frag
│   │       ├── surface3d.vert / .frag
│   │       ├── mesh3d.vert / .frag
│   │       └── ...
│   ├── ipc/                        # IPC protocol (no GPU, no UI)
│   │   ├── message.hpp             #   Message types, payloads
│   │   ├── codec.hpp               #   TLV encode/decode
│   │   ├── codec.cpp
│   │   ├── transport.hpp           #   Socket Server/Client/Connection
│   │   ├── transport.cpp
│   │   └── blob_store.hpp          #   (NEW) Blob reference management
│   ├── daemon/                     # Backend daemon process
│   │   ├── main.cpp                #   Entry point
│   │   ├── figure_model.hpp        #   Authoritative figure state
│   │   ├── figure_model.cpp
│   │   ├── session_graph.hpp       #   Session/agent/figure topology
│   │   ├── session_graph.cpp
│   │   ├── process_manager.hpp     #   Agent process spawning
│   │   ├── process_manager.cpp
│   │   └── client_router.hpp       #   (NEW) Python vs Agent dispatch
│   │   └── client_router.cpp
│   ├── agent/                      # Window agent process
│   │   └── main.cpp                #   Entry point (builds on ui/ stack)
│   ├── ui/                         # UI framework (ImGui, windowing, interaction)
│   │   ├── app.cpp
│   │   ├── app_inproc.cpp
│   │   ├── app_multiproc.cpp
│   │   ├── window_manager.hpp/.cpp
│   │   ├── session_runtime.hpp/.cpp
│   │   ├── window_runtime.hpp/.cpp
│   │   ├── imgui_integration.hpp/.cpp
│   │   ├── figure_manager.hpp/.cpp
│   │   ├── figure_registry.hpp/.cpp
│   │   ├── ... (all other UI modules)
│   ├── anim/                       # Animation engine
│   │   ├── animator.cpp
│   │   ├── easing.cpp
│   │   ├── frame_scheduler.hpp/.cpp
│   │   └── frame_profiler.hpp
│   ├── data/                       # Data utilities
│   │   └── ...
│   └── io/                         # File I/O (PNG, SVG, etc.)
│       └── ...
├── python/                         # (NEW) Python package
│   ├── spectra/
│   │   ├── __init__.py             #   Package init + convenience API
│   │   ├── _session.py             #   Session class (IPC connection)
│   │   ├── _figure.py              #   Figure proxy
│   │   ├── _axes.py                #   Axes proxy
│   │   ├── _series.py              #   Series proxy (Line, Scatter, etc.)
│   │   ├── _transport.py           #   Socket I/O + message framing
│   │   ├── _codec.py               #   TLV encode/decode (mirrors C++)
│   │   ├── _protocol.py            #   Message types + constants
│   │   ├── _blob.py                #   NumPy array → blob encoding
│   │   ├── _animation.py           #   Animation helpers
│   │   ├── _launcher.py            #   Auto-launch spectra-backend
│   │   └── _errors.py              #   Exception hierarchy
│   └── tests/
│       ├── test_session.py
│       ├── test_figure.py
│       ├── test_codec.py
│       ├── test_transport.py
│       └── test_integration.py
├── tests/                          # C++ tests
│   ├── unit/
│   ├── bench/
│   ├── golden/
│   └── util/
├── examples/
│   ├── *.cpp                       # C++ examples
│   └── python/                     # (NEW) Python examples
│       ├── basic_line.py
│       ├── multi_window.py
│       ├── streaming_update.py
│       ├── animation.py
│       └── large_dataset.py
├── tools/
├── docs/
├── third_party/
├── cmake/
├── icons/
└── plans/
```

### 2.2 Dependency Rules

```
                    ┌──────────┐
                    │  python/  │ ◄── Pure Python. No C/C++ deps.
                    │  spectra  │     Depends ONLY on: socket, struct, numpy
                    └─────┬────┘
                          │ (IPC protocol only — no library linking)
                          ▼
                    ┌──────────┐
                    │   ipc/   │ ◄── C++ IPC layer. No GPU, no UI.
                    │          │     Depends on: <system sockets>, core types
                    └─────┬────┘
                          │
              ┌───────────┼───────────┐
              ▼           ▼           ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ daemon/  │ │ agent/   │ │  core/   │ ◄── Plot model. No GPU.
        │          │ │          │ │          │     Depends on: nothing
        └──────────┘ └────┬─────┘ └──────────┘
                          │
                    ┌─────┴────┐
                    │ render/  │ ◄── Vulkan rendering.
                    │          │     Depends on: core, Vulkan SDK
                    └─────┬────┘
                          │
                    ┌─────┴────┐
                    │   ui/    │ ◄── UI framework.
                    │          │     Depends on: core, render, anim, GLFW, ImGui
                    └──────────┘
```

**Strict rules:**

| Module | MUST NOT depend on |
|--------|--------------------|
| `core/` | `render/`, `ui/`, `ipc/`, `daemon/`, `agent/`, Vulkan, GLFW, ImGui |
| `render/` | `ui/`, `ipc/`, `daemon/`, `agent/`, GLFW, ImGui |
| `ipc/` | `render/`, `ui/`, `daemon/`, `agent/`, Vulkan, GLFW, ImGui |
| `daemon/` | `render/`, `ui/`, Vulkan, GLFW, ImGui |
| `python/` | Any C++ code, Vulkan, GLFW, ImGui |
| `anim/` | `render/`, `ui/`, `ipc/`, Vulkan, GLFW, ImGui |

### 2.3 Public vs Internal Headers

| Location | Visibility | Consumer |
|----------|------------|----------|
| `include/spectra/*.hpp` | **Public** | C++ users embedding Spectra as a library |
| `src/*/**.hpp` | **Internal** | Only other `src/` modules within allowed dependencies |
| `python/spectra/_*.py` | **Internal** | Only the Python package (underscore-prefixed = private) |
| `python/spectra/__init__.py` | **Public** | Python users (`import spectra`) |

### 2.4 Preventing Architectural Leakage

1. **CMake target isolation** — `spectra-core` (core/), `spectra-render` (render/), `spectra-ipc` (ipc/), `spectra-ui` (ui/) as separate static libraries with explicit `target_link_libraries`. Currently everything is one `spectra` static lib — this should be split in Phase 2.

2. **Include path discipline** — `core/` sources only get `include/spectra/` in their include path. No access to `src/render/`, `src/ui/`, etc.

3. **CI enforcement** — Add a CI step that scans `#include` directives and fails if a forbidden dependency is detected (e.g., `core/*.cpp` includes anything from `render/`).

4. **Python purity** — `python/spectra/` must never import ctypes for Vulkan, never link to the C++ library. It talks to the backend exclusively via socket IPC.

---

## 3. IPC Protocol Extension for Python

### 3.1 Current State

The existing protocol (documented in `src/ipc/message.hpp`) already supports:
- HELLO/WELCOME handshake
- STATE_SNAPSHOT / STATE_DIFF / ACK_STATE
- CMD_ASSIGN_FIGURES / CMD_REMOVE_FIGURE / CMD_SET_ACTIVE / CMD_CLOSE_WINDOW
- REQ_CREATE_WINDOW / REQ_CLOSE_WINDOW / REQ_DETACH_FIGURE / REQ_MOVE_FIGURE
- EVT_INPUT / EVT_WINDOW / EVT_TAB_DRAG / EVT_HEARTBEAT

Wire format: `[40-byte header][TLV payload]`, max 16 MiB payload.

### 3.2 New Message Types for Python

```
// Client classification (in HELLO)
HELLO.client_type = "python" | "agent"

// Python → Backend: figure/series lifecycle
REQ_CREATE_FIGURE    = 0x0500   // Create a new figure
REQ_DESTROY_FIGURE   = 0x0501   // Destroy a figure
REQ_CREATE_AXES      = 0x0502   // Add axes to a figure (subplot)
REQ_ADD_SERIES       = 0x0503   // Add a series to axes
REQ_REMOVE_SERIES    = 0x0504   // Remove a series
REQ_SET_DATA         = 0x0505   // Set series data (bulk transfer)
REQ_UPDATE_PROPERTY  = 0x0506   // Set any figure/axes/series property
REQ_SHOW             = 0x0507   // Request window for figure (spawn agent)
REQ_CLOSE_FIGURE     = 0x0508   // Close figure's window (keep figure)

// Python → Backend: queries
REQ_GET_SNAPSHOT     = 0x0510   // Request full state snapshot
REQ_LIST_FIGURES     = 0x0511   // List all figure IDs in session

// Python → Backend: animation
REQ_ANIM_START       = 0x0520   // Start animation loop
REQ_ANIM_STOP        = 0x0521   // Stop animation loop
REQ_ANIM_FRAME       = 0x0522   // Submit one animation frame

// Python → Backend: session
REQ_RECONNECT        = 0x0530   // Reconnect to existing session
REQ_DISCONNECT       = 0x0531   // Graceful disconnect (keep session alive)

// Backend → Python: responses
RESP_FIGURE_CREATED  = 0x0540   // figure_id
RESP_AXES_CREATED    = 0x0541   // axes_index
RESP_SERIES_ADDED    = 0x0542   // series_index
RESP_SNAPSHOT        = 0x0543   // StateSnapshotPayload
RESP_FIGURE_LIST     = 0x0544   // list of figure_ids

// Backend → Python: events
EVT_WINDOW_CLOSED    = 0x0550   // A window was closed by user
EVT_KNOB_CHANGED     = 0x0551   // User changed an interactive knob
EVT_FIGURE_DESTROYED = 0x0552   // Figure was destroyed (e.g. timeout)
```

### 3.3 Bulk Data Transfer (Blob Protocol)

For large arrays (>64 KB), data is sent as a **blob reference** to avoid copying through the TLV layer.

```
Wire format for REQ_SET_DATA:

  [Header: 40 bytes]
  [TLV fields: figure_id, series_index, dtype, shape]
  [TAG_BLOB_INLINE: 0xB0] [length: uint32] [raw bytes...]

For V1: blob is always inline (appended to the TLV message).
For V2: blob may reference shared memory (TAG_BLOB_SHM: 0xB1, shm_name, offset, length).
```

**NumPy array encoding:**

```
TAG_DTYPE       = 0xB2   // uint8: 0=float32, 1=float64, 2=int32, 3=int64
TAG_SHAPE       = 0xB3   // uint32[]: array dimensions
TAG_BLOB_INLINE = 0xB0   // raw bytes (row-major, native endian)
TAG_BLOB_SHM    = 0xB1   // string: shm name + uint64 offset + uint64 length
```

Python side: `numpy.ndarray.tobytes()` for inline, `multiprocessing.shared_memory` for shm.

### 3.4 MAX_PAYLOAD_SIZE Increase

Current limit is 16 MiB. For 1M float32 points (x,y) = 8 MB — fits. For 10M points = 80 MB — doesn't fit.

**Strategy:**
- V1: Increase `MAX_PAYLOAD_SIZE` to 256 MiB. Simple and sufficient for most workloads.
- V1: For arrays > 256 MiB, Python client auto-chunks into multiple REQ_SET_DATA messages with `chunk_index` / `chunk_count` fields.
- V2: Shared memory path (zero-copy) eliminates the limit entirely.

### 3.5 Versioning Strategy

```
Protocol version: MAJOR.MINOR (currently 1.0)

MAJOR bump: Breaking wire format change (header layout, TLV encoding).
            All clients must match major version.
MINOR bump: New message types, new TLV tags.
            Old clients ignore unknown tags/types.

Negotiation: HELLO includes (major, minor).
             Backend rejects if major != supported.
             Backend records client's minor for feature gating.
```

**Backward compatibility rule:** New TLV tags are always optional. Decoders skip unknown tags. New message types get RESP_ERR(UNKNOWN_TYPE) if the receiver doesn't understand them.

### 3.6 Session Authentication (V1)

Local-only, minimal:

```
HELLO payload includes:
  TAG_AUTH_TOKEN = 0xC0   // string: token from file or env var

Backend writes a random 32-byte hex token to:
  $XDG_RUNTIME_DIR/spectra-<session_id>.token

Python reads this file to authenticate. Prevents other local users
from connecting to your backend on a shared machine.

V1: file-based token, verified on HELLO.
V2: consider SO_PEERCRED (Linux) for UID verification without tokens.
```

---

## 4. Python API Design (V1)

### 4.1 Core API Surface

```python
import spectra as sp

# --- Session lifecycle ---
s = sp.Session()                     # Connect (auto-launch backend if needed)
s = sp.Session(socket="/path/to.sock")  # Explicit socket
s.close()                            # Graceful disconnect

# --- Figure creation ---
fig = s.figure()                     # New figure → new window
fig = s.figure(title="My Plot", width=1280, height=720)

# --- Subplot ---
ax = fig.subplot(1, 1, 1)           # MATLAB-style grid
ax = fig.subplot(2, 2, 3)           # 2x2 grid, position 3

# --- Series ---
line = ax.line(x, y)                 # x, y are numpy arrays or lists
scat = ax.scatter(x, y)
surf = ax.surface(X, Y, Z)          # 3D surface
mesh = ax.mesh(vertices, indices)    # 3D mesh

# --- Property mutation ---
line.color(1.0, 0.0, 0.0)           # Red
line.width(2.5)
line.label("Sine wave")
ax.xlim(-10, 10)
ax.ylim(-1, 1)
ax.title("My Axes")
ax.grid(True)

# --- Data update ---
line.set_data(new_x, new_y)          # Replace data
line.append(new_x, new_y)           # Append (streaming)

# --- Display ---
fig.show()                           # Show window (non-blocking)
sp.show()                           # Show all figures (blocking until all closed)

# --- Convenience (module-level, auto-session) ---
sp.figure()
sp.subplot(1,1,1)
sp.line(x, y)
sp.show()
```

### 4.2 Class Hierarchy

```
Session
  ├── figure() → Figure
  ├── figures → List[Figure]
  ├── close()
  └── show()           # Block until all windows closed

Figure
  ├── subplot(r,c,i) → Axes
  ├── axes → List[Axes]
  ├── show()           # Spawn window for this figure
  ├── close()          # Close window (figure stays in session)
  ├── title(str)
  ├── size(w, h)
  └── savefig(path)    # Export PNG/SVG

Axes
  ├── line(x,y) → LineSeries
  ├── scatter(x,y) → ScatterSeries
  ├── surface(X,Y,Z) → SurfaceSeries
  ├── mesh(v,i) → MeshSeries
  ├── xlim(min, max)
  ├── ylim(min, max)
  ├── zlim(min, max)   # 3D only
  ├── title(str)
  ├── xlabel(str) / ylabel(str) / zlabel(str)
  ├── grid(bool)
  ├── legend(bool)
  └── clear()

LineSeries / ScatterSeries / SurfaceSeries / MeshSeries
  ├── set_data(...)
  ├── append(...)
  ├── color(r,g,b[,a])
  ├── width(float)     # Line only
  ├── size(float)      # Scatter only
  ├── opacity(float)
  ├── visible(bool)
  └── label(str)
```

### 4.3 Proxy Architecture

Every Python object is a **lightweight proxy** holding only IDs:

```python
class Figure:
    def __init__(self, session, figure_id):
        self._session = session      # Session reference
        self._id = figure_id         # uint64 from backend

    def subplot(self, rows, cols, index):
        axes_index = self._session._request_create_axes(
            self._id, rows, cols, index
        )
        return Axes(self._session, self._id, axes_index)
```

No figure/axes/series state is cached in Python. All queries go through the backend. This ensures Python never has stale state.

### 4.4 Blocking vs Async

| Method | Behavior |
|--------|----------|
| `figure()` | **Blocking** — waits for RESP_FIGURE_CREATED (fast, <10ms) |
| `line(x,y)` | **Blocking** — waits for RESP_SERIES_ADDED (fast) |
| `set_data(x,y)` | **Blocking for ack** — waits for RESP_OK (data transfer time depends on size) |
| `show()` on Figure | **Non-blocking** — sends REQ_SHOW, returns immediately |
| `sp.show()` module-level | **Blocking** — blocks until all windows are closed |
| `savefig(path)` | **Blocking** — waits for export to complete |

Future V2: `async` variants via `asyncio` for non-blocking data pushes.

### 4.5 Streaming Update Pattern

```python
fig = s.figure()
ax = fig.subplot(1,1,1)
line = ax.line([], [])
fig.show()

# Streaming loop
for chunk in data_source:
    line.append(chunk.x, chunk.y)
    ax.xlim(chunk.x[0], chunk.x[-1])  # Auto-scroll
```

`append()` sends incremental data. Backend appends to existing series data and broadcasts diff to agent.

### 4.6 Animation API

```python
fig = s.figure()
ax = fig.subplot(1,1,1)
line = ax.line(x, y)
fig.show()

# Option A: Python-driven animation (simple, higher latency)
for t in range(1000):
    y_new = np.sin(x + t * 0.1)
    line.set_data(x, y_new)
    sp.sleep(1/60)                   # Spectra-aware sleep (drains events)

# Option B: Backend-driven animation (low latency, callback)
def update(frame_num, dt):
    y_new = np.sin(x + frame_num * 0.1)
    line.set_data(x, y_new)

anim = fig.animate(update, fps=60, duration=10.0)
anim.start()                         # Backend drives the clock
anim.wait()                          # Block until done
```

### 4.7 Error Handling

```python
class SpectraError(Exception): pass
class ConnectionError(SpectraError): pass
class ProtocolError(SpectraError): pass
class TimeoutError(SpectraError): pass
class FigureNotFoundError(SpectraError): pass

# Errors are raised synchronously on the calling thread:
try:
    line.set_data(x, y)
except sp.ConnectionError:
    # Backend disconnected
    s.reconnect()
    line.set_data(x, y)
```

---

## 5. Data Transfer Strategy

### 5.1 V1: Inline Blob Transfer

```
Python                    Backend                   Agent
  │                          │                          │
  │ REQ_SET_DATA             │                          │
  │ [header][TLV: fig,ser,   │                          │
  │  dtype,shape]            │                          │
  │ [TAG_BLOB_INLINE][8MB]   │                          │
  │─────────────────────────▶│                          │
  │                          │ apply to FigureModel     │
  │                          │ (memcpy into model)      │
  │                          │                          │
  │                          │ STATE_DIFF               │
  │                          │ [SET_SERIES_DATA][8MB]   │
  │                          │─────────────────────────▶│
  │                          │                          │ upload to GPU
  │ RESP_OK                  │                          │
  │◀─────────────────────────│                          │
```

**Performance for 1M float32 points (x,y interleaved = 8 MB):**
- Python `tobytes()`: ~2ms
- Socket write 8MB: ~1ms (localhost, kernel buffering)
- Backend memcpy: ~1ms
- Socket write to agent: ~1ms
- Agent GPU upload: ~2ms
- **Total: ~7ms** — well within interactive frame budget

**Performance for 10M points (80 MB):**
- Socket write: ~10ms
- Backend memcpy: ~5ms
- Forward to agent: ~10ms
- **Total: ~35ms** — acceptable for batch updates

### 5.2 V2: POSIX Shared Memory (Optimization)

```
Python                    Backend                   Agent
  │                          │                          │
  │ shm_open("spectra-blob-42")                        │
  │ memcpy(shm_ptr, data)    │                          │
  │                          │                          │
  │ REQ_SET_DATA             │                          │
  │ [TAG_BLOB_SHM:           │                          │
  │  "spectra-blob-42",      │                          │
  │  offset=0, len=80MB]     │                          │
  │─────────────────────────▶│                          │
  │                          │ shm_open (read-only)     │
  │                          │ map into FigureModel     │
  │                          │                          │
  │                          │ STATE_DIFF               │
  │                          │ [TAG_BLOB_SHM: same ref] │
  │                          │─────────────────────────▶│
  │                          │                          │ shm_open
  │                          │                          │ GPU upload from shm
  │                          │                          │
  │ RESP_OK + BLOB_RELEASE   │                          │
  │◀─────────────────────────│                          │
  │ shm_unlink               │                          │
```

**Advantages:** Zero-copy path. Python writes once, agent reads directly. Backend only stores a reference.

**Memory lifetime rules:**
- Python creates shm, writes data
- Backend and agent open shm read-only
- Backend sends `BLOB_RELEASE` to Python when all agents have ACK'd
- Python calls `shm_unlink()` after receiving BLOB_RELEASE
- If Python crashes before unlink: backend unlinks after timeout

### 5.3 Memory Lifetime & Leak Prevention

| Owner | Resource | Freed when |
|-------|----------|------------|
| Python | NumPy array | Python GC / out of scope |
| Python | shm segment (V2) | After BLOB_RELEASE received |
| Backend | FigureModel data copy | Figure removed or data replaced |
| Agent | GPU buffer | Series removed or data re-uploaded |

**Leak prevention:**
- Backend tracks all active blob references per session
- On session teardown: unlink any orphaned shm segments
- Blob references have TTL (60s) — if not ACK'd, backend unlinks
- Agent never creates blobs — only reads them

### 5.4 Chunked Transfer (Large Arrays)

For arrays exceeding `MAX_PAYLOAD_SIZE`:

```python
# Python client auto-chunks:
CHUNK_SIZE = 128 * 1024 * 1024  # 128 MB

if data.nbytes > CHUNK_SIZE:
    for i, chunk in enumerate(split_array(data, CHUNK_SIZE)):
        session._send(REQ_SET_DATA,
            figure_id=fig_id,
            series_index=ser_idx,
            chunk_index=i,
            chunk_count=num_chunks,
            data=chunk)
```

Backend reassembles chunks before applying to FigureModel.

---

## 6. Animation Strategy

### 6.1 Two Animation Modes

#### Mode A: Python-Driven (V1 default)

```
Python loop:
  while animating:
    compute new data
    line.set_data(x, y_new)   → IPC → Backend → Agent
    sp.sleep(1/fps)            (drains IPC events during sleep)
```

- **Clock:** Python's wall clock
- **Pros:** Simple. Python controls everything. Works with any compute.
- **Cons:** IPC round-trip adds latency. Python GC can cause jitter.
- **Mitigation:** `sp.sleep()` is IPC-aware — it processes incoming events (EVT_KNOB_CHANGED, EVT_WINDOW_CLOSED) during the wait, preventing socket buffer backup.

#### Mode B: Backend-Driven (V2)

```
Python registers callback:
  fig.animate(update_fn, fps=60)

Backend runs fixed-timestep loop:
  every 1/fps:
    send ANIM_TICK(frame_num, t) to Python
    Python: compute new data, send SET_DATA
    Backend: wait for SET_DATA, broadcast to agents
```

- **Clock:** Backend's monotonic clock (steady_clock)
- **Pros:** Deterministic frame pacing. Backend can skip frames if Python is slow.
- **Cons:** More complex protocol. Python must respond within frame budget.

### 6.2 Frame Pacing

Backend animation tick loop:

```
target_dt = 1.0 / fps
accumulator = 0.0

while animating:
    frame_start = now()
    
    // Send tick to Python
    send(ANIM_TICK, frame_num, t)
    
    // Wait for Python's SET_DATA response (with timeout)
    if !recv_within(target_dt * 0.8):
        skip frame (reuse previous data)
    
    // Broadcast to agents
    broadcast(STATE_DIFF)
    
    // Sleep remainder
    elapsed = now() - frame_start
    if elapsed < target_dt:
        sleep(target_dt - elapsed)
    
    accumulator += target_dt
    frame_num++
```

### 6.3 Minimizing IPC Overhead

1. **Batch mutations** — Python can send multiple property changes in one message (REQ_UPDATE_BATCH with multiple DiffOps). Backend applies all and broadcasts one STATE_DIFF.

2. **Delta compression** — For streaming updates where only Y changes, send only Y values (not X+Y). Add `TAG_DATA_CHANNEL` flag: 0=XY interleaved, 1=Y only, 2=X only.

3. **Fire-and-forget mode** — For high-frequency animation, Python can set `ack_mode=false` on SET_DATA. Backend doesn't send RESP_OK, reducing round-trip to one-way latency.

4. **Agent-side interpolation** — Future: agent interpolates between the last two received frames to smooth over Python jitter.

### 6.4 Stutter Prevention

| Cause | Mitigation |
|-------|------------|
| Python GC pause | Backend skips frame, reuses previous data |
| Socket backpressure | Non-blocking send with outbox queue; drop oldest if full |
| Agent GPU stall | Agent decouples IPC recv from render via double-buffer |
| Backend overload | Backend coalesces rapid SET_DATA into one diff |

---

## 7. Failure Modes & Recovery

### 7.1 Python Crash

```
Detection: Backend's poll() returns POLLHUP/POLLERR on Python's fd.
           Or: recv() returns 0 (clean close) or -1 (error).

Recovery:
  1. Backend marks session as "disconnected" (not destroyed)
  2. Figures remain in FigureModel
  3. Agent windows stay open (user can still interact)
  4. Timer starts: reconnect_timeout = 30s (configurable)
  5. If Python reconnects within timeout:
     - HELLO with REQ_RECONNECT + session_token
     - Backend sends STATE_SNAPSHOT (full resync)
     - Python rebuilds proxy objects from snapshot
  6. If timeout expires:
     - Backend destroys session
     - Backend sends CMD_CLOSE_WINDOW to all session's agents
     - Figures removed from FigureModel
```

### 7.2 Backend Crash

```
Detection: Python's recv() returns error or connection refused.
           Agent's recv() returns error.

Recovery:
  Python:
    - ConnectionError raised on next API call
    - User can restart backend and call session.reconnect()
    - If auto-launch enabled: Python restarts backend automatically
    - Previous session state is LOST (V1). V2: backend persists to disk.

  Agent:
    - Agent detects broken connection
    - Agent continues rendering last known state (graceful degradation)
    - Agent periodically retries connection (backoff: 1s, 2s, 4s, 8s, max 30s)
    - If backend comes back: agent sends HELLO, receives STATE_SNAPSHOT
```

### 7.3 Agent Crash

```
Detection: Backend's poll() returns POLLHUP on agent's fd.
           Or: ProcessManager.reap_finished() finds dead PID.

Recovery:
  1. Backend removes agent from SessionGraph
  2. Figures assigned to that agent are marked "unwindowed"
  3. Backend does NOT auto-respawn (user closed window = intentional)
  4. If Python calls fig.show() again: backend spawns new agent
  5. Backend sends EVT_WINDOW_CLOSED to Python client
```

### 7.4 IPC Disconnect (Network Glitch)

```
Symptom: send() returns EPIPE or EAGAIN persists.

Recovery:
  1. Connection object detects failure
  2. Client (Python or agent) enters reconnect loop
  3. On reconnect: re-HELLO, receive STATE_SNAPSHOT
  4. Python re-issues any in-flight requests that didn't get RESP_OK
```

### 7.5 Window Close by User

```
Flow:
  1. User clicks X on agent window
  2. Agent → Backend: REQ_CLOSE_WINDOW(window_id, reason="user_close")
  3. Backend: removes agent from SessionGraph
  4. Backend: figure stays in FigureModel
  5. Backend → Python: EVT_WINDOW_CLOSED(window_id, figure_id)
  6. Agent: destroys Vulkan resources, exits process
  7. Python: fig.is_visible() now returns False
  8. Python: fig.show() would spawn a new agent
```

### 7.6 Snapshot Resync Protocol

Used on reconnect (Python or agent):

```
Client → Backend: HELLO + REQ_RECONNECT(session_token)
Backend → Client: WELCOME(session_id) + STATE_SNAPSHOT(full)
Client: replaces all local state from snapshot
Client → Backend: ACK_STATE(revision)
Backend: resumes sending STATE_DIFF from this revision
```

### 7.7 Backend Shutdown Rule

Backend exits when ALL of:
- Zero active sessions (all Python clients disconnected + timeout expired)
- Zero connected agents
- No pending reconnect timers
- OR: received SIGTERM/SIGINT

---

## 8. Versioning & Packaging

### 8.1 Version Numbers

| Component | Version | Scheme | Current |
|-----------|---------|--------|---------|
| Engine (spectra C++) | `SPECTRA_VERSION` | SemVer `MAJOR.MINOR.PATCH` | 0.1.0 |
| IPC Protocol | `PROTOCOL_MAJOR.MINOR` | Integer pair | 1.0 |
| Python client | `spectra.__version__` | SemVer, tracks engine | 0.1.0 |

### 8.2 Compatibility Matrix

```
                    Backend Protocol 1.x    Backend Protocol 2.x
Python Protocol 1.x      ✅ Compatible         ❌ Rejected at HELLO
Python Protocol 2.x      ❌ Rejected            ✅ Compatible
Agent Protocol 1.x       ✅ Compatible          ❌ Rejected
```

**Rule:** MAJOR must match exactly. MINOR is forward-compatible (old client, new server OK; new client, old server may lack features but won't crash).

### 8.3 Python Package (`pip install spectra-plot`)

```
pyproject.toml:
  [project]
  name = "spectra-plot"
  version = "0.1.0"
  requires-python = ">=3.9"
  dependencies = ["numpy>=1.20"]
  
  [project.optional-dependencies]
  dev = ["pytest", "pytest-asyncio"]
  
  [project.scripts]
  spectra-launch = "spectra._launcher:main"
```

**Package contents:** Pure Python. No compiled extensions. No Vulkan dependency.

**Backend distribution:** Separate binary package or system install. Not bundled with pip package (V1). V2 consideration: include pre-built binaries via platform wheels.

### 8.4 Backend Auto-Launch

```python
# In Session.__init__:
def __init__(self, socket=None, auto_launch=True):
    if socket is None:
        socket = self._find_socket()
    
    if not self._can_connect(socket) and auto_launch:
        self._launch_backend(socket)
        self._wait_for_backend(socket, timeout=5.0)
    
    self._connect(socket)
```

**Launch strategy:**
1. Check if `spectra-backend` is already running (try connect)
2. If not: `subprocess.Popen(["spectra-backend", "--socket", path])` 
3. Wait up to 5s for socket to appear
4. Connect

**Socket location:**
```
$SPECTRA_SOCKET                         # Env var override
$XDG_RUNTIME_DIR/spectra/spectra.sock   # Linux standard
/tmp/spectra-$USER/spectra.sock         # Fallback
```

### 8.5 Socket Discovery

Multiple Python scripts should share one backend:

```
1. First Python script: no socket exists → auto-launch backend
2. Second Python script: socket exists → connect to running backend
3. Backend assigns each script a unique SessionId
4. Each session is independent (separate figures, separate agents)
```

Socket path includes username but NOT PID — all scripts from the same user share one backend.

---

## 9. Phased Implementation Roadmap

### Phase 1: Minimal Python → Single Window (4 weeks)

**Goal:** `import spectra; sp.line(x,y); sp.show()` works.

| Week | Task | Modules | Deliverable |
|------|------|---------|-------------|
| 1 | ✅ Python transport layer | `python/spectra/_transport.py`, `_codec.py`, `_protocol.py` | Python can connect to backend, send HELLO, receive WELCOME |
| 1 | ✅ Backend client classification | `src/daemon/client_router.hpp/.cpp`, `main.cpp` | Backend distinguishes Python vs agent clients |
| 2 | ✅ Python API skeleton | `python/spectra/_session.py`, `_figure.py`, `_axes.py`, `_series.py` | Session/Figure/Axes/Series proxy classes |
| 2 | ✅ REQ_CREATE_FIGURE/AXES/SERIES | `src/ipc/message.hpp`, `codec.hpp/.cpp`, `daemon/main.cpp` | Backend handles Python figure creation |
| 3 | ✅ REQ_SET_DATA (inline blob) | `python/spectra/_codec.py`, `src/ipc/codec.cpp` | NumPy arrays transfer to backend and agents |
| 3 | ✅ REQ_SHOW (spawn agent) | `daemon/main.cpp` | Python's `fig.show()` spawns a window |
| 4 | ✅ Module-level convenience | `python/spectra/__init__.py` | `sp.line(x,y); sp.show()` works end-to-end |
| 4 | ✅ Auto-launch backend | `python/spectra/_launcher.py` | Backend starts automatically |

**Risks:**
- TLV codec mismatch between Python and C++ implementations
- Agent handshake race condition (Python creates figure before agent connects)

**Verification:**
- `python examples/basic_line.py` shows a window with a line plot
- `pytest python/tests/` — all transport + codec tests pass
- Kill Python → backend + agent stay alive for 30s
- Reconnect Python → state restored

### Phase 2: Multi-Window + Blob Transfer + Streaming (4 weeks)

**Goal:** Multiple windows, large datasets, streaming updates.

| Week | Task | Modules | Deliverable |
|------|------|---------|-------------|
| 5 | ✅ Multi-figure support | `_figure.py`, `daemon/main.cpp` | Multiple `fig.show()` calls spawn multiple agents |
| 5 | ✅ Window close handling | `daemon/main.cpp`, `_session.py` | EVT_WINDOW_CLOSED reaches Python |
| 6 | ✅ Inline blob optimization | `_codec.py`, `codec.cpp` | Raw numpy zero-copy path via `encode_req_set_data_raw` |
| 6 | ✅ Chunked transfer | `_codec.py`, `_series.py` | Arrays > 256MB work via auto-chunking (`encode_req_set_data_chunked`, `Series._send_chunked`) |
| 7 | ✅ Streaming API | `_series.py` | `line.append(x,y)` for live data (REQ_APPEND_DATA) |
| 7 | ✅ Property mutation | `_axes.py`, `_series.py` | Full xlim/ylim/color/width/opacity/label/xlabel/ylabel/title/grid API |
| 8 | ✅ CMake lib split | `CMakeLists.txt` | Separate spectra-core, spectra-ipc, spectra-render INTERFACE targets |
| 8 | ✅ Python test suite | `python/tests/` | 290 tests across test_codec, test_cross_codec, test_phase2, test_phase3, test_phase4, test_phase5 |

**Risks:**
- Socket buffer overflow with rapid streaming
- Figure assignment race when multiple windows spawn simultaneously

**Verification:**
- `python examples/multi_window.py` — 3 windows, close any one, others survive
- `python examples/large_dataset.py` — 1M points render in < 1s total
- `python examples/streaming_update.py` — 60fps streaming for 60s without OOM

### Phase 3: Animation + Shared Memory + Full Recovery (4 weeks)

**Goal:** Smooth animation, zero-copy for large data, robust failure recovery.

| Week | Task | Modules | Deliverable |
|------|------|---------|-------------|
| 9 | ✅ Python-driven animation | `_animation.py`, `_session.py` | `ipc_sleep()` + `FramePacer` for animation loops |
| 9 | ✅ Batch mutations | `_session.py`, `_axes.py`, `_codec.py`, `daemon/main.cpp` | REQ_UPDATE_BATCH: multiple property changes in one IPC message + Axes.batch() context manager |
| 10 | ✅ POSIX shared memory | `_blob.py`, `ipc/blob_store.hpp` | BlobStore + BlobRef with shm create/release/cleanup, C++ BlobStore with register/ack/TTL |
| 10 | ✅ Blob lifecycle | `ipc/blob_store.hpp`, `_blob.py`, `_session.py` | BLOB_RELEASE protocol (0x0570), TTL-based leak prevention, session cleanup |
| 11 | ✅ Backend-driven animation | `_animation.py`, `_codec.py`, `_protocol.py`, `_session.py` | BackendAnimator class, REQ_ANIM_START/STOP, ANIM_TICK dispatch |
| 11 | ✅ Full reconnect | `_session.py`, `_codec.py`, `daemon/main.cpp` | REQ_RECONNECT handler + Session.reconnect() → full state snapshot restored |
| 12 | ✅ Session persistence | `_persistence.py` | save_session/load_session_metadata/restore_session to JSON |
| 12 | ✅ Polish + docs | `docs/getting_started.md` | Python API section: quick start, streaming, animation, batch, persistence |

**Risks:**
- Shared memory cleanup on crash (shm_unlink not called)
- Backend-driven animation requires Python to respond within frame budget
- Cross-platform shm differences (Linux vs macOS)

**Verification:**
- `python examples/animation.py` — 60fps for 5 minutes, no stutter
- `python examples/large_dataset.py` with shm — 10M points, zero-copy confirmed via `/dev/shm/`
- Kill -9 Python during animation → backend stays alive → reconnect works
- Kill -9 agent during animation → backend logs, Python gets EVT_WINDOW_CLOSED

---

## 10. Risk Register

| ID | Risk | Impact | Likelihood | Mitigation |
|----|------|--------|------------|------------|
| R1 | Python↔C++ TLV codec divergence | Data corruption, silent bugs | High | Shared test vectors; golden byte-sequence tests in both languages |
| R2 | Socket buffer overflow during streaming | Lost messages, backpressure stall | Medium | Non-blocking send + bounded outbox; backpressure signal to Python |
| R3 | Agent spawn race (Python sends data before agent connects) | Agent misses initial state | High | Backend queues figure state; agent gets STATE_SNAPSHOT on handshake |
| R4 | Shared memory leak on crash | `/dev/shm` fills up | Medium | Backend tracks all shm segments; cleanup on session teardown + TTL |
| R5 | Backend single-threaded bottleneck | Latency spike with many clients | Low (V1) | V1 is poll-based single-thread; V2 can add thread pool for blob processing |
| R6 | Cross-platform socket differences | macOS/Windows incompatibility | Medium | V1 Linux-only; V2 abstract transport (TCP fallback for Windows) |
| R7 | NumPy array endianness | Wrong data on mixed-endian systems | Low | V1 assumes native endian (all local); V2 add endian tag |
| R8 | Python GIL blocking during IPC | UI freeze in Jupyter notebooks | Medium | All IPC in background thread; GIL released during socket I/O |
| R9 | Multiple backends from same user | Socket conflict, state confusion | Medium | PID file / flock on socket; refuse to start if already running |
| R10 | Agent binary not found | `fig.show()` fails silently | High | Clear error message; `spectra-launch --check` verifies installation |
| R11 | Protocol version mismatch after upgrade | Python can't talk to old backend | Medium | Version negotiation at HELLO; clear error message with upgrade instructions |
| R12 | FigureModel memory growth | Backend OOM with many large figures | Medium | Per-session memory limits; warn at 1GB; hard limit at 4GB |

---

## 11. Acceptance Checklist

### Functional

- [x] `import spectra as sp; sp.line(x,y); sp.show()` opens a window and displays a plot
- [x] `s.figure()` called 3 times creates 3 independent windows
- [x] Closing the first window does NOT close or affect the other two
- [ ] `line.set_data(x, y)` with 1,000,000 float32 points renders smoothly (< 200ms total latency)
- [ ] Animation loop at 60fps runs for 5 minutes without stutter (p95 frame time < 20ms)
- [ ] Killing Python process with Ctrl+C: backend + all agent windows stay alive for 30s
- [x] Reconnecting Python within 30s: full state restored, existing windows continue
- [ ] Backend receives SIGTERM: all agent windows close gracefully within 2s
- [x] No "primary window" or "first window" special-case logic exists anywhere in the codebase
- [x] `fig.show()` called after window was closed by user spawns a new window for the same figure

### Protocol

- [x] Python and C++ TLV codecs produce identical byte sequences for all message types
- [x] Unknown TLV tags are silently skipped (forward compatibility)
- [ ] Protocol version mismatch produces a clear error message on both sides
- [x] All IPC messages include `session_id` and `request_id` for correlation

### Performance

- [ ] 1M points: Python → visible in window < 500ms
- [ ] 10M points: Python → visible in window < 3s
- [ ] Streaming append 10K points/update at 60fps: no memory growth over 5 minutes
- [ ] Backend idle CPU usage < 1% when no animation is running
- [ ] Agent idle CPU usage < 2% when no interaction is happening

### Reliability

- [ ] No Vulkan validation errors in any test scenario
- [ ] No memory leaks (agent or backend) over 10-minute stress test
- [ ] No zombie processes after any crash scenario
- [ ] shm segments are cleaned up within 60s of owning process crash (V2)
- [ ] Backend survives rapid connect/disconnect cycles (100 connections in 10s)

### Architecture

- [x] `python/spectra/` contains zero C/C++ imports (no ctypes, no cffi, no pybind11)
- [x] `src/core/` contains zero `#include` of render, ui, or ipc headers
- [x] `src/ipc/` contains zero `#include` of Vulkan, GLFW, or ImGui headers
- [x] `grep -r "primary_window\|main_window\|first_window" src/` returns zero results
- [x] Every IPC message type has encode/decode tests in both C++ and Python

---

## Appendix A: Message Type Summary (Post-Extension)

```
Range       Purpose                 Direction
0x0001-0x00FF  Handshake            Bidirectional
0x0010-0x001F  Responses            Backend → Any
0x0100-0x01FF  Agent control        Agent → Backend
0x0200-0x02FF  Agent commands       Backend → Agent
0x0300-0x03FF  State sync           Backend → Any
0x0400-0x04FF  Events (input)       Agent → Backend
0x0500-0x05FF  Python requests      Python → Backend
0x0540-0x05FF  Python responses     Backend → Python
0x0550-0x05FF  Python events        Backend → Python
0xB0-0xBF      Blob tags            (within TLV payloads)
0xC0-0xCF      Auth tags            (within TLV payloads)
```

## Appendix B: Socket Path Resolution

```
Priority order:
  1. Explicit path passed to Session(socket=...)
  2. $SPECTRA_SOCKET environment variable
  3. $XDG_RUNTIME_DIR/spectra/spectra.sock
  4. /tmp/spectra-$USER/spectra.sock

Directory is created with mode 0700 if it doesn't exist.
Socket file is removed on clean backend shutdown.
Stale socket file (no process listening) is removed on backend startup.
```

## Appendix C: Future Considerations (Out of Scope for V1)

- **Windows support:** Named pipes or TCP localhost instead of Unix sockets
- **Remote rendering:** TCP transport + SSH tunnel for remote backend
- **Jupyter integration:** Inline widget that embeds agent rendering via WebSocket
- **C Python extension:** Optional pybind11 module for in-process mode (bypasses IPC)
- **GPU shared memory:** Vulkan external memory extension for direct GPU↔GPU transfer between agent and Python (CUDA interop)
- **Multi-backend:** Load balancing across multiple backends for heavy workloads

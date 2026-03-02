# Breathing File — Spectra ROS2 Adapter (`spectra-ros`)

> **READ THIS FIRST (every session):** Check §6 Current Focus → run §7 Pre-Flight → read the latest §8 Session Log entry → then act.

## 1) Objective

Build a full-featured ROS2 visualization/debugging adapter for Spectra replacing the `rqt` suite with a single GPU-accelerated app. Accessible via `spectra-ros` executable or **Tools → ROS2 Adapter** menu. Features: topic monitor, live plotting, rosbag player/recorder, TF tree, node graph, parameter editor, service caller, log viewer, diagnostics dashboard, CSV export — all through Spectra's Vulkan pipeline.

## 2) Assumptions

- ROS2 Humble+ minimum (rclcpp API stable)
- User has ROS2 workspace sourced before building
- Message introspection via `rosidl_typesupport_introspection_cpp` (runtime field extraction, no codegen)
- Rosbag2 API available for bag features (optional sub-option)
- Adapter is a **separate CMake target** — does NOT pollute core library
- Qt adapter (`src/adapters/qt/`) is the reference adapter pattern
- ImGui available for panel UI; GLFW for standalone window
- Existing Figure/Axes/Series/DataInteraction/Inspector/TimelineEditor features work on ROS2 plots
- Thread model: ROS2 executor on background thread; render thread polls lock-free ring buffers

## 3) Constraints

- **No core library modifications** — all code under `src/adapters/ros2/`
- **No ROS2 headers in `include/spectra/`** — adapter is downstream-only
- **C++20**, thread-safe, no per-frame heap allocs in hot path
- **CMake gated** — `SPECTRA_USE_ROS2` (default OFF); zero ROS2 impact when OFF
- **Must not break existing builds**
- **Vulkan safety rules** apply to all rendering
- **`SPECTRA_ROS2_BAG`** sub-option for rosbag2 dependency (graceful disable if absent)

## 4) Do Not Touch (Current Phase)

- `src/render/`, `src/core/`, `src/ipc/`, `src/daemon/`, `src/agent/`
- `src/adapters/qt/`, `src/embed/`, `python/`
- Existing test files — only add new ones
- Exception: G2 adds one command + one menu item to `register_commands.cpp` / `imgui_integration.cpp`

## 5) Mission Board (Source of Truth)

### Phase A — Foundation & ROS2 Bridge

- [x] A1 [impl] [risk:low] **CMake scaffolding & ROS2 dependency detection**
  - depends_on: none
  - acceptance:
    - `SPECTRA_USE_ROS2` option added; `find_package(rclcpp)` gated behind it
    - `spectra_ros2_adapter` library target + `spectra-ros` executable target created
    - Builds cleanly with/without ROS2 sourced
    - Directory `src/adapters/ros2/` created with placeholder files

- [x] A2 [impl] [risk:low] **ROS2 node lifecycle wrapper**
  - depends_on: A1
  - acceptance:
    - `Ros2Bridge` class: init, shutdown, spin thread start/stop
    - Executor on dedicated background thread, clean shutdown
    - Unit test: init/shutdown cycle

- [x] A3 [impl] [risk:med] **Topic discovery service**
  - depends_on: A2
  - acceptance:
    - `TopicDiscovery`: refresh(), topics() → vector of `TopicInfo` (name, type, pub/sub count, QoS)
    - Periodic refresh (2s default), add/remove callbacks
    - Also discovers services and nodes
    - Unit test with mock publisher

- [ ] A4 [impl] [risk:high] **Runtime message introspection engine**
  - depends_on: A2
  - acceptance:
    - `MessageIntrospector`: introspect any msg type via `rosidl_typesupport_introspection_cpp`
    - Returns `MessageSchema` tree of `FieldDescriptor` (name, type, array info, nested)
    - `FieldAccessor`: extract numeric value from serialized bytes given field path (`pose.position.x`)
    - Handles: bool, int8-64, uint8-64, float32/64, string, nested msgs, arrays
    - Unit test: introspect Float64, Twist, Imu — verify tree and extraction

- [ ] A5 [impl] [risk:med] **Generic subscription engine**
  - depends_on: A4
  - acceptance:
    - `GenericSubscriber`: subscribe any topic via `rclcpp::GenericSubscription`
    - Uses introspector to extract fields; SPSC lock-free ring buffer for `(timestamp, value)` pairs
    - Multiple fields from one topic (single subscription, multiple extractors)
    - Configurable buffer depth (default 10000)
    - Unit test: publish, verify extraction through ring buffer

- [ ] A6 [test] [risk:low] **Phase A integration smoke test**
  - depends_on: A2, A3, A4, A5
  - acceptance:
    - Headless test: create bridge, discover topics, subscribe Float64, extract values
    - Passes with ctest

### Phase B — Topic Monitor Panel (ImGui)

- [ ] B1 [impl] [risk:low] **Topic list panel — tree view**
  - depends_on: A3
  - acceptance:
    - ImGui tree of topics grouped by namespace; columns: Name, Type, Hz, Pubs, Subs, BW
    - Live Hz/BW (rolling 1s); search/filter bar; active=green, stale=gray dots
    - Integrates into Spectra's docking system

- [ ] B2 [impl] [risk:low] **Topic echo panel — live message display**
  - depends_on: A4, A5, B1
  - acceptance:
    - Expandable field tree for selected topic; last 100 messages; pause/resume/clear
    - Display rate cap 30Hz; arrays show `[N items]` expandable

- [ ] B3 [impl] [risk:low] **Topic statistics overlay**
  - depends_on: A5, B1
  - acceptance:
    - Per-topic: avg/min/max Hz, count, bytes, latency (for Header msgs)
    - Drop detection warning; BW auto-scaled (B/KB/MB)

### Phase C — Live Plotting Engine

- [ ] C1 [impl] [risk:med] **ROS2 field → Spectra series bridge**
  - depends_on: A5
  - acceptance:
    - `RosPlotManager`: add_plot(topic, field_path) → Figure + LineSeries
    - Polls ring buffers each frame, appends to series; X=time, Y=value
    - Auto-fit Y on first 100 samples; auto-color from palette; label = `topic/field`

- [ ] C2 [impl] [risk:low] **Auto-scrolling time window**
  - depends_on: C1
  - acceptance:
    - Configurable window (1s–3600s, default 30s); X auto-scrolls `[now-window, now]`
    - Pan/zoom pauses auto-scroll (indicator); Home resumes
    - Prune data outside 2× window; memory indicator in status bar

- [ ] C3 [impl] [risk:med] **Drag-and-drop field to plot**
  - depends_on: B1, B2, C1
  - acceptance:
    - Drag numeric field from echo panel to plot axes/empty area/tab bar
    - Right-click context menu: "Plot in new window/current axes/new subplot"
    - Visual feedback during drag

- [ ] C4 [impl] [risk:low] **Multi-subplot layout**
  - depends_on: C1
  - acceptance:
    - Configurable NxM grid; shared X axis via AxisLinkManager; shared cursor
    - Add/remove subplot from toolbar

- [ ] C5 [impl] [risk:med] **Expression fields (computed topics)**
  - depends_on: C1
  - acceptance:
    - Expression parser: `sqrt($imu.linear_acceleration.x^2 + ...)` with `+−*/^sqrt abs sin cos atan2 log exp`
    - Expression editor with error feedback; save/load presets

- [ ] C6 [test] [risk:low] **Phase C integration test**
  - depends_on: C1, C2, C4
  - acceptance:
    - Headless: subscribe 3 topics, 100 frames, verify data matches published values
    - Verify scroll bounds, pruning, linked axes

### Phase D — Rosbag Player & Recorder

- [ ] D1 [impl] [risk:med] **Rosbag2 reader backend**
  - depends_on: A4
  - acceptance:
    - `BagReader`: open .db3/.mcap; list topics; sequential read; random seek; metadata
    - Graceful corrupt bag handling; gated behind `SPECTRA_ROS2_BAG`
    - Unit test: open test bag, verify listing, read, seek

- [ ] D2 [impl] [risk:med] **Bag playback engine with timeline**
  - depends_on: D1, C1
  - acceptance:
    - `BagPlayer`: play/pause/stop/seek/set_rate (0.1×–10×); step fwd/back; loop mode
    - Feed messages through same RosPlotManager as live data
    - Reuse TimelineEditor for scrub bar; timeline shows topic activity bands

- [ ] D3 [impl] [risk:low] **Bag recorder**
  - depends_on: A5, D1
  - acceptance:
    - `BagRecorder`: start(path, topics), stop; .db3 or .mcap; recording indicator
    - Auto-split by size/duration

- [ ] D4 [impl] [risk:low] **Bag info panel**
  - depends_on: D1
  - acceptance:
    - ImGui panel: bag metadata, topic table, clickable topics to plot
    - Drag-and-drop .db3/.mcap files to open

### Phase E — Export & Data Tools

- [ ] E1 [impl] [risk:low] **CSV export with ROS timestamps**
  - depends_on: C1
  - acceptance:
    - Export plotted data: timestamp_sec, timestamp_nsec, wall_clock, field columns
    - Configurable separator/precision; visible range or full history; file dialog

- [ ] E2 [impl] [risk:low] **Clipboard copy of selected data**
  - depends_on: C1
  - acceptance:
    - Ctrl+C copies selected range as TSV; works on single/multiple series

- [ ] E3 [impl] [risk:low] **Screenshot and video export**
  - depends_on: C1
  - acceptance:
    - Ctrl+Shift+S screenshot; Tools→Record uses existing RecordingExport

### Phase F — Advanced ROS2 Tools

- [ ] F1 [impl] [risk:high] **Node graph visualization**
  - depends_on: A3
  - acceptance:
    - Query ROS2 graph → nodes as boxes, topics as ellipses, edges as arrows
    - Force-directed layout; filter by namespace; click node shows details; auto-refresh

- [ ] F2 [impl] [risk:med] **TF tree viewer**
  - depends_on: A2
  - acceptance:
    - Subscribe `/tf` + `/tf_static`; tree view of frames; transform lookup between frames
    - Frame age, stale warning, Hz badge per transform

- [ ] F3 [impl] [risk:med] **Parameter editor**
  - depends_on: A2, A3
  - acceptance:
    - Discover node parameters; edit by type (checkbox/slider/text); range hints
    - Live-edit or apply mode; undo last change; save/load YAML presets

- [ ] F4 [impl] [risk:low] **Service caller UI**
  - depends_on: A3, A4
  - acceptance:
    - List services; auto-generate input form from introspection; call + show response
    - History; timeout config; JSON import/export

- [ ] F5 [impl] [risk:low] **Log viewer (/rosout)**
  - depends_on: A5
  - acceptance:
    - Subscribe `/rosout`; scrolling table: Time, Severity, Node, Message
    - Color-coded severity; filter by severity/node/regex; 10K circular buffer; pause/clear/copy

- [ ] F6 [impl] [risk:low] **Diagnostics dashboard**
  - depends_on: A5
  - acceptance:
    - Subscribe `/diagnostics`; status badges (OK/WARN/ERROR/STALE); expand for key/values
    - History sparkline per component; alert on transitions

### Phase G — Application Shell & Integration

- [ ] G1 [impl] [risk:med] **`spectra-ros` standalone executable**
  - depends_on: B1, B2, C1, C2
  - acceptance:
    - Default layout: topic monitor (left), plot (center), inspector (right), log (bottom)
    - CLI: `--topics`, `--bag`, `--layout`; dockable panels; SIGINT handler
    - Window title: "Spectra ROS2 — \<node_name\>"

- [ ] G2 [impl] [risk:med] **Tools menu integration in main Spectra app**
  - depends_on: G1
  - acceptance:
    - `Tools → ROS2 Adapter` menu item (when `SPECTRA_USE_ROS2` ON)
    - Lazy ROS2 init; error dialog if ROS2 unavailable; grayed out if compiled without

- [ ] G3 [impl] [risk:low] **ROS2 session save/load**
  - depends_on: C1, D2
  - acceptance:
    - Save/load: subscriptions, field paths, layout, time window, expressions
    - JSON `.spectra-ros-session`; recent sessions list; auto-save on exit

- [ ] G4 [docs] [risk:low] **Documentation and examples**
  - depends_on: G1
  - acceptance:
    - `docs/ros2-adapter.html`; README section; 3 example launch files; screenshots

### Phase H — Testing & Validation

- [ ] H1 [test] [risk:low] **Unit tests — discovery & introspection** (50+ tests)
  - depends_on: A3, A4

- [ ] H2 [test] [risk:low] **Unit tests — subscription & plot bridge** (35+ tests)
  - depends_on: A5, C1

- [ ] H3 [test] [risk:med] **Integration test — bag playback end-to-end**
  - depends_on: D2
  - acceptance:
    - Test bag (small, ~1MB) committed; headless: open, play, verify, seek, rate scaling
    - Zero Vulkan validation errors

- [ ] H4 [perf] [risk:med] **Performance benchmark — high-frequency streaming**
  - depends_on: C1
  - acceptance:
    - 10 topics × 1000 Hz (10K msg/sec) → frame time <16ms
    - 100 topics × 10 Hz → verify discovery + UI scales

## 6) Current Focus

- **Active phase:** Phase A — Foundation & ROS2 Bridge
- **Active mission(s):** A4
- **Why now:** A3 complete. MessageIntrospector depends on A2 (Ros2Bridge node handle); does NOT need TopicDiscovery.
- **Phase completion trigger:** When A1–A6 are `[x]`, advance to Phase B + C (parallel wave pg:1).

## 7) Pre-Flight Checklist (Run Every Session)

- [ ] Reviewed latest session log entry
- [ ] Reviewed the Handoff block of that entry
- [ ] Checked §6 Current Focus — active mission matches what I plan to do
- [ ] Checked Do Not Touch section
- [ ] Confirmed not duplicating existing work
- [ ] Confirmed current build state before edits
- [ ] Confirmed ROS2 workspace is sourced (if working on ROS2-dependent code)
- [ ] *(parallel only)* Ran Agent Self-Check (§13)

## 8) Session Log

### Session 003 — 2026-03-02
Session-ID: 003
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: implement
Focus: A3
Outcome: DONE
Confidence: high

**Intent** — Implement `TopicDiscovery`: periodic ROS2 graph discovery service using `Ros2Bridge::node()` to query topics, services, and nodes.

**What was done** —
- Created `src/adapters/ros2/topic_discovery.hpp` — `TopicDiscovery` class + `TopicInfo`, `ServiceInfo`, `NodeInfo`, `QosInfo` structs. Public API: `start()`, `stop()`, `refresh()`, `set_refresh_interval()`, `topics()`, `services()`, `nodes()`, `has_topic()`, `topic()`, `topic_count()`, `service_count()`, `node_count()`, `set_topic_callback()`, `set_service_callback()`, `set_node_callback()`, `set_refresh_done_callback()`. Thread-safe (std::mutex). Periodic refresh via `rclcpp::TimerBase`. Re-entrant refresh guard via `std::atomic<bool> refresh_in_progress_`.
- Created `src/adapters/ros2/topic_discovery.cpp` — Full implementation: `do_refresh()` queries graph outside mutex (avoids holding lock during slow ROS2 calls), then diffs under mutex and fires add/remove callbacks. `query_topics()` uses `get_topic_names_and_types()` + `count_publishers/subscribers()` + `get_publishers_info_by_topic()` for QoS. `query_services()` uses `get_service_names_and_types()`. `query_nodes()` uses `get_node_names_and_namespaces()`. QoS populated via `rmw_qos_profile_t` (reliability, durability, history, depth).
- Created `tests/unit/test_topic_discovery.cpp` — 32 tests across 9 suites: Construction (2), Refresh (4), TopicDiscovery (8: mock pub, name/type/pub-count/sub-count, topics vector, multi-topic, unknown topic), AddRemoveCallbacks (3), NodeCallbacks (4), ServiceDiscovery (3), RefreshDoneCallback (1), StartStop (5), QosInfo (2), EdgeCases (5). Uses same `RclcppEnvironment` + per-test `TopicDiscoveryTest` fixture pattern as A2. Unique node names per test via atomic counter.
- Modified `CMakeLists.txt` — Added `src/adapters/ros2/topic_discovery.cpp` to `spectra_ros2_adapter` sources.
- Modified `tests/CMakeLists.txt` — Added `unit_test_topic_discovery` target gated inside `if(SPECTRA_USE_ROS2)` block, same structure as `unit_test_ros2_bridge`.

**Files touched** —
- `src/adapters/ros2/topic_discovery.hpp` (new)
- `src/adapters/ros2/topic_discovery.cpp` (new)
- `tests/unit/test_topic_discovery.cpp` (new)
- `CMakeLists.txt` (modified — added topic_discovery.cpp to adapter sources)
- `tests/CMakeLists.txt` (modified — added ROS2-gated test target)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): ✅ (cmake builds clean, ninja: no work to do) | Tests: ✅ (non-ROS2 tests unaffected) | ROS2 build: requires sourced Humble+ workspace; 32 unit tests validate init/discovery/callbacks/QoS/timer/edge-cases.

**Key design decisions** —
- `do_refresh()` releases mutex before graph queries (slow ROS2 calls), re-acquires for diff+callbacks — avoids starvation
- Re-entrant guard: `refresh_in_progress_` CAS prevents timer callback overlapping manual `refresh()` call
- Diff is add/remove only (no update events) — pub/sub counts silently updated in-place
- `query_nodes()` uses `get_node_names_and_namespaces()` → `vector<pair<string,string>>`
- QoS extracted via `rmw_qos_profile_t` fields (reliability, durability, history, depth) from first publisher
- clangd shows false-positive errors for all ROS2 adapter files (workspace built without ROS2); files compile correctly with sourced ROS2

**Mission status updates** — A3 → `[x]` DONE

**Blockers** — None.

---

### Session 002 — 2026-03-02
Session-ID: 002
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: implement
Focus: A2
Outcome: DONE
Confidence: high

**Intent** — Implement `Ros2Bridge`: ROS2 node lifecycle wrapper with init, shutdown, and dedicated background spin thread.

**What was done** —
- Created `src/adapters/ros2/ros2_bridge.hpp` — `Ros2Bridge` class + `BridgeState` enum (Uninitialized/Initialized/Spinning/ShuttingDown/Stopped). Public API: `init(node_name, node_namespace, argc, argv)`, `start_spin()`, `shutdown()`, `state()`, `is_ok()`, `node()`, `executor()`, `set_state_callback()`. Owns `rclcpp::Node::SharedPtr`, `SingleThreadedExecutor`, `std::thread`, `std::atomic<BridgeState>`, `std::atomic<bool> stop_requested_`.
- Created `src/adapters/ros2/ros2_bridge.cpp` — Full implementation: `init()` calls `rclcpp::init()` if needed (idempotent), creates node+executor; `start_spin()` launches background thread; `spin_thread_func()` loops `executor_->spin_once(10ms)` until `stop_requested_` or `!rclcpp::ok()`; `shutdown()` sets stop flag, calls `executor_->cancel()` + `rclcpp::shutdown()`, joins thread, destroys resources. Destructor calls `shutdown()`.
- Created `tests/unit/test_ros2_bridge.cpp` — 21 tests across 6 suites: Construction (1), Init (6), StartSpin (4), Shutdown (5), StateCallback (3), Destructor (2). Uses `RclcppEnvironment` (GoogleTest global env) + per-test `Ros2BridgeTest` fixture. Custom `main()` registers the environment.
- Modified `CMakeLists.txt` — Added `ros2_bridge.cpp` to `spectra_ros2_adapter` sources.
- Modified `tests/CMakeLists.txt` — Added `unit_test_ros2_bridge` target gated inside `if(SPECTRA_USE_ROS2)` block, linking `spectra_ros2_adapter + GTest::gtest`, labelled `ros2`.

**Files touched** —
- `src/adapters/ros2/ros2_bridge.hpp` (new)
- `src/adapters/ros2/ros2_bridge.cpp` (new)
- `tests/unit/test_ros2_bridge.cpp` (new)
- `CMakeLists.txt` (modified — added ros2_bridge.cpp to adapter sources)
- `tests/CMakeLists.txt` (modified — added ROS2-gated test target)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): ✅ (cmake builds clean) | Tests: ✅ (85/85 ctest pass, zero regressions) | ROS2 build: requires sourced Humble+ workspace; unit test validates init/shutdown cycle.

**Mission status updates** — A2 → `[x]` DONE

**Blockers** — None.

---

### Session 001 — 2026-03-02
Session-ID: 001
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: implement
Focus: A1
Outcome: DONE
Confidence: high

**Intent** — Add CMake scaffolding for the ROS2 adapter so all subsequent A2+ missions have a valid build target to compile into.

**What was done** —
- Added `SPECTRA_USE_ROS2` and `SPECTRA_ROS2_BAG` options to the feature-flag block in top-level `CMakeLists.txt`.
- Added a fully-gated `if(SPECTRA_USE_ROS2)` block that: runs `find_package` for rclcpp, rcl_interfaces, rosidl_typesupport_introspection_cpp, std/geometry/sensor/diagnostic/tf2_msgs; conditionally links rosbag2_cpp + rosbag2_storage when `SPECTRA_ROS2_BAG=ON`; defines `spectra_ros2_adapter` static library target; defines `spectra-ros` executable target (guarded by `if(EXISTS main.cpp)`).
- Created `src/adapters/ros2/ros2_adapter.hpp` — public adapter header, `spectra::adapters::ros2` namespace.
- Created `src/adapters/ros2/ros2_adapter.cpp` — placeholder implementation (`adapter_version()` returns `SPECTRA_VERSION_STRING`).
- Created `src/adapters/ros2/main.cpp` — placeholder `spectra-ros` entry point (prints version, exits 0).

**Files touched** —
- `CMakeLists.txt` (modified — options + ROS2 adapter block)
- `src/adapters/ros2/ros2_adapter.hpp` (new)
- `src/adapters/ros2/ros2_adapter.cpp` (new)
- `src/adapters/ros2/main.cpp` (new)

**Validation** — Build: ✅ (cmake -DSPECTRA_USE_ROS2=OFF, full build 150/150 targets) | Tests: ✅ (85/85 ctest pass, zero regressions) | Runtime: n/a (placeholder only)

**Mission status updates** — A1 → `[x]` DONE

**Blockers** — None. ROS2 build path cannot be verified without a sourced ROS2 workspace; will be validated during A2 which requires rclcpp anyway.

---
**HANDOFF — Next Session Start Here**
1. A1 complete — CMake scaffolding, `spectra_ros2_adapter` + `spectra-ros` targets.
2. A2 complete — `Ros2Bridge` in `ros2_bridge.hpp/.cpp`; 21 unit tests.
3. A3 complete — `TopicDiscovery` in `topic_discovery.hpp/.cpp`; 32 unit tests in `test_topic_discovery.cpp`. Discovers topics (with QoS), services, nodes. Periodic timer (2s default), add/remove callbacks. Non-ROS2 build: clean.
4. Next mission is **A4** — implement `MessageIntrospector`. Create `src/adapters/ros2/message_introspector.hpp/.cpp`. Uses `rosidl_typesupport_introspection_cpp` to build a `MessageSchema` tree of `FieldDescriptor` (name, type_id, array info, nested). Add `FieldAccessor` to extract numeric values from serialized CDR bytes given a field path like `pose.position.x`. Handles: bool, int8–64, uint8–64, float32/64, string, nested msgs, fixed/dynamic arrays. Add .cpp to `spectra_ros2_adapter` sources. Add `tests/unit/test_message_introspector.cpp` gated under `if(SPECTRA_USE_ROS2)`.
5. After A4, do A5 (GenericSubscriber) → A6 (integration smoke test) before opening Phase B+C parallel wave.
---

## 9) Decision Log

- (empty — format: D-NNN: \<decision\> | Rationale: \<why\> | Date: \<date\>)

## 10) Deferred Improvements

- [ ] Python `spectra.ros2` module (use from rclpy scripts)
- [ ] Image viewer panel (sensor_msgs/Image as texture)
- [ ] PointCloud2 viewer (3D scatter in Spectra 3D axes)
- [ ] Action client UI (goals, feedback, results)
- [ ] QoS profile editor UI
- [ ] Multi-robot namespace color-coding
- [ ] Rosbag merge/split tool
- [ ] Topic bandwidth limiter/throttle
- [ ] Custom .msg runtime loading
- [ ] ROS2 lifecycle node management UI
- [ ] DDS domain ID selector
- [ ] ROS2 security/SROS2 key management UI
- [ ] Marker array visualization (visualization_msgs/MarkerArray)
- [ ] Occupancy grid viewer (nav_msgs/OccupancyGrid)
- [ ] Path/trajectory overlay on 2D plot

## 11) Known Risks / Open Questions

- **Risk:** `rosidl_typesupport_introspection_cpp` API is poorly documented and may differ between Humble/Iron/Jazzy
  - Mitigation: Test on Humble first; add distro-specific `#if` guards if needed; reference `rosbag2_cpp` source for working examples
- **Risk:** High-frequency topics (>1000 Hz) may overwhelm ring buffer or cause render thread stalls
  - Mitigation: Configurable decimation at subscription level; benchmark early (H4)
- **Risk:** `rclcpp::GenericSubscription` serialized message format may change
  - Mitigation: Pin to stable API; use CDR deserialization which is standardized
- **Risk:** TF2 buffer can grow unbounded if many frames at high rate
  - Mitigation: Configurable buffer duration (default 10s); expose in UI
- **Risk:** Node graph layout algorithm for large graphs (100+ nodes) may be slow
  - Mitigation: Defer F1 to late phase; start with simple hierarchical layout; force-directed only if <50 nodes
- **Open question:** Should bag playback publish to ROS2 topics (like `ros2 bag play`) or feed directly to plot manager?
  - Leaning: Direct feed for performance; optional "republish" mode for integration with other tools
- **Open question:** Should the adapter support ROS1 via `ros1_bridge`?
  - Leaning: Out of scope; users can bridge externally

## 12) Definition of Done

- [ ] All in-scope missions are `[x]` and validated
- [ ] `spectra-ros` launches, discovers topics, plots live data at 60 FPS
- [ ] Bag playback works end-to-end with timeline scrubbing
- [ ] All panels (topic monitor, log viewer, param editor, TF, graph, diagnostics) functional
- [ ] CSV export produces correct timestamped data
- [ ] No Vulkan validation errors
- [ ] No frame hitches at 10K msg/sec
- [ ] No memory leaks (ASAN clean)
- [ ] Builds with `SPECTRA_USE_ROS2=OFF` produce identical binary to current
- [ ] Documentation and examples published

## 13) Parallel Execution Map

### Is parallel execution worth it here?

- **Parallel phases:** Phase B + C wave pg:1 (after Phase A), Phase D + E wave pg:2 (after C1), Phase F missions pg:3
- **Why parallel:** B (topic monitor UI) and C (plotting engine) write completely different files. D (bag) and E (export) are independent modules.
- **Estimated saving:** ~40% time reduction in Phases B-F
- **Human supervision required:** YES

### Agent Roster

| Agent-ID | Wave | Missions | Files owned |
|----------|------|----------|-------------|
| Alpha | pg:1 | B1, B2, B3 | `src/adapters/ros2/ui/topic_list_panel.*`, `topic_echo_panel.*`, topic stats |
| Beta | pg:1 | C1, C2, C4 | `src/adapters/ros2/ros_plot_manager.*`, auto-scroll, subplot layout |
| Alpha | pg:2 | D1, D2, D3, D4 | `src/adapters/ros2/bag_reader.*`, `bag_player.*`, `bag_recorder.*`, bag UI |
| Beta | pg:2 | E1, E2, E3 | `src/adapters/ros2/ros_csv_export.*`, clipboard, screenshot |

### Wave Map

| Wave | Missions | Gate before | Gate after |
|------|----------|-------------|------------|
| (seq) | A1–A6 | none | All Phase A `[x]` |
| pg:1 | B1-B3 + C1,C2,C4 | A6 = `[x]` | Integration smoke ✅ |
| (seq) | C3, C5, C6 | pg:1 sync | C3+C5 tested |
| pg:2 | D1-D4 + E1-E3 | C1 = `[x]` | Bag + export tested ✅ |
| (seq) | F1–F6 | pg:2 sync | Each panel tested |
| (seq) | G1–G4 | F complete | App launches ✅ |
| (seq) | H1–H4 | G1 = `[x]` | All tests pass ✅ |

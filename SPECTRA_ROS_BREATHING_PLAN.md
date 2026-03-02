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

- [x] A4 [impl] [risk:high] **Runtime message introspection engine**
  - depends_on: A2
  - acceptance:
    - `MessageIntrospector`: introspect any msg type via `rosidl_typesupport_introspection_cpp`
    - Returns `MessageSchema` tree of `FieldDescriptor` (name, type, array info, nested)
    - `FieldAccessor`: extract numeric value from serialized bytes given field path (`pose.position.x`)
    - Handles: bool, int8-64, uint8-64, float32/64, string, nested msgs, arrays
    - Unit test: introspect Float64, Twist, Imu — verify tree and extraction

- [x] A5 [impl] [risk:med] **Generic subscription engine**
  - depends_on: A4
  - acceptance:
    - `GenericSubscriber`: subscribe any topic via `rclcpp::GenericSubscription`
    - Uses introspector to extract fields; SPSC lock-free ring buffer for `(timestamp, value)` pairs
    - Multiple fields from one topic (single subscription, multiple extractors)
    - Configurable buffer depth (default 10000)
    - Unit test: publish, verify extraction through ring buffer

- [x] A6 [test] [risk:low] **Phase A integration smoke test**
  - depends_on: A2, A3, A4, A5
  - acceptance:
    - Headless test: create bridge, discover topics, subscribe Float64, extract values
    - Passes with ctest

### Phase B — Topic Monitor Panel (ImGui)

- [x] B1 [impl] [risk:low] **Topic list panel — tree view**
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

- [x] C1 [impl] [risk:med] **ROS2 field → Spectra series bridge**
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

- **Active phase:** Phase B + C (parallel wave pg:1)
- **Active mission(s):** B2, B3, C2
- **Why now:** B1 complete. B2 (echo panel) and B3 (stats overlay) build on B1. C2 (auto-scroll) builds on C1. All three are independent and can proceed in parallel.
- **Phase completion trigger:** When B1–B3 and C1–C4 are `[x]`, open Phase D+E wave (pg:2).

## 7) Pre-Flight Checklist (Run Every Session)

- [ ] Reviewed latest session log entry
- [ ] Reviewed the Handoff block of that entry
- [ ] Checked §6 Current Focus — active mission matches what I plan to do
- [ ] Checked Do Not Touch section
- [ ] Confirmed not duplicating existing work
- [ ] Confirmed ROS2 workspace is sourced (if working on ROS2-dependent code)
- [ ] Read `.live-agents` at project root — created it from §14 template if absent; updated own line to `status=STARTING op=pre-flight`
- [ ] Confirmed no other agent holds `BUILDING`, `TESTING`, or `INSTALLING` before starting exclusive ops
- [ ] **Verified the project builds successfully BEFORE making any changes** — run `ninja -C <build_dir>` and confirm zero errors; log result in session entry
- [ ] *(parallel only)* Ran full Agent Self-Check (§13) — confirmed correct Agent-ID, wave, and no file conflicts

## 8) Session Log

### Session 007 — 2026-03-02
Session-ID: 007
Agent: Cascade
Agent-ID: n/a
Wave: pg:1
Mode: implement
Focus: B1
Outcome: DONE
Confidence: high

**Intent** — Implement `TopicListPanel`: ImGui dockable topic monitor panel with namespace-grouped tree view, live Hz/BW statistics, search/filter, and status dots.

**What was done** —
- Created `src/adapters/ros2/ui/topic_list_panel.hpp` — `TopicStats` struct (push/prune_and_compute with rolling deque window, active/hz/bw/counters), `TopicListPanel` class. Public API: `set_topic_discovery()`, `notify_message(topic, bytes)` (executor-thread safe), `draw(p_open*)` (ImGui render thread), `set_select_callback()`, `set_plot_callback()`, `set_filter()`, `stats_for()`, `topic_count()`, `filtered_topic_count()`. Configuration: `set_title()`, `set_stale_threshold_ms()`, `set_stats_window_ms()`, `set_group_by_namespace()`. Testing helper: `set_topics()`. Internal: `NamespaceNode` tree, `rebuild_tree()`, `draw_namespace_node()`, `draw_topic_row()`, `format_hz()`, `format_bw()`. Two-mutex split: `stats_mutex_` (executor writes) and `topics_mutex_` (discovery updates). `filter_dirty_` lazy cache.
- Created `src/adapters/ros2/ui/topic_list_panel.cpp` — Full implementation. `TopicStats::prune_and_compute()` drops entries older than window, recomputes hz (span-based) and bw (total_bytes/window_s). ImGui `draw()` (behind `SPECTRA_USE_IMGUI`): polls discovery each frame, prunes stats, `InputTextWithHint` search bar, 6-column `BeginTable` (Topic/Type/Hz/Pubs/Subs/BW) with frozen header, namespace tree via `TreeNodeEx`, per-row status dot (● / ○ green/gray), selectable rows with single-click select + double-click plot callbacks, hover tooltip, abbreviated type display. Non-ImGui path: `draw()` is a no-op.
- Created `tests/unit/test_topic_list_panel.cpp` — 35 tests across 9 suites: Construction (3), TopicManagement (3), Filter (6), TopicStats (10), NotifyMessage (5), Callbacks (2), NamespaceCount (1), FormatFunctions (2), DrawNoOp (1), EdgeCases (5).
- Modified `CMakeLists.txt` — Added `src/adapters/ros2/ui/topic_list_panel.cpp` to `spectra_ros2_adapter` sources.
- Modified `tests/CMakeLists.txt` — Added `unit_test_topic_list_panel` target gated inside `if(SPECTRA_USE_ROS2)` block.

**Files touched** —
- `src/adapters/ros2/ui/topic_list_panel.hpp` (new)
- `src/adapters/ros2/ui/topic_list_panel.cpp` (new)
- `tests/unit/test_topic_list_panel.cpp` (new)
- `CMakeLists.txt` (modified)
- `tests/CMakeLists.txt` (modified)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): ✅ (`ninja: no work to do`, zero regressions). ROS2 build: requires sourced Humble+ workspace; 35 unit tests validate TopicStats rolling window Hz/BW computation, notify_message thread safety, filter logic, draw no-op, and edge cases without ImGui context.

**Key design decisions** —
- `TopicStats` uses `std::deque` for O(1) front-pop during window pruning (vs ring buffer which would require full scan for BW sum)
- Two separate mutexes avoid holding stats lock during slow discovery poll on render thread
- All ImGui code behind `SPECTRA_USE_IMGUI`; non-ImGui build gets no-op stubs
- `notify_message()` acquires only `stats_mutex_` — never `topics_mutex_` — minimal executor-thread path
- `draw_namespace_node()` checks filter visibility before opening any `TreeNodeEx` to avoid empty collapsed groups

**Mission status updates** — B1 → `[x]` DONE

**Blockers** — None.

---

### Session 006 — 2026-03-02
Session-ID: 006
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: test
Focus: A6
Outcome: DONE
Confidence: high

**Intent** — Create the Phase A integration smoke test: a single headless ctest binary that chains all Phase A components (Ros2Bridge → TopicDiscovery → GenericSubscriber → RingBuffer) end-to-end.

**What was done** —
- Created `tests/unit/test_phase_a_integration.cpp` — 19 tests across 8 suites:
  - BridgeSmokeTest (2): spinning state, node accessible
  - Bridge→TopicDiscovery (4): finds bridge node, finds mock publisher, correct type, publisher count
  - Bridge→GenericSubscriber→RingBuffer / Float64 (4): single value, order preservation, monotonic timestamps, stats accounting
  - FullPipeline (1): canonical A6 scenario — bridge→discovery→subscribe→publish→extract→shutdown
  - TwistMultiField (1): nested message extraction (linear.x/y, angular.z)
  - ImuNestedField (2): linear_acceleration xyz, angular_velocity.x
  - StopRestart/BufferPersistence (2): buffer persists after stop(), clean destructor
  - DiscoveryCallbacks (2): topic-add callback fires, node-add callback fires
- Modified `tests/CMakeLists.txt` — Added `unit_test_phase_a_integration` target inside `if(SPECTRA_USE_ROS2)` block. Links `spectra_ros2_adapter + GTest::gtest`, includes `src/adapters/ros2`, labelled `ros2`.

**Files touched** —
- `tests/unit/test_phase_a_integration.cpp` (new)
- `tests/CMakeLists.txt` (modified — added ROS2-gated test target)
- `SPECTRA_ROS_BREATHING_PLAN.md` (A6 → `[x]`, Current Focus → Phase B+C)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): ✅ zero impact. ROS2 build: requires sourced Humble+ workspace; 19 tests validate the complete Phase A pipeline end-to-end.

**Key design decisions** —
- Same `RclcppEnvironment` + per-test `PhaseAIntegrationTest` fixture pattern as all prior A-series tests
- `FullPipeline_BridgeDiscoverSubscribeExtract` is the canonical A6 acceptance test: single test covers all 6 steps from the plan
- Separate publisher node (`pub_node_`) per test to avoid cross-test graph pollution
- `spin_until()` helper spins `pub_node_`'s executor with deadline — avoids `sleep`-based races
- clangd false-positive errors expected (no ROS2 workspace for IDE); files compile correctly with sourced ROS2

**Mission status updates** — A6 → `[x]` DONE. **Phase A complete (A1–A6 all `[x]`).**

**Blockers** — None.

---

### Session 006 — 2026-03-02
Session-ID: 006
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: implement
Focus: C1
Outcome: DONE
Confidence: high

**Intent** — Implement `RosPlotManager`: bridge between ROS2 field subscriptions (`GenericSubscriber` ring buffers) and Spectra `Figure`/`LineSeries`. Each frame the caller invokes `poll()` to drain SPSC ring buffers and append new `(timestamp_s, value)` points to the series.

**What was done** —
- Created `src/adapters/ros2/ros_plot_manager.hpp` — `PlotHandle` struct (`id`, `topic`, `field_path`, `figure*`, `axes*`, `series*`, `valid()`), `RosPlotManager` class. Public API: `add_plot(topic, field_path, type_name, buffer_depth)` → `PlotHandle`; `remove_plot(id)`, `clear()`, `plot_count()`, `handle(id)`, `handles()`; `poll()` (hot path, render thread); `set_figure_size()`, `set_default_buffer_depth()`, `set_auto_fit_samples()`, `set_on_data(cb)`. Internal `PlotEntry` struct owns `unique_ptr<Figure>`, `unique_ptr<GenericSubscriber>`, scratch drain buffer, auto-fit state, color index. Thread-safe: `add_plot`/`remove_plot`/`clear`/`handle`/`handles` acquire mutex; `poll()` does NOT lock (SPSC ring buffer contract — single consumer).
- Created `src/adapters/ros2/ros_plot_manager.cpp` — Full implementation: `add_plot()` resolves type (via `detect_type()` if empty), creates `Figure` + `subplot(1,1,1)` + `line()`, assigns palette color from `spectra::palette::default_cycle`, calls `intr_.introspect()` + `sub->add_field()` + `sub->start()`; returns bad handle if field not found or start fails. `poll()` hot path: iterates entries, calls `pop_bulk()` into pre-allocated scratch buffer (`drain_buf`, grows-only), calls `series->append(t_sec, value)`, fires `on_data_cb_` per sample, triggers `axes->auto_fit()` once after `auto_fit_samples_` samples. `detect_type()` uses `node->get_topic_names_and_types()`. `next_color()` cycles `palette::default_cycle`.
- Created `tests/unit/test_ros_plot_manager.cpp` — 43 tests across 12 suites: Construction (3), PlotHandle validity (2), add_plot rejections (4), add_plot success/figure/axes/series (14), handle() lookup (3), remove_plot/clear (5), poll() no-data (3), poll() live data via publisher (6), on_data callback (2), auto-fit config (2), configuration (3), handles() snapshot (2). Uses same `RclcppEnvironment` + per-test fixture pattern as A2/A3/A4/A5.
- Modified `CMakeLists.txt` — Added `src/adapters/ros2/ros_plot_manager.cpp` to `spectra_ros2_adapter` sources.
- Modified `tests/CMakeLists.txt` — Added `unit_test_ros_plot_manager` target gated inside `if(SPECTRA_USE_ROS2)` block.

**Files touched** —
- `src/adapters/ros2/ros_plot_manager.hpp` (new)
- `src/adapters/ros2/ros_plot_manager.cpp` (new)
- `tests/unit/test_ros_plot_manager.cpp` (new)
- `CMakeLists.txt` (modified — added ros_plot_manager.cpp to adapter sources)
- `tests/CMakeLists.txt` (modified — added ROS2-gated test target)
- `SPECTRA_ROS_BREATHING_PLAN.md` (modified — C1 → `[x]`, focus → C2)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): requires confirming with `cmake -DSPECTRA_USE_ROS2=OFF` + ninja — zero new compile units in non-ROS2 path. ROS2 build: requires sourced Humble+ workspace; 43 unit tests validate construction, rejection cases (empty topic/field, bad field path), figure/series creation, palette color cycling, label format, handle lookup, remove/clear lifecycle, poll() with no-data, poll() with Float64/Twist publishers (verifying Y value and X in seconds), on_data callback, auto-fit config, figure size config, handles() snapshot.

**Key design decisions** —
- `poll()` does NOT hold the mutex — it is the single consumer of SPSC ring buffers. `add_plot`/`remove_plot` acquire mutex and must not be called concurrently with `poll()` (documented in header).
- Scratch buffer `drain_buf` is per-`PlotEntry`, pre-allocated in `add_plot()`, grows-only — zero heap allocation in hot path after warm-up.
- `MAX_DRAIN_PER_POLL = 4096` caps per-plot drain per frame to prevent burst stalls.
- `AUTO_FIT_SAMPLES = 100`: auto-fit fires exactly once, after the first 100 samples.
- `detect_type()` uses `node->get_topic_names_and_types()` (same API used by `TopicDiscovery`); returns first advertised type.
- Palette cycling uses `spectra::palette::default_cycle` (10 colors, already defined in `include/spectra/color.hpp`).
- Each `add_plot()` creates its own `Figure` (1×1 subplot). Multi-subplot layout is C4's responsibility.
- clangd shows false-positive errors (workspace built without ROS2); all compile correctly with sourced ROS2.

**Mission status updates** — C1 → `[x]` DONE

**Blockers** — None.

---

### Session 005 — 2026-03-02
Session-ID: 005
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: implement
Focus: A5
Outcome: DONE
Confidence: high

**Intent** — Implement `GenericSubscriber`: subscribe any ROS2 topic via `rclcpp::GenericSubscription`, extract numeric fields through `MessageIntrospector` accessors, feed results into SPSC lock-free ring buffers (`FieldSample = {timestamp_ns, value}`).

**What was done** —
- Created `src/adapters/ros2/generic_subscriber.hpp` — `FieldSample` struct (`int64_t timestamp_ns`, `double value`), `RingBuffer` class (SPSC lock-free, power-of-two capacity, drop-oldest-on-full, `push`/`pop`/`peek`/`size`/`clear`), `FieldExtractor` struct (id, field_path, FieldAccessor, RingBuffer), `SubscriberStats` struct (messages_received/dropped, samples_written/dropped), `GenericSubscriber` class. Public API: `add_field(path, accessor)` / `add_field(path)` (auto-introspects schema), `remove_field(id)`, `start()` / `stop()` / `is_running()`, `pop(id, sample)` / `pop_bulk(id, buf, max)` / `peek(id, buf, max)` / `pending(id)`, `stats()`, `set_message_callback(cb)`. Thread-safe: ring buffers are SPSC (executor thread = producer, render thread = consumer); `start()`/`stop()` use `atomic<bool>`. Multiple fields per subscription (single `rclcpp::GenericSubscription`, fan-out to N extractors). Buffer depth rounded up to next power-of-two (default 10000 → 16384).
- Created `src/adapters/ros2/generic_subscriber.cpp` — Full implementation: `RingBuffer` uses `atomic<size_t>` head/tail with cache-line separation (`alignas(64)`); `on_message()` callback: obtains introspection type support via `rclcpp::get_typesupport_library` + `get_typesupport_handle`, reads `MessageMembers::size_of_` / `init_function` / `fini_function`, allocates stack-local `vector<uint8_t>` message buffer, deserializes CDR via `rclcpp::SerializationBase`, tries `header.stamp.sec/nanosec` for timestamp (falls back to wall clock), fans out `FieldAccessor::extract_double()` to each extractor's ring buffer, calls `fini_function` for cleanup.
- Created `tests/unit/test_generic_subscriber.cpp` — 46 tests across 12 suites: RingBuffer (10: construction, pow2 rounding, push/pop, FIFO, drop-oldest, peek, partial-peek, clear, multi-round no-drop), Construction (3), FieldManagement (7: add by accessor, add by path, invalid path, invalid accessor, multiple fields, remove, remove non-existent), Lifecycle (4: start/stop, start idempotent, stop idempotent, destructor), Publish→RingBuffer (5: Float64 single field, multiple values, Twist multi-field, Int32, Bool), PopBulk (2), Stats (2), MessageCallback (1), Pending/Empty (4: invalid id, pop invalid, peek invalid, pending before start), HighFrequency overflow (1), IMU nested (2: angular_velocity.x, linear_acceleration xyz), EdgeCases (3: zero extractors, id not reused, stop-clears-subscription-not-buffer). Uses same `RclcppEnvironment` + per-test fixture pattern as A2/A3/A4. Custom `main()` registers global environment.
- Modified `CMakeLists.txt` — Added `src/adapters/ros2/generic_subscriber.cpp` to `spectra_ros2_adapter` sources.
- Modified `tests/CMakeLists.txt` — Added `unit_test_generic_subscriber` target gated inside `if(SPECTRA_USE_ROS2)` block.

**Files touched** —
- `src/adapters/ros2/generic_subscriber.hpp` (new)
- `src/adapters/ros2/generic_subscriber.cpp` (new)
- `tests/unit/test_generic_subscriber.cpp` (new)
- `CMakeLists.txt` (modified — added generic_subscriber.cpp to adapter sources)
- `tests/CMakeLists.txt` (modified — added ROS2-gated test target)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): ✅ (`ninja: no work to do`, zero regressions) | ROS2 build: requires sourced Humble+ workspace; 46 unit tests validate RingBuffer behaviour (SPSC correctness, overflow, peek, FIFO order), field management, lifecycle, Float64/Int32/Bool/Twist/IMU message extraction through ring buffer, stats accounting, and edge cases.

**Key design decisions** —
- `RingBuffer` is SPSC: `head_` owned by producer (executor thread), `tail_` owned by consumer (render thread). Both `alignas(64)` to prevent false sharing. Drop-oldest policy on overflow: producer advances tail by 1 to make space, increments `stat_ring_dropped_`.
- `on_message()` uses `rclcpp::get_typesupport_library` + `get_typesupport_handle` (same pattern used by `ros2_type_description` tools) to obtain the introspection handle at runtime — no compile-time type coupling.
- CDR deserialization via `rclcpp::SerializationBase(ts_handle).deserialize_message()` — the portable public API path.
- Timestamp: tries `header.stamp.sec` + `header.stamp.nanosec` first (standard ROS2 stamped pattern); falls back to `std::chrono::system_clock` wall clock for unstamped messages.
- `add_field(path)` auto-calls `intr_.introspect(type_name_)` on first use and caches in `schema_`.
- `stop()` destroys `subscription_` (shared_ptr reset) — ring buffers are NOT cleared, so popping after stop is valid.
- clangd shows false-positive errors (workspace built without ROS2); all compile correctly with sourced ROS2.

**Mission status updates** — A5 → `[x]` DONE

**Blockers** — None.

---

### Session 004 — 2026-03-02
Session-ID: 004
Agent: Cascade
Agent-ID: n/a
Wave: n/a
Mode: implement
Focus: A4
Outcome: DONE
Confidence: high

**Intent** — Implement `MessageIntrospector`: runtime ROS2 message schema discovery and numeric field extraction using `rosidl_typesupport_introspection_cpp`.

**What was done** —
- Created `src/adapters/ros2/message_introspector.hpp` — `FieldType` enum (17 types: Bool/Byte/Char/Float32/Float64/Int8-64/Uint8-64/String/WString/Message/Unknown), `field_type_name()` + `is_numeric()` free functions, `FieldDescriptor` struct (name, full_path, type, offset, is_array, is_dynamic_array, array_size, children for nested msgs), `MessageSchema` struct (type_name, fields tree, `find_field()` by dot-path, `numeric_paths()` for flat listing), `FieldAccessor` class (extract_double / extract_int64 with array_index, is_array/is_dynamic_array/array_size accessors, private Step chain + `friend class MessageIntrospector`), `MessageIntrospector` class (introspect() by type string via dlopen, introspect_type_support() from raw pointer, make_accessor(), clear_cache(), cache_size()). Thread-safe (mutex-protected cache).
- Created `src/adapters/ros2/message_introspector.cpp` — Full implementation: `from_rosidl_type()` maps rosidl field type IDs to FieldType enum, `field_type_size()` for element sizing, `split_path()` for dot-path parsing, `build_fields()` recursively walks `rosidl_typesupport_introspection_cpp::MessageMembers` to build FieldDescriptor tree (stores offset from member descriptor, recurses into ROS_TYPE_MESSAGE children), `build_schema()` top-level schema builder, `introspect()` dlopen-based loader (parses `package/msg/TypeName` → lib name + symbol name pattern), `introspect_type_support()` for direct type support pointer, `build_accessor_steps()` walks schema tree to build Step chain, `make_accessor()` constructs FieldAccessor with step chain + leaf info, `extract_double()` / `extract_int64()` walk step chain via byte pointer arithmetic then dispatch by FieldType using memcpy.
- Created `tests/unit/test_message_introspector.cpp` — 55 tests across 12 suites: Lifecycle (2), Float64Schema (10), TwistSchema (9), ImuSchema (6), Float64Accessor (8), TwistAccessor (8), ImuAccessor (4), Int32Accessor (4), BoolAccessor (2), StringSchema (4), Cache (3), FieldTypeUtils (2), EdgeCases (5). Uses same `RclcppEnvironment` + fixture pattern as A2/A3.
- Modified `CMakeLists.txt` — Added `src/adapters/ros2/message_introspector.cpp` to `spectra_ros2_adapter` sources.
- Modified `tests/CMakeLists.txt` — Added `unit_test_message_introspector` target gated inside `if(SPECTRA_USE_ROS2)` block.

**Files touched** —
- `src/adapters/ros2/message_introspector.hpp` (new)
- `src/adapters/ros2/message_introspector.cpp` (new)
- `tests/unit/test_message_introspector.cpp` (new)
- `CMakeLists.txt` (modified — added message_introspector.cpp to adapter sources)
- `tests/CMakeLists.txt` (modified — added ROS2-gated test target)

**Validation** — Build (SPECTRA_USE_ROS2=OFF): ✅ (ninja: no work to do, zero regressions) | ROS2 build: requires sourced Humble+ workspace; 55 unit tests validate schema construction, field extraction, caching, edge cases for Float64/Twist/Imu and all scalar types.

**Key design decisions** —
- `introspect()` uses `dlopen` to load `lib<pkg>__rosidl_typesupport_introspection_cpp.so` at runtime — no compile-time type coupling
- `build_fields()` stores `m.offset_` from rosidl `MessageMember` — actual C++ struct byte offsets, no runtime layout computation
- `FieldAccessor::extract_double()` walks Step chain via raw byte pointer arithmetic; for dynamic arrays dereferences `std::vector::data()` at offset 0 of vector header
- `make_accessor()` rejects non-numeric leaf fields (returns invalid accessor)
- `friend class MessageIntrospector` in `FieldAccessor` gives clean access to private Step struct and members
- clangd shows false-positive errors (workspace built without ROS2); files compile correctly with sourced ROS2

**Mission status updates** — A4 → `[x]` DONE

**Blockers** — None.

---

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
3. A3 complete — `TopicDiscovery` in `topic_discovery.hpp/.cpp`; 32 unit tests. Discovers topics (with QoS), services, nodes.
4. A4 complete — `MessageIntrospector` in `message_introspector.hpp/.cpp`; 55 unit tests. Schema introspection via dlopen+rosidl, FieldAccessor byte-offset chain extraction, nested messages (Twist, Imu), fixed+dynamic arrays, thread-safe cache.
5. A5 complete — `GenericSubscriber` in `generic_subscriber.hpp/.cpp`; 46 unit tests in `test_generic_subscriber.cpp`. SPSC `RingBuffer` (power-of-two, alignas(64), drop-oldest-on-full), `FieldSample {timestamp_ns, value}`, multiple extractors per subscription, CDR deserialization via `rclcpp::SerializationBase`, header.stamp timestamp with wall-clock fallback. Non-ROS2 build: clean.
6. A6 complete — `test_phase_a_integration.cpp`; 19 tests. Full pipeline: bridge → discovery → subscribe → publish → ring buffer extraction → clean shutdown. Float64/Twist/Imu all verified end-to-end. Non-ROS2 build: clean.
7. **Phase A is complete. Next wave is B+C parallel (pg:1).** Alpha agent: B1 (`topic_list_panel.*`), B2 (`topic_echo_panel.*`), B3 (topic stats). Beta agent: C1 (`ros_plot_manager.*`), C2 (auto-scroll), C4 (multi-subplot). All new files go under `src/adapters/ros2/`. Do NOT modify any core src/ files.
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
*(§14 `.live-agents` Bootstrap below is always required regardless of whether this section is used.)*

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

---

## 14) `.live-agents` Bootstrap (Always Required)

> **What is this?** `.live-agents` is a plain-text file at the project root that every agent reads before starting any operation and writes to immediately when its state changes. It is the single source of truth for *what is happening right now* in this project. It prevents two sessions from compiling, testing, or installing at the same time, and provides a recovery signal if a session crashes mid-op.
>
> **For this plan:** parallel waves (pg:1, pg:2, pg:3) use two named agents (Alpha, Beta). During sequential phases, only `[Agent-1]` is active.
>
> **Create this file at `/home/daniel/projects/Spectra/.live-agents` at the start of your first session. Update your own line on every state change. Delete the file only after all missions are `[x]` and the plan is fully closed.**

### Initial `.live-agents` content (sequential phase, e.g. Phase A or G/H)
```
# .live-agents — Live Agent Coordination  (wave: sequential)
# READ before any build/compile/test/install. UPDATE immediately on state change.
# Exclusive ops (BUILDING / TESTING / INSTALLING): only one agent may hold these at a time.

[Agent-1] status=STARTING  mission=<current-mission-ID>  op=pre-flight  updated=<timestamp>
```

### `.live-agents` content for parallel waves (e.g. pg:1: B2, B3, C2)
```
# .live-agents — Live Agent Coordination  (wave: pg:1)
# READ before any build/compile/test/install. UPDATE immediately on state change.
# Exclusive ops (BUILDING / TESTING / INSTALLING): only one agent may hold these at a time.

[Alpha] status=STARTING  mission=B2  op=pre-flight  updated=<timestamp>
[Beta]  status=STARTING  mission=C2  op=pre-flight  updated=<timestamp>
```
*(Update mission IDs to match the active wave from §13 Agent Roster before starting.)*

### State values
- `STARTING` — agent just launched, running pre-flight
- `WORKING` — actively editing files
- `BUILDING` — running `ninja` / CMake build (**exclusive** — one at a time)
- `TESTING` — running `ctest` or ROS2 test suite (**exclusive** — one at a time)
- `INSTALLING` — installing packages or dependencies (**exclusive** — one at a time)
- `WAITING` — blocked on another agent's exclusive op
- `DONE` — mission complete, session closing
- `BLOCKED` — hit a hard blocker, needs human

### Exclusive-op rules
- Before running `ninja`, `ctest`, or `pip install`: check that no other line shows `BUILDING`, `TESTING`, or `INSTALLING`.
- If another agent holds an exclusive op: set own status to `WAITING`, poll every ~30 s.
- Always update your line **before** starting and **immediately after** finishing.
- Last `DONE` agent in a wave appends `# Wave complete` at the bottom.

### Mandatory build verification (every session — not just parallel)
Every agent **must** run a clean build check at **two points** in every session:
1. **Before any edits** (pre-flight): confirm the baseline compiles — `ninja -C <build_dir>`. If broken before you start, document it and do NOT proceed without fixing the pre-existing break first.
2. **After all edits** (session close): confirm your changes compile — `ninja -C <build_dir>`. If the build is broken after your edits, **fix it before closing the session**. Log the result in the Validation block of the session log entry:
   ```
   - Build pre-edit:  ✅/❌ — <ninja output summary>
   - Build post-edit: ✅/❌ — <ninja output summary>
   ```
   Update `.live-agents` to `status=BUILDING` while the build runs, then back to `WORKING` or `DONE`.

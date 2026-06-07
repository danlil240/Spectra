# Spectra ROS — Product Plan: World's First Spatiotemporal ROS Debug Workbench

> **Created:** 2026-06-07  
> **Horizon:** 6 months to category leadership, 18 months to default-tool status in target stacks  
> **Status:** Active product roadmap — supersedes feature-scatter; complements [`ROS_UI_FIX_PLAN.md`](ROS_UI_FIX_PLAN.md) (UI foundation ✅) and [`archive/SPECTRA_ROS_STUDIO_PLAN.md`](archive/SPECTRA_ROS_STUDIO_PLAN.md) (3D implementation audit).  
> **Binary:** `spectra-ros` · **Codebase:** `src/adapters/ros2/`

---

## 0. North Star

**Be the first tool in the world where a ROS developer goes from “I have a live robot or a bag” to “I see the signals *and* spatial context I need, on a shared timeline, in under 10 seconds — without installing rqt, PlotJuggler, and RViz separately.”**

We are not building “another ROS GUI.” We are defining a new category:

| Existing tools | What they own | What they cannot do |
|----------------|---------------|---------------------|
| **rqt** | Modular panels, echo, basic plots | Slow Qt plots; fragmented UX; no GPU; no unified bag timeline |
| **PlotJuggler** | Best-in-class time-series + bag replay | No TF/3D; no live ROS graph context in one session |
| **RViz2** | 3D displays, TF, robot context | No serious plotting; no bag-scrub sync with signals |
| **Foxglove** | Web, bags, layouts, collaboration | Not native GPU; latency; offline/air-gapped friction |

**Spectra ROS owns the intersection:** native Vulkan performance, **generic message introspection** (plot any numeric field without per-type plugins), **unified live + bag timeline**, **2D signals + 3D scene in one persisted session**.

---

## 1. Category Definition — “First in the World” Claims

These are falsifiable claims we will ship evidence for. Each maps to a phase gate.

| # | Claim | Proof artifact |
|---|-------|----------------|
| **C1** | Fastest path from launch to first plot for *any* introspectable ROS 2 message | Timed benchmark script + published median &lt;10 s on reference hardware |
| **C2** | First native app where bag scrubbing drives plots, echo, and 3D displays on one clock | Video + automated QA scenario `bag_spatiotemporal_sync` |
| **C3** | First ROS GUI with drag-any-field plotting via runtime introspection (no custom plot plugins) | Demo with 5 unrelated message types in one session |
| **C4** | First shareable session format encoding layout + plots + expressions + 3D displays + fixed frame | Versioned `.spectra-ros-session` committed in example repos |
| **C5** | First ROS workbench with GPU-rendered million-point clouds *and* linked multi-subplot time series in one window | `bench_ros3d` + plot stress scenario published in docs |
| **C6** | First headless bag-analysis export path (CSV + PNG) suitable for CI regression on robot logs | `spectra-ros-analyze` CLI + GitHub Action example |

We do **not** claim full RViz plugin ecosystem parity in Phase C. We claim **superior integrated debug** for defined workflows.

---

## 2. Target Users & Wedge Order

| Priority | Persona | Job-to-be-done | Wedge workflow |
|----------|---------|----------------|----------------|
| **P0** | Controls / estimation engineer | Tune filters, compare commanded vs measured | **Live tuning** — IMU/PID session, expressions, linked axes |
| **P0** | Robotics integrator | Post-incident bag analysis | **Bag post-mortem** — scrub timeline, plots + echo + logs |
| **P1** | Navigation engineer | Debug Nav2 / mobility stack | **Nav debug** — cmd_vel, odom, laser, path, TF in one preset |
| **P2** | Perception engineer | Validate sensor streams | **Sensor validation** — cloud/image rate, decay, frame sync |
| **P3** | CI / platform engineer | Regression on recorded logs | **Headless analyze** — CSV/PNG thresholds in pipeline |

**Wedge order:** P0 live tuning → P0 bag post-mortem → P1 nav debug → P2/P3.

---

## 3. Strategic Pillars (Non-Negotiables)

Every phase must strengthen at least one pillar.

### Pillar A — Zero-friction discovery
- Launch with sensible defaults; no “empty shell” first run.
- Topic Monitor populated within 2 s on a standard graph.
- `--topics`, `--bag`, `--layout`, `--session` CLI fully documented.

### Pillar B — Temporal truth
- One master clock: live `ros_time` or bag playhead.
- All panels that show time-varying data subscribe to that clock.
- Scrubbing never desyncs plots from echo or 3D snapshot.

### Pillar C — Generic depth
- `MessageIntrospector` remains the plotting backbone — no return to per-message plot plugins for standard numeric fields.
- Expression engine (`expression_engine.hpp`) is a first-class product feature, not a hidden panel.

### Pillar D — Persisted intent
- `RosSession` v2+ stores: subscriptions, expressions, layout, `imgui_layout`, displays, camera, fixed frame.
- Team-shareable presets live in git next to launch files.

### Pillar E — Native performance
- SPSC rings, GPU plot drain, Vulkan scene pass — measurable budgets (see §10).
- Degrade gracefully under load (decimation, drop counters visible in UI).

### Pillar F — Distribution trust
- Installable via documented path: binary release, Docker, and ROS 2 package (`ros2 run spectra_ros spectra_ros`).
- Works on Ubuntu 22.04/24.04 + Humble/Jazzy without hunting dependencies.

---

## 4. Three-Phase Roadmap Overview

```
Phase A (Weeks 1–4)   TRUST THE DEBUG COCKPIT
    └─ First plot <10s · presets · docs · QA gates

Phase B (Weeks 5–10)  BAG-FIRST TEMPORAL WORKBENCH
    └─ Unified timeline · expressions in default UX · CI export

Phase C (Weeks 11–18) SPATIOTEMPORAL NAV DEBUG
    └─ Nav2 reference stack · 3D+plots sync · perf hardening · category demos

Phase D (Months 6–18) CATEGORY LEADERSHIP (rolling)
    └─ Headless analyze · team presets marketplace · hardware perf leaderboards
```

| Phase | Theme | Primary metric | Ship signal |
|-------|--------|----------------|-------------|
| **A** | Trust | Median time-to-first-plot &lt;10 s | v0.3.0 ROS “developer ready” |
| **B** | Bags | Bag scrub lag &lt;50 ms perceived; session round-trip | Reference bag workflows in docs |
| **C** | Spatial | Nav2 demo video + 30 min workshop script | Conference / blog launch |
| **D** | Leadership | 1k+ GitHub stars OR 3 corporate adopters public | “Default tool” narrative |

---

## 5. Phase A — Trust the Debug Cockpit (Weeks 1–4)

### 5.1 Goal

A new ROS developer clones the repo, sources Humble, runs one command, and **believes** Spectra is real — before they ever touch 3D.

### 5.2 Deliverables

| ID | Deliverable | Owner files | Notes |
|----|-------------|-------------|-------|
| **A1** | **Time-to-first-plot benchmark** | `tests/qa/ros_qa_agent.cpp`, new `tests/qa/scenario_first_plot.cpp` | Automate: launch → expand topic → plot field → pixel/series assertion; record duration |
| **A2** | **Default-first-run layout** | `ros_app_shell.cpp` `apply_default_dock_layout()`, `main.cpp` | Topic Monitor + Plot Area + Echo visible; stats hint when nothing selected |
| **A3** | **Stack session presets (3)** | `sessions/presets/tuning.spectra-ros-session`, `bag_review.spectra-ros-session`, `bringup.spectra-ros-session` | Checked into repo; load via `--session` |
| **A4** | **CLI `--session` flag** | `ros_app_shell.hpp` `RosAppConfig`, `parse_args()` | Load preset at startup before first dock layout |
| **A5** | **Launch file packaging** | `examples/ros2_launch/*.launch.py`, new `CMakeLists.txt` install rules | `ros2 launch spectra tuning.launch.py` documented |
| **A6** | **Quick Start rewrite** | `docs/ros2-adapter.html`, root `README.md` ROS section | Lead with 10-second workflow, not panel inventory |
| **A7** | **Comparison matrix doc** | `docs/ros2-comparison.html` | vs rqt / PlotJuggler / RViz / Foxglove — honest |
| **A8** | **ROS QA design-review gate in CI** | `.github/workflows/ci.yml` | `spectra_ros_qa_agent --design-review` on ROS matrix job (lavapipe + xvfb) |
| **A9** | **Fix flaky ROS unit tests** | `tests/unit/test_topic_discovery.cpp`, bridge tests | Green parallel `ctest -R ros` |

### 5.3 Acceptance criteria (Phase A exit gate)

| Criterion | Pass condition |
|-----------|----------------|
| **C1 benchmark** | Median time-to-first-plot ≤10 s over 20 runs on CI reference VM |
| **Preset load** | `./spectra-ros --session sessions/presets/tuning.spectra-ros-session` opens with ≥2 plots configured |
| **Launch recipes** | 3 launch files run without edit on synthetic `/cmd_vel` publisher |
| **Regression** | `ctest -LE gpu -R ros` 100% pass (serial + parallel) |
| **Core Spectra** | `spectra-window` unchanged — command bar + canvas visible |
| **Design review** | `spectra_ros_qa_agent --design-review` ≥95% manifest pass |

### 5.4 Verification commands

```bash
# Build
cmake -B build -G Ninja -DSPECTRA_USE_ROS2=ON -DSPECTRA_ROS2_BAG=ON
cmake --build build -j$(nproc) --target spectra-ros spectra_ros_qa_agent

# Unit
ctest --test-dir build -R 'ros|topic_discovery' --output-on-failure

# Timed workflow (manual until A1 automated)
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{ linear: { x: 0.5 } }" --rate 10 &
/usr/bin/time -f '%e' ./build/spectra-ros --topics /cmd_vel

# QA
./build/tests/spectra_ros_qa_agent --design-review --output-dir /tmp/ros_qa_a
```

### 5.5 Out of scope (Phase A)

- New display types
- RViz config import
- Python bindings
- Web streaming

---

## 6. Phase B — Bag-First Temporal Workbench (Weeks 5–10)

### 6.1 Goal

**Own bag post-mortem.** A developer opens a `.db3` / `.mcap` and gets PlotJuggler-class timeline UX **plus** echo, logs, and session persistence — in one app.

### 6.2 Deliverables

| ID | Deliverable | Owner files | Notes |
|----|-------------|-------------|-------|
| **B1** | **Master clock bus** | New `ros_time_clock.hpp`, wire in `ros_app_shell.cpp`, `bag_player.cpp` | `RosWorkspaceState` gains `time_mode`, `playhead_time`, `is_playing`; panels subscribe |
| **B2** | **Plot sync to playhead** | `ros_plot_manager.cpp`, `bag_player.cpp` | When scrubbing: plots show window ending at playhead; optional “follow live” toggle |
| **B3** | **Echo sync to playhead** | `topic_echo_panel.cpp` | Show message nearest playhead timestamp, not only latest |
| **B4** | **Unified transport bar** | `ros_app_shell.cpp` or new `ui/transport_bar.cpp` | Play/pause, rate, step ±1 msg, time readout — always visible in bag mode |
| **B5** | **Expression editor in default preset** | `ui/expression_editor.cpp`, `bag_review` preset | Pre-built examples: `sqrt(x^2+y^2)` on odometry |
| **B6** | **Multi-series compare** | `subplot_manager.cpp`, `ros_session.hpp` | Overlay two fields or two bags (same field) with legend |
| **B7** | **Session diff / merge** | `ros_session.cpp` | Import subscriptions from another session without wiping layout |
| **B8** | **Reference bag corpus** | `tests/data/ros_bags/` (small synthetic bags) | IMU, cmd_vel, Nav2 snippet — for CI |
| **B9** | **Headless bag CSV export CLI** | New `src/adapters/ros2/tools/ros_bag_analyze.cpp` binary `spectra-ros-analyze` | `--bag X --fields /topic.field --csv out.csv` no GUI |
| **B10** | **Bag replay launch + docs** | `examples/ros2_launch/bag_replay.launch.py`, docs | End-to-end tutorial with downloadable sample bag |

### 6.3 Acceptance criteria (Phase B exit gate)

| Criterion | Pass condition |
|-----------|----------------|
| **C2 claim** | Scrub bag: plot cursor, echo content, and transport readout agree within 1 message period |
| **Bag open** | `./spectra-ros --bag tests/data/ros_bags/nav_snippet.db3` loads transport bar &lt;3 s |
| **Expression** | `bag_review` preset shows ≥1 expression plot after load |
| **Headless** | `spectra-ros-analyze --bag ... --csv out.csv` exits 0; CSV row count matches bag |
| **Session round-trip** | Save session → quit → load → same plots, expressions, layout, playhead rate |
| **Benchmark** | 10 Hz bag, 4 plots, 60 s recording: UI stays ≥30 FPS on reference GPU |

### 6.4 Verification commands

```bash
./build/spectra-ros --bag tests/data/ros_bags/imu_60s.db3 --session sessions/presets/bag_review.spectra-ros-session
./build/spectra-ros-analyze --bag tests/data/ros_bags/imu_60s.db3 \
    --fields /imu/data.linear_acceleration.z --csv /tmp/imu_z.csv
wc -l /tmp/imu_z.csv
```

### 6.5 Out of scope (Phase B)

- 3D displays following playhead (Phase C)
- Remote bag streaming
- MCAP write / record from GUI

---

## 7. Phase C — Spatiotemporal Nav Debug (Weeks 11–18)

### 7.1 Goal

**First integrated nav post-mortem:** plots of cmd_vel/odom/errors **synchronized** with laser, path, TF, and robot model on the bag timeline. This is the “world first” demo no other tool ships as a single native session.

### 7.2 Deliverables

| ID | Deliverable | Owner files | Notes |
|----|-------------|-------------|-------|
| **C1** | **3D sync to playhead** | `display/*_display.cpp`, `scene_manager.cpp`, `bag_player.cpp` | Displays render state at `playhead_time`; decay for clouds |
| **C2** | **Nav2 reference preset** | `sessions/presets/nav2_debug.spectra-ros-session`, `nav_dashboard.launch.py` | Plots + laser + path + TF + robot model |
| **C3** | **TurtleBot / Nav2 demo bag** | `tests/data/ros_bags/nav2_5min.db3`, docs tutorial | Downloadable; workshop script |
| **C4** | **Perf hardening** | `bench_ros3d.cpp`, display adapters | Point cloud 500k pts @ 10 Hz: ≥30 FPS; memory cap + UI drop counter |
| **C5** | **Robot link picking** | `scene_renderer.cpp`, `inspector_panel.cpp` | Click link → inspector shows joint state, frame |
| **C6** | **Image encoding switch** | `display/image_display.cpp` | jpeg/png/rgb8/bgr8 without restart |
| **C7** | **Stress QA scenario** | `tests/qa/ros_qa_agent.cpp` `scenario_nav_spatiotemporal` | Automated bag scrub + screenshot diff |
| **C8** | **Workshop kit** | `docs/workshops/nav-debug-30min.md` | Step-by-step for meetups |
| **C9** | **Launch video script** | marketing asset list | 3 min: bag open → scrub → see cmd_vel spike + robot pose jump |
| **C10** | **RosSession v2 migration** | `ros_session.cpp`, nlohmann/json per studio plan | Nested displays/camera; v1 import |

### 7.3 Acceptance criteria (Phase C exit gate)

| Criterion | Pass condition |
|-----------|----------------|
| **C2 + C5 claims** | Workshop script completable in ≤30 min by engineer new to Spectra |
| **Spatiotemporal sync** | Scrub to known event in nav bag: plot annotation + 3D pose match within 100 ms |
| **Perf** | `bench_ros3d` thresholds documented and CI-monitored (no regression &gt;10%) |
| **Picking** | Click robot link in demo bag → inspector shows correct joint name |
| **Preset** | `nav2_debug` session loads displays + plots without manual setup |
| **Tests** | +5 GPU-labeled integration tests; ROS QA scenario green |

### 7.4 Verification commands

```bash
./build/spectra-ros --bag tests/data/ros_bags/nav2_5min.db3 \
    --session sessions/presets/nav2_debug.spectra-ros-session --layout rviz-plot
./build/tests/bench_ros3d --benchmark_filter=PointCloud
./build/tests/spectra_ros_qa_agent --scenario nav_spatiotemporal --output-dir /tmp/ros_qa_c
```

### 7.5 Out of scope (Phase C)

- Full RViz plugin API compatibility
- Nav2 panel (costmap, planner introspection) — future Phase D
- Multi-robot / fleet view

---

## 8. Phase D — Category Leadership (Months 6–18, Rolling)

Prioritized backlog once A–C gates pass. Not sequential — pick by adoption signal.

| ID | Initiative | Rationale |
|----|------------|-----------|
| **D1** | **ROS 2 ament package + deb** | `ros2 run spectra_ros` — adoption multiplier |
| **D2** | **Docker `spectra/ros:humble`** | One-liner for teams |
| **D3** | **Preset gallery** | Community sessions for Unitree, PX4 ROS 2, MoveIt |
| **D4** | **Python `spectra_ros` module** | `analyze_bag()`, `plot_field()` for notebooks / CI |
| **D5** | **Live + bag diff mode** | Overlay golden run on live stream |
| **D6** | **Foxglove bridge export** | Import layouts from `.json` (read-only) — migration path |
| **D7** | **Costmap / Nav2 debug layer** | Own mobility wedge deeper |
| **D8** | **Hardware perf leaderboard** | Publish FPS/memory on Jetson Orin, NUC, desktop |
| **D9** | **Plugin SDK for displays** | C ABI for third-party displays after core is trusted |
| **D10** | **Enterprise support narrative** | SLA, air-gapped install docs |

---

## 9. Killer Workflows (Product Spec)

Each workflow is a **named product surface** with preset + launch + doc + QA scenario.

### W1 — Live IMU / PID tuning (Phase A)

```
ros2 launch spectra imu_monitor.launch.py
→ 4 plots live within 5 s
→ adjust time window from Plots menu
→ save session to repo
```

**Success:** Engineer prefers this over `rqt_plot` for a full tuning afternoon.

### W2 — Bag post-mortem (Phase B)

```
spectra-ros --bag crash.db3 --session bag_review
→ transport bar visible
→ scrub to anomaly
→ echo shows message at playhead
→ export CSV for report
```

**Success:** Faster than PlotJuggler for “what was `/diagnostics` saying at t=42s?”

### W3 — Nav stack debug (Phase C)

```
spectra-ros --bag nav2_5min.db3 --session nav2_debug
→ cmd_vel + odom plots linked
→ laser + path + robot in 3D
→ scrub: robot jumps, plots cursor follows
```

**Success:** Team stops opening RViz + PlotJuggler side by side for nav bugs.

### W4 — CI log regression (Phase B/D)

```
spectra-ros-analyze --bag ci_log.db3 --fields /motor/current --csv out.csv
→ script asserts max(current) < threshold
```

**Success:** Platform team adds to GitHub Actions.

---

## 10. Performance & Quality Budgets

| Surface | Budget | Measurement |
|---------|--------|-------------|
| Topic discovery refresh | ≤2 s to populate list | `TopicDiscovery` timer |
| Subscribe → first sample in plot | ≤500 ms after click | QA scenario |
| Plot drain | 4096 samples/poll, no UI stall &gt;16 ms | Tracy / frame timer |
| Bag scrub response | ≤50 ms to update plot window | Phase B gate |
| Point cloud display | 500k pts @ 10 Hz ≥30 FPS | `bench_ros3d` |
| Memory (30 min bag, 4 plots) | RSS growth &lt;20% over 10 min | qa-memory-agent |
| Session save | &lt;200 ms for typical session | unit test |

---

## 11. Distribution & Adoption Plan

### 11.1 Install paths (by phase)

| Phase | Channel |
|-------|---------|
| A | Build from source docs; binary artifact in GitHub Release (optional) |
| B | Docker image `ghcr.io/.../spectra-ros:humble` |
| C | ament package `spectra_ros` in repo + install docs |
| D | apt.robotsdream.dev or ROS build farm submission |

### 11.2 Documentation deliverables

| Doc | Phase |
|-----|-------|
| `docs/ros2-comparison.html` | A |
| `docs/ros2-workflows.html` (W1–W3) | A–C |
| `docs/workshops/nav-debug-30min.md` | C |
| `docs/ci/bag-analysis.md` | B |

### 11.3 Community signals

- **Phase A:** Humble quick-start GIF in README
- **Phase B:** Blog: “Replace PlotJuggler for bag review”
- **Phase C:** Video: spatiotemporal nav debug
- **Phase D:** Preset contributed by external lab

---

## 12. Competitive Moat (Defensibility)

| Moat | Why it compounds |
|------|------------------|
| **Generic introspection plotting** | Every new ROS message type works without Spectra code changes |
| **Unified session artifact** | Teams accumulate presets — switching cost rises |
| **GPU unified 2D+3D** | Competitors are siloed apps or web clients |
| **Native + air-gapped** | Wins defense, automotive offline benches |
| **Expression engine in GUI** | PlotJuggler has this; RViz does not — we bridge both worlds |

---

## 13. Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| PlotJuggler “good enough” for plots | High | Win on bag+echo+session integration, not plots alone |
| RViz ecosystem inertia | High | Nav preset + workshop; don’t claim full parity |
| ROS distro fragmentation | Medium | CI matrix Humble + Jazzy; Docker |
| GPU/Vulkan on headless CI | Medium | lavapipe + xvfb gates; software fallback documented |
| `ros_app_shell.cpp` size / complexity | Medium | Continue split (`_*_menus.cpp`, `_*_wiring.cpp`); code-simplifier skill |
| Bag format changes (mcap vs db3) | Low | rosbag2 abstraction already in `bag_player.hpp` |
| Performance on Jetson | Medium | Phase C bench on ARM; decimation defaults |

---

## 14. Metrics Dashboard

Track monthly in this file (§15 update protocol).

| Metric | Baseline (2026-06) | Phase A target | Phase C target |
|--------|-------------------|----------------|----------------|
| Median time-to-first-plot (s) | Unverified | ≤10 | ≤7 |
| ROS unit tests green | ~99% (1 flaky) | 100% | 100% |
| `spectra_ros_qa_agent` scenarios | design-review + first_plot ✅ | +first_plot | +nav_spatiotemporal |
| Reference presets in repo | 0 | 3 ✅ | 5 ✅ |
| Documented launch workflows | 3 | 3 polished | 5 + workshop ✅ |
| GitHub ROS-related issues closed | — | track | track |

---

## 15. Agent Update Protocol

When completing product-phase work:

1. Update **§15 Metrics** and phase deliverable status in this file.
2. Update [`ROS_UI_FIX_PLAN.md`](ROS_UI_FIX_PLAN.md) if UI architecture changes.
3. Run verification from the phase section before marking deliverable ✅.
4. Cross-link new presets in `docs/ros2-adapter.html`.

**Deliverable status legend:** ⬜ Not started · 🔄 In progress · ✅ Done · ⏭ Deferred

### Phase A tracker

| ID | Status |
|----|--------|
| A1 | ✅ |
| A2 | ✅ |
| A3 | ✅ |
| A4 | ✅ |
| A5 | ✅ |
| A6 | ✅ |
| A7 | ✅ |
| A8 | ✅ |
| A9 | ✅ |

### Phase B tracker

| ID | Status |
|----|--------|
| B1 | ✅ |
| B2 | ✅ |
| B3 | ✅ |
| B4 | ✅ |
| B5 | ✅ |
| B6 | ✅ |
| B7 | ✅ |
| B8 | ✅ |
| B9 | ✅ |
| B10 | ✅ |

### Phase C tracker

| ID | Status |
|----|--------|
| C1 | ✅ 3D sync to playhead (`bag_display_sync`, shell wiring) |
| C2 | ✅ `nav2_debug.spectra-ros-session`, `nav2_debug.launch.py` |
| C3 | ✅ `nav2_debug/` reference bag + workshop doc |
| C4 | ✅ `bench_ros3d` 500k budgets, drop counter, `docs/ci/ros3d-perf.md` |
| C5 | ✅ Robot link pick → inspector joint/link metadata |
| C6 | ✅ Image jpeg/png + encoding override without resubscribe |
| C7 | ✅ `scenario_nav_spatiotemporal` in `spectra_ros_qa_agent` |
| C8 | ✅ `docs/workshops/nav-debug-30min.md` |
| C9 | ✅ `docs/marketing/nav-debug-video-script.md` |
| C10 | ✅ RosSession v2 + v1 import (`SESSION_FORMAT_VERSION=2`) |

---

## 16. Related Documents

| Document | Role |
|----------|------|
| [`ROS_UI_FIX_PLAN.md`](ROS_UI_FIX_PLAN.md) | UI foundation — complete; maintenance reference |
| [`archive/SPECTRA_ROS_STUDIO_PLAN.md`](archive/SPECTRA_ROS_STUDIO_PLAN.md) | 3D display implementation audit |
| [`docs/ros2-adapter.html`](../docs/ros2-adapter.html) | User-facing feature docs |
| [`examples/ros2_launch/`](../examples/ros2_launch/) | Launch recipes |
| `.github/agents/QA_ROS.agent.md` | ROS QA agent scope |
| `skills/qa-ros-performance-agent/` | Performance validation skill |

---

## 17. Immediate Next Actions (post Phase C)

Phases **A–C are complete**. Next product work moves to **Phase D** (distribution,
preset gallery, headless analyze adoption).

1. **D1** — ament package + `ros2 run spectra_ros` install path.
2. **D2** — Docker `spectra/ros:humble` image with reference bags baked in.
3. **D4** — Python `spectra_ros.analyze_bag()` wrapper for notebooks.
4. Record Jetson `bench_ros3d` baseline for hardware leaderboard (**D8**).
5. Tag **`ros-v0.4.0-nav-debug`** after Phase C exit gate verification on CI.

**Milestones shipped:** `ros-v0.3.0-trust` (Phase A), bag workbench (Phase B), nav debug (Phase C).

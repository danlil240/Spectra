---
name: qa-ros-performance-agent
description: Run spectra-ros stability and workflow QA end-to-end with `spectra_ros_qa_agent`, including deterministic scenario execution, report triage, ROS subsystem diagnosis, and verification of fixes. Use when asked to test spectra-ros, investigate `qa_report` findings, reproduce ROS UI/dataflow regressions, or update ROS QA tracking docs.
---

# QA ROS Performance Agent

Drive reproducible spectra-ros QA sessions focused on discovery, plotting, session persistence, diagnostics, TF, parameters, services, and bag playback, then convert findings into verified fixes and updated ROS QA notes.
When requested, also run the ROS shell design-review path to capture named screenshots, validate key theme contrast, and verify layout/chrome presentation.

---

## Required Context

Before starting any task, read these living documents:
- `plans/SPECTRA_ROS_STUDIO_PLAN.md` - current ROS architecture, roadmap, and validation goals
- `plans/ROS_UI_FIX_PLAN.md` - known shell/layout issues and intended behavior
- `skills/qa-ros-performance-agent/REPORT.md` - rolling ROS QA run log

Also update these when findings touch shared Spectra code or the generic QA backlog:
- `plans/QA_results.md`
- `plans/QA_update.md`

For command details, triage order, and file maps, read:
- `skills/qa-ros-performance-agent/references/qa-ros-performance-reference.md`

---

## Workflow

### 1. Build

```bash
cmake -S . -B build-ros2 -G Ninja -DSPECTRA_USE_ROS2=ON -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build-ros2 --target spectra_ros_qa_agent -j$(nproc)
```

### 2. Inspect scenarios

```bash
./build-ros2/tests/spectra_ros_qa_agent --list-scenarios
```

### 3. Run (required - at least once per task)

```bash
# Deterministic baseline
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed 42 --duration 120 \
  --output-dir /tmp/spectra_ros_qa

# Randomized follow-up
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed $(date +%s) --duration 120 \
  --output-dir /tmp/spectra_ros_qa_random

# Optional ROS design review
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed 42 --duration 120 \
  --design-review --output-dir /tmp/spectra_ros_qa_design
```

### 4. Triage findings

1. Check exit code first: `0` means no failed scenarios, `1` means at least one scenario failed or the agent could not initialize.
2. Read `qa_report.txt` summary before touching code.
3. Confirm scenario coverage. A wall-clock warning can stop the run early without turning the exit code non-zero.
4. Prioritize failed scenarios, then `ERROR`/`CRITICAL` issues, then warnings.
5. Review frame stats and RSS delta in context of the scenario that produced them.
6. Use `qa_report.json` and per-scenario screenshots for diffing and verification evidence.

### 5. Reproduce deterministically

- Re-run with the exact same `--seed`, `--duration`, and `--scenario` when isolating a failure.
- Prefer single-scenario reruns once the failing area is known.
- If initialization or discovery fails inside a sandbox/container, verify on a host environment before assuming a product bug.
- For memory or lifetime bugs, build an ASan variant before editing broad areas:

```bash
cmake -S . -B build-ros2-asan -G Ninja -DSPECTRA_USE_ROS2=ON \
  -DSPECTRA_BUILD_QA_AGENT=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=undefined"
cmake --build build-ros2-asan --target spectra_ros_qa_agent -j$(nproc)
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2-asan/tests/spectra_ros_qa_agent --seed 42 --duration 120 \
  --output-dir /tmp/spectra_ros_qa_asan
```

### 6. Apply minimal, targeted fixes

- Localize the failing subsystem using the issue-to-file map in the reference file.
- Preserve ROS distro compatibility; a graceful unsupported-path result is different from a crash.
- Avoid papering over DDS/discovery failures with retries unless the root cause is understood.
- Keep shell/layout fixes separate from message-pipeline fixes unless they are causally linked.

### 7. Verify

```bash
cmake --build build-ros2 --target spectra_ros_qa_agent -j$(nproc)

# Repro seed or targeted scenario
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed <repro-seed> \
  --scenario <scenario-name> --duration 120 \
  --output-dir /tmp/spectra_ros_qa_after

# Broader regression pass
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed $(date +%s) --duration 120 \
  --output-dir /tmp/spectra_ros_qa_regression

ctest --test-dir build-ros2 --output-on-failure
```

### 8. Update living documents

- `skills/qa-ros-performance-agent/REPORT.md` - append the run, commands, exit codes, metrics, findings, and fixes
- `plans/SPECTRA_ROS_STUDIO_PLAN.md` - update roadmap or validation status when the result changes product direction
- `plans/ROS_UI_FIX_PLAN.md` - update status of shell/layout bugs or acceptance criteria
- `plans/QA_results.md` - update shared-engine bugs exposed by ROS QA
- `plans/QA_update.md` - update missing ROS QA harness capability or workflow gaps

---

## CLI Reference

```
--seed <N>           RNG seed / unique suffix
--duration <sec>     Max wall-clock runtime (default: 120)
--scenario <name>    Run one named scenario
--output-dir <path>  Report and screenshot directory
--design-review      Run the named ROS-shell design review scenario and emit a manifest
--list-scenarios     Print scenario list and exit
```

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | No failed scenarios |
| `1` | At least one failed scenario, or agent initialization failed |

---

## Scenario Coverage Map

| Scenario | Primary validation target |
|---|---|
| `boot_and_layout` | Shell startup, panel creation, layout presets, nav rail clamping |
| `live_topic_monitoring` | Topic discovery, echo, stats, live plots, subplot growth |
| `session_roundtrip` | Save/load of subscriptions, panel visibility, and shell state |
| `node_graph_and_logs` | ROS log capture and node graph refresh/snapshot integrity |
| `diagnostics_and_tf` | Diagnostic status ingestion, TF tree population, transform lookup |
| `parameters_and_services` | Parameter discovery/editing, preset save/load, service schema/call flow |
| `bag_playback` | Synthetic rosbag open, playback injection, activity bands, bag metadata |
| `design_review` | Theme contrast, ROS shell layout/chrome captures, and named screenshot manifest |

---

## Review Thresholds

| Metric | Threshold |
|---|---|
| Scenario coverage | All requested scenarios ran |
| Failed scenario count | `0` |
| Average frame time | Review if consistently `> 20 ms` |
| Max frame spike | Review if repeated or `> 100 ms` outside startup/open/load moments |
| RSS delta | Review if `> 150 MB` on the default run |
| Bag playback status | Pass or explicit skip when `SPECTRA_ROS2_BAG=OFF` |

---

## Interpretation Rules

- `exit 0` is not enough. Confirm the report ran every intended scenario and did not stop early on wall-clock limits.
- Discovery failures in restricted environments can come from DDS/network/shared-memory restrictions rather than product logic. Reproduce on a normal host before changing code.
- `bag_playback` skipped because `SPECTRA_ROS2_BAG=OFF` is expected and should not be treated as a regression.
- Service-caller failure with an explicit `"Iron or later"` message is acceptable when parameter editing succeeded and the UI handled the limitation gracefully.
- Session roundtrip failures usually localize to `ros_session.*`, `ros_app_shell.*`, or subplot/session restore code, not topic discovery.
- Log-viewer success with node-graph failure usually points to graph/discovery refresh code, not global ROS node startup.
- Diagnostics passing while TF fails usually points to TF panel/QoS/update logic rather than generic message transport.

---

## Scenario-Specific Analysis

| Scenario | What to inspect first | Common root-cause area |
|---|---|---|
| `boot_and_layout` | Missing panels, wrong preset visibility, nav rail bounds | `ros_app_shell.cpp`, layout wiring |
| `live_topic_monitoring` | Missing topics, echo count `0`, plot add failures | `topic_discovery.cpp`, `generic_subscriber.cpp`, `subplot_manager.cpp` |
| `session_roundtrip` | Lost subscriptions, visibility mismatch, wrong nav rail state | `ros_session.cpp`, `ros_app_shell.cpp` |
| `node_graph_and_logs` | Missing helper node or `/qa/float`, absent log text | `ui/node_graph_panel.cpp`, `ros_log_viewer.cpp` |
| `diagnostics_and_tf` | No warn/error counts, missing frames, failed lookup | `ui/diagnostics_panel.cpp`, `ui/tf_tree_panel.cpp` |
| `parameters_and_services` | Params not loaded, preset not applied, schema/call failure | `ui/param_editor_panel.cpp`, `service_caller.cpp` |
| `bag_playback` | Bag open failure, no injected samples, empty activity bands | `bag_player.cpp`, `ui/bag_info_panel.cpp`, `ui/bag_playback_panel.cpp` |

---

## Self-Update Protocol

- If the same ROS failure mode appears twice and triage needed a new heuristic, update this skill or the reference file in the same session.
- Keep updates small and concrete: add one command, one interpretation rule, one file mapping, or one scenario note.
- Record every real QA run in `skills/qa-ros-performance-agent/REPORT.md`.

## Spectra MCP Server

Use the MCP server to verify that the Spectra rendering layer is healthy during ROS QA sessions, independently of the ROS QA agent harness.

### Start/restart procedure

**Always kill existing Spectra instances before launching a new one.**

```bash
pkill -f spectra || true
pkill -f spectra-backend || true
sleep 0.5
./build-ros2/app/spectra &
sleep 1
curl http://127.0.0.1:8765/   # health check
```

### ROS QA-relevant MCP commands

```bash
# Confirm app is alive and responsive
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_state","arguments":{}}}'

# Capture screenshot of the ROS shell layout for design review
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"capture_window","arguments":{"path":"/tmp/ros_shell_snap.png"}}}'

# Wait frames and capture inline for immediate inspection
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"wait_frames","arguments":{"count":10}}}'

curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_screenshot_base64","arguments":{}}}'

# Get window size (verify layout dimensions are correct)
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"get_window_size","arguments":{}}}'
```

MCP env vars: `SPECTRA_MCP_PORT` (default `8765`), `SPECTRA_MCP_BIND` (default `127.0.0.1`).

---

## Mandatory Session Backlog

At the end of each ROS QA task, decide whether one of these needs an update:
- command cookbook for a newly useful repro or verification command
- interpretation rules for a repeated failure signature
- issue-to-file map for a subsystem that was harder to localize than expected
- workflow notes for distro-specific or bag-feature-specific behavior

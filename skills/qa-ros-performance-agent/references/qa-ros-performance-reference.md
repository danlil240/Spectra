# QA ROS Performance Reference

## Command Cookbook

Build the ROS QA binary:

```bash
cmake -S . -B build-ros2 -G Ninja -DSPECTRA_USE_ROS2=ON -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build-ros2 --target spectra_ros_qa_agent -j$(nproc)
```

List scenarios:

```bash
./build-ros2/tests/spectra_ros_qa_agent --list-scenarios
```

Run deterministic baseline:

```bash
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed 42 --duration 120 \
  --output-dir /tmp/spectra_ros_qa
```

Run randomized follow-up:

```bash
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed $(date +%s) --duration 120 \
  --output-dir /tmp/spectra_ros_qa_random
```

Run one target scenario:

```bash
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed 42 \
  --scenario diagnostics_and_tf --duration 120 \
  --output-dir /tmp/spectra_ros_qa_diag
```

Run ASan follow-up:

```bash
cmake -S . -B build-ros2-asan -G Ninja -DSPECTRA_USE_ROS2=ON \
  -DSPECTRA_BUILD_QA_AGENT=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=undefined"
cmake --build build-ros2-asan --target spectra_ros_qa_agent -j$(nproc)
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2-asan/tests/spectra_ros_qa_agent --seed 42 --duration 120 \
  --output-dir /tmp/spectra_ros_qa_asan
```

## CLI Quick Reference

- `--seed <N>`: Set deterministic seed.
- `--duration <sec>`: Set max wall-clock runtime.
- `--scenario <name>`: Run one named scenario.
- `--output-dir <path>`: Choose report and screenshot directory.
- `--list-scenarios`: Print scenarios and exit.

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | No failed scenarios |
| `1` | At least one failed scenario, or initialization failed |

## Report Triage Order

1. Read `qa_report.txt` summary and scenario counts.
2. Confirm every expected scenario actually ran.
3. Prioritize failed scenarios.
4. Inspect `Issues:` for `ERROR` and `CRITICAL`.
5. Review frame stats and RSS delta.
6. Open per-scenario screenshots when UI state matters.
7. Use `qa_report.json` for diffing or scripting.

## Interpretation Rules

- Treat partial coverage as a failed QA session even if the process returned `0`.
- If the app never initializes, inspect environment constraints before changing product code.
- `bag_playback` skip is acceptable when the binary was built without `SPECTRA_ROS2_BAG`.
- `parameters_and_services` can still be a valid pass when parameter editing succeeds and the service path reports a handled distro limitation.
- Missing plots with healthy discovery usually points to field/type resolution or subplot wiring, not DDS.

## Scenario Coverage Map

| Scenario | Primary target |
|---|---|
| `boot_and_layout` | Startup, shell wiring, layout presets |
| `live_topic_monitoring` | Discovery, echo, stats, plotting |
| `session_roundtrip` | Session persistence and restore |
| `node_graph_and_logs` | Graph refresh and ROS log capture |
| `diagnostics_and_tf` | Diagnostics model and TF lookup |
| `parameters_and_services` | Parameter editor and service caller |
| `bag_playback` | Bag open, playback, injected samples |

## Issue-To-File Map

| Issue type | Primary file |
|---|---|
| Scenario harness and report generation | `tests/qa/ros_qa_agent.cpp` |
| Shell orchestration and layout presets | `src/adapters/ros2/ros_app_shell.cpp` |
| Topic discovery and graph source data | `src/adapters/ros2/topic_discovery.cpp` |
| Live subscription and field extraction | `src/adapters/ros2/generic_subscriber.cpp` |
| Plot injection and subplot growth | `src/adapters/ros2/ros_plot_manager.cpp`, `src/adapters/ros2/subplot_manager.cpp` |
| Message schema/introspection issues | `src/adapters/ros2/message_introspector.cpp` |
| Topic monitor, echo, stats UI | `src/adapters/ros2/ui/topic_list_panel.cpp`, `src/adapters/ros2/ui/topic_echo_panel.cpp`, `src/adapters/ros2/ui/topic_stats_overlay.cpp` |
| Session save/load and MRU state | `src/adapters/ros2/ros_session.cpp` |
| ROS log capture | `src/adapters/ros2/ros_log_viewer.cpp`, `src/adapters/ros2/ui/log_viewer_panel.cpp` |
| Node graph rendering | `src/adapters/ros2/ui/node_graph_panel.cpp` |
| Diagnostics ingestion | `src/adapters/ros2/ui/diagnostics_panel.cpp` |
| TF tree population and lookup | `src/adapters/ros2/ui/tf_tree_panel.cpp` |
| Parameter editing and preset YAML | `src/adapters/ros2/ui/param_editor_panel.cpp` |
| Service discovery/schema/call flow | `src/adapters/ros2/service_caller.cpp`, `src/adapters/ros2/ui/service_caller_panel.cpp` |
| Bag open/playback metadata and activity | `src/adapters/ros2/bag_player.cpp`, `src/adapters/ros2/ui/bag_info_panel.cpp`, `src/adapters/ros2/ui/bag_playback_panel.cpp` |

## Verification Checklist

1. Run at least one ROS QA command in the task.
2. Re-run the exact failing scenario/seed after the fix.
3. Run a broader regression pass with a new seed.
4. Rebuild and run `ctest --test-dir build-ros2 --output-on-failure` when feasible.
5. Append the session to `skills/qa-ros-performance-agent/REPORT.md`.
6. Update `plans/SPECTRA_ROS_STUDIO_PLAN.md`, `plans/ROS_UI_FIX_PLAN.md`, `plans/QA_results.md`, or `plans/QA_update.md` when the result changes the documented status.

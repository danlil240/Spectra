# QA Performance Reference

## Command Cookbook

Build QA binary:

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

Run deterministic baseline:

```bash
./build/tests/spectra_qa_agent --seed 42 --duration 120 --output-dir /tmp/spectra_qa
```

Run randomized pass:

```bash
./build/tests/spectra_qa_agent --duration 60 --output-dir /tmp/spectra_qa
```

Run scenarios only:

```bash
./build/tests/spectra_qa_agent --seed 42 --no-fuzz --output-dir /tmp/spectra_qa
```

Run fuzzing only:

```bash
./build/tests/spectra_qa_agent --seed 42 --no-scenarios --fuzz-frames 5000 --output-dir /tmp/spectra_qa
```

Run one target scenario:

```bash
./build/tests/spectra_qa_agent --scenario massive_datasets --no-fuzz --output-dir /tmp/spectra_qa
```

List scenarios:

```bash
./build/tests/spectra_qa_agent --list-scenarios
```

Run sanitizer follow-up for crash diagnostics:

```bash
cmake -B build-asan -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON -DCMAKE_CXX_FLAGS="-fsanitize=address"
cmake --build build-asan -j$(nproc)
./build-asan/tests/spectra_qa_agent --seed 42 --duration 120 --output-dir /tmp/spectra_qa_asan
```

## CLI Quick Reference

- `--seed <N>`: Set deterministic RNG seed.
- `--duration <sec>`: Set max wall-clock runtime.
- `--scenario <name>`: Run one named scenario.
- `--fuzz-frames <N>`: Set fuzz frame count.
- `--output-dir <path>`: Choose report/screenshot directory.
- `--no-fuzz`: Skip fuzzing phase.
- `--no-scenarios`: Skip scenario phase.
- `--list-scenarios`: Print scenarios and exit.

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | No `ERROR` or `CRITICAL` findings |
| `1` | At least one `ERROR` or `CRITICAL` finding |
| `2` | Crash (seed printed to stderr) |

## Report Triage Order

1. Read `qa_report.txt` summary and severity counts.
2. Prioritize `CRITICAL` then `ERROR` items.
3. Inspect frame metrics (mean, p99, max).
4. Inspect memory growth against baseline.
5. Correlate anomalies with scenario/fuzz phase.
6. Use `qa_report.json` for machine-readable diffing.

## Interpretation Rules

- Investigate frame spikes above 50 ms as likely real stalls.
- Treat warnings based on `> 3x EMA` carefully during startup; VSync-locked ~16 ms frames can be false positives when early EMA is very low.
- Investigate RSS growth above 100 MB unless expected by explicit large datasets.
- Reproduce every crash with the exact seed before changing code.

## Scenario Coverage Map

| Scenario | Primary stress target |
|---|---|
| `rapid_figure_lifecycle` | Figure creation/switch/close stability |
| `massive_datasets` | Large dataset ingestion and render pressure |
| `undo_redo_stress` | Command history correctness and state churn |
| `animation_stress` | Timeline play/pause churn |
| `input_storm` | Input handling under heavy event volume |
| `command_exhaustion` | Command registry coverage and sequencing |
| `series_mixing` | Series add/remove/visibility transitions |
| `mode_switching` | 2D/3D mode transition robustness |
| `stress_docking` | Dock layout switching and split stability |
| `resize_stress` | Frame pumping during repeated resize-like load |
| `3d_zoom_then_rotate` | 3D camera zoom then orbit rotation correctness |
| `window_resize_glfw` | Real GLFW window resize with extreme aspect ratios |
| `multi_window_lifecycle` | Create/destroy/move figures across multiple OS windows |
| `tab_drag_between_windows` | Detach tabs into new windows, cross-window figure moves |
| `window_drag_stress` | Rapid window repositioning + resize combos |

## Issue-To-File Map

| Issue type | Primary file |
|---|---|
| Scenario and fuzz behavior | `tests/qa/qa_agent.cpp` |
| Frame-step runtime lifecycle | `src/ui/app/app_step.cpp` |
| UI runtime entry points used by QA | `src/ui/app/window_ui_context.hpp` |
| Dock/split behavior under stress | `src/ui/docking/split_view.cpp` |
| Figure lifecycle/tab switching | `src/ui/figures/figure_manager.cpp` |
| Undo/redo stress findings | `src/ui/commands/undo_manager.cpp` |
| Timeline stress findings | `src/ui/animation/timeline_editor.cpp` |
| Render-time anomalies | `src/render/renderer.cpp` |

## Verification Checklist

1. Run at least one QA command in the task.
2. Re-run with original repro seed after fixes.
3. Run another seed to check regressions.
4. Rebuild and execute `ctest --test-dir build --output-on-failure`.
5. Update `plans/QA_results.md` and/or `plans/QA_update.md` with evidence and status.

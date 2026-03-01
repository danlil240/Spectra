---
name: qa-api-agent
description: Test Spectra's Python API, IPC protocol, and C++ easy-API for correctness, backwards compatibility, and crash-safety. Use when adding new Python bindings, debugging IPC message handling, verifying easy-API examples still work, or running the Python test suite.
---

# QA API Agent

Validate Spectra's public-facing APIs: the Python IPC bindings, the C++ easy-API (`include/spectra/`), and the daemon/agent protocol. This agent owns the contract layer — if a user's Python script or C++ example breaks, this agent finds why.

---

## Required Context

Before starting any task, read:
- `python/pyproject.toml` — Python package config and dependencies
- `plans/PYTHON_IPC_ARCHITECTURE.md` — IPC protocol architecture
- `plans/QA_results.md` — open API bugs

---

## Workflow

### 1. Build with daemon support

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

### 2. Run Python unit tests

```bash
cd python
python -m pytest tests/ -v --tb=short

# Run specific test files
python -m pytest tests/test_codec.py -v
python -m pytest tests/test_cross_codec.py -v
```

### 3. Run C++ API unit tests

```bash
ctest --test-dir build --output-on-failure -R "test_easy_api|test_ipc|test_session_graph|test_python_ipc|test_process_manager|test_cross_codec"
```

### 4. Smoke-test Python examples

```bash
# Start daemon in background, run each example with timeout
timeout 10 python python/examples/basic_line.py || echo "FAILED: basic_line"
timeout 10 python python/examples/easy_3d.py || echo "FAILED: easy_3d"
timeout 10 python python/examples/easy_live_dashboard.py || echo "FAILED: easy_live_dashboard"
```

### 5. Smoke-test C++ examples

```bash
# Build examples
cmake -B build -G Ninja -DSPECTRA_BUILD_EXAMPLES=ON
cmake --build build --target spectra_examples -j$(nproc)

# Run each example with timeout
for ex in basic_line animated_scatter; do
    timeout 5 ./build/examples/$ex && echo "PASS: $ex" || echo "FAIL: $ex"
done
```

### 6. Verify IPC serialization roundtrips

Key IPC paths to test:
- `PlotCommand` encode/decode (via `test_codec.cpp`)
- Cross-language codec (C++ encode → Python decode, vice versa) via `test_cross_codec.cpp`
- Session graph construction and figure routing via `test_session_graph.cpp`
- Process manager spawn/kill via `test_process_manager.cpp`

```bash
ctest --test-dir build --output-on-failure -R "test_codec|test_cross_codec|test_session|test_process"
```

### 7. Test new API surface from recent commits

| Feature | Test file | What to verify |
|---|---|---|
| `file.save_figure` / `file.load_figure` | `tests/unit/test_workspace_v3.cpp` | Roundtrip over IPC |
| `series.copy/paste` via Python | `python/tests/` | Clipboard commands reachable from Python |
| `App::window_manager()` accessor | `tests/unit/test_window_manager.cpp` | Accessor returns non-null after `init_runtime()` |
| `FigureSerializer` | `tests/unit/test_workspace_v3.cpp` | Direct save/load API |
| `SeriesClipboard` | `tests/unit/test_series_clipboard.cpp` | All 6 clipboard operations |

### 8. Check backwards compatibility

Verify existing Python examples and C++ examples still compile and run without modification:
- `python/examples/*.py` — all 7 examples
- `examples/*.cpp` — all examples in `examples/CMakeLists.txt`

If a public API changed, update `python/spectra/__init__.py` and add a deprecation note.

### 9. Verify and document

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
cd python && python -m pytest tests/ -v
```

Update `plans/QA_results.md` with any API contract violations found.

---

## API Coverage Map

### C++ Public API (`include/spectra/`)

| Header | Key API | Test coverage |
|---|---|---|
| `app.hpp` | `App::step()`, `init_runtime()`, `shutdown_runtime()`, `ui_context()`, `window_manager()` | `test_easy_api.cpp` |
| `figure.hpp` | `Figure::subplot()`, `save_png()`, `save_svg()` | `test_easy_api.cpp` |
| `axes.hpp` | `Axes::line()`, `scatter()`, `title()`, `xlabel()`, `ylabel()`, `x_limits()` | `test_easy_api.cpp` |
| `series.hpp` | `LineSeries::set_y()`, `label()`, `color()` | `test_series_data.cpp` |
| `series3d.hpp` | `LineSeries3D`, `ScatterSeries3D`, `SurfaceSeries3D` | `test_series3d.cpp` |
| `export.hpp` | `ImageExporter::write_png()`, `SvgExporter::write_svg()` | `test_svg_export.cpp` |
| `animator.hpp` | `Animator` frame callbacks | `test_animation_controller.cpp` |

### Python API (`python/spectra/`)

| Module | Key API | Test |
|---|---|---|
| `__init__.py` | `figure()`, `show()`, `line()`, `scatter()` | `python/examples/` smoke |
| `_axes.py` | Axes builder methods | `python/tests/` |
| `_animation.py` | Animation builder | `python/tests/` |

### IPC Protocol

| Component | File | Test |
|---|---|---|
| Codec encode/decode | `src/daemon/` | `test_codec.cpp` |
| Cross-language roundtrip | `src/daemon/` | `test_cross_codec.cpp` |
| Session graph routing | `src/daemon/` | `test_session_graph.cpp` |
| Python IPC client | `python/spectra/` | `test_python_ipc.cpp` |

---

## Issue-to-File Map

| Issue type | Primary file |
|---|---|
| Python API surface | `python/spectra/__init__.py` |
| Python IPC client | `python/spectra/_ipc.py` (or equivalent) |
| C++ easy API | `include/spectra/app.hpp`, `figure.hpp`, `axes.hpp` |
| IPC codec encode/decode | `src/daemon/figure_model.cpp` + `client_router.hpp` |
| Session graph construction | `src/daemon/` |
| Process manager spawn/kill | `src/agent/main.cpp` |
| Figure serialization (save/load) | `src/ui/workspace/figure_serializer.cpp` |
| Series clipboard public API | `src/ui/commands/series_clipboard.hpp` |

---

## Performance Targets

| Metric | Target |
|---|---|
| Python unit tests | 100% pass |
| C++ API unit tests | 100% pass |
| IPC roundtrip latency | < 5ms per command |
| Python example smoke tests | All run without crash within timeout |
| API backwards compatibility | Zero breaking changes without deprecation notice |

---

## Self-Update Protocol

The agent **may** update this file when it is 100% certain a change is correct. Never speculate — only update after C++ unit tests + Python tests both pass.

### Permitted self-updates

| Section | When to update | What to write |
|---|---|---|
| **API Coverage Map** (C++ Public API table) | A new public header/method is confirmed working | Add the header, key API, test coverage row |
| **API Coverage Map** (Python API table) | A new Python module or method is added | Add the module + key API row |
| **Issue-to-File Map** | A new API file is identified during a session | Add the file + issue type row |
| **New features table** (step 7) | A new feature's test status changes | Update Status column |
| **Performance Targets** | A target is officially revised with evidence | Update the value |

### Forbidden self-updates

- Never change **Workflow steps** without human approval.
- Never change **Guardrails** without human approval.
- Never mark a feature as ✅ without both C++ and Python test confirmation.
- Never remove an existing entry — mark it `~~deprecated~~` with a date instead.
- Never update if `test_cross_codec.cpp` failed.

### Self-update procedure

1. Run `ctest` (all API tests) and `python -m pytest tests/`, confirm all pass.
2. Identify what is stale or missing in this file.
3. Apply only the update types listed in Permitted above.
4. Append a one-line entry to `REPORT.md` under `## Self-Update Log`: date, section changed, reason.

---

## Mandatory Session Self-Improvement

**This rule is non-negotiable: every session must produce exactly one improvement to this agent's detection capabilities, regardless of whether bugs were found.**

There is no such thing as "nothing to improve." If the session found no bugs, that is a signal the agent's detection is too weak — not that the API is perfect. The agent must then make itself better so the next session finds something.

### Required format (append to REPORT.md every session)

```
## Self-Improvement — YYYY-MM-DD
Improvement: <one sentence describing what was added/changed>
Motivation: <why the previous version would miss or underreport this>
Change: <file(s) edited OR new check described in this SKILL.md>
Next gap: <one sentence describing the next blind spot to tackle next session>
```

### How to pick an improvement

1. **If bugs were found:** Turn the most surprising finding into a new test, a new smoke test command, or a new row in the API Coverage Map. Ask: "What automated check would have caught this immediately?"
2. **If no bugs were found:** The detection is too weak. Pick from the Improvement Backlog below, implement it, and document the result.

### Improvement Backlog (consume one per session, add new ones as discovered)

| ID | Improvement | How to implement |
|---|---|---|
| API-I1 | Test every Python `__init__.py` export is actually importable (not just listed in `__all__`) | Add pytest that does `from spectra import X` for each `__all__` entry; catch `ImportError` |
| API-I2 | Verify IPC roundtrip with NaN and ±Inf float values | Add codec test: encode array containing `NaN`, `-Inf`, `+Inf`; decode and assert values preserved |
| API-I3 | Test `Series.append()` with mismatched x/y lengths raises a clear error (not silent truncation) | Add test asserting `ValueError` or equivalent when `len(x) != len(y)` |
| API-I4 | Smoke-test all Python examples with `--check` / dry-run mode to catch import errors without a live daemon | Add `python -c "import spectra; ..."` pre-flight check for each example file's imports |
| API-I5 | Check that C++ easy-API `render()` handles empty x/y vectors without crashing | Add `test_easy_embed.cpp` case: `render({}, {})` must return a blank image, not crash |
| API-I6 | Verify IPC message `request_id` is unique across concurrent Python clients | Add test spawning 2 threads sending 100 requests each; assert no `request_id` collision |
| API-I7 | Test backward compat: load a workspace saved by version N-1 format (v2) in current code (v3) | Save a hardcoded v2 binary blob in test; load via `FigureSerializer`; assert no crash + axes restored |
| API-I8 | Verify `Axes.remove_series()` out-of-range index raises cleanly in Python, not segfaults | Add pytest: `ax.remove_series(9999)` must raise `IndexError` not crash |

---

## Live Report

The agent writes to `skills/qa-api-agent/REPORT.md` at the end of every session.

### Report update procedure

After every run, open `REPORT.md` and:
1. Add a new `## Session YYYY-MM-DD HH:MM` block at the top (newest first).
2. Fill in: C++ test results (pass/fail), Python test results (pass/fail), example smoke test results, IPC roundtrip latency measured, API regressions found (ID + description), fixes applied, files changed, self-updates made.
3. Update the `## Current Status` block at the very top.
4. Never delete old session blocks.

---

## Guardrails

- Never change a public header (`include/spectra/*.hpp`) without running all C++ and Python tests.
- Treat any IPC message format change as a breaking change — update both encoder and decoder together.
- Run Python tests with the **installed** package, not just the source tree, to catch missing `__init__.py` exports.
- Verify `test_cross_codec.cpp` passes after any codec change — it tests C++↔Python binary compatibility.
- Do not add new required constructor parameters to public types — use default arguments.
- Route rendering/visual bugs to `qa-designer-agent`; route crash/perf bugs to `qa-performance-agent`.
- Self-updates to this file require all C++ and Python tests passing — never update speculatively.

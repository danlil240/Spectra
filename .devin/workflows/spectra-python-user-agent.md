---
description: Spectra Python User Agent — Exercise every Python API feature end-to-end against the live backend, verify results via Python API introspection, find bugs, and improve coverage each session.
---

# Spectra Python User Agent Workflow

**Purpose**: Act as a real Spectra Python user. Each session: exercise Python API features systematically, verify outcomes via Python return values and API queries, log bugs found, and improve this workflow when gaps are discovered.

> **Architecture note — two separate modes:**
>
> | Mode | Binary | MCP Available? | Used by |
> |------|--------|----------------|--------|
> | In-process | `build/spectra` | Yes (port 8765) | MCP tools, direct UI testing |
> | Multi-process | `build/spectra-backend` + `build/spectra-window` | **No** | Python API |
>
> The Python API auto-launches `spectra-backend` (daemon) which spawns `spectra-window` (agent) processes.
> These are **completely separate** from the in-process `spectra` binary. MCP tools cannot see figures
> created by Python, and Python cannot see figures in the in-process app.
>
> **Do NOT launch the in-process `spectra` binary for Python API testing** — it creates a second,
> independent app that confuses verification.

---

## Pre-Session Checklist

1. **Kill any stale Spectra processes** — avoid conflicts from previous sessions:
   ```bash
   pkill -f 'spectra-backend|spectra-window' 2>/dev/null || true
   ```
   Also confirm no in-process `spectra` is running (`pkill -f '/build/spectra$'`).

2. **Ensure the `spectra-backend` binary exists** — this is what Python auto-launches:
   ```bash
   file /home/daniel/projects/Spectra/build/spectra-backend
   file /home/daniel/projects/Spectra/build/spectra-window
   ```
   If missing, rebuild:
   ```bash
   cmake --build /home/daniel/projects/Spectra/build -j$(nproc)
   ```

3. **Install the Python package** in development mode:
   ```bash
   cd /home/daniel/projects/Spectra/python && pip install -e . 2>/dev/null || true
   ```

4. **Verify Python can connect** — run a quick smoke test:
   ```python
   python3 -c "from spectra import Session; s = Session(); print('session_id:', s.session_id); s.close()"
   ```
   This auto-launches `spectra-backend`. If it fails, check build and socket path.

5. **Read** `plans/PYTHON_USER_AGENT_LOG.md` for coverage history.

> **Important**: Do NOT launch the in-process `build/spectra` binary. The Python API uses
> `spectra-backend` + `spectra-window` (multi-process mode). These are separate systems.
> MCP tools connect to the in-process binary and **cannot** verify Python API results.

---

## Python Test Script Execution

All feature exercises are done by writing small Python scripts and running them in the terminal. Each script follows this pattern:

```python
#!/usr/bin/env python3
"""Test: <feature name>"""
import spectra as sp

# ... exercise feature ...
result = sp.plot([1, 4, 9, 16])
print(f"Returned: {result}")       # verify return type
print(f"Type: {type(result)}")     # verify class

import time; time.sleep(1)  # let the backend render

# Verify via Python API queries (not MCP):
fig = sp.gcf()
print(f"Figure id: {fig.id}")
print(f"Axes count: {len(fig.axes)}")
for ax in fig.axes:
    print(f"  Series count: {len(ax.series)}")

sp.close_all()  # clean up after each test
```

Run with:
```bash
cd /home/daniel/projects/Spectra/python
python3 -c "<inline script>"
# OR
python3 /tmp/spectra_test_<feature>.py
```

**Verification is done via Python return values and API queries** — not MCP tools.
MCP tools only work with the in-process `spectra` binary, which Python does not use.

---

## Session Feature Budget

**Each session must cover 8–12 features** chosen pseudo-randomly from the categories below. Prioritize categories least-recently-tested (from `plans/PYTHON_USER_AGENT_LOG.md`). **Do not repeat the same feature set two sessions in a row.**

---

### Feature Categories

#### A — Session Lifecycle
- `Session()` — auto-launch backend, verify connection
- `Session(socket="/path")` — explicit socket path
- `Session` context manager — `with sp.Session() as s: ...`
- `session.close()` — graceful disconnect
- `session.session_id` — verify non-zero after connect
- `session.list_figures()` — query backend figure list
- `session.reconnect()` — reconnect to existing session
- Multiple `Session()` instances — verify isolation

#### B — Figure Management (OOP API)
- `session.figure()` — create figure, verify `figure.id` is returned
- `session.figure(title="My Plot")` — custom title
- `session.figure(width=800, height=600)` — custom dimensions
- `figure.show()` — verify window becomes visible
- `figure.show(window_id=N)` — show as tab in existing window
- `figure.close_window()` — close but keep in model
- `figure.close()` — destroy figure entirely
- `figure.title = "New Title"` — update title via property
- `figure.subplot(2, 1, 1)` — create 2D subplot grid
- `figure.subplot3d(1, 1, 1)` — create 3D subplot
- `figure.axes` — verify axes list populated
- `figure.is_visible` — verify state tracking
- `session.figures` — verify figure list accumulation

#### C — Axes Properties (OOP API)
- `axes.set_xlim(min, max)` — set x limits
- `axes.set_ylim(min, max)` — set y limits
- `axes.set_zlim(min, max)` — set z limits (3D)
- `axes.set_xlabel("X")` — set x-axis label
- `axes.set_ylabel("Y")` — set y-axis label
- `axes.set_title("Title")` — set axes title
- `axes.grid(True/False)` — toggle grid
- `axes.legend(True/False)` — toggle legend
- `axes.remove_series(index)` — remove specific series
- `axes.clear()` — clear all series
- `axes.series` — verify series list

#### D — Series Creation & Data (OOP API)
- `axes.line(x, y)` — create line series
- `axes.line(x, y, label="name")` — with label
- `axes.scatter(x, y)` — create scatter series
- `series.set_data(x, y)` — replace all data
- `series.append(x, y)` — streaming append
- `series.set_data_xyz(x, y, z)` — 3D data
- `series.set_label("new")` — change label
- `series.set_color(r, g, b, a)` — set RGBA color
- `series.set_line_width(w)` — set line width
- `series.set_marker_size(s)` — set marker size
- `series.set_visible(True/False)` — toggle visibility
- `series.set_opacity(0.5)` — set transparency
- Large data (10k+ points) — verify performance
- Chunked transfer (>128 MiB) — verify automatic chunking
- NumPy arrays — verify numpy fast-path interleaving

#### E — Easy API: Basic Plotting
- `sp.plot([1, 4, 9, 16])` — y-only
- `sp.plot(x, y)` — x and y
- `sp.plot(x, y, color="red")` — named color
- `sp.plot(x, y, color="#FF0000")` — hex color
- `sp.plot(x, y, color=(1, 0, 0))` — tuple color
- `sp.plot(x, y, width=3)` — line width
- `sp.plot(x, y, label="data")` — with label
- `sp.plot(x, y, opacity=0.5)` — with opacity
- `sp.scatter(x, y)` — scatter plot
- `sp.scatter(x, y, size=5, color="blue")` — styled scatter
- `sp.stem(x, y)` — stem plot
- `sp.line` alias — verify `sp.line == sp.plot`
- Return value — verify Series object returned

#### F — Easy API: Statistical Plots
- `sp.hist(data, bins=30)` — histogram
- `sp.hist(data, bins=50, color="orange")` — styled histogram
- `sp.bar(x, heights)` — bar chart
- `sp.bar(x, heights, bar_width=0.5)` — custom bar width
- `sp.boxplot([data1, data2])` — box-and-whisker
- `sp.boxplot([data], positions=[2], color="blue")` — styled boxplot
- `sp.boxplot([], show_outliers=False)` — outlier toggle
- `sp.violin_plot([data1, data2])` — violin plot
- `sp.violin_plot([data], show_box=True)` — with inner box
- `sp.hline(y_val)` — horizontal line
- `sp.vline(x_val)` — vertical line

#### G — Easy API: 3D Plotting
- `sp.plot3(x, y, z)` — 3D line
- `sp.plot3(x, y, z, color="red")` — styled 3D line
- `sp.scatter3(x, y, z)` — 3D scatter
- `sp.scatter3(x, y, z, size=3)` — styled 3D scatter
- `sp.surf(X, Y, Z)` — surface plot
- `sp.subplot3d(1, 1, 1)` — explicit 3D subplot

#### H — Easy API: Figure & Axes Management
- `sp.figure()` — new figure window
- `sp.figure(title="My Plot", width=800, height=600)` — custom params
- `sp.tab()` — new tab in current window
- `sp.tab("Analysis")` — named tab
- `sp.subplot(2, 1, 1)` — subplot selection
- `sp.subplot(2, 2, 3)` — grid subplot
- `sp.gcf()` — get current figure
- `sp.gca()` — get current axes
- `sp.subplots(2, 2)` — create subplot grid, returns axes list
- `sp.plotn(y1, y2, y3)` — plot multiple on same axes
- `sp.plotn(y1, y2, labels=["a", "b"])` — with labels

#### I — Easy API: Axes Configuration
- `sp.title("Plot Title")` — set axes title
- `sp.xlabel("X Axis")` — set x label
- `sp.ylabel("Y Axis")` — set y label
- `sp.xlim(0, 10)` — set x limits
- `sp.ylim(-1, 1)` — set y limits
- `sp.grid(True)` / `sp.grid(False)` — toggle grid
- `sp.legend(True)` / `sp.legend(False)` — toggle legend

#### J — Easy API: Live Streaming
- `sp.live(lambda t: math.sin(t))` — auto-append mode
- `sp.live(lambda t, dt: ...)` — two-arg callback
- `sp.live(lambda t, dt, ax: ...)` — three-arg with axes
- `sp.live(callback, fps=60)` — custom FPS
- `sp.live(callback, duration=5.0)` — timed duration
- `sp.stop_live()` — stop all live threads
- Return value (stop_event) — verify Event returned
- Live with tuple return `(x, y)` — auto-append xy mode
- Multiple live streams — two figures streaming simultaneously

#### K — Easy API: Lifecycle
- `sp.show()` — block until windows closed
- `sp.close()` — close current figure
- `sp.close_all()` — close all, shutdown session
- `sp.clear()` — clear current axes
- `sp.append(series, [x], [y])` — append to series
- `sp.append(series, 5.0)` — single value append

#### L — Animation (Backend-Driven)
- `BackendAnimator(session, fig_id, fps=60)` — create animator
- `animator.on_tick = callback` — set tick callback
- `animator.start()` — start backend ticks
- `animator.stop()` — stop backend ticks
- `animator.is_running` — verify state
- `FramePacer(fps=30)` — create frame pacer
- `pacer.pace(session)` — pace with event drain
- `ipc_sleep(session, 0.1)` — sleep with IPC drain

#### M — Batch Updates
- `session.batch_update(updates)` — batch property changes
- Batch with multiple properties — xlim, ylim, grid in one call
- Empty batch — verify no error
- Single-item batch — verify correctness

#### N — Persistence
- `save_session(session, path)` — save to JSON
- `load_session_metadata(path)` — load without connecting
- `restore_session(session, path)` — restore structure
- Round-trip: save → close → new session → restore → verify

#### O — Embedding API
- `EmbedSurface(800, 600)` — create surface
- `surface.is_valid` — verify validity
- `surface.figure()` — create embed figure
- `fig.subplot(1, 1, 1)` — create axes
- `axes.line(x, y)` — add data
- `surface.render()` — get pixel buffer
- `surface.resize(w, h)` — resize
- Mouse/keyboard event injection — verify event handling

#### P — Error Handling & Edge Cases
- Connection to non-existent socket — verify `ConnectionError`
- Invalid data types — non-numeric, strings
- Empty data arrays — `sp.plot([], [])`
- Mismatched array lengths — `series.set_data([1,2], [1,2,3])`
- NaN/Inf in data — verify handling
- Very large data — 1M+ points
- Unicode labels — `sp.plot(x, y, label="数据 📊")`
- Concurrent access — multiple threads plotting
- `ProtocolError` — trigger with protocol mismatch
- `BackendError` — trigger with invalid operations
- `FigureNotFoundError` — access closed figure
- `TimeoutError` — verify timeout behavior

#### Q — NumPy Integration
- NumPy array input — `sp.plot(np.array, np.array)`
- NumPy 2D array — verify flattening
- NumPy integer arrays — verify float conversion
- NumPy float64 → float32 conversion — verify precision
- `_try_interleave_numpy` fast path — verify zero-copy
- Large NumPy arrays (1M floats) — verify performance
- NumPy not installed — verify graceful fallback

#### R — Color Parsing
- All named colors — r, g, b, c, m, y, k, w, red, green, blue, etc.
- Hex colors — `#FF0000`, `#FF000080` (with alpha)
- Tuple colors — `(1.0, 0.0, 0.0)`, `(1.0, 0.0, 0.0, 0.5)`
- Case insensitivity — `"RED"`, `"Red"`, `"red"`
- Invalid color — verify `None` returned
- Extended colors — orange, purple, pink, gray/grey

#### S — Cross-Codec Parity (Python ↔ C++)
- Run `python -m pytest tests/test_cross_codec.py -v`
- Run `python -m pytest tests/test_codec.py -v`
- Verify protocol constants match C++ message.hpp
- Verify tag IDs match C++ codec constants

#### T — Python Examples
- Run each example in `python/examples/` and verify no crash:
  - `basic_line.py`
  - `easy_minimal.py`
  - `easy_one_liner.py`
  - `easy_showcase.py`
  - `easy_subplots.py`
  - `easy_multi_tab.py`
  - `easy_multi_live.py`
  - `easy_live_dashboard.py`
  - `easy_3d.py`
  - `streaming_update.py`
  - `easy_embed_demo.py` (requires libspectra_embed.so)
  - `easy_realtime_demo.py`
- Verify each produces expected output (no crash, correct return values, window appears)

---

## Verification Protocol

For **every feature exercised**, follow this exact sequence:

1. **Action**: Run the Python script/snippet in the terminal
2. **Check return values**: Inspect printed return values, types, and error output
3. **Query state via Python**: Use `session.list_figures()`, `fig.axes`, `ax.series`, etc.
4. **Assert**: State the expected vs actual result, confirm or flag as bug
5. **Clean up**: Call `sp.close_all()` or `session.close()` between tests to avoid state leakage

> **No MCP verification**: The Python API uses `spectra-backend` (multi-process mode),
> which does not have an MCP server. All verification is done through Python return values
> and the Python API's own query methods.

### Assertion Rules

| Feature | Expected (verified via Python) |
|---------|----------|
| `Session()` | `session.session_id > 0` |
| `session.figure()` | `figure.id` is a positive integer |
| `figure.show()` | No error raised |
| `axes.line(x, y)` | Returns `Series` object with correct type |
| `axes.scatter(x, y)` | Returns `Series` object with scatter type |
| `series.set_data(x, y)` | No error raised |
| `series.set_color(1,0,0)` | No error raised |
| `sp.plot(x, y)` | Returns `Series(figure_id=N, index=M, type='line')` |
| `sp.figure()` | Returns `Figure` with valid `.id` |
| `sp.gcf()` | Returns the most recently created figure |
| `sp.gca()` | Returns the current axes |
| `sp.subplot(r,c,i)` | Returns `Axes` at correct grid position |
| `sp.title("T")` | No error raised |
| `sp.grid(True)` | No error raised |
| `sp.live(cb)` | Returns `threading.Event`; callback is invoked |
| `sp.close()` | No error raised, figure cleaned up |
| `sp.close_all()` | No error raised, session shutdown |
| `axes.clear()` | `len(ax.series) == 0` after call |
| `EmbedSurface(w, h)` | `surface.is_valid == True` |

---

## Bug Reporting Protocol

When an assertion fails or unexpected behavior occurs:

1. Record in `plans/PYTHON_USER_AGENT_LOG.md` under `## Session YYYY-MM-DD`:
   ```markdown
   ### BUG: [short title]
   - **API call**: `sp.plot([1,2,3], [4,5,6], color="red")`
   - **Expected**: Red line series appears
   - **Actual**: [what actually happened — error message, wrong color, crash, hang]
   - **Traceback**: [if exception, full traceback]
   - **Python state**: [return values, `fig.axes`, `ax.series` output]
   - **Visual**: [describe window appearance if relevant]
   - **Severity**: P0 (crash/hang) / P1 (wrong output) / P2 (cosmetic) / P3 (minor)
   - **Reproducible**: yes/no
   - **Root cause**: [if identified]
   ```

2. If the bug is in the Python layer (not C++ backend):
   - Identify the file: `_easy.py`, `_figure.py`, `_axes.py`, `_series.py`, `_codec.py`, `_session.py`, etc.
   - State intent, scope, acceptance criteria, risk.
   - Make minimal fix.
   - Re-run the failing test, confirm fix.

---

## Test Suite Verification

In addition to manual feature exercises, run the existing automated tests:

```bash
cd /home/daniel/projects/Spectra/python

# All Python unit tests
python -m pytest tests/ -v --tb=short

# Specific test suites
python -m pytest tests/test_easy.py -v        # Easy API pure-Python tests
python -m pytest tests/test_codec.py -v        # Codec round-trips
python -m pytest tests/test_cross_codec.py -v  # Python↔C++ codec parity
python -m pytest tests/test_phase2.py -v       # Phase 2: append, streaming
python -m pytest tests/test_phase3.py -v       # Phase 3: remove, close, animation
python -m pytest tests/test_phase4.py -v       # Phase 4: batch, reconnect
python -m pytest tests/test_phase5.py -v       # Phase 5: chunked, persistence
python -m pytest tests/test_embed.py -v        # Embedding API
python -m pytest tests/test_easy_embed.py -v   # Easy embed integration
```

Any test failures should be recorded as bugs and investigated.

---

## Code Fix Protocol

When a bug is confirmed:

1. **Identify root cause** — read the relevant source file (don't guess).
2. **State**: intent, scope, non-goals, acceptance criteria, risk.
3. **Make minimal fix** — prefer single-line changes over rewrites.
4. **Verify**: Re-run the failing test/script, confirm fix.
5. **Run full test suite**: `python -m pytest tests/ -v` — no regressions.
6. **Log the fix** in `plans/PYTHON_USER_AGENT_LOG.md`.

---

## Session Log Protocol

At the start of every session, **read `plans/PYTHON_USER_AGENT_LOG.md`** to:
- See which features were covered in recent sessions
- Prioritize **uncovered or previously-bugged** features
- Check open bugs to optionally retest

At the end of every session, **write to `plans/PYTHON_USER_AGENT_LOG.md`**:
```markdown
## Session YYYY-MM-DD HH:MM

### Test Suite Results
- `test_easy.py`: X passed, Y failed
- `test_codec.py`: X passed, Y failed
- ... (all test files)

### Features Exercised
- [category letter] feature_name — PASS/FAIL/BUG

### Bugs Found
- [list or "none"]

### Bugs Fixed
- [list or "none"]

### Uncovered Areas (priority for next session)
- [list based on what hasn't been tested]
```

---

## Session Execution Order

1. Pre-session checklist (kill stale processes, verify binaries, pip install, smoke test)
2. Read `plans/PYTHON_USER_AGENT_LOG.md` for coverage history
3. Run full `python -m pytest tests/ -v` to establish baseline
4. Pick 8–12 features, weighted toward least-recently-tested categories
5. For each feature: Action → Check returns → Query state → Assert → Clean up
6. For each bug: log it, attempt fix if root cause is clear
7. Re-run test suite after any fixes
8. Write session summary to `plans/PYTHON_USER_AGENT_LOG.md`

---

## Quick Reference: Python API Surface

### Easy API (module-level functions)

| Function | Category | Purpose |
|----------|----------|---------|
| `sp.plot(*args, color, width, label)` | 2D Plot | Line plot |
| `sp.scatter(*args, color, size, label)` | 2D Plot | Scatter plot |
| `sp.stem(*args, color, label)` | 2D Plot | Stem plot |
| `sp.hist(data, bins, color, label)` | Stats | Histogram |
| `sp.bar(x, heights, bar_width, color)` | Stats | Bar chart |
| `sp.boxplot(datasets, positions, widths)` | Stats | Box-and-whisker |
| `sp.violin_plot(datasets, positions)` | Stats | Violin plot |
| `sp.hline(y, color, label)` | Annotation | Horizontal line |
| `sp.vline(x, color, label)` | Annotation | Vertical line |
| `sp.plot3(x, y, z, color, width)` | 3D Plot | 3D line |
| `sp.scatter3(x, y, z, color, size)` | 3D Plot | 3D scatter |
| `sp.surf(x, y, z, color)` | 3D Plot | Surface plot |
| `sp.plotn(*ys, labels)` | Multi | Multiple y-arrays |
| `sp.subplots(rows, cols)` | Layout | Create grid |
| `sp.figure(title, width, height)` | Figure | New window |
| `sp.tab(title)` | Figure | New tab |
| `sp.subplot(rows, cols, index)` | Layout | Select subplot |
| `sp.subplot3d(rows, cols, index)` | Layout | 3D subplot |
| `sp.gcf()` | Access | Get current figure |
| `sp.gca()` | Access | Get current axes |
| `sp.title(text)` | Config | Axes title |
| `sp.xlabel(text)` | Config | X-axis label |
| `sp.ylabel(text)` | Config | Y-axis label |
| `sp.xlim(min, max)` | Config | X-axis limits |
| `sp.ylim(min, max)` | Config | Y-axis limits |
| `sp.grid(visible)` | Config | Toggle grid |
| `sp.legend(visible)` | Config | Toggle legend |
| `sp.live(callback, fps, duration)` | Stream | Live plot |
| `sp.stop_live()` | Stream | Stop live threads |
| `sp.append(series, x, y)` | Stream | Append data |
| `sp.show()` | Lifecycle | Block until closed |
| `sp.close()` | Lifecycle | Close current figure |
| `sp.close_all()` | Lifecycle | Close all, shutdown |
| `sp.clear()` | Lifecycle | Clear current axes |

### OOP API

| Class | Key Methods |
|-------|-------------|
| `Session` | `figure()`, `show()`, `close()`, `list_figures()`, `reconnect()`, `batch_update()` |
| `Figure` | `subplot()`, `subplot3d()`, `show()`, `close()`, `close_window()`, `.title`, `.axes`, `.id` |
| `Axes` | `line()`, `scatter()`, `set_xlim()`, `set_ylim()`, `grid()`, `legend()`, `clear()`, `remove_series()` |
| `Series` | `set_data()`, `append()`, `set_data_xyz()`, `set_color()`, `set_line_width()`, `set_marker_size()`, `set_visible()`, `set_opacity()`, `set_label()` |
| `BackendAnimator` | `start()`, `stop()`, `.on_tick`, `.is_running` |
| `FramePacer` | `pace(session)`, `.fps` |
| `EmbedSurface` | `figure()`, `render()`, `resize()`, `.is_valid`, `.width`, `.height` |

### Error Classes

| Exception | When |
|-----------|------|
| `SpectraError` | Base class |
| `ConnectionError` | Cannot connect to backend socket |
| `ProtocolError` | Message type mismatch |
| `TimeoutError` | Backend not responding |
| `FigureNotFoundError` | Reference to destroyed figure |
| `BackendError` | Backend returns error code |

---

## Category Coverage Tracker

Track which categories have been tested. Mark with date of last test.

| Cat | Name | Last Tested | Status |
|-----|------|-------------|--------|
| A | Session Lifecycle | — | untested |
| B | Figure Management (OOP) | — | untested |
| C | Axes Properties (OOP) | — | untested |
| D | Series Data (OOP) | — | untested |
| E | Easy API: Basic Plotting | — | untested |
| F | Easy API: Statistical Plots | — | untested |
| G | Easy API: 3D Plotting | — | untested |
| H | Easy API: Figure/Axes Mgmt | — | untested |
| I | Easy API: Axes Config | — | untested |
| J | Easy API: Live Streaming | — | untested |
| K | Easy API: Lifecycle | — | untested |
| L | Animation (Backend-Driven) | — | untested |
| M | Batch Updates | — | untested |
| N | Persistence | — | untested |
| O | Embedding API | — | untested |
| P | Error Handling & Edge Cases | — | untested |
| Q | NumPy Integration | — | untested |
| R | Color Parsing | — | untested |
| S | Cross-Codec Parity | — | untested |
| T | Python Examples | — | untested |

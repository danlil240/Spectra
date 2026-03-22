# Spectra Python User Agent — Session Log

Track Python API testing coverage, bugs found, and fixes applied.

---

<!-- Sessions are appended below in reverse-chronological order -->

## Session 2 — 2026-03-22

### Environment
- Build: Debug, CMake+Ninja
- Python: spectra-plot 0.2.0 (editable install)
- Backend: spectra-backend + spectra-window (multi-process)

### Pytest Baseline
- **377 passed**, 38 failed (unchanged from Session 1)
- 38 failures: embed tests requiring `libspectra_embed.so` (not built)

### Features Exercised (10/10)

| # | Category | Feature | Result | Notes |
|---|----------|---------|--------|-------|
| B | Figure OOP | `figure()`, `title` property, `show()`, `is_visible`, `window_id`, `subplot()`, `subplot3d()`, `close_window()`, `close()` | PASS | `session.figures` not pruned after `fig.close()` (observed, not critical) |
| C | Axes OOP | `ax.line()`, `ax.scatter()`, `set_xlim/ylim`, `set_xlabel/ylabel`, `set_title`, `grid`, `legend`, `remove_series`, `clear`, `series` | PASS | All 11 operations work |
| J | Live Streaming | `sp.live()` 1-arg, 2-arg, 3-arg, auto-append, tuple return, `stop_live()` | PASS | **Bug B6 found & fixed** — see below |
| L | Animation | `FramePacer`, `ipc_sleep`, `BackendAnimator` | PARTIAL | FramePacer & ipc_sleep: PASS. BackendAnimator.start() timeout — C++ backend doesn't implement REQ_ANIM_START |
| M | Batch Updates | `session.batch_update()`, empty batch, single-item, `axes.batch()` context manager | PASS | All 4 variants work |
| N | Persistence | `save_session()`, `load_session_metadata()`, `restore_session()` | PASS | Round-trip preserves titles, axes counts |
| P | Error Handling | Empty data, NaN, Inf, mismatched lengths, unicode labels, 100k points, single-value append | PASS | All 7 edge cases handled correctly. Mismatched lengths raises `ValueError` |
| Q | NumPy | float32, float64, int32, int64, 1M points, 2D flattened, `set_data`, `append`, scatter | PASS | 1M points: 1.1s |
| F2 | Statistical | `violin_plot` (basic, show_box, color, positions, widths, resolution), `boxplot` (show_outliers, positions, color) | PASS | All 7 variants work |
| H2 | Multi-figure | Multiple figures, `plotn`, `subplots(2,2)`, `tab`, subplot selection, `gcf`/`gca` | PASS | **Bug B7 found & fixed** — see below |

### Bugs Found & Fixed

| ID | Severity | Description | Location | Status |
|----|----------|-------------|----------|--------|
| B6 | P2 | `close_all()` sets `_shutting_down=True` but never resets it, so subsequent `sp.live()` threads exit immediately | `python/spectra/_easy.py:shutdown()` | **FIXED** — reset `_shutting_down=False` at end of `shutdown()` |
| B7 | P2 | `sp.subplot(r,c,i)` always creates new backend axes (with new index), losing Python-side series tracking. Re-selecting an existing subplot yields empty axes | `python/spectra/_easy.py:subplot()` | **FIXED** — added `_subplot_cache` dict keyed by `(fig_id, rows, cols, index)` |

### Bugs Observed (not fixed)

| ID | Severity | Description | Location | Status |
|----|----------|-------------|----------|--------|
| B8 | P3 | `session.figures` not pruned after `fig.close()` — Python-side list retains destroyed figure objects | `python/spectra/_session.py` | Observed, cosmetic |
| B9 | P3 | C++ backend does not implement `REQ_ANIM_START`/`REQ_ANIM_STOP` — `BackendAnimator` unusable | `src/daemon/` (missing handler) | Backend limitation |

### Fixes Applied
- `python/spectra/_easy.py`: Reset `_shutting_down = False` at end of `shutdown()` so `close_all()` allows subsequent API use
- `python/spectra/_easy.py`: Added `_subplot_cache` dict and caching logic in `subplot()` and `subplot3d()`
- `python/tests/test_easy.py`: Updated `test_shutdown_without_session` assertion (`_shutting_down` is now `False` after shutdown)

### Pytest After Fixes
- **377 passed**, 38 failed (no regressions — same 38 embed failures)

### Uncovered Areas (prioritize next session)
- A2: `Session.reconnect()`, multiple `Session()` instances
- D2: Large data chunking (>128 MiB), `set_data_xyz` for 3D
- J2: `sp.live` with `duration` parameter, simultaneous live streams
- O: Embedding API (requires `libspectra_embed.so` build)
- S: Cross-codec parity tests (`test_cross_codec.py`)
- T: Running Python examples from `python/examples/`

## Session 1 — 2025-01-20

### Environment
- Build: Debug, CMake+Ninja
- Python: spectra-plot 0.2.0 (editable install)
- Backend: spectra-backend + spectra-window (multi-process)

### Pytest Baseline
- **376 passed**, 39 failed
- 38 failures: embed tests requiring `libspectra_embed.so` (not built)
- 1 failure: `test_phase2.py::test_version_exists` expected `0.1.0` but package is `0.2.0` → **fixed in this session**

### Features Exercised (10/10 PASS)

| # | Category | Feature | Result | Notes |
|---|----------|---------|--------|-------|
| E1 | Easy Plot | `sp.plot([1,4,9,16])` y-only | PASS | Returns `Series(figure_id=1, index=0, series_type='line')` |
| E2 | Easy Plot | `sp.plot(x,y,color,width,label)` + `sp.scatter()` | PASS | `sp.line is sp.plot` alias confirmed |
| H | Hierarchy | `sp.figure()`, `sp.subplot()`, `sp.gcf()`, `sp.gca()`, `sp.tab()`, `sp.subplots(2,2)` | PASS | All return correct types |
| I | Axes Config | `sp.title()`, `sp.xlabel()`, `sp.ylabel()`, `sp.xlim()`, `sp.ylim()`, `sp.grid()`, `sp.legend()` | PASS | |
| F | Special | `sp.hist()`, `sp.bar()`, `sp.boxplot()`, `sp.hline()`, `sp.vline()` | PASS | Bug: `sp.bar()` crashes on string category labels |
| A | Session | `Session()`, `session_id`, `figure()`, `list_figures()`, `close()`, context manager | PASS | |
| D | Series Data | `ax.line()`, `ax.scatter()`, `set_data()`, `append()`, `set_color/width/label/visible/opacity/marker_size()` | PASS | All 11 operations work |
| G | 3D | `sp.plot3()`, `sp.scatter3()`, `sp.subplot3d()` | PASS | Returns line3d/scatter3d types, is_3d=True |
| K | Lifecycle | `sp.clear()`, `sp.append()`, `sp.close()`, `sp.close('all')` | PASS | series count drops to 0 after clear |
| R | Colors | Short (r,g,b,k,w,y,m,c), long names, hex, tuples, case insensitivity | PASS | See bugs below |

### Bugs Found

| ID | Severity | Description | Location |
|----|----------|-------------|----------|
| B1 | P2 | `sp.bar(['A','B','C'], [10,20,30])` crashes: `ValueError: could not convert string to float: 'A'` in `_to_list()` | `python/spectra/_easy.py:65` |
| B2 | P3 | Invalid color names (e.g. 'notacolor', 'foobar') silently accepted — `_parse_color()` returns None, no error raised | `python/spectra/_easy.py:75` |
| B3 | P3 | Extended color names missing from `_NAMED` dict: brown, lime, navy, teal, coral, salmon, gold, indigo, violet, turquoise | `python/spectra/_easy.py:87` |
| B4 | P3 | Short hex colors `#RGB` not supported (only `#RRGGBB` / `#RRGGBBAA`) | `python/spectra/_easy.py:117` |
| B5 | P2 | Stale `spectra/` directory in site-packages can shadow editable install — `import spectra` loads empty namespace package | `site-packages/spectra/` |

### Fixes Applied
- `python/tests/test_phase2.py:282` — version assertion `0.1.0` → `0.2.0`
- Removed stale `site-packages/spectra/` directory blocking editable install

### Uncovered Areas (prioritize next session)
- B: Figure management OOP API (figure title property, show/close_window, subplot grids)
- C: Axes OOP API (create series via axes object, axes config properties)
- J: Multi-figure workflow (multiple figures, switching between them)
- L: Live streaming (`sp.live()`)
- M: Animation (`sp.animate()`)
- N: Save/export (`sp.savefig()`)
- O: Format strings (MATLAB-style `"r--o"`)

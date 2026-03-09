# Code Simplifier — Report

Tracks completed and in-progress simplification work.

---

## Completed Simplifications

### [2026-03-09] Deduplicate 3D Series Setters, Centroid, and Bounds

- **Files:** `src/core/series3d.cpp`
- **Risk:** low
- **Change:** Extracted three file-local anonymous-namespace helpers (`assign_coord`, `compute_centroid_xyz`, `get_bounds_xyz`) to replace six identical setter bodies and four identical algorithm bodies across `LineSeries3D` and `ScatterSeries3D`. Public API and behavior unchanged.
- **Verification:** Build + `ctest --test-dir build -LE gpu`
- **Status:** Done

### [2026-03-09] Deduplicate Logger Sink Formatting

- **Files:** `src/core/logger.cpp`
- **Risk:** low
- **Change:** Extracted `format_log_entry()` static helper in the `sinks` namespace. Both `console_sink()` and `file_sink()` now call it instead of repeating the timestamp+level+category+message+optional-file conditional block.
- **Verification:** Build + `ctest --test-dir build -LE gpu`
- **Status:** Done

### [2026-03-09] Deduplicate DataTransform apply_* Functions

- **Files:** `src/math/data_transform.cpp`
- **Risk:** low
- **Change:** Added two template helpers in the existing anonymous namespace: `apply_positive_log` (shared between `apply_log10` and `apply_ln`) and `apply_elementwise_y` (shared between `apply_abs`, `apply_negate`, `apply_scale`, `apply_offset`, `apply_clamp`). Reduced 7 near-identical ~10-line function bodies to single-line calls.
- **Verification:** Build + `ctest --test-dir build -LE gpu`
- **Status:** Done

<!--
Use this format for each entry:

### [YYYY-MM-DD] <Title>

- **Files:** `src/path/file.cpp`
- **Risk:** low / med / high
- **Change:** Brief description of what was simplified
- **Verification:** Commands and steps used to validate
- **Status:** Done
-->

---

## In Progress

_None._

---

## Candidate Backlog

| File + Symbol | Why Complex | Proposed Change | Risk | Est. Diff |
|---------------|-------------|-----------------|------|-----------|
| `src/ui/overlay/` | Not yet analyzed — 16 files | Analyze next session for repeated ImGui widget patterns | — | — |
| `src/ui/commands/` | Not yet analyzed — 14 files | Analyze next session for command registration boilerplate | — | — |

<!--
| File + Symbol | Why Complex | Proposed Change | Risk | Est. Diff |
|---------------|-------------|-----------------|------|-----------|
| example       | example     | example         | low  | ~20 lines |
-->

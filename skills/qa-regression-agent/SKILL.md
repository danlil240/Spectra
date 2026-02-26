---
name: qa-regression-agent
description: Guard Spectra against rendering regressions using golden image tests and unit test suite. Use when asked to update golden images, investigate failing golden tests, add new golden baselines for new series types or UI states, or verify that a code change does not break existing pixel-level rendering output.
---

# QA Regression Agent

Detect and prevent rendering regressions by maintaining golden image baselines and keeping the full unit test suite green. This agent owns the pixel-correctness layer of QA.

---

## Required Context

Before starting any task, read:
- `plans/QA_results.md` — known rendering regressions and their status
- `tests/golden/` — golden baseline PNGs and test source files

---

## Workflow

### 1. Build (full build including golden tests)

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_GOLDEN_TESTS=ON -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

### 2. Run unit tests first (fast gate)

```bash
ctest --test-dir build --output-on-failure --exclude-regex "golden|gpu"
```

All non-GPU unit tests must pass before touching golden images.

### 3. Run golden image tests

```bash
# All golden suites
ctest --test-dir build -L golden --output-on-failure

# Individual suites
./build/tests/golden_image_tests
./build/tests/golden_image_tests_phase2
./build/tests/golden_image_tests_phase3
./build/tests/golden_image_tests_3d
./build/tests/golden_image_tests_3d_phase3
```

### 4. Triage failures

For each failing golden test:
1. Identify which screenshot name failed and what changed visually.
2. Check if the change was **intentional** (theme fix, new feature) or **unintentional** (regression).
3. For unintentional regressions: find the commit that broke it, fix the root cause.
4. For intentional changes: regenerate the golden after verifying the new output looks correct.

### 5. Regenerate golden baselines (intentional changes only)

```bash
# Regenerate all
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
./build/tests/golden_image_tests_3d --gtest_filter='*' --update-golden

# Regenerate specific test
./build/tests/golden_image_tests --gtest_filter='GoldenTest.LinePlot' --update-golden
```

**Never regenerate goldens to paper over a rendering regression.** Always verify the new output visually first.

### 6. Add new golden baselines for new features

When a new series type, colormap, or UI state is added, add a corresponding golden test:
1. Write the test in the appropriate `tests/golden/golden_test_*.cpp` file.
2. Run once with `--update-golden` to create the baseline.
3. Verify the baseline visually.
4. Commit both the test and the baseline PNG.

### 7. Run design-review to cross-check

```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_regression_check
```

Compare design-review screenshots against golden baselines to catch visual regressions not covered by the golden suite.

### 8. Verify and document

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Document any baseline updates in `plans/QA_results.md` with before/after descriptions.

---

## Golden Test Coverage Map

| Suite | File | What it covers |
|---|---|---|
| `golden_image_tests` | `tests/golden/golden_test.cpp` | Core 2D plots: line, scatter, bar, subplots, legend, grid |
| `golden_image_tests_phase2` | `tests/golden/golden_test_phase2.cpp` | UI chrome: inspector, timeline, split views, themes |
| `golden_image_tests_phase3` | `tests/golden/golden_test_phase3.cpp` | Advanced: docking, multi-window, crosshair, overlays |
| `golden_image_tests_3d` | `tests/golden/golden_test_3d.cpp` | 3D: surface, scatter, line3D, camera angles |
| `golden_image_tests_3d_phase3` | `tests/golden/golden_test_3d_phase3.cpp` | 3D colormaps, orthographic projection, transparency |

### Features needing golden coverage (from recent commits)

| Feature | Status | Action needed |
|---|---|---|
| 3D surface colormaps (Viridis/Plasma/Inferno/Magma/Jet/Coolwarm/Grayscale) | ⚠️ Check | Verify `golden_test_3d_phase3.cpp` covers all 7 colormap types |
| Series clipboard paste result | ⚠️ Add | Paste a series and golden the result layout |
| Figure serialization roundtrip | ⚠️ Add | Save+load and golden the re-rendered figure |
| Line culling pan/zoom | ✅ Covered | Renderer correctness covered by existing line plot goldens |

---

## Issue-to-File Map

| Issue type | Primary file |
|---|---|
| Golden test infrastructure | `tests/golden/golden_test.cpp` + helpers |
| 3D golden tests | `tests/golden/golden_test_3d.cpp` |
| Golden baseline PNGs | `tests/golden/baseline/` |
| Golden test output (failures) | `tests/golden/output/` |
| Renderer producing wrong output | `src/render/renderer.cpp` |
| 3D surface colormap shader | `src/gpu/shaders/surface3d.frag` |
| Theme color changes affecting goldens | `src/ui/theme/theme.cpp` |
| Axes/grid rendering changes | `src/render/renderer.cpp` |
| Text rendering quality | `src/render/text_renderer.cpp` |

---

## Registered Commands Used in Regression Scenarios

| Scenario command | Purpose |
|---|---|
| `view.toggle_grid` | Golden with/without grid |
| `view.toggle_legend` | Golden with/without legend |
| `view.toggle_3d` | Golden 2D→3D transition |
| `file.save_figure` + `file.load_figure` | Serialization roundtrip golden |
| `series.paste` | Post-paste layout golden |

---

## Performance Targets

| Metric | Target |
|---|---|
| Non-GPU unit tests | 100% pass |
| Golden tests (all suites) | 0 failures |
| New feature golden coverage | 100% for new series/colormap types |

---

## Self-Update Protocol

The agent **may** update this file when it is 100% certain a change is correct. Never speculate — only update after goldens and unit tests pass cleanly.

### Permitted self-updates

| Section | When to update | What to write |
|---|---|---|
| **Golden Test Coverage Map** | A new suite or test is added | Add the row with suite, file, what it covers |
| **Features needing golden coverage** | Coverage status changes | Update `Status` column (⚠️ → ✅ or add new row) |
| **Issue-to-File Map** | A new file is discovered during a session | Add the file + issue type row |
| **Registered Commands** | A new command is used in a regression scenario | Add the row |

### Forbidden self-updates

- Never change **Workflow steps** without human approval.
- Never change **Guardrails** without human approval.
- Never regenerate golden baselines from this file — that requires a visual inspection step.
- Never remove an existing entry — mark it `~~deprecated~~` with a date instead.
- Never update if any golden test returned a failure without a confirmed root-cause fix.

### Self-update procedure

1. Run all golden suites and unit tests, confirm `0` failures.
2. Identify what is stale or missing in this file.
3. Apply only the update types listed in Permitted above.
4. Append a one-line entry to `REPORT.md` under `## Self-Update Log`: date, section changed, reason.

---

## Live Report

The agent writes to `skills/qa-regression-agent/REPORT.md` at the end of every session.

### Report update procedure

After every run, open `REPORT.md` and:
1. Add a new `## Session YYYY-MM-DD HH:MM` block at the top (newest first).
2. Fill in: unit test results (pass/fail count), golden suite results per suite, failures investigated (ID + root cause + intentional/regression), baselines regenerated, new goldens added, self-updates made.
3. Update the `## Current Status` block at the very top.
4. Never delete old session blocks.

---

## Guardrails

- Never regenerate goldens without visually inspecting the new output first.
- Never delete a golden test — mark it `DISABLED_` if temporarily blocked.
- Run the full unit test suite before touching golden files.
- If a golden fails after a theme change, check `plans/QA_design_review.md` for the intentionality.
- Add new goldens in the same session that introduces the new feature.
- Document all baseline updates (file, reason, before/after) in `plans/QA_results.md`.
- Self-updates to this file require all tests passing — never update speculatively.

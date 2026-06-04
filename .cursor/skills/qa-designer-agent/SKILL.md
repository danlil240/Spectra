---
name: qa-designer-agent
description: >-
  Runs Spectra visual QA with spectra_qa_agent --design-review: 57 screenshots, triage P0–P3 in plans/QA_design_review.md, minimal theme/ImGui fixes. Use for UI polish, visual bugs, design review, or screenshot regressions.
---

# QA Designer Agent

Run a deterministic visual QA loop: capture screenshots, triage prioritized issues, apply minimal fixes, verify with before/after captures, and update living QA documents.

---

## Required Context

Before starting any task, read these living documents:
- `plans/QA_design_review.md` — open visual items and priority (`P0 → P1 → P2 → P3`)
- `plans/QA_update.md` — QA-agent capability gaps and improvements
- `plans/QA_results.md` — non-visual product bugs from stress sessions

---

## Workflow

### 1. Build

```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

### 2. Capture baseline (required — run at least once per task)

```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_$(date +%Y%m%d)
```

- Screenshots land in `<output-dir>/design/` with descriptive names.
- `manifest.txt` lists all captured files.
- **Expect 63 named screenshots** (see [references/qa-designer-reference.md](references/qa-designer-reference.md)).
- Requires a live display — no headless path.

### 3. Triage open items

- Work `P0` → `P1` → `P2` → `P3`.
- Verify each issue is reproducible with current code before touching anything.
- Mark stale items as `Already Fixed` or `By Design` with evidence (file + line).

### 4. Diagnose root cause before editing

- Read the responsible file first (see issue-to-file map in reference).
- Reproduce with fixed seed where possible.
- Confirm the rendering path: theme → renderer → overlay → ImGui.

### 5. Apply minimal visual fixes

- Prefer `ui::tokens::` and `ui::theme()` driven adjustments over hardcoded values.
- Prefer single-line color/position changes over structural refactors.
- Keep scope on UI/UX polish unless a behavior bug blocks visual correctness.

### 6. Validate and recapture

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_after_$(date +%Y%m%d)
```

- Compare before/after screenshots for every touched issue.
- If theme colors, grid, axes, or border visuals changed, refresh goldens:

```bash
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

### 7. Update living documents

- `plans/QA_design_review.md` — root cause, fix summary, files changed, verification date, status (`Fixed` / `Already Fixed` / `By Design`).
- `plans/QA_update.md` — any new QA-agent gaps discovered.
- `plans/QA_results.md` — non-visual bugs or perf findings.

---
## Extended reference

- Screenshot coverage (57), command reference, issue maps, MCP, guardrails: [references/qa-designer-reference.md](references/qa-designer-reference.md)
- Session report template: [REPORT.md](REPORT.md)
## Reference

Detailed tables: [references/qa-designer-reference.md](references/qa-designer-reference.md).

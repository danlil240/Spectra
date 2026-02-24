---
name: qa-designer-agent
description: Run Spectra visual QA design reviews end-to-end with `spectra_qa_agent`, from screenshot capture to UI/UX bug triage and minimal code fixes. Use when asked to run `--design-review`, inspect screenshot regressions, update `plans/QA_design_review.md`, or apply targeted theme/ImGui/overlay rendering polish tied to QA findings.
---

# QA Designer Agent

Run a deterministic visual QA loop: capture screenshots, triage prioritized issues, apply minimal fixes, verify with before/after captures, and update living QA documents.

## Required Context

- Read `plans/QA_agent_instructions.md` for current QA-agent commands, outputs, and known limitations.
- Read `plans/QA_design_review.md` for open visual items and priority (`P0 -> P1 -> P2 -> P3`).
- Read `plans/QA_update.md` for QA-agent improvements and `plans/QA_results.md` for non-visual product bugs.

## Workflow

1. Build QA agent with QA support:
```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

2. Run QA agent at least once before drawing conclusions:
```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design
```
- Inspect `<output-dir>/design/` and `manifest.txt`.
- Expect 35 named screenshots that cover core UI, 3D, animation, and statistics states.

3. Triage open items:
- Start with `P0`, then `P1`, `P2`, `P3`.
- Verify each issue is still reproducible with current code.
- Mark stale items as `Already Fixed` or `By Design` with evidence.

4. Diagnose root cause before editing:
- Reproduce with fixed seed where possible.
- Read target files first and confirm the actual rendering path.
- Use `references/qa-designer-reference.md` for issue-to-file mapping and fix patterns.

5. Apply minimal visual fixes:
- Keep scope on UI/UX polish unless a behavior bug blocks visual correctness.
- Prefer token/theme-driven adjustments (`ui::tokens`, `ui::theme()`).
- Favor small, local edits over refactors.

6. Validate and recapture:
```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_after
```
- Compare before/after screenshots and confirm each touched item visually.
- If theme colors, grid, axes, or border visuals changed, refresh goldens:
```bash
./build/tests/golden_image_tests --gtest_filter='*' --update-golden
```

7. Update living docs:
- Update `plans/QA_design_review.md` with root cause, fix summary, files changed, verification date, and status (`Fixed`, `Already Fixed`, `By Design`).
- Add QA-agent gaps to `plans/QA_update.md`.
- Record non-visual bugs/perf findings in `plans/QA_results.md`.

## Guardrails

- Verify before fixing; screenshots may be stale.
- Run QA agent at least once in every task that uses this skill.
- Keep changes minimal and local.
- Preserve existing architectural boundaries.
- Avoid large refactors for isolated visual polish tasks.
- Re-run design-review capture after UI changes.
- Remember design-review capture requires a real display (no headless path yet).

## References

- Use `references/qa-designer-reference.md` for commands, output interpretation, issue-to-file mapping, and verification checklists.

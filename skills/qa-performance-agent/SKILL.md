---
name: qa-performance-agent
description: Run Spectra stability and performance QA end-to-end with `spectra_qa_agent`, including deterministic stress/fuzz execution, report triage, crash reproduction, and verification of fixes. Use when asked to run QA scenarios or fuzzing, investigate `qa_report` warnings/errors, reproduce seed-based regressions, or update `plans/QA_results.md` and `plans/QA_update.md`.
---

# QA Performance Agent

Drive reproducible QA sessions focused on crashes, frame-time regressions, and memory growth, then convert findings into verified fixes and updated living QA documents.

## Required Context

- Read `plans/QA_agent_instructions.md` for current commands, scenarios, output formats, and known limitations.
- Read `plans/QA_results.md` for open product bugs and previously observed regressions.
- Read `plans/QA_update.md` for QA-agent capability gaps and backlog items.
- Load `references/qa-performance-reference.md` for exact command patterns and triage rules.

## Workflow

1. Build the QA binary:
```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON
cmake --build build -j$(nproc)
```

2. Run at least one QA command in every task:
```bash
./build/tests/spectra_qa_agent --seed 42 --duration 120 --output-dir /tmp/spectra_qa
```

3. Triage findings in order:
- Check process exit code first (`0`, `1`, `2`).
- Read `qa_report.txt` summary, then issue list (`CRITICAL`/`ERROR` before warnings).
- Inspect frame-time outliers, memory growth, and scenario/fuzz context.
- Use `qa_report.json` when automated comparison or scripting is needed.

4. Reproduce deterministically:
- Re-run with the same `--seed`, `--duration`, and relevant mode flags.
- For crashes (`exit 2`), preserve the seed from stderr and reproduce before editing.

5. Apply minimal, targeted fixes:
- Focus on root cause in the affected subsystem.
- Avoid unrelated refactors while triaging regressions.

6. Verify with both stability and regression coverage:
```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/tests/spectra_qa_agent --seed <repro-seed> --duration 120 --output-dir /tmp/spectra_qa_after
./build/tests/spectra_qa_agent --seed <new-seed> --duration 120 --output-dir /tmp/spectra_qa_regression
```

7. Update living docs:
- Record product-side fixes and current statuses in `plans/QA_results.md`.
- Record QA-agent enhancements in `plans/QA_update.md`.

## Guardrails

- Execute `spectra_qa_agent` at least once before drawing conclusions.
- Keep seed values in notes so runs are reproducible.
- Treat frame spikes above 50 ms as likely real stalls.
- Treat early ~16 ms spikes as possible VSync/EMA false positives when startup EMA is low.
- Treat RSS growth above 100 MB over baseline as suspicious unless explained by intended dataset scale.
- Route purely visual screenshot-polish tasks to `qa-designer-agent`.

## References

- Use `references/qa-performance-reference.md` for command cookbook, scenario targeting, interpretation rules, and verification checklist.

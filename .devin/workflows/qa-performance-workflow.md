---
description: QA Performance Review — Stress testing, performance analysis, and optimization workflow
---

> **All instructions, scenario tables, fuzz action coverage, file maps, and triage rules are consolidated in:**
> `skills/qa-performance-agent/SKILL.md`
>
> Follow the workflow defined there. This file is kept as a workflow entry point only.

## Quick Start

// turbo
1. Build QA agent:
```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON && cmake --build build -j$(nproc)
```

2. Run deterministic baseline (20 scenarios + fuzz):
```bash
./build/tests/spectra_qa_agent --seed 42 --duration 120 \
    --output-dir /tmp/spectra_qa_perf_$(date +%Y%m%d)
```

3. Check exit code (`0`=pass, `1`=issues, `2`=crash). Read `qa_report.txt`. Prioritize CRITICAL → ERROR → warnings.

4. Reproduce crashes with same seed + ASan before editing code.

5. Update `plans/QA_results.md` (product bugs) and `plans/QA_update.md` (agent improvements).

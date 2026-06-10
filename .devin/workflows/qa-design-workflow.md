---
description: QA Design Review — Visual UI/UX analysis and improvement workflow
---

> **All instructions, file maps, commands, and coverage tables are consolidated in:**
> `skills/qa-designer-agent/SKILL.md`
>
> Follow the workflow defined there. This file is kept as a workflow entry point only.

## Quick Start

// turbo
1. Build QA agent:
```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON && cmake --build build -j$(nproc)
```

2. Capture baseline (51 screenshots):
```bash
./build/tests/spectra_qa_agent --seed 42 --design-review --no-fuzz --no-scenarios \
    --output-dir /tmp/spectra_qa_design_$(date +%Y%m%d)
```

3. Read `plans/QA_design_review.md`, triage P0 → P1 → P2 → P3, apply minimal fixes, recapture.

4. Update `plans/QA_design_review.md` with findings and `plans/QA_update.md` with agent gaps.

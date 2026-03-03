---
description: QA Regression — Golden image and unit test regression guard workflow
---

> **All instructions, coverage maps, and file locations are consolidated in:**
> `skills/qa-regression-agent/SKILL.md`

## Quick Start

// turbo
1. Build:
```bash
cmake -B build -G Ninja -DSPECTRA_BUILD_GOLDEN_TESTS=ON && cmake --build build -j$(nproc)
```

2. Run unit tests then golden tests:
```bash
ctest --test-dir build --output-on-failure --exclude-regex "golden|gpu"
ctest --test-dir build -L golden --output-on-failure
```

3. If intentional visual change: regenerate baselines, verify visually, commit PNG + test together.

4. Never regenerate to paper over a regression — fix root cause first.

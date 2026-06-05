---
name: github-ci
description: >-
  Fix or edit Spectra GitHub Actions (.github/workflows). Use for CI failures,
  new matrix jobs, golden/sanitizer jobs, release.yml, lavapipe/xvfb setup, or
  masking vs root-cause CI fixes.
---

# GitHub CI (Spectra)

**Fix root cause** — never green CI by hiding failures (unless user approves temporary exception).

## Forbidden (masking)

| Do not | Do instead |
|--------|------------|
| Widen golden tolerance | Fix renderer/leak; regen baselines on lavapipe |
| `ASAN_OPTIONS=detect_leaks=0` | Fix leak or avoid `App`/Vulkan in unit tests |
| Skip/disable tests or `if: false` jobs | Fix or tracked quarantine |
| Weaken sanitizers / `-Werror` | Fix UB/leak |
| Broaden `ctest` to exclude failures | Fix or correct labels |

## Allowed CI-only config

`SPECTRA_USE_ROS2=OFF` without ROS workspace · `-LE gpu` under sanitizers · `xvfb-run -a` for golden · lavapipe ICD pin · workflow structure/caching.

## Golden failures

1. Log: scene, `percent_different`, MAE, diff artifact.
2. Order-dependent / after other tests → fix teardown before new baselines.
3. Intentional visual change → regen on **lavapipe** (`update-golden-baselines.yml` or CI output), not NVIDIA/Apple GPU.

## ASan failures

Identify test + stack → fix Spectra ownership; refactor tests that only need JSON/dispatch to not construct `App`/Vulkan.

## Debug failing CI

1. `gh` / Actions UI for latest failing run → job → annotations.
2. Classify: compile, unit, sanitizer, golden, workflow/env.
3. Edit source/tests/baselines **or** YAML as appropriate.
4. Report summary (see below).

## Constraints

- Keep `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: "true"` in workflow env.
- Use `actions/checkout@v4`, `upload-artifact@v4`, `cache@v4`.
- Keep apt `sudo rm -f .../microsoft-prod.list` line on Ubuntu jobs.
- Golden on Linux: `xvfb-run -a`; never GPU tests under sanitizers.
- Always `ctest ... --output-on-failure`.

## Output

```md
## CI Change Summary
- Workflow / area:
- Job(s):
- Root cause:
- What changed: (not a mask)
- Verify next run:
- Follow-up:
```

Workflow inventory, matrices, lavapipe snippet → [references/github-ci-reference.md](references/github-ci-reference.md)

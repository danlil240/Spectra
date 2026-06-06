---
name: spectra-dev
description: >-
  End-to-end Spectra development orchestration: plan → design → implement →
  build → test → scoped QA. Use for full features, large refactors, or when the
  user wants the full dev cycle with sub-skill routing.
---

# Spectra Dev Orchestrator

Coordinate skills — do not skip planning; do not run QA before build+tests are green.

## Pipeline

```text
spectra-implementation (plan → design? → code)
  → build-and-test (compile + ctest)
  → loop on FAIL until green
  → scoped QA (qa-orchestrator or single qa-* skill)
  → report to user
```

| Stage | Skill |
|-------|--------|
| Plan / design / code | [spectra-implementation](../spectra-implementation/SKILL.md) |
| Build / test | [build-and-test](../build-and-test/SKILL.md) |
| Visual change | [graphical-change-workflow](../graphical-change-workflow/SKILL.md) |
| Full QA | [qa-orchestrator](../qa-orchestrator/SKILL.md) |

## QA routing (minimal scope)

| Paths | QA skills |
|-------|-----------|
| `src/ui/theme/` | qa-designer-agent, qa-accessibility-agent, qa-regression-agent |
| `src/gpu/shaders/` | qa-regression-agent, qa-performance-agent |
| `src/render/vulkan/` | qa-regression-agent, qa-memory-agent, qa-performance-agent |
| `src/core/` | qa-performance-agent, qa-regression-agent, qa-api-agent |
| `src/ipc/`, `src/daemon/`, `src/agent/` | qa-api-agent, qa-performance-agent |
| `python/` | qa-api-agent |
| `include/spectra/` | qa-api-agent, qa-regression-agent |
| `src/anim/` | qa-performance-agent, qa-regression-agent |
| ROS adapters | qa-ros-performance-agent |
| Unknown / release gate | qa-orchestrator |

## Recovery

| Failure | Action |
|---------|--------|
| Build fail | fix via spectra-implementation → rebuild |
| Test fail | fix → re-run build-and-test |
| QA finding | fix → re-run affected qa-* only |
| 3+ cycles | pause; ask user |

## User touchpoints

- Share plan after planning; ask approval before large implementation.
- Status after each build/test cycle.
- Final report:

```text
## Development Complete: <title>
### Done
### Files changed
### Evidence (build, tests, QA)
### Follow-ups
```

Domain work: use [spectra-index](../spectra-index/SKILL.md).

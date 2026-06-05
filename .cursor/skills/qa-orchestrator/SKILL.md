---
name: qa-orchestrator
description: >-
  Coordinates multi-domain Spectra QA: performance, golden, memory, design,
  accessibility, API, ROS. Use for full QA sweep, release gate, or routing QA
  after changes when scope spans multiple areas.
---

# QA Orchestrator

Read `plans/QA_results.md`, `plans/QA_update.md`. Delegate to domain skills — do not fix issues in the orchestrator role.

## Sub-skills

| Skill | Domain |
|-------|--------|
| [qa-performance-agent](../qa-performance-agent/SKILL.md) | Stability / fuzz |
| [qa-regression-agent](../qa-regression-agent/SKILL.md) | Unit + golden pixels |
| [qa-memory-agent](../qa-memory-agent/SKILL.md) | ASan / RSS / VMA |
| [qa-designer-agent](../qa-designer-agent/SKILL.md) | Visual P0–P3 |
| [qa-accessibility-agent](../qa-accessibility-agent/SKILL.md) | WCAG / keyboard |
| [qa-api-agent](../qa-api-agent/SKILL.md) | Python / IPC / public API |
| [qa-ros-performance-agent](../qa-ros-performance-agent/SKILL.md) | ROS2 |

## Routing (by changed paths)

| Pattern | Required |
|---------|----------|
| `src/ui/theme/` | design, accessibility, regression |
| `src/gpu/shaders/` | regression, performance |
| `src/render/vulkan/` | regression, memory, performance |
| `src/core/` | performance, regression, api |
| `src/ipc/`, `src/daemon/`, `src/agent/` | api, performance |
| `python/` | api |
| `include/spectra/` | api, regression |
| ROS files | qa-ros-performance-agent |
| Full / unknown | all gates below |

## Full sweep order

1. Build: `-DSPECTRA_BUILD_QA_AGENT=ON -DSPECTRA_BUILD_GOLDEN_TESTS=ON`
2. **Stability** (qa-performance, seed 42) — stop if crash (exit 2)
3. **Pixel** (qa-regression) — unit gate then golden
4. **Memory** (qa-memory, ASan)
5. **Parallel:** design, accessibility, api
6. **ROS** if ROS paths changed
7. Update `plans/QA_results.md`, `plans/QA_update.md`

## Report

```text
## QA Sweep — <date>
### Gates
| Gate | Skill | Status | Notes |
| Stability | qa-performance | ✅/❌ | |
| Pixel | qa-regression | ✅/❌ | |
| Memory | qa-memory | ✅/❌ | |

### Domains
| Domain | Skill | Status | P0–P3 / count |

### Action items
- [CRITICAL] … → skill

### Docs updated
- plans/…
```

Targeted sweep: apply routing table only; run stability first if included.

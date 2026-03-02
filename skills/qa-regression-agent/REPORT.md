# QA Regression Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | 2026-03-02 14:10 |
| Unit tests | 80/80 non-GPU gate passed; 85/85 full ctest passed |
| Golden tests (core 2D) | passed |
| Golden tests (phase2) | passed |
| Golden tests (phase3) | passed |
| Golden tests (3D) | passed |
| Golden tests (3D phase3) | passed |
| Baselines last regenerated | none this session |
| Open coverage gaps | Series clipboard paste, Figure serialization roundtrip |
| SKILL.md last self-updated | 2026-02-26 (initial creation) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-02-26 | Initial file created | New agent created |

---

## Session 2026-03-02 14:10

**Unit tests**
- Total: 80 non-GPU gate + 85 full-suite ctest
- Passed: all
- Failed: 0

**Golden suite results**
| Suite | Total | Passed | Failed |
|---|---|---|---|
| golden_image_tests | 1 | 1 | 0 |
| golden_image_tests_phase2 | 1 | 1 | 0 |
| golden_image_tests_phase3 | 1 | 1 | 0 |
| golden_image_tests_3d | 1 | 1 | 0 |
| golden_image_tests_3d_phase3 | 1 | 1 | 0 |

**Failures investigated**
| Test name | Root cause | Intentional / Regression | Action taken |
|---|---|---|---|
| none | — | — | — |

**Baselines regenerated**
- none

**New goldens added**
- none

**Self-updates to SKILL.md**
- none

## Self-Improvement — 2026-03-02
Improvement: Tightened `Golden3D.CameraAngle_Orthographic` with an explicit `<=2` differing-pixels budget.
Motivation: Percent/MAE-only thresholds can pass localized projection regressions in small but critical geometry.
Change: `tests/golden/golden_test_3d.cpp` (added configurable differing-pixel limit to helper and applied it to orthographic camera golden).
Next gap: Add a golden for series clipboard paste result layout stability (REG-I1).

---

<!-- ============================================================ -->
<!-- SESSION TEMPLATE                                             -->
<!-- ============================================================ -->
<!--
## Session YYYY-MM-DD HH:MM

**Unit tests**
- Total: 59
- Passed: 59
- Failed: 0

**Golden suite results**
| Suite | Total | Passed | Failed |
|---|---|---|---|
| golden_image_tests | ? | ? | ? |
| golden_image_tests_phase2 | ? | ? | ? |
| golden_image_tests_phase3 | ? | ? | ? |
| golden_image_tests_3d | ? | ? | ? |
| golden_image_tests_3d_phase3 | ? | ? | ? |

**Failures investigated**
| Test name | Root cause | Intentional / Regression | Action taken |
|---|---|---|---|

**Baselines regenerated**
- none

**New goldens added**
- none

**Self-updates to SKILL.md**
- none
-->

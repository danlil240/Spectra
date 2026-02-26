# QA Memory Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | _(not yet run)_ |
| Last mode | — |
| Last seed | — |
| ASan/LSan result | — |
| RSS delta (full session) | — |
| Highest per-scenario RSS delta | — |
| Open issues | M1 (RSS 80–260MB), #7 (baseline stabilization), #13 (GPU tracking) |
| SKILL.md last self-updated | 2026-02-26 (initial creation) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-02-26 | Initial file created | New agent created |

---

<!-- ============================================================ -->
<!-- SESSION TEMPLATE                                             -->
<!-- ============================================================ -->
<!--
## Session YYYY-MM-DD HH:MM

**Run config**
- Seed: `42`
- Mode: `--no-fuzz` (isolation)
- Tool: ASan / Valgrind Massif / RSS-only
- Output dir: `/tmp/spectra_qa_memory_YYYYMMDD`

**Per-scenario RSS deltas**
| Scenario | RSS start (MB) | RSS end (MB) | Delta (MB) | Status |
|---|---|---|---|---|
| rapid_figure_lifecycle | ? | ? | ? | ✅ / ⚠️ |
| massive_datasets | ? | ? | ? | ✅ / ⚠️ |
| figure_serialization | ? | ? | ? | ✅ / ⚠️ |

**ASan/LSan summary**
- Leaks: 0
- UAF: 0
- Heap errors: 0

**Leaks found**
| Category | File | Description | Fixed? |
|---|---|---|---|

**Open issues updated**
| ID | Old status | New status | Notes |
|---|---|---|---|

**Self-updates to SKILL.md**
- none
-->

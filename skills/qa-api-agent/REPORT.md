# QA API Agent — Live Report

> Newest session first. Never delete old session blocks.

---

## Current Status

| Field | Value |
|---|---|
| Last run | 2026-02-27 15:57 UTC+02 |
| C++ unit tests | Targeted API: 6/6 pass; full suite: 78/78 pass |
| Python unit tests | 340/340 pass |
| Python examples smoke | Unpinned launcher: 3/3 pass (`basic_line.py`, `easy_3d.py`, `easy_live_dashboard.py` all timeout-as-expected). Rapid restart stress test: 5/5 pass (retry recovers from `Connection reset by peer`). |
| IPC roundtrip latency | `Session.list_figures()` 200 requests: avg 0.012 ms, p95 0.013 ms, max 0.016 ms |
| Open API regressions | 0 open (both fixed this session) |
| SKILL.md last self-updated | 2026-02-26 (initial creation) |

---

## Self-Update Log

| Date | Section | Reason |
|---|---|---|
| 2026-02-26 | Initial file created | New agent created |

---

## Session 2026-02-27 15:57

**C++ unit tests**
- Targeted API suite: 6/6 passed
- Full ctest suite: 78/78 passed
- Failed: 0

**Python unit tests**
- Total: 340
- Passed: 340
- Failed: 0

**Python example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line.py | ✅ | Unpinned launcher — reached interactive state, timeout-as-expected. |
| easy_3d.py | ✅ | Unpinned launcher — 5/5 rapid restart stress test pass (retry recovers from `Connection reset by peer`). |
| easy_live_dashboard.py | ✅ | Unpinned launcher — reached live loop, timeout-as-expected. |

**C++ example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line | ✅ | (not re-tested — no C++ changes) |
| animated_scatter | ✅ | (not re-tested — no C++ changes) |

**IPC codec**
- unit_test_ipc: ✅ pass
- unit_test_cross_codec: ✅ pass

**API regressions found**
| ID | Header/module | Description | Fixed? |
|---|---|---|---|
| API-2026-02-27-01 | `python/spectra/_launcher.py` | Backend auto-discovery preferred PATH over workspace build. **Fixed**: reordered `_find_backend_binary()` to check workspace build dirs (step 3) before system PATH (step 4). | ✅ |
| API-2026-02-27-02 | `python/spectra/_session.py` + `_launcher.py` | Intermittent `Connection reset by peer` during rapid restart. **Root cause**: `ensure_backend()` connected to dying backend, backend then removed socket, `_connect()` couldn't retry. **Fixed**: (a) `_can_connect()` now peeks for immediate EOF to detect dying backends, (b) `Session.__init__` retries up to 3× with `ensure_backend()` re-invocation on each attempt, (c) `_connect()` cleans up failed transport on error. | ✅ |

**Fixes applied**
| File | Change |
|---|---|
| `python/spectra/_launcher.py` | Reordered `_find_backend_binary()`: workspace build dirs checked before system PATH. Improved `_can_connect()` with recv-peek to detect dying backends. |
| `python/spectra/_session.py` | `Session.__init__` retry loop (3 attempts with backoff) re-invokes `ensure_backend()` on each retry. `_connect()` cleans up transport on failure. |

**Self-updates to SKILL.md**
- none

---

## Session 2026-02-27 15:50

**C++ unit tests**
- Targeted API suite: 6/6 passed
- Full ctest suite: 78/78 passed
- Failed: 0

**Python unit tests**
- Total: 340
- Passed: 340
- Failed: 0

**Python example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line.py | ❌ (unpinned) / ✅ (pinned) | Unpinned run failed with handshake reset. With `SPECTRA_BACKEND_PATH=/home/daniel/projects/Spectra/build/spectra-backend`, run reached interactive state and timed out as expected. |
| easy_3d.py | ❌ intermittent | With pinned backend, 5 attempts: 3 reached interactive state (timeout expected), 2 failed with `Connection reset by peer` during Session handshake. |
| easy_live_dashboard.py | ❌ (unpinned) / ✅ (pinned) | Unpinned run failed backend startup timeout. Pinned run reached live loop and timed out as expected. |

**C++ example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line | ✅ | Interactive run timed out as expected after startup (`timeout 5`). |
| animated_scatter | ✅ | Interactive run timed out as expected after startup (`timeout 5`). |

**IPC codec**
- unit_test_ipc: ✅ pass
- unit_test_cross_codec: ✅ pass
- Roundtrip latency: avg 0.012 ms, p95 0.013 ms, max 0.016 ms (`Session.list_figures()` x200; connect succeeded on retry attempt 2)

**API regressions found**
| ID | Header/module | Description | Fixed? |
|---|---|---|---|
| API-2026-02-27-01 | `python/spectra/_launcher.py` | Backend auto-discovery still prefers PATH binary over workspace build unless `SPECTRA_BACKEND_PATH` is exported; unpinned smoke run remains unstable. | ❌ |
| API-2026-02-27-02 | Python handshake path (`_session.py` + backend startup) | Intermittent `Connection reset by peer` still reproducible in repeated `easy_3d.py` runs even with pinned backend (2/5 failures). | ❌ |

**Self-updates to SKILL.md**
- none

---

## Session 2026-02-27 15:41

**C++ unit tests**
- Targeted API suite: 6/6 passed
- Full ctest suite: 78/78 passed
- Failed: 0

**Python unit tests**
- Total: 340
- Passed: 340
- Failed: 0

**Python example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line.py | ✅ | Isolated run passed (expected interactive timeout). |
| easy_3d.py | ✅ / ❌ (intermittent) | Isolated run passed; fails intermittently in rapid restart loops with `Connection reset by peer` during `Session()` handshake. |
| easy_live_dashboard.py | ✅ / ❌ (intermittent) | Isolated run passed; same intermittent handshake reset under rapid restart loops. |

**C++ example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line | ✅ | Ran with timeout; interactive run expected to block until window close. |
| animated_scatter | ✅ | Ran with timeout; interactive run expected to block until window close. |

**IPC codec**
- unit_test_ipc: ✅ pass
- unit_test_cross_codec: ✅ pass
- Roundtrip latency: avg 0.021 ms, p95 0.035 ms, max 0.071 ms (`Session.list_figures()` x200)

**API regressions found**
| ID | Header/module | Description | Fixed? |
|---|---|---|---|
| API-2026-02-27-01 | `python/spectra/_launcher.py` | Backend auto-discovery prefers PATH (`~/.local/bin/spectra-backend`) over local workspace build; can launch stale backend and cause startup failures unless `SPECTRA_BACKEND_PATH` is set. | ❌ |
| API-2026-02-27-02 | Python handshake path (`_session.py` + backend startup) | Intermittent `Connection reset by peer` during example startup in rapid restart loops; isolated runs pass, suggesting startup race/flaky handshake under churn. | ❌ |

**Self-updates to SKILL.md**
- none

---

<!-- ============================================================ -->
<!-- SESSION TEMPLATE                                             -->
<!-- ============================================================ -->
<!--
## Session YYYY-MM-DD HH:MM

**C++ unit tests**
- Total: ?
- Passed: ?
- Failed: 0

**Python unit tests**
- Total: ?
- Passed: ?
- Failed: 0

**Python example smoke tests**
| Example | Result | Notes |
|---|---|---|
| basic_line.py | ✅ / ❌ | |
| easy_3d.py | ✅ / ❌ | |
| easy_live_dashboard.py | ✅ / ❌ | |

**IPC codec**
- test_codec: ✅ pass
- test_cross_codec: ✅ pass
- Roundtrip latency: ? ms

**API regressions found**
| ID | Header/module | Description | Fixed? |
|---|---|---|---|

**Self-updates to SKILL.md**
- none
-->

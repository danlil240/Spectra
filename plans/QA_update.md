# Spectra QA Agent — Capability Gaps & Backlog

**Last updated:** 2026-06-04

---

## Open Gaps

### G-6 — Design-review capture does not include Settings panel states

**Status:** Resolved 2026-06-04 — added shots `57_settings_appearance_night` … `62_settings_ui_defaults` (6 frames); expected design-review count **63** (57 core + `45b` + 6 settings).

### G-5 — `mouse_click` automation handler does not inject into ImGui IO

**Impact:** All ImGui widget clicks (tab switches, combo opens, buttons, checkboxes) silently fail when issued through MCP `mouse_click`. The call returns `ok` but the widget does not respond. Discovered during Settings panel visual QA 2026-05-17.  
**Root cause:** `handlers_input.cpp` `mouse_click` calls `input_handler.on_mouse_button()` which routes through `InputHandler` (axes hit-testing only) and returns early when `active_axes_` is null. `ImGui_ImplGlfw_MouseButtonCallback` is never called, so ImGui's IO never sees the press.  
**File:** `src/ui/automation/handlers/handlers_input.cpp` — `mouse_click` lambda  
**Suggested fix:** After calling `input_handler.on_mouse_button()`, also inject into ImGui's IO directly:
```cpp
auto& io = ImGui::GetIO();
io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
io.AddMouseButtonEvent(btn, true);   // press
io.AddMouseButtonEvent(btn, false);  // release
```
The correct ImGui context must be active (set via `wctx->imgui_context`) before calling these.

---

### G-1 — `move_figure` ownership tracking gap

**Impact:** Cosmetic warning during fuzz tab-detach when source window has already released a figure. No crash.  
**Gap:** The `WindowManager::move_figure` path does not guard against a figure that was concurrently closed or transferred before the fuzz-injected move command executes.  
**File:** `src/ui/app/window_manager.cpp`  
**Suggested fix:** Check figure existence in source window before attempting move; log at DEBUG rather than WARN if not found.

---

### G-2 — Fuzz RSS growth reporting threshold tuned too tightly for LargeDataset weight

**Impact:** 65 memory warnings generated during every standard fuzz pass because `LargeDataset` actions accumulate series until `CloseFigure` actions reclaim them.  
**Gap:** The 100 MB RSS threshold is correct for detecting real leaks, but the fuzz action weights produce legitimate growth beyond this during normal operation. The report is noisy.  
**Suggested fix:** Track per-fuzz-action accumulated series count; report memory warnings only when growth persists more than N frames after `CloseFigure` actions have had a chance to run. Alternatively, suppress the warning when `LargeDataset` action count in the current window exceeds a threshold.

---

### G-3 — GPU test exclusion leaves BUG-1/2/3 unverified each run

**Impact:** Three HIGH-severity crashes (Golden3D teardown SIGSEGV, multi-window heap corruption, golden test SIGSEGV) are not validated by the standard QA sweep because they require GPU execution.  
**Gap:** The `spectra_qa_agent --seed 42 --duration 120` run does not exercise GPU rendering paths that `golden_image_tests_3d` and `unit_test_multi_window` test.  
**Suggested fix:** Add a GPU-QA pass gate (`ctest --test-dir build -L gpu`) to the QA workflow checklist even if it runs separately.

---

### G-4 — No regression coverage for Python API codec layer

**Impact:** BUG-4 (TLV/FlatBuffers codec mismatch) and BUG-5 (socket API change) are not detected by the QA agent or ctest non-GPU sweep.  
**Gap:** Python integration tests (`tests/python/`) are not included in the `ctest -LE gpu` sweep.  
**Suggested fix:** Add Python test suite to the standard QA checklist as a separate step: `cd python && python -m pytest tests/ -x`.

---

## Completed / Resolved

| Item | Resolution | Date |
|------|------------|------|
| Frame spike max 240 ms (BUG-10) | Max bounded to ~100 ms in current build | 2026-05-16 |
| `massive_datasets` +38 MB retention (BUG-8) | Not reproduced in seed-42 run; +30 MB with no post-scenario retention | 2026-05-16 |

# Spectra QA Agent — Capability Gaps & Backlog

**Last updated:** 2026-06-17

---

## Open Gaps

### G-9 — MCP fuzz: export/clipboard SIGSEGV after stress

**Observed:** 2026-06-16 full MCP fuzz session — `file.copy_to_clipboard` reliably kills process (isolated repro: single command after 50 fuzz steps); `file.export_png` crashes command-exhaustion on both `spectra` and `spectra-ros`. GPU path survives 200 fuzz steps; llvmpipe segfaults at step 41 (TabDetach).  
**Impact:** Export and clipboard unusable after stress; MCP session dies; blocks full command coverage.  
**Suggested fix:** ASan repro: `fuzz_reset seed=42` → 50 steps → `file.copy_to_clipboard`; audit export with detached figures / multi-window state; handle missing `xclip` without crashing.

### G-10 — Side-effect commands not suppressed in automation mode (partial fix)

**Observed 2026-06-17:** `handlers_fuzz.cpp` denylist now skips `help.show`, `data.export_html_table`, `accessibility.sonify_series` on fuzz ExecuteCommand — but direct `execute_command` and command exhaustion still run them. `data.export_html_table` writes `spectra_data.html`; `help.show` forks `xdg-open` with `SPECTRA_NO_NATIVE_DIALOGS=1`. Skipped fuzz hits log only `kind=fuzz id=ExecuteCommand` (no `result=skipped` audit).  
**Suggested fix:** No-op stubs in command handlers when `!native_dialogs_enabled()`; log `kind=command id=<cmd> result=skipped`; extend `py_fuzz.py` SKIP for exhaustion.

### G-13 — Fuzz input actions bypass ImGui IO

**Observed 2026-06-17:** Fuzz `MouseClick`, `KeyPress`, `MouseScroll` in `handlers_fuzz.cpp` use `input_handler` only. MCP `mouse_click` injects ImGui IO (`handlers_input.cpp`). Fuzz clicks/scrolls miss menus, panels, and scrollable widgets.  
**Suggested fix:** Share ImGui injection helper between MCP and fuzz handlers.

### G-14 — py_fuzz false-positive on skipped_command ui.action

**Observed 2026-06-17:** Harness warns when `details.skipped_command` is set but no `kind=command` log appears — expected for denied commands. 11 false WARNINGs in 600-step run.  
**Suggested fix:** Only check `command_id` (not `skipped_command`); or require product to emit `result=skipped` log.

### G-12 — `list_commands` returns disabled/stale command IDs

**Observed:** `view.toggle_3d`, `figure.tab_close`, `figure.tab_new`, `accessibility.high_contrast`, `data.clear_series` appear in `list_commands` but `execute_command` returns "not found or disabled".  
**Suggested fix:** Filter `list_commands` to enabled-only; or fix registration so listed commands are runnable.

### G-11 — spectra-ros launch requires manual ROS env in agent shells

**Observed:** `./build/spectra-ros` fails linker without `source /opt/ros/jazzy/setup.zsh`.  
**Suggested fix:** Wrapper script or RPATH for ROS libs; document in AGENTS.md fuzz section.

### G-8 — CMake `CMAKE_ROOT` broken in some agent shells

**Observed:** 2026-06-05 design-review — `cmake -B build` fails with `Could not find CMAKE_ROOT`; manual `clang++` + `ar rs` workaround used to relink `spectra_qa_agent`.  
**Impact:** Agents cannot run canonical `cmake --build` until CMake install is repaired.  
**Suggested fix:** Reinstall `cmake` package or fix `CMAKE_ROOT` env; prefer `make -f build/Makefile <target>` when CMake CLI is broken.

### G-7 — QA agent crashes without display (GLFW init failure)

**Observed:** 2026-06-05 targeted sweep — `spectra_qa_agent --seed 42` SIGSEGV in `rapid_figure_lifecycle` when GLFW fails to init (no `DISPLAY`/Xvfb on agent VM). Vulkan (lavapipe) initializes; crash stack in `libvulkan_lvp.so`. Golden headless tests pass in same env.  
**Impact:** Stability gate cannot run on display-less CI/agent hosts without Xvfb.  
**Suggested fix:** Install `xvfb` and wrap QA agent in `xvfb-run -a`; or add headless GLFW fallback for QA scenarios that do not need a visible window.


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

### G-6 — `py_fuzz.py` ROS launch uses broken `bash -lc` wrapper

**Impact:** `python3 scripts/mcp_fuzz/py_fuzz.py ros` fails when invoked via `bash -lc 'source /opt/ros/jazzy/setup.zsh'` — zsh-specific setup script errors under bash.  
**Gap:** Fuzz harness cannot launch `spectra-ros` from bash-only CI shells without manual zsh wrapper.  
**Suggested fix:** Change `py_fuzz.py` launch to `zsh -lc 'source ... && exec ./build/spectra-ros'` or detect shell and use `setup.bash`.

### G-7 — `spectra-ros` fuzz crash at step 151 (`WindowDrag`)

**Impact:** ROS adapter dies mid-fuzz at seed 42 before completing 200 steps; ROS panel/topic coverage never reached.  
**Gap:** No unit test or ASan trace for WindowDrag after LargeDataset+TabDetach stress.  
**Suggested fix:** Add `tests/unit` replay harness for fuzz step 151 with seed 42; run under ASan.

### G-8 — Export/clipboard commands crash only after fuzz state corruption

**Impact:** `file.copy_to_clipboard` and `file.export_svg` pass in isolation but SIGSEGV after fuzz or command exhaustion — hard to catch in unit tests.  
**Gap:** `handlers_fuzz.cpp` denylist and `py_fuzz.py` SKIP set exist but C++ fuzz path may still hit these via `ExecuteCommand` action.  
**Suggested fix:** Denylist at command registry level; add post-fuzz integration test: 50 fuzz steps → clipboard probe.

### G-13 — MCP port conflict when fuzzing `spectra-ros` after `spectra`

**Observed:** 2026-06-16 15:09 session — launching `spectra-ros` without killing `spectra` leaves port 8765 bound; ros binds 8766, fuzz harness still POSTs to 8765.  
**Impact:** Wrong process fuzzed; ros coverage invalid; false PASS/FAIL signals.  
**Suggested fix:** `py_fuzz.py` launch() must pkill both `spectra` and `spectra-ros`; poll until 8765 free; assert MCP health on expected port before fuzz loop.

### G-14 — `py_fuzz.py` ROS launch uses `setup.bash` but agent docs say `setup.zsh`

**Observed:** `py_fuzz.py` line 129 uses `setup.bash`; user env and terminal history use `setup.zsh`. Both work when shell matches; mismatch causes silent launch failures in zsh-only agent shells.  
**Suggested fix:** Detect shell or use `zsh -lc` consistently; document in fuzz agent instructions.

---

## Completed / Resolved

| Item | Resolution | Date |
|------|------------|------|
| Frame spike max 240 ms (BUG-10) | Max bounded to ~100 ms in current build | 2026-05-16 |
| `massive_datasets` +38 MB retention (BUG-8) | Not reproduced in seed-42 run; +30 MB with no post-scenario retention | 2026-05-16 |

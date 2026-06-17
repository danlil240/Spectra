# Spectra MCP Fuzzer — Live Report

## Current Status
Last run: **2026-06-17 10:08** — button hunt (primary) + 100 fuzz steps (`spectra`, seed 42). **0 broken buttons** (ERROR); process survived both phases. Issues JSON: `/tmp/button_hunt_spectra_issues.json`.

---

## Session 2026-06-17 10:08 (button hunt)
- Binary: `./build/spectra` (DISPLAY=:1, `SPECTRA_NO_NATIVE_DIALOGS=1`, `SPECTRA_LOG_LEVEL=debug`, RTX 3080 Ti)
- Harness: `python3 scripts/mcp_fuzz/button_hunt.py spectra`, then `FUZZ_STEPS=100 python3 scripts/mcp_fuzz/py_fuzz.py spectra`
- Seed: 42
- Duration: ~22 s total (button hunt ~9 s, fuzz ~12 s)
- Outcome: **PASS** (0 ERROR broken-button issues)

### Button hunt summary
| Phase | Result |
|-------|--------|
| Command registry (all safe IDs from `list_commands`) | 0 `result=miss`, 0 missing `ui.action` |
| Menu bar (File…Settings + submenu rows) | 0 silent clicks |
| Toolbar icon strip (y≈52, right side) | 0 silent clicks |
| Panel toggles + nav-rail/inspector sweep | 0 non-functional toggles/clicks |

### Broken-button classification
| Class | Count |
|-------|-------|
| Silent click (only `mcp_click`) | 0 |
| Command miss (`result=miss`) | 0 |
| No `ui.action` at all | 0 |
| **Total ERROR** | **0** |

### Fuzz follow-up (100 steps)
| Metric | Value |
|--------|-------|
| Steps completed | 100/100 |
| Alive after loop | Yes |
| Harness issues (`/tmp/pyfuzz_spectra_issues.json`) | `[]` |
| `result=miss` in stderr log | 0 |

### Issues
None (broken-button hunt clean).

### Coverage gaps
- `spectra-ros` not exercised
- Settings dialog sub-buttons (Appearance / Shortcuts / UI Defaults) not spot-checked this session
- Command palette row clicks not manually probed
- Context menu (right-click canvas) not manually probed

### Next session
- Run `button_hunt.py ros` when ROS env available
- Add Settings-dialog and command-palette click phases to `button_hunt.py`

---

## Session 2026-06-17 09:26–09:32
- Binary: `./build/spectra` (DISPLAY=:1, `SPECTRA_NO_NATIVE_DIALOGS=1`, `SPECTRA_LOG_LEVEL=debug`)
- Seeds: 42 (400 steps), 1337 (200 steps)
- Harness: `FUZZ_STEPS=400 SEED=42 python3 scripts/mcp_fuzz/py_fuzz.py spectra`, `SEED=1337` rerun, targeted bursts (resize×20, TabDetach×8, figure lifecycle×10), `command_probe.py spectra`
- Duration: ~90 s total
- Outcome: **ISSUES** (10 bugs, 0 crashes)

### Fuzz loop summary
| Run | Steps | Alive after | Harness issues |
|-----|-------|-------------|----------------|
| seed 42 | 400/400 | Yes | 8 WARNING (skipped-command ui.action) |
| seed 1337 | 200/200 | Yes | 3 WARNING (skipped-command ui.action) |
| targeted bursts | resize/TabDetach/lifecycle | Yes | 0 (SplitDock/WindowDrag log correctly in isolation) |
| command_probe | all safe + 6 isolated | Yes | 0 |

### Issues (10 found)

1. **[ERROR] Skipped fuzz ExecuteCommand lacks per-command ui.action** — When `is_fuzz_denied_command` skips (e.g. `file.save_figure`, `help.show`), only `kind=fuzz id=ExecuteCommand` logs; no `kind=command id=<cmd> result=skipped`. Harness flags 11 steps (seed 42: 27,95,115,127,180,207,240,267; seed 1337: 50,134,178). Repro: `FUZZ_STEPS=400 SEED=42 python3 scripts/mcp_fuzz/py_fuzz.py spectra`; inspect `/tmp/pyfuzz_spectra.log`.

2. **[ERROR] `data.export_html_table` writes file in automation mode** — `execute_command` creates `spectra_data.html` in cwd despite `SPECTRA_NO_NATIVE_DIALOGS=1`. Repro: launch with automation → `execute_command data.export_html_table`. Evidence: `/tmp/sidefx_test.log`, file `spectra_data.html`.

3. **[ERROR] `help.show` opens browser in automation mode** — No automation guard; forks `xdg-open`. Log: `Opening documentation` + `Opening in existing browser session` in `/tmp/pyfuzz_spectra.log` (command exhaustion phase).

4. **[ERROR] `accessibility.sonify_series` writes WAV without automation guard** — `commands_data.cpp` always writes `spectra_sonify.wav`; only blocked on fuzz ExecuteCommand denylist, not direct `execute_command`. Repro: automation launch → add series → `execute_command accessibility.sonify_series`.

5. **[ERROR] Fuzz MouseClick bypasses ImGui IO** — `handlers_fuzz.cpp` calls `input_handler.on_mouse_button` only; MCP `mouse_click` injects ImGui IO. Fuzz clicks miss menu/panel/widgets. Repro: `fuzz_step {"action":"MouseClick"}` — no `kind=imgui`/`kind=widget` for menu-region hits.

6. **[ERROR] Fuzz KeyPress bypasses ImGui IO** — Random keys 32–126 via `input_handler.on_key` only; can spuriously trigger shortcuts without focused-widget semantics.

7. **[ERROR] Fuzz MouseScroll bypasses ImGui IO** — `input_handler.on_scroll` only; scrollable panels may not receive fuzz scroll events.

8. **[WARNING] `move_figure` ownership warning during TabDetach fuzz** — `/tmp/pyfuzz_spectra.log`: `move_figure: source window 5 does not have figure 5`. Repro: extended fuzz with TabDetach actions (seed 42).

9. **[WARNING] `figure.close` not in `py_fuzz.py` exhaustion SKIP** — Command exhaustion runs `figure.close`, can close last figure / destabilize post-fuzz state. Repro: full `py_fuzz.py` run through exhaustion phase.

10. **[WARNING] TabDetach silent no-op with single figure** — `fuzz_step {"action":"TabDetach"}` returns `ok` with `details:{}` when `<2` figures; no diagnostic. Repro: fresh instance, one figure, forced TabDetach.

### Coverage gaps
- `spectra-ros` not exercised this session
- Step screenshots (`/tmp/pyfuzz_step_*.png`) not retained; burst captures at `/tmp/burst_resize_19.png`, `/tmp/burst_3d.png`
- ROS nav-rail panel clicks not tested (G-5)
- 3D orbit interaction not validated visually

### Next session
- Add `result=skipped` ui.action in `handlers_fuzz.cpp` for denied commands; fix `py_fuzz.py` to not warn on `skipped_command`
- No-op stubs for `help.show`, `data.export_html_table`, `accessibility.sonify_series` when `native_dialogs_enabled()==false`
- Align fuzz MouseClick/KeyPress/Scroll with MCP input handlers (ImGui IO injection)

---

## Session 2026-06-16 16:32–16:33 (VERIFICATION)
- Binaries: `./build/spectra`, `./build/spectra-ros` (GPU, DISPLAY=:1, ROS jazzy via harness)
- Seeds: 42 (full 200-step runs both binaries); 1337 (ros 60-step spot-check)
- Commands: `python3 scripts/mcp_fuzz/py_fuzz.py spectra|ros`, `command_probe.py spectra`
- Outcome: **PASS** (individual runs) — see evidence below

### Fuzz loop summary (verified)
| Binary | Fuzz steps | Alive after loop | Post-fuzz clipboard/export |
|--------|------------|------------------|----------------------------|
| `spectra` | **200/200** | Yes | `file.copy_to_clipboard` ok (no SIGSEGV) |
| `spectra-ros` | **200/200** | Yes | `file.copy_to_clipboard` ok (no SIGSEGV) |

### Evidence
| Check | Result |
|-------|--------|
| `/tmp/pyfuzz_spectra_issues.json` | `[]` |
| `/tmp/pyfuzz_ros_issues.json` | `[]` (after clean solo run) |
| `/tmp/command_probe.json` (isolated) | All 6 crash-prone commands `ok` |
| `/tmp/pyfuzz_spectra.jsonl` | Side-effect cmds show `skipped_command` during fuzz |
| `list_commands` stale IDs | `figure.tab_close`, `tab_new`, `high_contrast`, `clear_series`, `toggle_3d` **absent** |
| `get_state` bootstrap | `active_figure_id: null` (no UINT64_MAX) |
| `/tmp/pyfuzz_spectra-ros.log` | `Figure copied to clipboard`; no `xclip` SIGSEGV |

### Issues (2 open)

6. **[ERROR] MCP port conflict on back-to-back `spectra` → `spectra-ros` without gap** — Intermittent when launching ros immediately after spectra post-fuzz cleanup. Mitigations landed (`kill_stale_spectra` kills `xclip`/`wl-copy`, port-release wait, MCP `FD_CLOEXEC`, clipboard child FD sweep); **2 s gap or solo runs are reliable**. Still fails occasionally if orphaned `xclip` held port 8765 (fixed in code, verify in next dual run).

12. **[WARNING] MCP `mouse_click` ineffective on ROS nav-rail/panels (G-5)** — 5 panel click coordinates; `get_state` unchanged. Not addressed in code this pass. Workaround: `execute_command` panel IDs instead of `mouse_click`.

**Also observed (not counted):** `move_figure` stale ownership WARN during TabDetach fuzz (harmless); `figure.close` during command exhaustion can terminate the process (expected — exhaustion intentionally runs all commands).

### Coverage gaps
- ROS topic list / bag controls not exercised via MCP
- `panel.open_settings` post-fuzz not re-tested
- Back-to-back dual-binary harness without sleep still flaky (issue #6)

### Next session
- **ROS panels:** wire `execute_command` burst for nav-rail/panel IDs (workaround G-5)
- **Harness:** confirm back-to-back `spectra`→`ros` with zero sleep after xclip FD fix
- **Optional:** skip `figure.close` in `command_exhaustion` to avoid false-positive “crash” at end of ros run

---

## Session 2026-06-16 15:09–15:15 (prior — superseded)
- 12 issues logged; clipboard SIGSEGV, UINT64_MAX sentinel, ros WindowDrag crashes, side-effect fuzz hits.
- Fixed in code 2026-06-16; verified in session above.

## Session 2026-06-16 14:46–14:50 (prior)
- Superseded.

## Session 2026-06-16 14:27 (prior)
- Superseded.

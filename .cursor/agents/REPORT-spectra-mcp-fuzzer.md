# Spectra MCP Fuzzer — Live Report

## Current Status
Last run: **2026-06-16 15:09–15:15** — Full dual-binary session (`spectra` + `spectra-ros`). **12 issues** logged with fresh evidence. Primary repro: `file.copy_to_clipboard` SIGSEGV after 200 fuzz steps (seed 42). `spectra` survives 200 fuzz steps on GPU; `spectra-ros` dies at fuzz step 151.

---

## Session 2026-06-16 15:09–15:15 (FULL)
- Binaries: `./build/spectra` (GPU, DISPLAY=:1), `./build/spectra-ros` (GPU, ROS jazzy)
- Seeds: 42 (`spectra`), 42/1337 (`spectra-ros` + deep probes)
- Fuzz steps: 200 target (`spectra` complete; `spectra-ros` crash step 151)
- Duration: ~14s (spectra py_fuzz) + ~23s (ros py_fuzz) + ~152s (deep probes) + ~28s (command_probe)
- Outcome: **ISSUES** — fuzz loop passes on `spectra`; export/clipboard kills process post-fuzz
- Session logs: `/tmp/pyfuzz_spectra.jsonl`, `/tmp/pyfuzz_ros.jsonl`, `/tmp/spectra_deep_probe_issues.jsonl`, `/tmp/command_probe.json`
- Issue summaries: `/tmp/pyfuzz_spectra_issues.json`, `/tmp/pyfuzz_ros_issues.json`, `/tmp/spectra_deep_probe_summary.json`
- Screenshots: `/tmp/pyfuzz_step_{50,100,150,200}.png`, `/tmp/deep_probe_ros_panels.png`
- Stderr: `/tmp/pyfuzz_spectra.log`, `/tmp/pyfuzz_spectra-ros.log`

### Fuzz loop summary
| Binary | Fuzz steps completed | Process alive after loop | Post-fuzz crash |
|--------|---------------------|--------------------------|-----------------|
| `spectra` | **200/200** | Yes | `file.copy_to_clipboard` SIGSEGV |
| `spectra-ros` | **151/200** | No (step 151 `WindowDrag`) | N/A — died in loop |

All 16 weighted fuzz actions exercised on `spectra` (TabDetach, LargeDataset, WindowResize, ExecuteCommand hits including side-effect commands). `spectra-ros` ROS panel clicks attempted via deep probe.

### Issues (12)

1. **[CRITICAL] SIGSEGV on `file.copy_to_clipboard` after 200 fuzz steps** — Reliable repro (3/3 this session). `fuzz_reset seed=42` → create_figure + add_series → 200× `fuzz_step` → `execute_command file.copy_to_clipboard` → ping fails. Evidence: `/tmp/pyfuzz_spectra_issues.json`, `/tmp/pyfuzz_spectra.log` line 32 (`xclip: not found` immediately before crash).

2. **[CRITICAL] SIGSEGV on `file.copy_to_clipboard` during command exhaustion** — Fresh instance; crashes at ~30th command after `figure.tab_9`. Isolated run on fresh instance passes. Evidence: `/tmp/command_probe.json` (`"status":"CRASH"` at `file.copy_to_clipboard`; isolated section `"status":"ok"`).

3. **[CRITICAL] SIGSEGV on `file.export_png` / `file.export_svg` after state corruption** — Isolated probes crash when run after exhaustion/dead process cascade; pass on completely fresh instance per `command_probe.json` isolated section. Deep probe also hit SIGSEGV on both export commands. Evidence: `/tmp/spectra_deep_probe_summary.json`, `/tmp/command_probe.json`.

4. **[CRITICAL] `spectra-ros` crash at fuzz step 151 (`WindowDrag`)** — Seed 42, after LargeDataset + TabDetach stress. Last logged action: `WindowDrag` at (1222, 702). Evidence: `/tmp/pyfuzz_ros.jsonl` step 151, `/tmp/pyfuzz_ros_issues.json`.

5. **[CRITICAL] `spectra-ros` crash at fuzz step 55 (seed 1337)** — Deep probe with seed 1337; distinct from step-151 repro. Evidence: `/tmp/spectra_deep_probe_summary.json`.

6. **[ERROR] MCP port conflict when launching `spectra-ros` without killing `spectra`** — Port 8765 in use; ros falls back to 8766, fuzz harness still targets 8765 → wrong process fuzzed or stale MCP. Evidence: `/tmp/pyfuzz_spectra-ros.log` lines 16–17 (`Port 8765 already in use`).

7. **[ERROR] `spectra-ros` requires ROS env sourcing** — `./build/spectra-ros` fails linker without `source /opt/ros/jazzy/setup.zsh` (`libservice_msgs__rosidl_generator_py.so`). Evidence: terminal history, `/tmp/pyfuzz_spectra-ros.log`.

8. **[ERROR] Stale/disabled commands in `list_commands`** — `figure.tab_close`, `figure.tab_new`, `accessibility.high_contrast`, `data.clear_series` fail at runtime; `view.toggle_3d` listed but disabled. Re-verified via prior targeted probe + command_probe exhaustion path.

9. **[WARNING] `active_figure_id` UINT64_MAX sentinel at bootstrap** — `get_state` when `figure_count: 0` returns `18446744073709551615`. Both binaries. Evidence: `/tmp/pyfuzz_spectra_issues.json`, `/tmp/pyfuzz_ros_issues.json`.

10. **[WARNING] `xclip` not installed — clipboard path broken** — `sh: 1: xclip: not found` in stderr; may contribute to MCP-C1 crash instead of graceful error. Evidence: `/tmp/pyfuzz_spectra.log` line 32.

11. **[WARNING] Side-effect commands execute during fuzz ExecuteCommand hits** — `help.show` opens browser (`Opening in existing browser session`), `accessibility.sonify_series` writes `spectra_sonify.wav`, `data.export_html_table` writes `spectra_data.html` to cwd unprompted. Evidence: `/tmp/pyfuzz_spectra.log` lines 17–21, 25.

12. **[WARNING] MCP `mouse_click` ineffective on ROS nav-rail/panels (G-5)** — 5 panel click coordinates; `get_state` unchanged. Evidence: `/tmp/deep_probe_ros_panels.png`, `/tmp/spectra_deep_probe_summary.json`.

**Also observed (not counted separately):** `move_figure` stale ownership WARN during TabDetach fuzz (`/tmp/pyfuzz_spectra.log` line 29); export commands pass in isolation but fail after fuzz state corruption (state-dependent crash class).

### Coverage gaps
- Command exhaustion incomplete on both binaries — stops at `file.copy_to_clipboard` crash (~30 commands)
- `file.export_svg` never reached in exhaustion (crash on clipboard first)
- ROS topic list / bag controls not exercised — ros died at step 151 before panel burst
- `panel.open_settings` post-fuzz SIGSEGV not re-tested this session (prior flaky repro)
- CloseFigure+TabDetach ~step 41 intermittent crash not re-tested on GPU

### Next session
- **Script fix:** `py_fuzz.py` must `pkill` both binaries and verify MCP port before ros launch; use `zsh -lc` for ROS setup (G-6)
- **Denylist:** Add `file.copy_to_clipboard`, `file.export_png`, `file.export_svg`, side-effect commands to `handlers_fuzz.cpp` ExecuteCommand denylist
- **ASan repro:** `fuzz_reset seed=42` → 50 fuzz steps → `file.copy_to_clipboard` with detached figures
- **New burst:** ROS-specific panel command IDs via `execute_command` instead of `mouse_click` (workaround G-5)

---

## Session 2026-06-16 14:46–14:50 (prior)
- 14 issues logged; superseded by 15:09–15:15 session above for current status.
- See git history for full prior issue list.

## Session 2026-06-16 14:27 (prior)
- Binary: `./build/spectra`
- Outcome: **CRASH** during command-exhaustion (~40 commands)
- Superseded by later sessions.

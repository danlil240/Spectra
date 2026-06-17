#!/usr/bin/env python3
"""Hunt for non-functioning buttons in Spectra via MCP.

A button/command is **broken** when:
- `execute_command` succeeds but `[ui.action] kind=command result=miss`
- `mouse_click` succeeds but only `kind=mcp_click` logs (silent ImGui miss)
- A listed command never produces any `kind=command` log line

Usage:
  python3 scripts/mcp_fuzz/button_hunt.py [spectra|ros]
  python3 scripts/mcp_fuzz/button_hunt.py --no-launch   # live instance

Env: SPECTRA_ROOT, DISPLAY, SPECTRA_MCP_URL, SPECTRA_FUZZ_LOG, SEED
Outputs: /tmp/button_hunt_<tag>_issues.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from ui_action_log import StderrActionTracker, classify_interaction  # noqa: E402
from nav_rail_layout import (  # noqa: E402
    discover_nav_rail_centers,
    expected_buttons,
    nav_rail_click_positions,
    sanitize_id,
)

from menu_bar_hunt import (  # noqa: E402
    MENU_EFFECTS,
    MENU_TOP_YS,
    discover_top_menu_x,
    first_actionable_label,
    is_tab_bar_region,
    menu_item_y,
    menu_log_id,
    open_menu,
    skip_menu_label,
)

# Reuse launch/MCP helpers from py_fuzz.
import py_fuzz  # noqa: E402

SKIP_COMMANDS = py_fuzz.SKIP | {
    "figure.close",
}

PANEL_TOGGLE_COMMANDS = (
    "panel.toggle_inspector",
    "panel.toggle_timeline",
    "panel.toggle_curve_editor",
    "panel.toggle_topics",
    "panel.toggle_plugins",
    "panel.toggle_nav_rail",
    "panel.toggle_data_editor",
)

ISSUES: list[dict[str, str]] = []


def issue(sev: str, title: str, detail: str) -> None:
    entry = {"severity": sev, "title": title, "detail": detail}
    ISSUES.append(entry)
    print(f"[{sev}] {title}: {detail[:240]}")


def pump(count: int = 3) -> None:
    py_fuzz.mcp("pump_frames", {"count": count})


def dismiss_ui() -> None:
    py_fuzz.mcp("dismiss_ui_capture")
    pump(2)


def safe_click(x: int, y: int) -> None:
    """Click while avoiding the tab strip; dismiss only if tab drag is active."""
    if is_tab_bar_region(x, y):
        return
    py_fuzz.mcp("mouse_click", {"x": x, "y": y})
    state = get_app_state()
    if state.get("ui", {}).get("tab_drag_active"):
        dismiss_ui()


def window_size() -> tuple[int, int]:
    body = py_fuzz.payload(py_fuzz.mcp("get_window_size"))
    return int(body.get("width", 1280)), int(body.get("height", 720))


def command_bar_targets(width: int) -> list[tuple[int, int, str]]:
    """Icon strip along the top toolbar (right side)."""
    y = 52
    return [(width - offset, y, f"toolbar@{offset}") for offset in range(36, 420, 32)]


def get_app_state() -> dict:
    return py_fuzz.payload(py_fuzz.mcp("get_state"))


def state_changed(before: dict, after: dict, expect: str | None) -> bool:
    """Return True when the expected UI/panel side effect occurred."""
    if expect is None:
        return True
    if expect.startswith("interaction_mode:"):
        mode = expect.split(":", 1)[1]
        return after.get("ui", {}).get("interaction_mode") == mode
    if expect.startswith("transform_dialog_open:"):
        want = expect.split(":", 1)[1] == "true"
        return after.get("ui", {}).get("transform_dialog_open") is want
    if expect.startswith("panel:"):
        panel_id = expect.split(":", 1)[1]
        before_vis = before.get("panels", {}).get(panel_id)
        after_vis = after.get("panels", {}).get(panel_id)
        if before_vis is None or after_vis is None:
            return False
        return before_vis != after_vis
    return True


def menu_effect_changed(before: dict, after: dict, effect: str) -> bool:
    if effect.startswith("panel:"):
        return state_changed(before, after, effect)
    if effect == "nav_rail_visible":
        return before.get("ui", {}).get("nav_rail_visible") != after.get("ui", {}).get(
            "nav_rail_visible"
        )
    if effect == "theme_settings_visible":
        return before.get("ui", {}).get("theme_settings_visible") != after.get("ui", {}).get(
            "theme_settings_visible"
        )
    if effect == "figure_count":
        return after.get("figure_count", 0) > before.get("figure_count", 0)
    return True


def hunt_menus(tracker: StderrActionTracker, width: int, height: int) -> None:
    """Exercise every menu bar item from list_menus; verify menu log + side effects."""
    listed = py_fuzz.payload(py_fuzz.mcp("list_menus"))
    menus = listed.get("menus", [])
    if not menus:
        issue("WARNING", "list_menus returned empty", json.dumps(listed)[:200])
        return

    def click_at(x: int, y: int) -> None:
        safe_click(x, y)

    menu_x: dict[str, int] = {}
    item_y: dict[str, dict[str, int]] = {}

    for menu in menus:
        name = menu.get("name", "")
        items = menu.get("items", [])
        probe = first_actionable_label(items)
        if not probe:
            continue
        x = discover_top_menu_x(
            tracker,
            probe,
            width,
            pump_fn=pump,
            click_fn=click_at,
        )
        if x is None:
            issue("ERROR", "Top-level menu not found", f"menu={name} probe={probe}")
            continue
        menu_x[name] = x
        item_y[name] = {}
        for idx, item in enumerate(items):
            label = item.get("label", "")
            if item.get("separator") or not label or skip_menu_label(label):
                continue
            if item.get("enabled") is False:
                continue
            item_y[name][menu_log_id(label)] = menu_item_y(items, idx)

    warmup()

    for menu in menus:
        name = menu.get("name", "")
        x = menu_x.get(name)
        if x is None:
            continue
        centers = item_y.get(name, {})
        for item in menu.get("items", []):
            label = item.get("label", "")
            if item.get("separator") or not label or skip_menu_label(label):
                continue
            if item.get("enabled") is False:
                continue
            log_id = menu_log_id(label)
            y = centers.get(log_id)
            if y is None:
                issue(
                    "ERROR",
                    "Menu item not reachable",
                    f"menu={name} label={label} discovered={sorted(centers)}",
                )
                continue

            effect = MENU_EFFECTS.get(label)
            before = get_app_state()
            tracker.mark()
            open_menu(x, pump_fn=pump, click_fn=click_at)
            tracker.mark()
            click_at(x, y)
            pump(10)
            after = get_app_state()
            if after.get("ui", {}).get("tab_drag_active"):
                issue(
                    "WARNING",
                    "Stuck in tab drag after menu click",
                    f"menu={name} label={label} x={x} y={y}",
                )
                dismiss_ui()
            actions = tracker.wait_for_any_response(retries=8, delay_s=0.08)

            has_menu_log = any(
                action["kind"] == "menu" and action["id"] == log_id for action in actions
            )
            if not has_menu_log:
                issue(
                    "ERROR",
                    "Menu item silent (no menu ui.action)",
                    f"menu={name} label={label} x={x} y={y} log={tracker.summarize(actions)}",
                )
            elif effect and not menu_effect_changed(before, after, effect):
                issue(
                    "ERROR",
                    "Menu item clicked but had no effect",
                    f"menu={name} label={label} effect={effect} "
                    f"before_figures={before.get('figure_count')} after={after.get('figure_count')} "
                    f"before_ui={before.get('ui', {})} after_ui={after.get('ui', {})} "
                    f"before_panels={before.get('panels', {})} after_panels={after.get('panels', {})}",
                )

    cap = Path("/tmp/button_hunt_menus.png")
    py_fuzz.mcp("wait_frames", {"count": 5})
    py_fuzz.mcp("capture_window", {"path": str(cap)})


def hunt_nav_rail(tracker: StderrActionTracker, height: int, width: int) -> None:
    """Click every nav rail button; verify nav_rail log + UI/panel state delta."""
    py_fuzz.mcp("resize_window", {"width": max(width, 1280), "height": max(height, 900)})
    pump(8)

    def click_at(x: int, y: int) -> None:
        safe_click(x, y)

    wh = py_fuzz.payload(py_fuzz.mcp("get_window_size"))
    hunt_h = int(wh.get("height", height))

    discovered = discover_nav_rail_centers(
        tracker,
        hunt_h,
        pump_fn=pump,
        click_fn=click_at,
    )
    expected_ids = {sanitize_id(label) for label, _ in expected_buttons() if label != "Help"}
    missing = sorted(expected_ids - set(discovered))
    if missing:
        issue(
            "ERROR",
            "Nav rail buttons not reachable (no click hit)",
            f"missing={missing} discovered={sorted(discovered)} height={hunt_h}",
        )

    warmup()

    for label, x, y, expect in nav_rail_click_positions(hunt_h, discovered):
        if label == "Help":
            continue
        before = get_app_state()
        tracker.mark()
        safe_click(x, y)
        pump(8)
        after = get_app_state()
        actions = tracker.wait_for_any_response(retries=8, delay_s=0.08)

        log_id = sanitize_id(label)
        has_nav_log = any(
            action["kind"] == "nav_rail" and action["id"] == log_id for action in actions
        )
        changed = state_changed(before, after, expect)

        if not has_nav_log:
            issue(
                "ERROR",
                "Nav rail button silent (click missed widget)",
                f"label={label} x={x} y={y} log={tracker.summarize(actions)}",
            )
        elif expect and not changed:
            issue(
                "ERROR",
                "Nav rail button clicked but had no effect",
                f"label={label} expect={expect} "
                f"before_ui={before.get('ui', {})} after_ui={after.get('ui', {})} "
                f"before_panels={before.get('panels', {})} after_panels={after.get('panels', {})}",
            )

    cap = Path("/tmp/button_hunt_nav_rail.png")
    py_fuzz.mcp("wait_frames", {"count": 5})
    py_fuzz.mcp("capture_window", {"path": str(cap)})


def panel_body_targets(width: int, height: int) -> list[tuple[int, int, str]]:
    """Sweep left nav rail and right inspector regions after panels open."""
    targets: list[tuple[int, int, str]] = []
    for y in range(100, min(height - 80, 500), 40):
        targets.append((28, y, f"nav_rail@{y}"))
        targets.append((max(80, width - 220), y, f"inspector@{y}"))
    return targets


def probe_click(
    tracker: StderrActionTracker,
    x: int,
    y: int,
    label: str,
    *,
    require_functional: bool = True,
) -> str:
    tracker.mark()
    safe_click(x, y)
    pump()
    actions = tracker.wait_for_any_response()
    outcome = classify_interaction(actions)
    if require_functional and outcome in ("silent", "none"):
        issue(
            "ERROR",
            "Non-functioning click (no widget/menu response)",
            f"label={label} x={x} y={y} outcome={outcome} log={tracker.summarize(actions)}",
        )
    elif outcome == "miss":
        issue(
            "ERROR",
            "Click produced result=miss",
            f"label={label} x={x} y={y} log={tracker.summarize(actions)}",
        )
    return outcome


def hunt_commands(tracker: StderrActionTracker, tag: str) -> None:
    listed = py_fuzz.payload(py_fuzz.mcp("list_commands"))
    commands = listed.get("commands", [])
    ids: list[str] = []
    for entry in commands:
        cid = entry if isinstance(entry, str) else entry.get("id", entry.get("command_id", ""))
        if cid and cid not in SKIP_COMMANDS:
            ids.append(cid)

    if not ids:
        issue("WARNING", "Empty command list", tag)
        return

    for cid in ids:
        if not py_fuzz.alive():
            issue("CRITICAL", "Crash during command hunt", cid)
            return
        tracker.mark()
        response = py_fuzz.mcp("execute_command", {"command_id": cid}, timeout=15)
        if "error" in response:
            issue("CRITICAL", "Crash on execute_command", cid)
            break
        body = py_fuzz.payload(response)
        if body.get("error") or body.get("success") is False:
            issue(
                "WARNING",
                "execute_command returned error",
                f"{cid}: {body.get('error', body)}",
            )
        pump()
        actions = tracker.wait_for_command_logged(cid)
        outcome = classify_interaction(actions)
        if outcome == "none":
            issue(
                "ERROR",
                "Non-functioning command (no ui.action)",
                f"cmd={cid} mcp_ok={not body.get('error')}",
            )
        elif outcome == "miss":
            issue(
                "ERROR",
                "Non-functioning command (result=miss)",
                f"cmd={cid} log={tracker.summarize(actions)}",
            )
        elif outcome == "silent":
            issue(
                "ERROR",
                "Command produced only click log, no command line",
                f"cmd={cid} log={tracker.summarize(actions)}",
            )


def hunt_menu_bar(tracker: StderrActionTracker, width: int, height: int) -> None:
    hunt_menus(tracker, width, height)


def hunt_toolbar(tracker: StderrActionTracker, width: int) -> None:
    for x, y, label in command_bar_targets(width):
        probe_click(tracker, x, y, label)


def hunt_panels(tracker: StderrActionTracker, width: int, height: int) -> None:
    for cmd in PANEL_TOGGLE_COMMANDS:
        if not py_fuzz.alive():
            return
        tracker.mark()
        py_fuzz.mcp("execute_command", {"command_id": cmd})
        pump(5)
        actions = tracker.wait_for_command_logged(cmd)
        if classify_interaction(actions) in ("none", "miss"):
            issue(
                "ERROR",
                "Panel toggle command non-functional",
                f"cmd={cmd} log={tracker.summarize(actions)}",
            )
    for x, y, label in panel_body_targets(width, height):
        probe_click(tracker, x, y, label)


def warmup() -> None:
    py_fuzz.mcp("fuzz_reset", {"seed": int(os.environ.get("SEED", "42"))})
    dismiss_ui()
    py_fuzz.mcp("create_figure")
    pump(5)
    py_fuzz.mcp("add_series", {"n_points": 200})
    py_fuzz.mcp("wait_frames", {"count": 10})


def main() -> int:
    parser = argparse.ArgumentParser(description="Hunt non-functioning Spectra buttons")
    parser.add_argument("tag", nargs="?", default="spectra", choices=("spectra", "ros"))
    parser.add_argument(
        "--no-launch",
        action="store_true",
        help="Use already-running Spectra; set SPECTRA_FUZZ_LOG to its stderr file",
    )
    parser.add_argument(
        "--menus-only",
        action="store_true",
        help="Only run nav rail + menu bar hunts (skip command/toolbar/panel sweeps)",
    )
    args = parser.parse_args()

    tag = args.tag
    stderr_path: str | None

    if args.no_launch:
        stderr_path = os.environ.get("SPECTRA_FUZZ_LOG", "/tmp/pyfuzz_spectra.log")
        if not py_fuzz.alive():
            issue("CRITICAL", "MCP not reachable", py_fuzz.MCP)
            return 1
    else:
        binary_name = "spectra-ros" if tag == "ros" else "spectra"
        binary = py_fuzz.WORKSPACE / "build" / binary_name
        stderr_path = py_fuzz.launch(binary, ros=(tag == "ros"))
        if not stderr_path:
            return 1

    tracker = StderrActionTracker(stderr_path)
    tracker.mark()

    if not py_fuzz.alive():
        issue("CRITICAL", "MCP ping failed after launch", tag)
        return 1

    warmup()
    width, height = window_size()
    if width == 0:
        issue("ERROR", "Zero window size", tag)
        return 1

    if py_fuzz.alive():
        if args.menus_only:
            hunt_menu_bar(tracker, width, height)
        else:
            hunt_nav_rail(tracker, height, width)
    if py_fuzz.alive() and not args.menus_only:
        hunt_menu_bar(tracker, width, height)
    if py_fuzz.alive() and not args.menus_only:
        hunt_commands(tracker, tag)
    if py_fuzz.alive() and not args.menus_only:
        hunt_toolbar(tracker, width)
    if py_fuzz.alive() and not args.menus_only:
        hunt_panels(tracker, width, height)

    out_path = Path(f"/tmp/button_hunt_{tag}_issues.json")
    out_path.write_text(json.dumps(ISSUES, indent=2), encoding="utf-8")
    alive = py_fuzz.alive()
    broken = sum(1 for item in ISSUES if item["severity"] == "ERROR")
    print(f"DONE {tag} alive={alive} broken={broken} total_issues={len(ISSUES)} -> {out_path}")
    if not args.no_launch:
        py_fuzz.kill_stale_spectra()
    return 0 if alive and broken == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

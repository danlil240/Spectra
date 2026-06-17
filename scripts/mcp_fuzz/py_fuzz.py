#!/usr/bin/env python3
"""Dual-binary MCP fuzz harness — launch, 200 fuzz steps, exhaustion, isolated probes.

Usage:
  python3 scripts/mcp_fuzz/py_fuzz.py [spectra|ros]

Outputs:
  /tmp/pyfuzz_<tag>.jsonl       — fuzz step log
  /tmp/pyfuzz_<tag>_issues.json — detected issues
  /tmp/pyfuzz_<binary>.log      — process stderr

Env: SPECTRA_ROOT, DISPLAY (prefer :1), SEED (default 42), FUZZ_STEPS (default 200)
     SPECTRA_NO_NATIVE_DIALOGS=1 and SPECTRA_LOG_LEVEL=debug are set on launch.
     Build the app binary first: cmake --build build --target spectra-app
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from ui_action_log import StderrActionTracker  # noqa: E402

WORKSPACE = Path(os.environ.get("SPECTRA_ROOT", SCRIPT_DIR.parent.parent))
MCP = os.environ.get("SPECTRA_MCP_URL", "http://127.0.0.1:8765/mcp")
SEED = int(os.environ.get("SEED", "42"))
FUZZ_STEPS = int(os.environ.get("FUZZ_STEPS", "200"))

SKIP = {
    "app.quit",
    "figure.close",
    "help.show",
    "file.save_figure",
    "file.load_figure",
    "file.export_png",
    "file.export_svg",
    "file.copy_to_clipboard",
    "accessibility.sonify_series",
    "data.export_html_table",
}

ISSUES: list[dict[str, str]] = []


def mcp(tool: str, args: dict | None = None, timeout: float = 30) -> dict:
    payload = json.dumps(
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": tool, "arguments": args or {}},
        }
    ).encode()
    req = urllib.request.Request(
        MCP,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read())
    except Exception as exc:
        return {"error": str(exc)}


def structured(d: dict) -> dict:
    result = d.get("result", {})
    if isinstance(result, dict):
        sc = result.get("structuredContent")
        if isinstance(sc, dict):
            return sc
    return {}


def text_payload(d: dict) -> dict:
    result = d.get("result", {})
    if isinstance(result, dict):
        for block in result.get("content", []):
            if block.get("type") == "text":
                raw = block.get("text", "")
                try:
                    return json.loads(raw)
                except json.JSONDecodeError:
                    return {"raw": raw}
    return {}


def payload(d: dict) -> dict:
    sc = structured(d)
    return sc if sc else text_payload(d)


def alive() -> bool:
    body = mcp("ping", timeout=5)
    if "error" in body:
        return False
    sc = structured(body)
    return bool(sc.get("pong") or sc.get("status") == "ok" or body.get("result"))


def dismiss_ui() -> None:
    mcp("dismiss_ui_capture")
    mcp("pump_frames", {"count": 2})


def issue(sev: str, title: str, detail: str) -> None:
    ISSUES.append({"severity": sev, "title": title, "detail": detail})
    print(f"[{sev}] {title}: {detail[:200]}")


def wait_for_mcp_port(timeout_s: float = 5.0) -> bool:
    """Return True once default MCP port accepts connections."""
    deadline = time.time() + timeout_s
    host = os.environ.get("SPECTRA_MCP_BIND", "127.0.0.1")
    port = int(os.environ.get("SPECTRA_MCP_PORT", "8765"))
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.3):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def kill_stale_spectra() -> None:
    for name in ("spectra", "spectra-ros", "xclip", "wl-copy"):
        subprocess.run(["pkill", "-x", name], check=False)
    time.sleep(0.8)
    host = os.environ.get("SPECTRA_MCP_BIND", "127.0.0.1")
    port = int(os.environ.get("SPECTRA_MCP_PORT", "8765"))
    deadline = time.time() + 8.0
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                time.sleep(0.2)
        except OSError:
            return


def launch(binary: Path, ros: bool = False) -> str | None:
    kill_stale_spectra()
    env = os.environ.copy()
    env.setdefault("DISPLAY", ":1")
    env["XDG_RUNTIME_DIR"] = f"/run/user/{os.getuid()}"
    env["SPECTRA_NO_NATIVE_DIALOGS"] = "1"
    env.setdefault("SPECTRA_LOG_LEVEL", "debug")
    env.setdefault("SPECTRA_FUZZ_SKIP_DRAG", "1")
    if env.get("DISPLAY") == ":99":
        env.setdefault(
            "VK_ICD_FILENAMES",
            "/usr/share/vulkan/icd.d/lvp_icd.x86_64.json",
        )
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    else:
        env.pop("LIBGL_ALWAYS_SOFTWARE", None)
        env.pop("VK_ICD_FILENAMES", None)

    stderr_path = f"/tmp/pyfuzz_{binary.name}.log"
    with open(stderr_path, "w", encoding="utf-8") as log_file:
        if ros:
            shell = f"source /opt/ros/jazzy/setup.bash && exec {binary} --no-native-dialogs"
            subprocess.Popen(
                ["bash", "-lc", shell],
                stdout=log_file,
                stderr=subprocess.STDOUT,
                env=env,
                cwd=WORKSPACE,
            )
        else:
            subprocess.Popen(
                [str(binary), "--no-native-dialogs"],
                stdout=log_file,
                stderr=subprocess.STDOUT,
                env=env,
                cwd=WORKSPACE,
            )

    for _ in range(50):
        if wait_for_mcp_port(timeout_s=0.5) and alive():
            time.sleep(1.0)
            return stderr_path
        time.sleep(0.2)

    tail = Path(stderr_path).read_text(encoding="utf-8")[-500:]
    if "Port 8765 already in use" in tail or "8766/mcp" in tail:
        issue(
            "ERROR",
            f"MCP port conflict ({binary.name})",
            "Process bound alternate port; kill stale spectra/spectra-ros first",
        )
    issue("CRITICAL", f"MCP start failed ({binary.name})", tail)
    return None


def state_checks(tag: str) -> None:
    state = payload(mcp("get_state"))
    if state.get("active_figure_id") == 18446744073709551615:
        issue("WARNING", "UINT64_MAX active_figure_id", tag)
    if state.get("figure_count", 0) > 25:
        issue("WARNING", "Runaway figure_count", f"{tag}: {state.get('figure_count')}")

    window = payload(mcp("get_window_size"))
    if window.get("width", 0) == 0:
        issue("ERROR", "Zero window size", tag)


def fuzz_loop(steps: int, log_path: Path, tracker: StderrActionTracker) -> int:
    mcp("fuzz_reset", {"seed": SEED})
    mcp("create_figure")
    mcp("add_series", {"n_points": 200})
    mcp("wait_frames", {"count": 10})

    with log_path.open("a", encoding="utf-8") as log_file:
        for step in range(1, steps + 1):
            tracker.mark()
            response = mcp("fuzz_step")
            log_file.write(json.dumps({"step": step, "response": response}) + "\n")
            if not alive():
                issue("CRITICAL", "Crash during fuzz", f"step={step} seed={SEED}")
                return step
            body = payload(response)
            pump_count = body.get("pump_frames", 2) or 2
            mcp("pump_frames", {"count": int(pump_count)})
            dismiss_ui()
            actions = tracker.wait_for_any_response()
            if not actions:
                issue(
                    "ERROR",
                    "No ui.action log after fuzz step",
                    f"step={step} action={body.get('action', '')}",
                )
            elif body.get("action") == "ExecuteCommand":
                    details = body.get("details", {})
                    if isinstance(details, str):
                        try:
                            details = json.loads(details)
                        except json.JSONDecodeError:
                            details = {}
                    cmd_id = details.get("command_id") or details.get("skipped_command")
                    if cmd_id and not any(
                        item["kind"] == "command"
                        and item["id"] == cmd_id
                        and item["result"] in ("ok", "skipped", "miss")
                        for item in actions
                    ):
                        issue(
                            "WARNING",
                            "Command fuzz step without matching command log",
                            f"step={step} cmd={cmd_id} got={tracker.summarize(actions)}",
                        )
            if step % 50 == 0:
                cap = Path(f"/tmp/pyfuzz_step_{step}.png")
                mcp("wait_frames", {"count": 5})
                mcp("capture_window", {"path": str(cap)})
                if not cap.exists() or cap.stat().st_size < 500:
                    issue("ERROR", "Bad screenshot", str(cap))
    return steps


def command_exhaustion(tag: str, tracker: StderrActionTracker) -> None:
    listed = payload(mcp("list_commands"))
    commands = listed.get("commands", [])
    ids: list[str] = []
    for entry in commands:
        cid = entry if isinstance(entry, str) else entry.get("id", entry.get("command_id", ""))
        if cid and cid not in SKIP:
            ids.append(cid)

    if not ids:
        issue("WARNING", f"Empty command list ({tag})", "list_commands returned no IDs")
        return

    for index, cid in enumerate(ids):
        if not alive():
            issue("CRITICAL", f"Crash during exhaustion ({tag})", f"command={cid} index={index}")
            return
        tracker.mark()
        response = mcp("execute_command", {"command_id": cid}, timeout=15)
        if "error" in response:
            issue("WARNING", f"Command MCP error ({tag})", f"{cid}: {response['error']}")
        else:
            body = payload(response)
            if body.get("error") or body.get("success") is False:
                issue("WARNING", f"Command failed ({tag})", f"{cid}: {body}")
        mcp("pump_frames", {"count": 2})
        actions = tracker.wait_for_command_logged(cid)
        if not actions:
            issue(
                "ERROR",
                "No ui.action for execute_command",
                f"{tag} cmd={cid} got={tracker.summarize(actions)}",
            )


def isolated_crash_commands() -> None:
    for cid in (
        "file.copy_to_clipboard",
        "file.export_png",
        "file.export_svg",
        "help.show",
        "accessibility.sonify_series",
        "data.export_html_table",
    ):
        if not alive():
            issue("CRITICAL", "Dead before isolated probe", cid)
            return
        response = mcp("execute_command", {"command_id": cid}, timeout=15)
        mcp("pump_frames", {"count": 3})
        if not alive():
            issue("CRITICAL", "Crash on isolated command", cid)
        else:
            body = payload(response)
            if body.get("error"):
                issue("WARNING", "Isolated command error", f"{cid}: {body.get('error')}")


def ros_panels(tracker: StderrActionTracker) -> None:
    for x, y in [(40, 200), (40, 350), (120, 150), (80, 50), (500, 300)]:
        tracker.mark()
        mcp("mouse_click", {"x": x, "y": y})
        mcp("pump_frames", {"count": 3})
        actions = tracker.wait_for_any_response()
        if not any(item["kind"] in ("mcp_click", "input", "widget", "imgui", "command") for item in actions):
            issue(
                "ERROR",
                "No ui.action after ROS panel click",
                f"x={x} y={y} got={tracker.summarize(actions)}",
            )
    mcp("wait_frames", {"count": 10})
    mcp("capture_window", {"path": "/tmp/pyfuzz_ros_panels.png"})


def scan_stderr(path: str | None) -> None:
    if not path or not Path(path).exists():
        return
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        lower = line.lower()
        if "xclip" in lower and "not found" in lower:
            issue("WARNING", "xclip missing", line.strip())
        if "segmentation fault" in lower or "segfault" in lower:
            issue("CRITICAL", "stderr segfault", line.strip())


def main() -> int:
    tag = sys.argv[1] if len(sys.argv) > 1 else "spectra"
    binary_name = "spectra-ros" if tag == "ros" else "spectra"
    binary = WORKSPACE / "build" / binary_name
    log_path = Path(f"/tmp/pyfuzz_{tag}.jsonl")
    log_path.write_text("", encoding="utf-8")

    stderr = launch(binary, ros=(tag == "ros"))
    if not stderr:
        return 1

    tracker = StderrActionTracker(stderr)
    tracker.mark()

    state_checks(f"bootstrap-{tag}")
    completed = fuzz_loop(FUZZ_STEPS, log_path, tracker)
    scan_stderr(stderr)

    if alive():
        command_exhaustion(tag, tracker)
    if alive():
        isolated_crash_commands()
    if alive() and tag == "ros":
        ros_panels(tracker)
    if alive():
        state_checks(f"final-{tag}")

    out_path = Path(f"/tmp/pyfuzz_{tag}_issues.json")
    out_path.write_text(json.dumps(ISSUES, indent=2), encoding="utf-8")
    alive_at_end = alive()
    kill_stale_spectra()
    print(f"DONE {tag} steps={completed} alive={alive_at_end} issues={len(ISSUES)} -> {out_path}")
    return 0 if alive_at_end and len(ISSUES) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

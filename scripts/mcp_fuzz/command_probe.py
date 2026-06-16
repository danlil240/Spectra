#!/usr/bin/env python3
"""One-by-one command probe — find which command crashes a fresh Spectra instance.

Usage:
  python3 scripts/mcp_fuzz/command_probe.py [spectra|ros]

Writes /tmp/command_probe.json with per-command status (ok, FAIL, CRASH).
Restarts the process after each isolated crash-prone command probe.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
WORKSPACE = Path(os.environ.get("SPECTRA_ROOT", SCRIPT_DIR.parent.parent))
MCP = os.environ.get("SPECTRA_MCP_URL", "http://127.0.0.1:8765/mcp")
OUTPUT = Path(os.environ.get("SPECTRA_COMMAND_PROBE_OUT", "/tmp/command_probe.json"))

SKIP = {
    "app.quit",
    "file.save_figure",
    "file.load_figure",
    "file.save_workspace",
    "file.load_workspace",
}

ISOLATED = (
    "file.copy_to_clipboard",
    "file.export_png",
    "file.export_svg",
    "help.show",
    "accessibility.sonify_series",
    "data.export_html_table",
)


def mcp(tool: str, args: dict | None = None, timeout: float = 20) -> dict:
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
        return {"conn_error": str(exc)}


def alive() -> bool:
    return "conn_error" not in mcp("ping", timeout=5)


def launch(ros: bool = False) -> None:
    for name in ("spectra", "spectra-ros"):
        subprocess.run(["pkill", "-x", name], check=False)
    time.sleep(0.8)

    env = os.environ.copy()
    env["DISPLAY"] = os.environ.get("DISPLAY", ":1")
    env["XDG_RUNTIME_DIR"] = f"/run/user/{os.getuid()}"
    env["SPECTRA_NO_NATIVE_DIALOGS"] = "1"
    env.pop("VK_ICD_FILENAMES", None)
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)

    binary = WORKSPACE / "build" / ("spectra-ros" if ros else "spectra")
    stderr_path = Path(f"/tmp/command_probe_{binary.name}.log")

    with stderr_path.open("w", encoding="utf-8") as log_file:
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

    for _ in range(40):
        if alive():
            time.sleep(1)
            return
        time.sleep(0.2)


def warmup() -> None:
    mcp("create_figure")
    mcp("add_series", {"n_points": 100})
    mcp("wait_frames", {"count": 10})


def command_status(response: dict) -> str:
    if "conn_error" in response:
        return f"CONN_ERR:{response['conn_error']}"
    result = response.get("result", {})
    if isinstance(result, dict) and result.get("isError"):
        return "FAIL:isError"
    sc = result.get("structuredContent", {}) if isinstance(result, dict) else {}
    if sc.get("success") is False or sc.get("error"):
        return f"FAIL:{sc.get('error', sc)}"
    return "ok"


def main() -> None:
    ros = len(sys.argv) > 1 and sys.argv[1] == "ros"
    launch(ros=ros)
    warmup()

    listed = mcp("list_commands")
    commands = listed.get("result", {}).get("structuredContent", {}).get("commands", [])
    results: list[dict] = []

    for entry in commands:
        cid = entry if isinstance(entry, str) else entry.get("id", "")
        if not cid or cid in SKIP:
            continue
        if not alive():
            results.append({"cmd": cid, "status": "CRASH_BEFORE"})
            break
        response = mcp("execute_command", {"command_id": cid})
        mcp("pump_frames", {"count": 2})
        status = "CRASH" if not alive() else command_status(response)
        results.append({"cmd": cid, "status": status})
        print(f"{cid}: {status}")

    for cid in ISOLATED:
        if not alive():
            launch(ros=ros)
            warmup()
        if not alive():
            results.append({"cmd": cid, "status": "CRASH_BEFORE", "isolated": True})
            break
        mcp("execute_command", {"command_id": cid})
        mcp("pump_frames", {"count": 3})
        status = "CRASH" if not alive() else "ok"
        results.append({"cmd": cid, "status": status, "isolated": True})
        print(f"ISOLATED {cid}: {status}")
        if not alive():
            launch(ros=ros)
            warmup()

    OUTPUT.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"Wrote {OUTPUT}")


if __name__ == "__main__":
    main()

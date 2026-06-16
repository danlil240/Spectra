#!/usr/bin/env python3
"""MCP bug hunt against an already-running Spectra instance (no launch).

Usage (start Spectra first, then):
  python3 scripts/mcp_fuzz/bug_hunt.py

Env: SPECTRA_MCP_URL, SPECTRA_BUG_HUNT_ISSUES (default /tmp/spectra_bug_hunt_issues.jsonl)
"""

from __future__ import annotations

import json
import os
import re
import sys
import urllib.request

MCP = os.environ.get("SPECTRA_MCP_URL", "http://127.0.0.1:8765/mcp")
ISSUES_PATH = os.environ.get(
    "SPECTRA_BUG_HUNT_ISSUES", "/tmp/spectra_bug_hunt_issues.jsonl"
)
SEED = int(os.environ.get("SEED", "42"))
FUZZ_STEPS = int(os.environ.get("FUZZ_STEPS", "200"))

SKIP_EXHAUSTION = {
    "app.quit",
    "file.save_figure",
    "file.load_figure",
    "file.save_workspace",
    "file.load_workspace",
    "file.copy_to_clipboard",
    "file.export_png",
    "file.export_svg",
    "help.show",
    "accessibility.sonify_series",
    "data.export_html_table",
}


def log_issue(sev: str, title: str, detail: str) -> None:
    entry = {"severity": sev, "title": title, "detail": detail}
    with open(ISSUES_PATH, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry) + "\n")
    print(f"[{sev}] {title}: {detail[:200]}")


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
        return {"_error": str(exc)}


def text_of(resp: dict) -> str:
    if "_error" in resp:
        return resp["_error"]
    if "error" in resp:
        return json.dumps(resp["error"])
    result = resp.get("result", {})
    if isinstance(result, dict):
        sc = result.get("structuredContent")
        if sc:
            return json.dumps(sc)
        for block in result.get("content", []):
            if block.get("type") == "text":
                return block.get("text", "")
    return json.dumps(result)


def ok(resp: dict) -> bool:
    if "_error" in resp or "error" in resp:
        return False
    result = resp.get("result", {})
    return not (isinstance(result, dict) and result.get("isError"))


def pump(count: int = 2) -> None:
    mcp("pump_frames", {"count": count})


def main() -> int:
    open(ISSUES_PATH, "w").close()

    for tool in ("ping", "get_state", "get_window_size", "list_commands", "list_fuzz_actions"):
        response = mcp(tool)
        if not ok(response):
            log_issue("ERROR", f"Bootstrap {tool} failed", text_of(response))

    state_text = text_of(mcp("get_state"))
    if "18446744073709551615" in state_text:
        log_issue("WARNING", "active_figure_id UINT64_MAX sentinel", state_text[:200])

    window_text = text_of(mcp("get_window_size"))
    try:
        window = json.loads(window_text)
        if window.get("width", 0) == 0:
            log_issue("ERROR", "get_window_size returns 0x0", window_text[:200])
    except json.JSONDecodeError:
        pass

    mcp("fuzz_reset", {"seed": SEED})
    mcp("create_figure")
    pump(5)
    mcp("add_series", {"n_points": 200})
    pump(10)

    for step in range(1, FUZZ_STEPS + 1):
        response = mcp("fuzz_step")
        if "_error" in response:
            log_issue("CRITICAL", f"Crash at fuzz step {step}", response["_error"])
            break
        try:
            body = json.loads(text_of(response))
            pump(int(body.get("pump_frames", 2)))
        except (json.JSONDecodeError, ValueError, TypeError):
            pump(2)

    if not ok(mcp("ping")):
        log_issue("CRITICAL", "Process dead after fuzz loop", "ping failed")
        return 1

    commands_text = text_of(mcp("list_commands"))
    try:
        parsed = json.loads(commands_text)
        commands = parsed if isinstance(parsed, list) else parsed.get("commands", [])
    except json.JSONDecodeError:
        commands = re.findall(r'"([a-z][a-z0-9_.]+)"', commands_text)

    for entry in commands:
        cid = entry if isinstance(entry, str) else entry.get("id", entry.get("command_id", ""))
        if not cid or cid in SKIP_EXHAUSTION or any(x in cid for x in ("quit", "save", "load")):
            continue
        response = mcp("execute_command", {"command_id": cid}, timeout=15)
        if "_error" in response:
            log_issue("CRITICAL", f"Crash on command {cid}", response["_error"])
            break
        if not ok(response):
            log_issue("WARNING", f"Command error: {cid}", text_of(response)[:200])
        pump(2)

    for cid in (
        "file.copy_to_clipboard",
        "file.export_png",
        "help.show",
        "accessibility.sonify_series",
        "data.export_html_table",
    ):
        if not ok(mcp("ping")):
            break
        response = mcp("execute_command", {"command_id": cid}, timeout=15)
        pump(3)
        if "_error" in response:
            log_issue("CRITICAL", f"Crash on isolated {cid}", response["_error"])
        elif not ok(response):
            log_issue("WARNING", f"Isolated command failed: {cid}", text_of(response)[:200])

    alive = ok(mcp("ping"))
    with open(ISSUES_PATH, encoding="utf-8") as handle:
        count = sum(1 for _ in handle)
    print(f"DONE alive={alive} issues={count} -> {ISSUES_PATH}")
    return 0 if alive else 1


if __name__ == "__main__":
    sys.exit(main())

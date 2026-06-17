#!/usr/bin/env python3
"""Parse Spectra stderr for structured [ui.action] LOG_DEBUG lines.

Log line shape (ANSI codes stripped):
  ... DEBUG [ui.action] kind=<kind> id=<id> result=<ok|miss|...> [detail=<text>]
"""

from __future__ import annotations

import re
import time
from pathlib import Path

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
ACTION_RE = re.compile(
    r"\[ui\.action\]\s+kind=(\S+)\s+id=([^\s]+)\s+result=(\S+)(?:\s+detail=(.*))?$"
)

# Any ui.action line counts as a click/interaction response for fuzz verification.
RESPONSE_KINDS = frozenset(
    {
        "command",
        "widget",
        "menu",
        "imgui",
        "nav_rail",
        "input",
        "mcp_click",
        "fuzz_click",
        "fuzz_scroll",
        "fuzz_key",
        "fuzz",
    }
)


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def parse_action_line(line: str) -> dict[str, str] | None:
    match = ACTION_RE.search(strip_ansi(line))
    if not match:
        return None
    return {
        "kind": match.group(1),
        "id": match.group(2),
        "result": match.group(3),
        "detail": (match.group(4) or "").strip(),
    }


class StderrActionTracker:
    """Track new [ui.action] log lines appended to a Spectra stderr file."""

    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self._offset = 0

    def mark(self) -> None:
        self._offset = len(strip_ansi(self._read_all()))

    def _read_all(self) -> str:
        if not self.path.exists():
            return ""
        return self.path.read_text(encoding="utf-8", errors="replace")

    def new_actions(self) -> list[dict[str, str]]:
        text = strip_ansi(self._read_all())
        chunk = text[self._offset :]
        self._offset = len(text)
        actions: list[dict[str, str]] = []
        for line in chunk.splitlines():
            parsed = parse_action_line(line)
            if parsed:
                actions.append(parsed)
        return actions

    def wait_for_command(
        self,
        command_id: str,
        *,
        result: str | None = "ok",
        retries: int = 5,
        delay_s: float = 0.05,
    ) -> list[dict[str, str]]:
        """Poll log file until command action appears (handles stdout buffering)."""
        actions: list[dict[str, str]] = []
        for attempt in range(retries):
            saved = self._offset
            actions = self.new_actions()
            matched = [
                action
                for action in actions
                if action["kind"] == "command"
                and action["id"] == command_id
                and (result is None or action["result"] == result)
            ]
            if matched:
                return matched
            if attempt + 1 < retries:
                self._offset = saved
                time.sleep(delay_s)
        return actions

    def wait_for_command_logged(
        self,
        command_id: str,
        *,
        retries: int = 5,
        delay_s: float = 0.05,
    ) -> list[dict[str, str]]:
        """Wait for any command log line (result ok or miss)."""
        return self.wait_for_command(command_id, result=None, retries=retries, delay_s=delay_s)

    def wait_for_any_response(
        self,
        *,
        retries: int = 5,
        delay_s: float = 0.05,
    ) -> list[dict[str, str]]:
        actions: list[dict[str, str]] = []
        for attempt in range(retries):
            saved = self._offset
            actions = self.new_actions()
            if any(action["kind"] in RESPONSE_KINDS for action in actions):
                return actions
            if attempt + 1 < retries:
                self._offset = saved
                time.sleep(delay_s)
        return actions

    def has_command(self, command_id: str, *, result: str = "ok") -> bool:
        return any(
            action["kind"] == "command"
            and action["id"] == command_id
            and action["result"] == result
            for action in self.new_actions()
        )

    def has_any_response(self) -> bool:
        return any(action["kind"] in RESPONSE_KINDS for action in self.new_actions())

    def summarize(self, actions: list[dict[str, str]]) -> str:
        if not actions:
            return "(none)"
        return "; ".join(
            f"{action['kind']}:{action['id']}:{action['result']}" for action in actions[:5]
        )


# Kinds that indicate a button/menu/widget actually handled the interaction.
FUNCTIONAL_KINDS = frozenset(
    {
        "widget",
        "menu",
        "imgui",
        "nav_rail",
        "command",
        "input",
    }
)


def classify_interaction(actions: list[dict[str, str]]) -> str:
    """Classify MCP click/command outcome for button-hunt reporting.

    Returns one of: ok, miss, silent, none
    - ok: widget/menu/imgui/command/input with non-miss result
    - miss: command or widget logged result=miss (listed but dead/disabled)
    - silent: only mcp_click/fuzz_click — click accepted but nothing handled it
    - none: no ui.action lines at all
    """
    if not actions:
        return "none"
    meaningful = [action for action in actions if action["kind"] in FUNCTIONAL_KINDS]
    if not meaningful:
        if any(action["kind"] in ("mcp_click", "fuzz_click") for action in actions):
            return "silent"
        return "none"
    if any(action["result"] == "miss" for action in meaningful):
        return "miss"
    return "ok"

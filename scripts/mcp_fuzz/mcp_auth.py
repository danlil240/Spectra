"""Shared helpers for talking to Spectra's (opt-in, token-protected) MCP server.

Spectra only starts its automation/MCP server when asked to (``SPECTRA_AUTOMATION=1``),
and every HTTP request must carry ``Authorization: Bearer <token>``.  See SECURITY.md.

* :func:`prepare_launch_env` — call on the env dict used to launch Spectra: enables
  automation, pins a token (``SPECTRA_MCP_TOKEN``) and a capture directory.
* :func:`mcp_headers` — headers for each JSON-RPC request.
* :func:`capture_dir` — directory that capture_screenshot / capture_window may write to.
"""

from __future__ import annotations

import glob
import json
import os
import secrets
from pathlib import Path


def _runtime_dirs() -> list[str]:
    dirs = []
    for var in ("XDG_RUNTIME_DIR", "TMPDIR"):
        v = os.environ.get(var)
        if v:
            dirs.append(v)
    dirs.append(f"/run/user/{os.getuid()}")
    dirs.append("/tmp")
    return dirs


def discover_token() -> str | None:
    """Token of the newest running Spectra (from its 0600 spectra-mcp-<pid>.json file)."""
    files: list[str] = []
    for d in _runtime_dirs():
        files.extend(glob.glob(os.path.join(d, "spectra-mcp-*.json")))
    for path in sorted(set(files), key=os.path.getmtime, reverse=True):
        try:
            with open(path, encoding="utf-8") as fh:
                token = json.load(fh).get("token")
            if token:
                return token
        except (OSError, ValueError):
            continue
    return None


def token() -> str:
    """Token to present: $SPECTRA_MCP_TOKEN, else a discovered token file, else a new one.

    A newly generated token is exported to os.environ so a Spectra launched afterwards
    (inheriting the environment) uses the same value.
    """
    tok = os.environ.get("SPECTRA_MCP_TOKEN") or discover_token()
    if not tok:
        tok = secrets.token_hex(32)
    os.environ["SPECTRA_MCP_TOKEN"] = tok
    return tok


def capture_dir() -> Path:
    d = os.environ.get("SPECTRA_CAPTURE_DIR") or f"/tmp/spectra-qa-captures-{os.getuid()}"
    os.environ["SPECTRA_CAPTURE_DIR"] = d
    path = Path(d)
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    return path


def prepare_launch_env(env: dict) -> dict:
    """Opt the launched Spectra process into automation with a known token + capture dir."""
    tok = os.environ.get("SPECTRA_MCP_TOKEN") or secrets.token_hex(32)
    os.environ["SPECTRA_MCP_TOKEN"] = tok
    env["SPECTRA_AUTOMATION"] = "1"
    env["SPECTRA_MCP_TOKEN"] = tok
    env["SPECTRA_CAPTURE_DIR"] = str(capture_dir())
    return env


def mcp_headers() -> dict:
    return {"Content-Type": "application/json", "Authorization": f"Bearer {token()}"}

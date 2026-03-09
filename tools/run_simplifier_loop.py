#!/usr/bin/env python3
"""
run_simplifier_loop.py
======================
Drives the Spectra code-simplifier agent in a clear-context loop using the
GitHub Models API (https://models.inference.ai.azure.com/chat/completions).

Each iteration:
  1. Picks the next 'not-started' module from EXPLORATION.md.
  2. Reads context files + all source files in the module.
  3. Sends a multi-turn conversation to the model:
        Turn 1 — analysis + code changes (model emits <file> edit blocks)
        Turn 2 — build + test output fed back; model updates EXPLORATION.md
                 and REPORT.md and emits final verification steps.
  4. Applies all <file> edit blocks written by the model.
  5. Runs cmake build + ctest; feeds results back (turn 2).
  6. Updates EXPLORATION.md / REPORT.md from the model's final output.

Usage
-----
    ./tools/run_simplifier_loop.sh              # interactive
    AUTO=1 ./tools/run_simplifier_loop.sh       # no prompts
    MAX_SESSIONS=3 ./tools/run_simplifier_loop.sh
    MODEL=gpt-4o ./tools/run_simplifier_loop.sh

Environment variables
---------------------
    AUTO           = 1  skip confirmation prompts
    MAX_SESSIONS   = N  stop after N modules  (0 = unlimited)
    MODEL          GitHub Models model name  (default: gpt-4.1)
                   Other options: gpt-4o, claude-3.5-sonnet, o1, o3-mini
    NPROC          parallelism for cmake build  (default: nproc)
    LOG_LEVEL      debug | info | silent  (default: info)
    GITHUB_TOKEN   override token (otherwise read from gh CLI config)

Auth
----
Uses the GitHub OAuth token stored by `gh auth login` as a Bearer token
against the GitHub Models API.  No separate token exchange is required.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import textwrap
from pathlib import Path
from typing import Optional

import requests
import yaml

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_DIR = Path(__file__).resolve().parent.parent
EXPLORATION_FILE = REPO_DIR / "skills/code-simplifier/EXPLORATION.md"
REPORT_FILE      = REPO_DIR / "skills/code-simplifier/REPORT.md"
SKILL_FILE       = REPO_DIR / "skills/code-simplifier/SKILL.md"
CLAUDE_MD        = REPO_DIR / "CLAUDE.md"
FORMAT_MD        = REPO_DIR / "FORMAT.md"
CODEBASE_MAP     = REPO_DIR / "CODEBASE_MAP.md"
LOG_DIR          = REPO_DIR / "skills/code-simplifier/logs"

# ---------------------------------------------------------------------------
# Config from env
# ---------------------------------------------------------------------------

AUTO         = os.getenv("AUTO", "0") == "1"
MAX_SESSIONS = int(os.getenv("MAX_SESSIONS", "0"))
MODEL        = os.getenv("MODEL", "gpt-4o")
NPROC        = os.getenv("NPROC", str(os.cpu_count() or 4))
LOG_LEVEL    = os.getenv("LOG_LEVEL", "info")
GITHUB_TOKEN = os.getenv("GITHUB_TOKEN", "")
# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

def log(msg: str, level: str = "info") -> None:
    if LOG_LEVEL == "silent":
        return
    if level == "debug" and LOG_LEVEL != "debug":
        return
    print(msg, flush=True)

# ---------------------------------------------------------------------------
# GitHub auth  (reads token stored by `gh auth login`)
# ---------------------------------------------------------------------------

GH_HOSTS_FILE = Path.home() / ".config/gh/hosts.yml"


def get_github_token() -> str:
    """Return a GitHub OAuth token: GITHUB_TOKEN env var, or gh CLI config."""
    if GITHUB_TOKEN:
        return GITHUB_TOKEN
    if GH_HOSTS_FILE.exists():
        with open(GH_HOSTS_FILE) as f:
            data = yaml.safe_load(f)
        entry = data.get("github.com", {})
        token = entry.get("oauth_token") or entry.get("token")
        if token:
            log("  Auth: using token from gh CLI config", "debug")
            return token
    sys.exit(
        "ERROR: No GitHub token found.\n"
        "Run `gh auth login` or set GITHUB_TOKEN=<token>."
    )

# ---------------------------------------------------------------------------
# GitHub Models chat API  (OpenAI-compatible, auth = Bearer <gh_token>)
# ---------------------------------------------------------------------------

GH_MODELS_URL     = "https://models.inference.ai.azure.com/chat/completions"
GH_MODELS_CATALOG = "https://models.github.ai/catalog/models"


def list_available_models(gh_token: str) -> list[str]:
    """Return chat-capable model IDs from the GitHub Models catalog."""
    try:
        r = requests.get(
            GH_MODELS_CATALOG,
            headers={
                "Authorization": f"Bearer {gh_token}",
                "Accept": "application/vnd.github+json",
                "X-GitHub-Api-Version": "2022-11-28",
            },
            timeout=10,
        )
        if r.status_code == 200:
            data = r.json()
            if isinstance(data, list):
                skip_caps  = {"embeddings"}
                skip_names = {"embedding", "embed"}
                return [
                    m["id"] for m in data
                    if isinstance(m, dict)
                    and m.get("id")
                    and m.get("rate_limit_tier") not in skip_caps
                    and not any(s in m.get("name", "").lower() for s in skip_names)
                    and "text" in m.get("supported_output_modalities", [])
                ]
    except Exception:
        pass
    return []


def gh_models_chat(
    messages: list[dict],
    gh_token: str,
    *,
    model: str = MODEL,
    temperature: float = 0.2,
) -> str:
    """Send messages to GitHub Models and return the assistant reply text."""
    headers = {
        "Authorization": f"Bearer {gh_token}",
        "Content-Type": "application/json",
        "User-Agent": "spectra-simplifier/1.0",
    }
    body = {
        "model": model,
        "messages": messages,
        "temperature": temperature,
        "max_tokens": 16000,
    }
    r = requests.post(GH_MODELS_URL, headers=headers, json=body, timeout=300)
    if r.status_code == 401:
        sys.exit(
            "ERROR: GitHub Models API returned 401 Unauthorized.\n"
            "Run `gh auth login` or set GITHUB_TOKEN=<token>."
        )
    if r.status_code == 403:
        sys.exit(
            "ERROR: GitHub Models API returned 403 Forbidden.\n"
            "Make sure your account has access to GitHub Models "
            "(https://github.com/marketplace/models)."
        )
    if r.status_code == 400:
        try:
            err = r.json().get("error", {})
        except Exception:
            err = {}
        if err.get("code") == "unknown_model":
            available = list_available_models(gh_token)
            lines = [f"ERROR: Unknown model: {model}"]
            if available:
                lines.append("\nAvailable models on GitHub Models:")
                for mid in sorted(available):
                    lines.append(f"  {mid}")
                best = next((m for m in available if "gpt-4.1" in m or "gpt-4o" in m), available[0])
                lines.append(f"\nSet MODEL=<id> and retry, e.g.:\n  MODEL={best} ./tools/run_simplifier_loop.sh")
            else:
                lines.append("Could not fetch model list. Check https://github.com/marketplace/models")
            sys.exit("\n".join(lines))
    if r.status_code != 200:
        sys.exit(f"ERROR: GitHub Models API error ({r.status_code}): {r.text[:400]}")
    choices = r.json().get("choices", [])
    if not choices:
        sys.exit(f"ERROR: No choices in response: {r.text[:200]}")
    return choices[0]["message"]["content"]

# ---------------------------------------------------------------------------
# EXPLORATION.md parsing
# ---------------------------------------------------------------------------

ROW_RE = re.compile(
    r"^\|\s*(\d+)\s*\|\s*`([^`]+)`\s*\|\s*(\S+)\s*\|\s*(not-started)\s*\|",
    re.MULTILINE,
)


def get_next_module() -> Optional[tuple[int, str, str]]:
    """Return (number, path, files_estimate) for the next not-started module, or None."""
    text = EXPLORATION_FILE.read_text()
    m = ROW_RE.search(text)
    if not m:
        return None
    return int(m.group(1)), m.group(2), m.group(3)

# ---------------------------------------------------------------------------
# File collection
# ---------------------------------------------------------------------------

CONTEXT_FILES = [CLAUDE_MD, FORMAT_MD, CODEBASE_MAP, SKILL_FILE, EXPLORATION_FILE, REPORT_FILE]
MAX_FILE_BYTES = 40_000   # truncate individual files larger than this
MAX_MODULE_BYTES = 120_000  # total cap for module source content

SOURCE_EXTS = {".cpp", ".hpp", ".h", ".c", ".cc", ".cxx"}


def _read_capped(path: Path, cap: int = MAX_FILE_BYTES) -> str:
    text = path.read_text(errors="replace")
    if len(text) > cap:
        text = text[:cap] + f"\n... [truncated at {cap} chars]"
    return text


def build_context_block() -> str:
    parts = []
    for p in CONTEXT_FILES:
        if p.exists():
            parts.append(f"### {p.relative_to(REPO_DIR)}\n```\n{_read_capped(p)}\n```")
    return "\n\n".join(parts)


def build_module_block(module_path: str) -> str:
    """Read all source files in the module directory and return them as a block."""
    abs_path = REPO_DIR / module_path
    if not abs_path.is_dir():
        return f"(directory not found: {module_path})"

    parts = []
    total = 0
    for ext in SOURCE_EXTS:
        for f in sorted(abs_path.glob(f"*{ext}")):
            content = _read_capped(f, MAX_FILE_BYTES)
            parts.append(f"### {f.relative_to(REPO_DIR)}\n```cpp\n{content}\n```")
            total += len(content)
            if total >= MAX_MODULE_BYTES:
                parts.append(f"\n... [stopped reading module files at {MAX_MODULE_BYTES} chars]")
                break
        if total >= MAX_MODULE_BYTES:
            break
    return "\n\n".join(parts) if parts else "(no source files found)"

# ---------------------------------------------------------------------------
# Build & test
# ---------------------------------------------------------------------------

def run_build_and_test() -> str:
    """Run cmake build and non-GPU ctest; return combined output."""
    build_cmd = ["cmake", "--build", str(REPO_DIR / "build"), f"-j{NPROC}"]
    test_cmd  = ["ctest", "--test-dir", str(REPO_DIR / "build"),
                 "-LE", "gpu", "--output-on-failure"]

    results: list[str] = []
    for cmd in [build_cmd, test_cmd]:
        label = " ".join(cmd[:3]) + " ..."
        log(f"  Running: {label}", "debug")
        r = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            cwd=str(REPO_DIR),
        )
        out = (r.stdout + r.stderr).strip()
        # keep output manageable
        if len(out) > 8000:
            lines = out.splitlines()
            out = "\n".join(lines[:60]) + f"\n... [{len(lines)-60} lines omitted] ...\n" + \
                  "\n".join(lines[-40:])
        results.append(f"$ {' '.join(cmd)}\n(exit {r.returncode})\n{out}")
    return "\n\n".join(results)

# ---------------------------------------------------------------------------
# File edit parsing & application
# ---------------------------------------------------------------------------

FILE_BLOCK_RE = re.compile(
    r'<file\s+path="([^"]+)">(.*?)</file>',
    re.DOTALL,
)


def parse_file_edits(response: str) -> list[tuple[str, str]]:
    """Extract (repo_relative_path, new_content) pairs from <file> blocks."""
    return [(m.group(1).strip(), m.group(2)) for m in FILE_BLOCK_RE.finditer(response)]


def apply_edits(edits: list[tuple[str, str]]) -> list[str]:
    """Write each edit to disk. Returns list of applied paths."""
    applied = []
    for rel_path, content in edits:
        abs_path = REPO_DIR / rel_path
        abs_path.parent.mkdir(parents=True, exist_ok=True)
        abs_path.write_text(content)
        log(f"    wrote: {rel_path}", "debug")
        applied.append(rel_path)
    return applied

# ---------------------------------------------------------------------------
# Logging to file
# ---------------------------------------------------------------------------

def open_log(module_path: str):
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    import datetime
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    slug  = module_path.strip("/").replace("/", "_")
    path  = LOG_DIR / f"session_{stamp}_{slug}.log"
    return open(path, "w")

# ---------------------------------------------------------------------------
# Prompt helpers
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = textwrap.dedent("""\
    You are an expert C++ code simplifier working on the Spectra GPU plotting library.
    Your ONLY job in this session is to apply safe, minimal simplifications to a single
    module — reducing duplication, flattening over-nested control flow, removing dead
    code, and narrowing interfaces — WITHOUT changing any observable behavior.

    Hard constraints (NEVER violate):
    - No behavior changes unless it is a confirmed bug fix with a test.
    - No sweeping renames, large file moves, or formatting-only changes.
    - Do NOT touch fence/semaphore lifetimes, swapchain recreation, or IPC protocol.
    - No per-frame allocations added in hot loops.

    When you want to modify a file, output the COMPLETE new file contents inside XML tags:
        <file path="relative/path/from/repo/root">
        ...full file content...
        </file>

    Only output <file> blocks for files you are actually changing.
    Output ALL changed files in a single response.
    After all file blocks, write a brief "## Summary" section describing what you changed
    and why it is safe, using the SKILL.md Step E report format.
""")

ANALYSIS_PROMPT_TEMPLATE = textwrap.dedent("""\
    ## Session context (required reading — already provided below)

    {context}

    ---

    ## Target module for this session

    Module #{num}: `{module_path}`  ({files} source files)

    ## Module source files

    {module_src}

    ---

    ## Your task

    Follow the workflow in SKILL.md (Steps A–E):

    1. Identify simplification candidates with evidence.
    2. For each low-risk candidate: output the complete modified file inside
       `<file path="...">...</file>` XML tags.
    3. After all file edits, write a `## Summary` section following the SKILL.md
       Step E report format.
    4. In the summary, include updated rows for EXPLORATION.md and REPORT.md — you
       will be asked to emit those files after the build passes.

    If there are no actionable candidates, say so clearly and explain why the module
    is clean.
""")

VERIFY_PROMPT_TEMPLATE = textwrap.dedent("""\
    ## Build and test results

    ```
    {build_output}
    ```

    ---

    If there are build/test failures caused by your changes, fix them now by emitting
    corrected `<file path="...">...</file>` blocks.

    If the build is clean, emit the updated:
    - `<file path="skills/code-simplifier/EXPLORATION.md">` — mark module #{num} as
      `simplified` (or `clean` if no changes were made), set session number and today's
      date (2026-03-09), and append a session log entry at the bottom.
    - `<file path="skills/code-simplifier/REPORT.md">` — add a new entry for any
      simplifications applied this session.

    Then write a short final `## Verification Steps` section with the exact commands
    the developer should run to confirm correctness.
""")

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def confirm(prompt: str) -> bool:
    if AUTO:
        return True
    try:
        reply = input(prompt).strip().lower()
    except EOFError:
        return False
    if reply in ("q", "quit"):
        log("Aborted."); sys.exit(0)
    return reply not in ("n", "no")


def run_session(num: int, module_path: str, files: str, gh_token: str) -> None:
    """Run one full simplification session."""

    log(f"\n{'─'*64}")
    log(f"  Module #{num}: {module_path}  ({files} files)")
    log(f"{'─'*64}")

    logfile = open_log(module_path)

    def emit(label: str, text: str) -> None:
        logfile.write(f"\n{'='*60}\n{label}\n{'='*60}\n{text}\n")
        logfile.flush()

    # ── Build prompt ──────────────────────────────────────────────────
    log("  Reading context files...")
    context_block = build_context_block()
    log("  Reading module source files...")
    module_block = build_module_block(module_path)

    analysis_user = ANALYSIS_PROMPT_TEMPLATE.format(
        context=context_block,
        num=num,
        module_path=module_path,
        files=files,
        module_src=module_block,
    )

    messages: list[dict] = [
        {"role": "system",  "content": SYSTEM_PROMPT},
        {"role": "user",    "content": analysis_user},
    ]

    emit("TURN 1 — USER", analysis_user)

    # ── Turn 1: analysis + code changes ──────────────────────────────
    log("  Calling GitHub Models API (turn 1 — analysis + edits)...")
    response1 = gh_models_chat(messages, gh_token)

    emit("TURN 1 — ASSISTANT", response1)
    log(f"\n{'─'*40}")
    log(response1[:3000] + ("\n..." if len(response1) > 3000 else ""))
    log(f"{'─'*40}\n")

    # ── Apply edits ───────────────────────────────────────────────────
    edits = parse_file_edits(response1)
    applied: list[str] = []
    if edits:
        log(f"  Applying {len(edits)} file edit(s):")
        for rel_path, _ in edits:
            # Guard: do not allow writes outside the repo
            abs_target = (REPO_DIR / rel_path).resolve()
            if not str(abs_target).startswith(str(REPO_DIR)):
                log(f"    SKIPPED (outside repo): {rel_path}")
                continue
            log(f"    → {rel_path}")
        applied = apply_edits(
            [(p, c) for p, c in edits
             if str((REPO_DIR / p).resolve()).startswith(str(REPO_DIR))]
        )
    else:
        log("  No file edits in response.")

    # ── Build + test ──────────────────────────────────────────────────
    log("  Running build + tests...")
    build_output = run_build_and_test()
    emit("BUILD OUTPUT", build_output)
    log(f"\n{build_output[:2000]}\n")

    # ── Turn 2: verification + EXPLORATION/REPORT update ─────────────
    verify_user = VERIFY_PROMPT_TEMPLATE.format(
        build_output=build_output,
        num=num,
    )
    messages.append({"role": "assistant", "content": response1})
    messages.append({"role": "user",      "content": verify_user})
    emit("TURN 2 — USER", verify_user)

    log("  Calling GitHub Models API (turn 2 — verification + metadata update)...")
    response2 = gh_models_chat(messages, gh_token)

    emit("TURN 2 — ASSISTANT", response2)
    log(f"\n{'─'*40}")
    log(response2[:3000] + ("\n..." if len(response2) > 3000 else ""))
    log(f"{'─'*40}\n")

    # ── Apply metadata edits (EXPLORATION.md / REPORT.md) ────────────
    meta_edits = parse_file_edits(response2)
    if meta_edits:
        log(f"  Applying {len(meta_edits)} metadata file(s):")
        for rel_path, _ in meta_edits:
            log(f"    → {rel_path}")
        apply_edits(
            [(p, c) for p, c in meta_edits
             if str((REPO_DIR / p).resolve()).startswith(str(REPO_DIR))]
        )

    logfile.close()
    log(f"  Log: {LOG_DIR}")


def main() -> None:
    log("╔══════════════════════════════════════════════════════════════╗")
    log("║   Spectra Code Simplifier — GitHub Models REST API          ║")
    log("╚══════════════════════════════════════════════════════════════╝")
    log(f"  Model:        {MODEL}")
    log(f"  API:          {GH_MODELS_URL}")
    log(f"  Auto:         {AUTO}")
    log(f"  Max sessions: {MAX_SESSIONS or 'unlimited'}")
    log("")

    gh_token = get_github_token()
    # Quick connectivity check
    log("  Verifying GitHub Models API access...")
    try:
        test = gh_models_chat(
            [{"role": "user", "content": "Reply with the single word: ready"}],
            gh_token,
            model=MODEL,
            temperature=0.0,
        )
        log(f"  API OK — model replied: {test.strip()[:40]}\n")
    except SystemExit:
        raise
    except Exception as exc:
        # On connectivity failure, show what models are available
        available = list_available_models(gh_token)
        if available:
            log(f"  Available models: {', '.join(sorted(available))}")
        sys.exit(f"ERROR: GitHub Models API check failed: {exc}")

    session_count = 0

    while True:
        if MAX_SESSIONS and session_count >= MAX_SESSIONS:
            log(f"Reached MAX_SESSIONS={MAX_SESSIONS}. Done.")
            break

        result = get_next_module()
        if result is None:
            log("All modules analyzed. Sweep complete.")
            break

        num, module_path, files = result

        if not confirm(f"  Process module #{num} ({module_path})? [Y/n/q] "):
            log("  Skipping. Mark module as 'clean' in EXPLORATION.md to advance.")
            log("  (The script cannot auto-advance a skipped module.)")
            break

        run_session(num, module_path, files, gh_token)
        session_count += 1

        if not AUTO and session_count > 0:
            if not confirm("\n  Continue to next module? [Y/n/q] "):
                break

    log("\nDone.")


if __name__ == "__main__":
    main()

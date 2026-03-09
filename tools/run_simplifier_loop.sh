#!/usr/bin/env bash
# run_simplifier_loop.sh — thin wrapper; delegates to run_simplifier_loop.py
exec python3 "$(dirname "$(realpath "$0")")/run_simplifier_loop.py" "$@"

# Each iteration picks the next `not-started` module from
# skills/code-simplifier/EXPLORATION.md, runs claude with a fresh
# context (--no-session-persistence), and logs the output.
#
# Usage:
#   ./tools/run_simplifier_loop.sh              # interactive (confirm each module)
#   AUTO=1 ./tools/run_simplifier_loop.sh       # fully automated
#   MAX_SESSIONS=3 ./tools/run_simplifier_loop.sh  # stop after N modules
#
# Environment variables:
#   CLAUDE_BIN      Path to claude binary  (default: ~/.local/bin/claude)
#   AUTO            Set to 1 to skip confirmation prompts
#   MAX_SESSIONS    Maximum modules to process before stopping (default: unlimited)
#   MAX_BUDGET_USD  Per-session dollar cap passed to claude  (default: 2.00)
#   EFFORT          claude effort level: low/medium/high     (default: high)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXPLORATION_FILE="$REPO_DIR/skills/code-simplifier/EXPLORATION.md"
LOG_DIR="$REPO_DIR/skills/code-simplifier/logs"

CLAUDE_BIN="${CLAUDE_BIN:-$HOME/.local/bin/claude}"
AUTO="${AUTO:-0}"
MAX_SESSIONS="${MAX_SESSIONS:-0}"   # 0 = unlimited
MAX_BUDGET_USD="${MAX_BUDGET_USD:-2.00}"
EFFORT="${EFFORT:-high}"

# ─── helpers ────────────────────────────────────────────────────────────────

die() { echo "ERROR: $*" >&2; exit 1; }

check_deps() {
    [[ -x "$CLAUDE_BIN" ]] || die "claude not found at $CLAUDE_BIN. Set CLAUDE_BIN=<path>."
    [[ -f "$EXPLORATION_FILE" ]] || die "EXPLORATION.md not found: $EXPLORATION_FILE"
}

# Returns the first table row whose status column is `not-started`.
# Row format: | # | `src/path/` | files | not-started | session | notes |
get_next_row() {
    grep '| not-started |' "$EXPLORATION_FILE" | head -1
}

parse_field() {
    # $1 = row, $2 = column index (1-based field after splitting on |)
    echo "$1" | awk -F'|' "{print \$$2}" | sed 's/[[:space:]`]//g'
}

# ─── main loop ───────────────────────────────────────────────────────────────

main() {
    check_deps
    mkdir -p "$LOG_DIR"

    echo "╔══════════════════════════════════════════════════════════════╗"
    echo "║         Spectra Code Simplifier — Automated Loop            ║"
    echo "╚══════════════════════════════════════════════════════════════╝"
    echo "  Repo:          $REPO_DIR"
    echo "  Exploration:   $EXPLORATION_FILE"
    echo "  Logs:          $LOG_DIR"
    echo "  Max budget:    \$$MAX_BUDGET_USD / session"
    echo "  Effort:        $EFFORT"
    echo "  Auto mode:     $AUTO"
    [[ "$MAX_SESSIONS" -gt 0 ]] && echo "  Max sessions:  $MAX_SESSIONS"
    echo ""

    SESSION_COUNT=0

    while true; do
        # ── Stop if we hit the session cap ───────────────────────────
        if [[ "$MAX_SESSIONS" -gt 0 && "$SESSION_COUNT" -ge "$MAX_SESSIONS" ]]; then
            echo "Reached MAX_SESSIONS=$MAX_SESSIONS. Stopping."
            exit 0
        fi

        # ── Find next module ─────────────────────────────────────────
        ROW=$(get_next_row)
        if [[ -z "$ROW" ]]; then
            echo "All modules have been analyzed. Sweep complete."
            exit 0
        fi

        MODULE_NUM=$(parse_field "$ROW" 2)
        MODULE_PATH=$(parse_field "$ROW" 3)
        FILES_COUNT=$(parse_field "$ROW" 4)
        NOTES=$(echo "$ROW" | awk -F'|' '{print $7}' | sed 's/^[[:space:]]*//')

        echo "──────────────────────────────────────────────────────────────"
        echo "  Module #$MODULE_NUM: $MODULE_PATH  ($FILES_COUNT files)"
        [[ -n "$NOTES" ]] && echo "  Notes: $NOTES"
        echo "──────────────────────────────────────────────────────────────"

        # ── Confirm (unless AUTO) ────────────────────────────────────
        if [[ "$AUTO" != "1" ]]; then
            read -rp "  Process this module? [Y/n/q] " REPLY
            REPLY="${REPLY:-Y}"
            case "$REPLY" in
                [Qq]*) echo "Aborted."; exit 0 ;;
                [Nn]*) echo "  Skipping — manually mark as 'clean' in EXPLORATION.md to advance."; echo ""; continue ;;
                *) ;;
            esac
        fi

        # ── Build prompt ─────────────────────────────────────────────
        LOG_FILE="$LOG_DIR/session_$(date +%Y%m%d_%H%M%S)_${MODULE_PATH//\//_}.log"

        PROMPT=$(cat <<PROMPT
You are running a focused, single-module code-simplification session on the Spectra
codebase at: $REPO_DIR

## Required reading (do this first, before any analysis)
1. $REPO_DIR/CLAUDE.md
2. $REPO_DIR/FORMAT.md
3. $REPO_DIR/CODEBASE_MAP.md
4. $REPO_DIR/skills/code-simplifier/SKILL.md         ← workflow + non-negotiable rules
5. $REPO_DIR/skills/code-simplifier/EXPLORATION.md   ← sweep state
6. $REPO_DIR/skills/code-simplifier/REPORT.md        ← candidate backlog

## Target module this session
  Path:  $MODULE_PATH
  Files: $FILES_COUNT
  Notes: $NOTES

## What to do
Follow the workflow in SKILL.md exactly:

  Step A – Identify candidates with evidence (duplication, nesting >3, functions >60 lines,
            parameter lists >5, dead code, copy-paste patterns).
  Step B – Define acceptance criteria for each candidate.
  Step C – Implement low-risk candidates (one concept per change, no formatting noise).
  Step D – Validate: cmake --build build -j\$(nproc) then
            ctest --test-dir build -LE gpu --output-on-failure
  Step E – Write a simplification report and update EXPLORATION.md + REPORT.md.

## Hard constraints (from SKILL.md — do NOT violate)
- No behavior changes without a confirmed bug fix + test.
- No sweeping renames, large file moves, or formatting-only changes.
- Never touch fence/semaphore lifetimes or swapchain recreation logic.
- No per-frame allocations in hot paths.
- Mark module #$MODULE_NUM as 'analyzed' or 'simplified' in EXPLORATION.md when done.
- Add a session log entry at the bottom of EXPLORATION.md.
PROMPT
)

        echo "  Starting claude session..."
        echo "  Log: $LOG_FILE"
        echo ""

        # ── Run claude with fresh context ────────────────────────────
        set +e
        "$CLAUDE_BIN" \
            --print \
            --no-session-persistence \
            --permission-mode acceptEdits \
            --effort "$EFFORT" \
            --max-budget-usd "$MAX_BUDGET_USD" \
            --add-dir "$REPO_DIR" \
            "$PROMPT" 2>&1 | tee "$LOG_FILE"
        CLAUDE_EXIT=${PIPESTATUS[0]}
        set -e

        echo ""
        if [[ $CLAUDE_EXIT -ne 0 ]]; then
            echo "  ⚠ claude exited with code $CLAUDE_EXIT. See: $LOG_FILE"
        else
            echo "  ✓ Session complete. Log: $LOG_FILE"
        fi

        SESSION_COUNT=$(( SESSION_COUNT + 1 ))
        echo ""

        # ── Pause between sessions (unless AUTO) ─────────────────────
        if [[ "$AUTO" != "1" && "$CLAUDE_EXIT" -ne 0 ]]; then
            read -rp "  Continue to next module? [Y/n] " REPLY
            REPLY="${REPLY:-Y}"
            [[ "$REPLY" =~ ^[Nn] ]] && exit 1
        fi
    done
}

main "$@"

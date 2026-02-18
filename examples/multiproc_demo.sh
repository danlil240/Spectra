#!/usr/bin/env bash
# multiproc_demo.sh — Launch spectra-backend + one or more spectra-window agents
#
# Usage:
#   ./examples/multiproc_demo.sh                  # 1 agent (default)
#   ./examples/multiproc_demo.sh 2                # 2 agents side-by-side
#   ./examples/multiproc_demo.sh --build-dir /path/to/build-multiproc
#
# The backend creates a default figure (sin(x) line plot) and assigns it to
# the first connecting agent.  Each additional agent gets an empty figure.
# Closing all agent windows shuts down the backend automatically.

set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build-multiproc"
NUM_AGENTS=2
SOCKET="/tmp/spectra-demo-$$.sock"

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --socket)    SOCKET="$2";    shift 2 ;;
        [0-9]*)      NUM_AGENTS="$1"; shift ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

BACKEND="${BUILD_DIR}/spectra-backend"
AGENT="${BUILD_DIR}/spectra-window"

# ── Sanity checks ────────────────────────────────────────────────────────────
if [[ ! -x "$BACKEND" ]]; then
    echo "ERROR: spectra-backend not found at: $BACKEND"
    echo "Build with: cmake --build ${BUILD_DIR} --target spectra-backend spectra-window -j\$(nproc)"
    exit 1
fi
if [[ ! -x "$AGENT" ]]; then
    echo "ERROR: spectra-window not found at: $AGENT"
    exit 1
fi

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Spectra Multi-Process Demo"
echo "  Backend : $BACKEND"
echo "  Agent   : $AGENT"
echo "  Socket  : $SOCKET"
echo "  Agents  : $NUM_AGENTS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ── Cleanup on exit ──────────────────────────────────────────────────────────
PIDS=()
cleanup() {
    echo ""
    echo "[demo] Shutting down..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    rm -f "$SOCKET"
}
trap cleanup EXIT INT TERM

# ── Launch backend ───────────────────────────────────────────────────────────
echo "[demo] Starting backend..."
"$BACKEND" --socket "$SOCKET" &
BACKEND_PID=$!
PIDS+=("$BACKEND_PID")

# Give the backend a moment to bind the socket
sleep 0.3

if ! kill -0 "$BACKEND_PID" 2>/dev/null; then
    echo "ERROR: Backend failed to start"
    exit 1
fi
echo "[demo] Backend running (pid=$BACKEND_PID)"

# ── Launch agents ────────────────────────────────────────────────────────────
for i in $(seq 1 "$NUM_AGENTS"); do
    echo "[demo] Starting agent $i..."
    "$AGENT" --socket "$SOCKET" &
    AGENT_PID=$!
    PIDS+=("$AGENT_PID")
    echo "[demo] Agent $i running (pid=$AGENT_PID)"
    # Stagger agents slightly so they don't race on the socket
    sleep 0.1
done

echo ""
echo "[demo] All processes running. Close all agent windows to exit."
echo "[demo] Tips:"
echo "  • Scroll to zoom in/out (routed through backend)"
echo "  • Press 'g' to toggle grid (routed through backend)"
echo ""

# ── Wait for backend to exit ─────────────────────────────────────────────────
wait "$BACKEND_PID" 2>/dev/null || true
echo "[demo] Backend exited. Demo complete."

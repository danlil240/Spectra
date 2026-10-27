#!/usr/bin/env bash
# Smoke test for the Qt embed demos: launch the demo, let it paint for a few
# seconds, and fail if it exits early (e.g. the first-paint segfault fixed in
# P0-3, where QtRuntime never wired its ThemeManager into ImGuiIntegration).
#
# Usage: qt_demo_smoke.sh <demo-binary> [seconds]
# Needs an X display; re-execs under xvfb-run when DISPLAY is unset, and
# returns 77 (ctest SKIP) when neither is available.
set -u

bin=$1
secs=${2:-8}

if [ -z "${DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        exec xvfb-run -a "$0" "$@"
    fi
    echo "SKIP: no DISPLAY and xvfb-run not found"
    exit 77
fi

export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-xcb}

timeout -s TERM -k 5 "$secs" "$bin"
rc=$?

# 124: still running when the timeout fired (expected).
# 137: needed SIGKILL after the timeout (still alive, so still a pass).
if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    echo "PASS: $(basename "$bin") ran for ${secs}s"
    exit 0
fi

echo "FAIL: $(basename "$bin") exited early with status $rc"
exit 1

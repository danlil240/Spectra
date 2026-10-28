#!/usr/bin/env bash
# Run the QA agent's design review under CI-like conditions and fail when any
# of its 63 captures does not show the state it is named after.
#
# Every capture carries semantic assertions (menu popup open, crosshair at the
# hovered point, panels closed, UI scale in effect, Settings modal + tab, ...)
# that the agent checks on the captured frame; a failed assertion is an ERROR
# issue in <out>/qa_report.{txt,json} and makes the agent exit non-zero.
#
# Conditions: a private Xvfb display (always, when xvfb-run exists, so window
# positions/sizes are not changed by a desktop window manager), software Vulkan
# (lavapipe) unless an ICD is already pinned, and a throw-away
# SPECTRA_CONFIG_DIR / XDG_CONFIG_HOME / XDG_STATE_HOME / XDG_RUNTIME_DIR so
# saved user settings (UI scale, theme), autosave and logs cannot leak in or
# out.  HOME is inherited unchanged.
#
# Usage: run_design_review.sh <spectra_qa_agent> [output-dir]
# Exit:  the agent's exit code; 77 (ctest SKIP) when no X server is available.
set -u

agent=$1
out=${2:-}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/config" "$tmp/xdg-config" "$tmp/state" "$tmp/xdg"
chmod 700 "$tmp/xdg"
[ -n "$out" ] || out="$tmp/out"
rm -rf "$out"
mkdir -p "$out"

export SPECTRA_CONFIG_DIR="$tmp/config"
export XDG_CONFIG_HOME="$tmp/xdg-config"
export XDG_STATE_HOME="$tmp/state"
export XDG_RUNTIME_DIR="$tmp/xdg"

if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -z "${VK_DRIVER_FILES:-}" ]; then
    for d in /usr/share/vulkan/icd.d /etc/vulkan/icd.d /usr/lib/x86_64-linux-gnu/vulkan/icd.d; do
        for f in "$d"/lvp_icd*.json "$d"/*lavapipe*.json; do
            if [ -f "$f" ]; then
                export VK_ICD_FILENAMES="$f"
                break 2
            fi
        done
    done
fi

args=(--design-review --no-fuzz --no-scenarios --seed 42 --duration 900 --output-dir "$out")

if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a -s '-screen 0 1920x1200x24' "$agent" "${args[@]}"
    rc=$?
elif [ -n "${DISPLAY:-}" ]; then
    "$agent" "${args[@]}"
    rc=$?
else
    echo "SKIP: no DISPLAY and xvfb-run not found"
    exit 77
fi

if [ -f "$out/qa_report.txt" ]; then
    sed -n '/^Design Assertions:/,/^$/p' "$out/qa_report.txt"
    sed -n '/^Issue Summary/,/^Seed for reproduction/p' "$out/qa_report.txt"
fi
echo "design review exit code: $rc (report: $out/qa_report.txt)"
exit "$rc"

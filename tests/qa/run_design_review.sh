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
# (lavapipe) unless an ICD is already pinned, and a throw-away HOME /
# XDG_RUNTIME_DIR so saved user settings (UI scale, theme) cannot leak in.
#
# Usage: run_design_review.sh <spectra_qa_agent> [output-dir]
# Env:   SPECTRA_QA_SEED (default 42), SPECTRA_QA_SCREEN (Xvfb screen, default
#        1920x1200x24; the pointer starts at its centre) for independent repeats.
# Exit:  the agent's exit code; 77 (ctest SKIP) when no X server is available.
set -u

agent=$1
out=${2:-}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/home" "$tmp/xdg"
chmod 700 "$tmp/xdg"
[ -n "$out" ] || out="$tmp/out"
rm -rf "$out"
mkdir -p "$out"

export HOME="$tmp/home"
export XDG_CONFIG_HOME="$tmp/home/.config"
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

seed=${SPECTRA_QA_SEED:-42}
screen=${SPECTRA_QA_SCREEN:-1920x1200x24}
args=(--design-review --no-fuzz --no-scenarios --seed "$seed" --duration 900 --output-dir "$out")

if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a -s "-screen 0 $screen" "$agent" "${args[@]}"
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

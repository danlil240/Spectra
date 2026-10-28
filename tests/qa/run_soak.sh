#!/usr/bin/env bash
# Long-run lifetime / memory soak of the QA agent: each scenario runs N times
# in one process (--repeat), RSS and the in-use heap are sampled after every
# iteration (after returning to a single window / single figure), and the
# least-squares slope after the warm-up iteration is gated.  A slope above
# the threshold is an ERROR in <out>/<scenario>/qa_report.{txt,json} and makes
# the agent (and this script) exit non-zero.
#
# Conditions match run_design_review.sh: private Xvfb display, software Vulkan
# (lavapipe) unless an ICD is pinned, throw-away HOME / XDG dirs.
#
# Usage: run_soak.sh <spectra_qa_agent> [output-dir]
# Env:   SPECTRA_SOAK_SCENARIOS  space-separated scenarios (default below)
#        SPECTRA_SOAK_REPEAT     iterations per scenario (default 12)
#        SPECTRA_SOAK_WARMUP     leading iterations excluded from the slope (default 2:
#                                RSS settles over the first 2-3 iterations, e.g.
#                                figure_serialization 186->188MB, then flat for 30)
#        SPECTRA_SOAK_MAX_RSS_SLOPE / SPECTRA_SOAK_MAX_HEAP_SLOPE  MB/iteration
# Exit:  0 all pass, 1 any scenario failed, 77 (ctest SKIP) without an X server.
set -u

agent=$1
out=${2:-}
scenarios=${SPECTRA_SOAK_SCENARIOS:-"stress_docking series_mixing figure_serialization"}
repeat=${SPECTRA_SOAK_REPEAT:-12}
warmup=${SPECTRA_SOAK_WARMUP:-2}
max_rss=${SPECTRA_SOAK_MAX_RSS_SLOPE:-0.5}
max_heap=${SPECTRA_SOAK_MAX_HEAP_SLOPE:-0.05}

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

if command -v xvfb-run >/dev/null 2>&1; then
    run=(xvfb-run -a -s '-screen 0 1920x1200x24')
elif [ -n "${DISPLAY:-}" ]; then
    run=()
else
    echo "SKIP: no DISPLAY and xvfb-run not found"
    exit 77
fi

rc=0
for s in $scenarios; do
    mkdir -p "$out/$s"
    "${run[@]}" "$agent" --scenario "$s" --no-fuzz --seed 42 --duration 600 \
        --repeat "$repeat" --repeat-warmup "$warmup" --max-rss-slope "$max_rss" --max-heap-slope "$max_heap" \
        --output-dir "$out/$s" >"$out/$s/agent.log" 2>&1
    src=$?
    if [ -f "$out/$s/qa_report.txt" ]; then
        sed -n '/^Soak RSS Slope/,/^$/p' "$out/$s/qa_report.txt"
    fi
    if [ "$src" -ne 0 ]; then
        echo "FAIL: $s (agent exit $src, log: $out/$s/agent.log)"
        sed -n '/^Issue Summary/,/^Seed for reproduction/p' "$out/$s/qa_report.txt" 2>/dev/null
        tail -n 30 "$out/$s/agent.log"
        rc=1
    fi
done
echo "soak exit code: $rc (reports: $out)"
exit "$rc"

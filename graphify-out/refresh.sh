#!/usr/bin/env bash
# Refresh graphify after code changes (Spectra workflow).
set -euo pipefail
cd "$(dirname "$0")/.."
PY="$(cat graphify-out/.graphify_python 2>/dev/null || echo python3)"
graphify update .
"$PY" graphify-out/relabel_communities.py
echo "Done: graphify-out/graph.json + graph.html + GRAPH_REPORT.md"

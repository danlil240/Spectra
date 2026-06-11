#!/usr/bin/env python3
"""Merge optional SCIP JSON into graphify extraction (manual enrichment).

Graphify's scip_ingest is not wired to the CLI yet. When you have a SCIP-style
JSON export (e.g. from a future scip-clang run), place it at:

  graphify-out/scip.json

Then run:

  python graphify-out/merge_scip.py
  graphify cluster-only .
  python graphify-out/relabel_communities.py

Requires graphify's ingest_scip_json (bundled with graphifyy).
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "graphify-out"
SCIP_PATH = OUT / "scip.json"
EXTRACT_PATH = OUT / ".graphify_extract.json"
GRAPH_PATH = OUT / "graph.json"


def main() -> int:
    if not SCIP_PATH.exists():
        print(f"No {SCIP_PATH} — skip SCIP merge.")
        print("Install scip-clang (when available), export JSON, save to graphify-out/scip.json")
        return 0

    from graphify.scip_ingest import ingest_scip_json
    from graphify.build import build_from_json
    from graphify.semantic_cleanup import sanitize_semantic_fragment
    from networkx.readwrite import json_graph
    import networkx as nx

    doc = json.loads(SCIP_PATH.read_text(encoding="utf-8"))
    scip = ingest_scip_json(doc)
    print(f"SCIP fragment: {len(scip.get('nodes', []))} nodes, {len(scip.get('edges', []))} edges")

    if EXTRACT_PATH.exists():
        base = json.loads(EXTRACT_PATH.read_text(encoding="utf-8"))
    elif GRAPH_PATH.exists():
        print("No .graphify_extract.json — rebuild graph first (graphify update .)")
        return 1
    else:
        base = {"nodes": [], "edges": [], "hyperedges": []}

    seen = {n["id"] for n in base.get("nodes", [])}
    for n in scip.get("nodes", []):
        if n["id"] not in seen:
            base["nodes"].append(n)
            seen.add(n["id"])
    base["edges"] = base.get("edges", []) + scip.get("edges", [])
    base = sanitize_semantic_fragment(base)
    EXTRACT_PATH.write_text(json.dumps(base, indent=2), encoding="utf-8")
    print(f"Wrote merged extraction: {len(base['nodes'])} nodes, {len(base['edges'])} edges")
    print("Next: graphify cluster-only . && python graphify-out/relabel_communities.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())

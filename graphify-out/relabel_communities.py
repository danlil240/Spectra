#!/usr/bin/env python3
"""Heuristic community labeling for graphify graph.json (no LLM)."""
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import networkx as nx
from networkx.readwrite import json_graph

from graphify.analyze import god_nodes, surprising_connections, suggest_questions
from graphify.cluster import score_all
from graphify.export import to_html
from graphify.report import generate

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "graphify-out"

GENERIC = frozenset(
    {
        "TEST",
        "String",
        "Int",
        "Float",
        "Double",
        "Bool",
        "Void",
        "True",
        "False",
        "None",
        "Object",
        "Array",
        "Map",
        "Set",
        "Vector",
        "Node",
        "Edge",
        "Graph",
        "init",
        "run",
        "main",
        "step",
        "update",
        "render",
        "draw",
        "build",
        "create",
    }
)

TYPE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def is_type_name(lbl: str) -> bool:
    if not lbl or lbl.startswith(".") or len(lbl) < 3 or len(lbl) > 48:
        return False
    if lbl in GENERIC:
        return False
    if not lbl[0].isupper():
        return False
    if lbl.isupper() and len(lbl) <= 4:
        return False
    return TYPE_RE.match(lbl.rstrip("$")) is not None


def path_key(sf: str) -> str:
    if not sf:
        return ""
    parts = Path(sf).parts
    if not parts:
        return sf
    if parts[0] in (
        "src",
        "include",
        "tests",
        "python",
        "examples",
        "docs",
        "plans",
        "skills",
        ".cursor",
        ".github",
    ):
        if len(parts) >= 3:
            return "/".join(parts[:3])
        return "/".join(parts[:2])
    if len(parts) >= 2:
        return "/".join(parts[:2])
    return parts[0]


def label_community(G: nx.Graph, members: list[str], cid: int) -> str:
    path_counts: Counter[str] = Counter()
    stems: Counter[str] = Counter()
    types: Counter[str] = Counter()
    concepts: Counter[str] = Counter()

    for m in members:
        d = G.nodes[m]
        sf = d.get("source_file") or ""
        if sf:
            path_counts[path_key(sf)] += 1
            stems[Path(sf).stem] += 1
        lbl = d.get("label") or ""
        ft = d.get("file_type") or ""
        if ft in ("concept", "rationale", "document", "paper") and lbl and len(lbl) < 60:
            concepts[lbl] += 1
        if is_type_name(lbl):
            types[lbl] += 1

    if len(members) == 1:
        d = G.nodes[members[0]]
        sf = d.get("source_file") or ""
        if sf:
            return Path(sf).stem
        return (d.get("label") or members[0])[:40]

    if concepts and concepts.most_common(1)[0][1] >= max(2, len(members) // 3):
        return concepts.most_common(1)[0][0][:40]

    top_path = path_counts.most_common(1)[0][0] if path_counts else ""
    top_stem = stems.most_common(1)[0][0] if stems else ""

    # Tests / third_party: prefer file stem over incidental symbol names.
    if top_path.startswith("tests/") and top_stem:
        if top_stem.startswith("test_"):
            return top_stem.removeprefix("test_")
        return top_stem
    if "third_party" in top_path and top_stem:
        return top_stem

    type_by_deg = sorted(
        (
            (G.degree(m), G.nodes[m].get("label", ""))
            for m in members
            if is_type_name(G.nodes[m].get("label", ""))
        ),
        reverse=True,
    )
    # Prefer type names that match the dominant file stem (e.g. VulkanBackend in vk_backend.cpp).
    anchor = None
    if type_by_deg and top_stem:
        stem_lower = top_stem.lower().replace("_", "")
        for _, lbl in type_by_deg:
            compact = lbl.lower().replace("_", "")
            if compact in stem_lower or stem_lower in compact:
                anchor = lbl
                break
    if anchor is None and type_by_deg:
        anchor = type_by_deg[0][1]

    if stems and stems.most_common(1)[0][1] >= len(members) * 0.7:
        stem = stems.most_common(1)[0][0]
        if anchor and anchor.lower() not in stem.lower():
            return f"{anchor} ({stem})"
        return stem

    if anchor and top_path:
        short = top_path.split("/")[-1]
        if anchor.lower() == short.lower() or anchor.lower() in top_path.lower():
            return anchor
        return f"{anchor} · {top_path}"
    if anchor:
        return anchor
    if types:
        return types.most_common(1)[0][0]
    if top_path:
        return top_path
    if top_stem:
        return top_stem
    for m in sorted(members, key=lambda x: G.degree(x), reverse=True):
        lbl = G.nodes[m].get("label") or ""
        if lbl and not lbl.startswith(".") and len(lbl) < 40:
            return lbl
    return f"misc-{cid}"


def dedupe_labels(labels: dict[int, str]) -> dict[int, str]:
    used: Counter[str] = Counter()
    out: dict[int, str] = {}
    for cid in sorted(labels.keys()):
        base = labels[cid]
        if used[base]:
            used[base] += 1
            out[cid] = f"{base} #{used[base]}"
        else:
            used[base] = 1
            out[cid] = base
    return out


def main() -> int:
    graph_path = OUT / "graph.json"
    data = json.loads(graph_path.read_text(encoding="utf-8"))
    G = json_graph.node_link_graph(data, edges="links")

    communities: dict[int, list[str]] = defaultdict(list)
    for n, d in G.nodes(data=True):
        cid = d.get("community")
        if cid is not None:
            communities[int(cid)].append(n)
    communities = dict(communities)

    raw_labels = {cid: label_community(G, members, cid) for cid, members in communities.items()}
    labels = dedupe_labels(raw_labels)

    community_n = sum(1 for v in labels.values() if re.match(r"^Community \d+$", v))
    misc = sum(1 for v in labels.values() if v.startswith("misc-"))
    print(
        f"Communities: {len(labels)} | Community N: {community_n} | misc: {misc} | "
        f"unique: {len(set(labels.values()))}"
    )

    cohesion = score_all(G, communities)
    gods = god_nodes(G)
    surprises = surprising_connections(G, communities)
    questions = suggest_questions(G, communities, labels)

    detection = {"total_files": 1126, "total_words": 1574269, "files": {"code": [], "document": []}}
    tokens = {"input": 1067326, "output": 53513}
    report = generate(
        G,
        communities,
        cohesion,
        labels,
        gods,
        surprises,
        detection,
        tokens,
        str(ROOT),
        suggested_questions=questions,
    )
    (OUT / "GRAPH_REPORT.md").write_text(report, encoding="utf-8")
    (OUT / ".graphify_labels.json").write_text(
        json.dumps({str(k): v for k, v in labels.items()}, indent=2),
        encoding="utf-8",
    )

    if not isinstance(data.get("graph"), dict):
        data["graph"] = {}
    data["graph"]["community_labels"] = {str(k): v for k, v in labels.items()}
    graph_path.write_text(json.dumps(data), encoding="utf-8")

    node_to_community = {nid: cid for cid, members in communities.items() for nid in members}
    meta = nx.Graph()
    for cid in communities:
        meta.add_node(str(cid), label=labels[cid])
    edge_counts: Counter[tuple[int, int]] = Counter()
    for u, v in G.edges():
        cu, cv = node_to_community.get(u), node_to_community.get(v)
        if cu is not None and cv is not None and cu != cv:
            edge_counts[(min(cu, cv), max(cu, cv))] += 1
    for (cu, cv), w in edge_counts.items():
        meta.add_edge(
            str(cu),
            str(cv),
            weight=w,
            relation=f"{w} cross-community edges",
            confidence="AGGREGATED",
        )
    member_counts = {cid: len(members) for cid, members in communities.items()}
    meta_communities = {cid: [str(cid)] for cid in communities}
    to_html(
        meta,
        meta_communities,
        str(OUT / "graph.html"),
        community_labels=labels,
        member_counts=member_counts,
    )

    print("Top communities:")
    for cid in sorted(communities.keys(), key=lambda c: len(communities[c]), reverse=True)[:12]:
        print(f"  {labels[cid]} ({len(communities[cid])})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

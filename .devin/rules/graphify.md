---
trigger: always_on
---
## graphify — Mandatory Pre-Search Rule

This project has a graphify knowledge graph at `graphify-out/`.

**MANDATORY: Before using code_search, grep_search, find_by_name, read_file, or any search/grep/read tool to explore the codebase, you MUST run graphify first:**
- `graphify query "<question>"` — scoped subgraph for any codebase or architecture question
- `graphify path "<A>" "<B>"` — dependency path between two symbols
- `graphify explain "<concept>"` — all nodes related to a concept

This applies to YOU and to every subagent you spawn. Include this rule explicitly in every subagent prompt that involves code exploration. Do not skip graphify because files are "already known" or because you are executing a plan — the graph surfaces cross-file dependencies and INFERRED edges that grep and Read cannot find.

Only use code_search/grep_search/find_by_name/read_file directly when:
1. graphify has already oriented you and you need to modify or debug specific lines
2. `graphify-out/graph.json` does not exist yet
3. You need to read a specific file whose exact path is already known (e.g. from graphify output or user instruction)

- If `graphify-out/wiki/index.md` exists, navigate it instead of reading raw files
- Read `graphify-out/GRAPH_REPORT.md` only for broad architecture review when query/path/explain do not surface enough context
- After modifying code files in this session, run `graphify update .` to keep the graph current (AST-only, no API cost)

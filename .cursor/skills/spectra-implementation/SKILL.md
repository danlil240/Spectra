---
name: spectra-implementation
description: >-
  Plan, design, and implement Spectra C++/GLSL/Python changes. Use when breaking
  down a feature, making architecture decisions before coding, or writing/fixing
  source in src/, include/spectra/, shaders, tests, or python/.
---

# Spectra Implementation

Read first: `CLAUDE.md`, `CODEBASE_MAP.md`, affected `src/` / `include/spectra/` files.

## 1. Plan (before code)

Read `plans/ROADMAP.md`, `plans/QA_results.md` if relevant.

Output:

```text
## Task: <title>
Type: feature | bug | refactor | chore
Affected: <subsystems>
Skill: <.cursor/skills/... or none>
Risks: API surface | IPC version | GPU lifetime | threads

### Steps
1. [design] …
2. [code] <files>
3. [test] …
4. [build-and-test skill]

### Acceptance
- measurable criteria
```

Flag: `include/spectra/` changes need deprecation discipline; IPC wire format needs version bump.

## 2. Design (non-trivial changes only)

**Invariants:** no globals; RAII/`unique_ptr`; `span`/`string_view` in APIs; no exceptions in render backend; deferred GPU cleanup; explicit IPC versioning.

Output:

```text
## Design: <title>
### Proposed
<signatures + ownership; which thread creates/destroys>
### Integration
<App → Figure → …>
### Risks
- …
### Coder tasks
1. file …
```

Do not write full implementations — interfaces and ownership only.

## 3. Code

**Style:** Allman, 4 spaces, 100 cols; `PascalCase` types, `snake_case` functions, trailing `_` members.

**Rules:**

- Minimal diff; match architect spec; register new `.cpp` in CMakeLists.
- No global/singleton state; never destroy Vulkan resources on app thread.
- Tests in `tests/unit/` (GTest); label GPU tests `gpu`; golden in `tests/golden/`.
- No comments on unchanged code; no speculative error handling.

Output:

```text
## Implementation Summary
### Modified / Created
- path — one line
### CMake
- …
### Gaps for build/test
- …
```

Route: shaders → `add-shader`; series → `add-series-type`; IPC → `ipc-protocol-dev`; Python → `python-bindings`.

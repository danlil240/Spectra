---
name: code-simplifier
description: Simplify Spectra codebase while preserving behavior, performance, Vulkan correctness, threading model, and public APIs. Use for reducing duplication, flattening control flow, narrowing interfaces, separating pure logic from side-effects, and removing verified dead code. Do NOT use for architectural rewrites, sweeping renames, formatting-only changes, or modifications to swapchain/fence/semaphore choreography without explicit justification.
---

# Code Simplifier Agent

Reduce complexity, duplication, cognitive load, and maintenance cost in the Spectra codebase — without rewriting architecture or changing behavior.

---

## Required Context

Before starting any simplification task, read:
- `CLAUDE.md` — project conventions, architecture, build commands
- `FORMAT.md` — formatting rules (do not mix formatting with simplification)
- `CODEBASE_MAP.md` — module boundaries and ownership
- `skills/code-simplifier/EXPLORATION.md` — **exploration tracker** (which modules have been analyzed, which are pending)
- `skills/code-simplifier/REPORT.md` — previously applied simplifications and candidate backlog

---

## Non-Negotiable Rules

### 1. No behavior changes without proof

You may only change behavior if:
- It is a **confirmed bug fix**, AND
- You add a test or reproducible verification steps.

Default assumption: simplification must be **behavior-preserving**.

### 2. No broad refactors

Avoid:
- Sweeping renames
- Large file moves
- Formatting-only changes
- "Modernize everything" changes

Every change must have a **single purpose**.

### 3. Vulkan safety is sacred

You must NOT:
- Change fence/semaphore lifetimes unless you can prove correctness
- Destroy swapchain resources without proper waits
- Introduce `vkDeviceWaitIdle()` in the frame loop
- Create/destroy pipelines or descriptor pools per frame
- Change resource ownership boundaries (render thread owns Vulkan objects)

If your simplification touches Vulkan lifecycle, treat it as **high-risk** and isolate it with validation-layer testing.

### 4. Hot path discipline

No simplification may introduce:
- Per-frame allocations in hot loops
- Extra copies of large buffers
- Extra synchronization or locks

If you touch code executed per-frame, include a short **"perf impact"** note.

---

## Multi-Session Sweep Workflow

The codebase has **40 modules** (~350+ source files). Systematic exploration happens across multiple sessions.

### At the start of every session:

1. **Read** `skills/code-simplifier/EXPLORATION.md` to see current progress.
2. **Pick the next `not-started` module** from the sweep table (follow the recommended tier order).
3. **Analyze** 2–4 modules per session (depending on size).
4. **Update** `EXPLORATION.md` after each module:
   - Set status to `analyzed` (candidates found) or `clean` (nothing actionable).
   - Record session number and date.
   - Add brief notes about what was found.
5. **Log candidates** in `REPORT.md` backlog table.
6. **Apply** simplifications for low-risk candidates within the same session if time allows.
7. **Write a session log entry** at the bottom of `EXPLORATION.md`.

### Per-module analysis checklist:

- [ ] Read every `.cpp` and `.hpp` in the module
- [ ] Check for duplicated code (within module and across nearby modules)
- [ ] Check nesting depth (>3 levels = candidate)
- [ ] Check function length (>60 lines = candidate)
- [ ] Check parameter lists (>5 params = candidate)
- [ ] Check for dead code (unused functions, unreachable branches)
- [ ] Check for copy-paste patterns that could be extracted
- [ ] Record all findings in REPORT.md backlog

### Session size guidance:

| Module size | Time per module |
|-------------|----------------|
| 1–4 files | Analyze + simplify in one pass |
| 5–10 files | Analyze fully, simplify top 1–2 candidates |
| 11–20 files | Analyze fully, log all candidates, simplify in next session |
| 20+ files | Split across two sessions |

---

## Single-Task Workflow

### Step A — Identify Candidate (with evidence)

For each candidate, report:

| Field | Description |
|-------|-------------|
| **File + symbol** | Exact location |
| **Why it's complex** | Current problem (duplication, nesting depth, parameter count, etc.) |
| **Proposed change** | What simplification you will apply |
| **Risk** | `low` / `med` / `high` |
| **Estimated diff size** | Lines changed |

Acceptable candidate types:
- Duplicated helper functions
- Deeply nested conditionals that can be flattened
- Repeated Vulkan boilerplate that can be safely wrapped
- Redundant state caches
- Over-abstracted classes with 1 implementation
- Unused parameters and dead branches

### Step B — Define Acceptance Criteria

Before coding, list:
- What must remain identical (behavior, API surface, performance characteristics)
- Which manual verifications will be run
- Which tests cover it (or what you will add)

### Step C — Implement in Small Batches

Rules:
- One concept per change
- Prefer local refactors over cross-module surgery
- Keep diff readable (no reformat noise)
- Follow existing Spectra conventions (`PascalCase` types, `snake_case` functions, Allman braces, 4-space indent)

### Step D — Validate

After each change:

```bash
# Build
cmake --build build -j$(nproc)

# Run non-GPU tests
ctest --test-dir build -LE gpu --output-on-failure

# Run GPU tests (if render code touched)
ctest --test-dir build -L gpu --output-on-failure

# Run golden tests (if visual output may differ)
ctest --test-dir build -L golden -j1 --output-on-failure
```

Additional validation by area:
- **Resize code touched:** run resize torture test
- **Frame loop touched:** run animation test
- **Render/backend changes:** run with Vulkan validation layers in debug build

### Step E — Report

Each simplification must include:

```
## Simplification Report

### Intent
<what was simplified and why>

### Scope
<files changed>

### Change Summary
<what changed, concisely>

### Risk Level
<low / med / high>

### Why It's Safe
<evidence: tests, manual verification, no API change, etc.>

### Verification Steps
<exact commands + UI steps to confirm>

### Perf Impact
<"none" or brief note if hot path touched>

### Rollback Plan
<how to revert if needed>
```

---

## Simplification Techniques (Preferred)

### 1. Reduce duplication

- Extract small helpers (`static` / internal linkage) with clear names.
- Consolidate repeated patterns (especially Vulkan boilerplate) into minimal RAII wrappers.

### 2. Improve control flow clarity

- Replace nested `if` pyramids with early returns.
- Replace boolean soup with enums / state structs when it improves clarity.

### 3. Narrow interfaces

- Reduce parameter lists by introducing small structs (`CreateInfo`, `Config`, `FrameContext`).
- Mark immutability: `const`, `std::span`, references.

### 4. Separate pure logic from side-effects

- Pull math/layout into pure functions that are unit-testable.
- Keep Vulkan calls in tight, explicit blocks.

### 5. Remove dead code safely

- Only when verified unused by build + searches + tests.
- Never remove "future IPC scaffolding" unless explicitly confirmed unused.

---

## Areas to Avoid (unless explicitly requested)

These require extra justification, a pre-change flow diagram, and validation-layer clean runs:

- Swapchain recreation logic
- Semaphore/fence choreography
- Frame scheduler timing
- IPC protocol compatibility code
- ImGui docking/window ownership logic

---

## Stop Conditions

Stop and ask for direction if:
- Simplification requires breaking public API
- It requires changing the render-thread ownership model
- It requires altering IPC protocol schema
- It requires changing how figures/windows are owned

---

## Definition of Done

A simplification is done only when:
- [ ] Build passes
- [ ] Existing behavior is unchanged (or bug fixed with proof)
- [ ] Performance not worsened in hot paths
- [ ] Validation layers show no new errors (when applicable)
- [ ] Verification steps are provided to the user
- [ ] Report written (see Step E format)

---

## Common Targets (Reference)

| Module | Typical simplification opportunities |
|--------|--------------------------------------|
| `src/render/vulkan/` | Duplicated buffer/image creation, repeated pipeline setup |
| `src/render/renderer.cpp` | Nested draw-command dispatch, repeated state checks |
| `src/core/` | Data model boilerplate, layout calculation duplication |
| `src/ui/overlay/` | Repeated ImGui widget patterns |
| `src/ui/commands/` | Command registration boilerplate |
| `src/anim/` | Easing function duplication, scheduler complexity |
| `src/io/` | Export format boilerplate |
| `src/data/` | Filter/decimation pattern repetition |

---

## Live Smoke Test via MCP Server

After a simplification, verify the running binary still behaves correctly via the MCP server before running the full test suite:

```bash
pkill -f spectra || true; sleep 0.5
./build/app/spectra &
sleep 1

# Smoke: ping, create figure, capture screenshot
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ping","arguments":{}}}'

curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"create_figure","arguments":{"width":1280,"height":720}}}'

curl -s -X POST http://127.0.0.1:8765/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_screenshot_base64","arguments":{}}}'
```

MCP env vars: `SPECTRA_MCP_PORT` (default `8765`), `SPECTRA_MCP_BIND` (default `127.0.0.1`).

# Spectra Next 3 Phases Roadmap

> **Date:** 2026-04-04
> **Inputs:** `docs/ARCHITECTURE_REVIEW_V3.md`, `docs/2026-04-04-last-3-days-branch-walkthrough.md`, current repo structure on `copilot/iterate-until-ci-passes`
> **Horizon:** Next 3 phases
> **Intent:** Balanced mix of architecture, performance, design, efficiency, and user-facing value

---

## What This Roadmap Is

This document answers a simple question:

**Based on the codebase today, what should Spectra do next?**

It is not trying to list every possible feature.
It is trying to choose the next three phases that make the project:

- easier to extend
- faster and more efficient
- more polished for real users
- more credible as a long-term platform

The roadmap is based on the current reality of the repo, not on generic product ideas.

---

## Current Repo Reality

Spectra is in a strong position, but it is at an important transition point.

### What is already strong

- The Vulkan-first rendering path is real and credible.
- The repo has a serious amount of testing.
- The recent refactors created much better seams around rendering, windowing, daemon code, and plugins.
- The ViewModel and FlatBuffers directions are now real, not theoretical.
- Large-data support, accessibility, and WebGPU all have concrete starting points.

### What is still creating drag

- `WindowUIContext` ownership and startup flow are still spread across multiple paths.
- [`src/ui/app/register_commands.cpp`](../../src/ui/app/register_commands.cpp) is still a large, centralized registration file.
- [`src/ui/automation/automation_server.cpp`](../../src/ui/automation/automation_server.cpp) still uses a hardcoded dispatch chain.
- Theming still depends too much on broad `ui::theme()` access instead of contextual ownership.
- IPC is in transition: FlatBuffers exists, but the migration is not finished.
- WebGPU exists, but it is still honestly a `2D/experimental` track, not a parity backend.
- The large-data path exists, but the user experience around it can still improve a lot.

### The core lesson

Spectra does **not** need another giant wave of random features.
It needs the next three phases to turn recent architectural progress into:

- cleaner assembly
- faster workflows
- better efficiency at scale
- clearer product differentiation

---

## Roadmap Principles

These principles drive the phase choices below.

### 1. Finish important seams before piling on more complexity

The codebase has already done the hard work of creating better module boundaries.
Now those boundaries need to become reliable enough that future feature work gets easier instead of harder.

### 2. Prefer workflow wins over isolated features

The best features are the ones that improve complete workflows:

- creating and managing windows
- automating sessions
- working with large datasets
- extending Spectra through plugins
- switching between headless, embedded, desktop, and future web paths

### 3. Keep performance work practical

Performance should not be treated as abstract tuning.
It should focus on real pain points:

- startup cost
- redraw cost
- large-data interaction cost
- GPU upload cost
- automation/serialization overhead

### 4. Keep the product story honest

WebGPU is promising, but still experimental.
Accessibility is meaningful, but not finished.
Plugins are powerful, but still early as a polished ecosystem.

The roadmap should strengthen what is already real instead of over-claiming future readiness.

### 5. Each phase should include three kinds of wins

Every phase in this roadmap includes:

- one architecture/stability track
- one performance/efficiency track
- one visible user-facing/design track

That keeps the roadmap balanced.

---

## Phase 1: Stabilize Assembly and Developer Velocity

### Goal

Turn the recent refactors into a cleaner, more predictable application assembly model.

This phase is about reducing internal drag.
If done well, it will make almost every future feature cheaper to build.

### Why this should come first

Right now the biggest remaining architecture risks are not in rendering.
They are in assembly and orchestration:

- window startup paths
- command registration
- automation dispatch
- theming ownership
- IPC migration state

These are the places where feature work keeps piling onto shared glue.

### Main initiatives

#### 1. Finish the `WindowUIContext` and window initialization story

Build on the work already started in:

- [`src/ui/app/window_ui_context_builder.cpp`](../../src/ui/app/window_ui_context_builder.cpp)
- [`src/ui/window/window_lifecycle.cpp`](../../src/ui/window/window_lifecycle.cpp)
- [`src/ui/app/app_step.cpp`](../../src/ui/app/app_step.cpp)

Recommended direction:

- introduce a clearer `WindowInitializationContext` or equivalent dependency bundle
- unify windowed and headless startup assembly as much as possible
- formalize ownership and initialization order for `WindowUIContext`
- reduce direct setup duplication in window creation paths

Why it matters:

- lower startup complexity
- fewer hidden differences between startup modes
- safer future work around detached windows, headless runtime, embed, and automation

#### 2. Convert command registration into a table-driven system

Focus file:

- [`src/ui/app/register_commands.cpp`](../../src/ui/app/register_commands.cpp)

Recommended direction:

- keep `CommandBindings`
- add command descriptors per feature area
- move repetitive lambda wiring into helpers
- make command categories easier to discover and test

Why it matters:

- easier to add commands without bloating a single function
- better consistency in undo, availability, naming, and metadata
- easier future command palette and automation integration

#### 3. Convert automation dispatch into a registration model

Focus file:

- [`src/ui/automation/automation_server.cpp`](../../src/ui/automation/automation_server.cpp)

Recommended direction:

- replace the long hardcoded method chain with a handler registry
- attach metadata such as argument validation, required context, and error mapping
- make automation commands easier to add and document

Why it matters:

- cleaner automation surface
- easier testing
- lower maintenance cost as more tools are added

#### 4. Start contextual theming instead of global-style access

Key files:

- [`src/ui/theme/theme.cpp`](../../src/ui/theme/theme.cpp)
- [`src/ui/theme/theme.hpp`](../../src/ui/theme/theme.hpp)
- files currently calling `ui::theme()`

Recommended direction:

- identify the highest-churn UI layers still pulling theme globally
- push theme access through owned context where practical
- start with the most central rendering/UI paths rather than trying to touch all 154 call sites at once

Why it matters:

- cleaner per-window theming story
- easier future customization and testing
- less hidden global coupling

#### 5. Move IPC toward FlatBuffers-by-default writes

Key files:

- [`src/ipc/codec.cpp`](../../src/ipc/codec.cpp)
- [`src/ipc/codec_fb.cpp`](../../src/ipc/codec_fb.cpp)
- [`python/spectra/_codec.py`](../../python/spectra/_codec.py)
- [`python/spectra/_codec_fb.py`](../../python/spectra/_codec_fb.py)

Recommended direction:

- keep auto-detect on reads
- move new/default writes to FlatBuffers
- add clear instrumentation around which paths still use legacy TLV
- remove dead TLV only after the write path is stable

Why it matters:

- less protocol ambiguity
- better long-term schema evolution
- easier Python/C++ parity

### Performance and efficiency track

This phase should also add measurement, not just refactors.

Suggested work:

- add frame-time and redraw-cause instrumentation to runtime paths
- track startup timing for windowed and headless boot
- measure automation request latency by command type
- add visibility into serialization overhead for TLV vs FlatBuffers
- track large-data render/update cost in a benchmarkable way

Key candidate areas:

- [`src/ui/app/session_runtime.cpp`](../../src/ui/app/session_runtime.cpp)
- [`src/ui/app/window_runtime.cpp`](../../src/ui/app/window_runtime.cpp)
- [`src/ui/automation/automation_server.cpp`](../../src/ui/automation/automation_server.cpp)
- [`tests/bench/`](../../tests/bench/)

### User-facing and design track

This phase should still deliver visible improvements.

Suggested improvements:

- more consistent startup behavior across desktop/headless paths
- cleaner command discoverability and categorization
- less fragile automation tooling
- groundwork for per-window theme behavior and future UX polish

### Phase 1 success criteria

- One shared, documented startup assembly path exists for most window/headless cases.
- `register_commands.cpp` is descriptor-driven instead of one giant hand-wired function.
- `automation_server.cpp` uses a handler registry instead of a long branch chain.
- FlatBuffers is the default write path for newly touched IPC flows.
- A simple performance dashboard or benchmark set exists for startup, redraw, and automation latency.

---

## Phase 2: Make Real User Workflows Faster and More Powerful

### Goal

Turn the new architecture seams into smoother day-to-day workflows for actual users.

This phase is where Spectra should feel better, not just look cleaner internally.

### Why this should come second

Phase 1 reduces friction inside the codebase.
Phase 2 should cash that in by making the product more efficient for analysis work, plugin usage, and large datasets.

### Main initiatives

#### 1. Upgrade the large-data experience from infrastructure to workflow

The repo already has:

- chunked series
- mapped files
- LoD caching
- adapter integration

Key files:

- [`src/core/chunked_series.cpp`](../../src/core/chunked_series.cpp)
- [`src/data/chunked_array.hpp`](../../src/data/chunked_array.hpp)
- [`src/data/lod_cache.hpp`](../../src/data/lod_cache.hpp)
- [`src/render/render_2d.cpp`](../../src/render/render_2d.cpp)
- [`src/render/render_upload.cpp`](../../src/render/render_upload.cpp)

Recommended next step:

- add progressive loading states in the UI
- expose large-data status in the inspector or overlays
- support smarter viewport-aware prefetching
- make LoD and chunk behavior visible enough to debug
- improve user control over quality vs speed tradeoffs

Why it matters:

Right now the large-data system is architecturally important, but the user-facing story is still thin.
This phase should make large datasets feel like a first-class workflow, not just an internal capability.

#### 2. Make plugins easier and safer to use as a platform

The plugin API is much stronger now, and plugin guard has been added.

Key files:

- [`src/ui/workspace/plugin_api.cpp`](../../src/ui/workspace/plugin_api.cpp)
- [`src/ui/workspace/plugin_guard.cpp`](../../src/ui/workspace/plugin_guard.cpp)
- [`docs/plugin_developer_guide.md`](../plugin_developer_guide.md)
- [`examples/plugins/`](../../examples/plugins/)

Recommended direction:

- improve plugin lifecycle diagnostics
- add clearer version/capability negotiation reporting
- improve example plugin coverage and documentation
- expose plugin failures in a cleaner user-facing way
- consider a lightweight plugin manifest format if one does not already exist

Why it matters:

Spectra has the beginnings of a real extension platform.
This phase should make that platform more approachable for developers and less scary for users.

#### 3. Improve workspace and session workflows

Suggested direction:

- improve session restore reliability
- better surface background/runtime/session state in the UI
- add faster recovery for long-running plotting sessions
- make automation + workspace capture more predictable

Related files:

- [`src/ui/app/session_runtime.cpp`](../../src/ui/app/session_runtime.cpp)
- [`src/ui/app/window_runtime.cpp`](../../src/ui/app/window_runtime.cpp)
- [`src/ui/workspace/`](../../src/ui/workspace/)

Why it matters:

For real users, productivity is not just rendering speed.
It is also:

- how fast they can resume work
- how safely they can automate or recover a session
- how understandable the system feels when many things are happening

#### 4. Expand accessibility from feature set to polished workflow

The accessibility work has started well.

Key files:

- [`src/ui/accessibility/sonification.cpp`](../../src/ui/accessibility/sonification.cpp)
- [`src/ui/data/html_table_export.cpp`](../../src/ui/data/html_table_export.cpp)
- [`src/ui/commands/shortcut_manager.cpp`](../../src/ui/commands/shortcut_manager.cpp)

Recommended direction:

- improve keyboard focus maps and command discoverability
- add accessible summaries for figures/axes/series state
- improve export paths for non-visual workflows
- test accessibility flows as complete scenarios, not only unit behavior

Why it matters:

Accessibility should become a workflow quality story, not only a set of isolated capabilities.

### Performance and efficiency track

This phase should target workflow speed under real usage.

Suggested work:

- background prefetch and smarter cache eviction for large data
- more aggressive redraw invalidation control
- reduce unnecessary GPU uploads when view state changes but data does not
- benchmark plugin overhead and automation overhead under typical sessions
- add memory pressure metrics for chunked and cached data paths

### User-facing and design track

This phase should make Spectra feel more polished.

Suggested improvements:

- clearer status surfaces for loading, background work, and plugin state
- better inspector presentation for large-data and transformed-data contexts
- more legible command and accessibility flows
- more confidence-building session/workspace behavior

### Phase 2 success criteria

- Large datasets feel visibly better to use, not just technically supported.
- Plugin failures and plugin capabilities are easier to understand.
- Workspace/session recovery becomes more trustworthy.
- Accessibility workflows are broader and more polished.
- Performance metrics show better redraw efficiency and better large-data behavior.

---

## Phase 3: Scale Into Differentiated Capabilities

### Goal

Use the stronger platform from Phases 1 and 2 to push Spectra into areas that make it stand out.

This is where Spectra should stop being "a plotting app with many features" and start feeling like a distinct system.

### Why this should come third

Differentiators are most valuable after the foundation is stable.
Otherwise they become expensive demos instead of durable capabilities.

### Main initiatives

#### 1. Choose a real WebGPU strategy

Today the honest message is still:

- WebGPU exists
- it is real
- it is useful as an experimental/2D path
- it is not yet parity with Vulkan

Key files:

- [`src/render/webgpu/wgpu_backend.cpp`](../../src/render/webgpu/wgpu_backend.cpp)
- [`src/gpu/shaders/wgsl/`](../../src/gpu/shaders/wgsl/)
- [`examples/webgpu_demo.cpp`](../../examples/webgpu_demo.cpp)

Phase 3 should decide between two concrete paths:

- `Path A: Strong 2D parity first`
  Make WebGPU excellent for 2D plotting in browser/wasm settings before expanding scope.

- `Path B: Selected 3D expansion`
  Add only the highest-value 3D pipeline pieces rather than promising full parity.

My recommendation:

- do not chase full parity immediately
- first make WebGPU a great story for targeted 2D/browser/demo/embedded workflows
- then expand only where usage proves it is worth it

Why it matters:

WebGPU can become one of Spectra's clearest differentiators, but only if it is framed and scoped honestly.

#### 2. Build stronger remote and multi-process workflows

Spectra already has daemon, agent, IPC, automation, and Python layers.
Phase 3 should combine them into stronger workflows instead of treating them as separate technical pieces.

Key areas:

- [`src/daemon/`](../../src/daemon/)
- [`src/agent/`](../../src/agent/)
- [`src/ipc/`](../../src/ipc/)
- [`src/ui/automation/`](../../src/ui/automation/)
- [`python/spectra/`](../../python/spectra/)

Suggested direction:

- make remote plotting and automation sessions more robust
- improve multi-process observability and diagnostics
- support richer state snapshots and recovery
- create clearer remote/headless/embedded usage stories

Why it matters:

This is one of the areas where Spectra can become more than just a local GUI app.

#### 3. Mature the plugin ecosystem into a supported product surface

By Phase 3, the goal should not be just "plugins work."
The goal should be:

- developers can build plugins confidently
- users can install/use them safely
- Spectra can evolve the plugin model without chaos

Suggested direction:

- stronger plugin packaging and metadata
- capability declarations
- richer example gallery
- clearer compatibility guarantees
- safer plugin execution/reporting

Why it matters:

A strong plugin ecosystem multiplies product scope without putting every feature into core.

#### 4. Push rendering efficiency for large and complex sessions

Suggested direction:

- smarter upload batching
- better render invalidation policies
- lower cost overlays
- asynchronous preparation of expensive derived draw data
- tighter memory and cache budgeting for multi-window or long-running sessions

Key areas:

- [`src/render/render_geometry.cpp`](../../src/render/render_geometry.cpp)
- [`src/render/render_upload.cpp`](../../src/render/render_upload.cpp)
- [`src/render/renderer.cpp`](../../src/render/renderer.cpp)
- [`src/ui/window/window_runtime.cpp`](../../src/ui/window/window_runtime.cpp)

Why it matters:

This is the phase where Spectra should start feeling obviously efficient under pressure, not just architecturally clean.

### Performance and efficiency track

This phase should target scale:

- sustained performance in long-running sessions
- multi-window efficiency
- better memory behavior under large datasets
- practical browser/wasm efficiency for WebGPU
- lower automation and IPC overhead at higher volume

### User-facing and design track

This phase should create differentiation that users can actually feel:

- smoother large-session behavior
- better remote/headless workflows
- credible browser/WebGPU demos
- better plugin ecosystem usability
- more polished multi-window and multi-runtime behavior

### Phase 3 success criteria

- WebGPU has a clear, honest, and useful product story.
- Remote/headless/multiprocess flows are visibly more mature.
- Plugin usage feels more like a supported system than an advanced hack path.
- Large sessions remain responsive for longer with less manual babysitting.

---

## Suggested Feature Ideas by Theme

These are not all Phase 1 items.
They are a curated list of strong candidates across the next three phases.

### Performance

- frame-time and redraw-cause timeline in debug builds
- GPU upload budget visualization
- cache hit/miss metrics for LoD and chunked data
- background prefetch for large visible ranges
- asynchronous preparation for expensive overlays and derived geometry
- session-level performance summaries for automation or QA runs

### Design and UX

- clearer status surfaces for loading, caching, plugin failures, and automation activity
- per-window theme behavior and more explicit theme ownership
- better command organization and discoverability
- accessibility-first summaries for figures and data
- more transparent session restore and recovery feedback

### Efficiency and maintainability

- descriptor-driven command and automation registration
- stricter ownership of `WindowUIContext` lifecycle
- continued ViewModel migration where it reduces state ambiguity
- phased retirement of legacy TLV writes
- plugin capability reporting and better failure containment

### Product differentiation

- strong browser demo story via WebGPU 2D
- robust remote plotting and automation story
- plugin ecosystem growth with safer defaults
- first-class huge-dataset workflows

---

## Priority Matrix

### Must do soon

- finish `WindowUIContext` assembly/lifecycle cleanup
- table-drive command registration
- table-drive automation dispatch
- move IPC toward FlatBuffers-by-default writes
- add practical performance instrumentation

### Should do next

- contextualize theme access in the highest-value paths
- improve large-data UX and cache/prefetch behavior
- improve plugin diagnostics and lifecycle reporting
- improve session/workspace reliability and recovery
- broaden accessibility workflows

### Later, after foundation is calmer

- broader WebGPU expansion
- richer remote/multiprocess product workflows
- stronger plugin packaging/distribution story
- more ambitious browser and embedded runtime positioning

---

## Suggested First Wins

If you want a practical starting sequence, this is the order I would use:

1. Finish shared startup assembly around `WindowUIContext`.
2. Replace the hardcoded command registration style with descriptors/helpers.
3. Replace the hardcoded automation dispatch style with a handler registry.
4. Add baseline instrumentation for startup, redraw, automation latency, and serialization path usage.
5. Move a narrow, well-defined set of write paths to FlatBuffers-by-default.
6. Use the calmer architecture to improve one visible workflow:
   large-data session handling, plugin diagnostics, or workspace recovery.

This order gives you both technical payoff and visible user value without trying to do everything at once.

---

## Final Recommendation

The next three phases for Spectra should **not** be:

- "just add more features"
- "just split more files"
- "just tune performance in isolation"

They should be:

1. **Phase 1:** finish the architecture seams that still create drag
2. **Phase 2:** turn that cleanup into faster and more polished real workflows
3. **Phase 3:** scale the strongest bets into real differentiators

That roadmap gives you the best balance of:

- performance
- design quality
- engineering efficiency
- future product leverage

It also matches what the codebase is actually ready for right now.

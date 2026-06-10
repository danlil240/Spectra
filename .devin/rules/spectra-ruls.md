---
trigger: always_on
---

# Spectra Agent Rules

> Spectra is a performance-critical Vulkan application.
> Agents must prioritize: **correctness**, **deterministic behavior**, **no GPU hangs**, **no hidden blocking**, and **no architectural rewrites without explicit approval**.

---

## 1. Required Behavior Before Coding

### 1.1 Always Define Intent

Before writing code, the agent must state:

- **Intent** (1 sentence)
- **Scope** (files/modules affected)
- **Non-goals**
- **Acceptance criteria**
- **Risk assessment**

No implementation begins without this header.

### 1.2 Never Guess — Measure First

For any performance, animation, resize, or stutter issue, the agent must:

- Add instrumentation
- Capture timing
- Log fence waits
- Log swapchain recreations
- Identify blocking calls
- Provide before metrics

**No speculative fixes allowed.**

---

## 2. Vulkan Safety Rules (Non-Negotiable)

Agents must **never**:

- Destroy swapchain resources without waiting on fences
- Call `vkDeviceWaitIdle()` inside the frame loop
- Recreate pipelines per frame
- Allocate descriptor pools per frame
- Perform blocking readbacks in the animation path
- Submit command buffers referencing destroyed framebuffers
- Ignore `VK_ERROR_OUT_OF_DATE_KHR`

**Safe resize sequence** (must always follow this order):

1. Wait fences
2. Destroy swapchain-dependent resources
3. Recreate swapchain
4. Recreate framebuffers
5. Update projection
6. Re-record command buffers

---

## 3. No Global State Expansion

Agents must not:

- Add new global singletons
- Introduce static hidden ownership
- Create cross-module implicit coupling

All ownership must be explicit: `FigureId`, `WindowId`, `SessionId`, `ProcessId`.

---

## 4. Architectural Guardrails

### 4.1 No Primary Window Assumptions

There is no "main window", "primary process", or "first window special case". All windows must behave identically.

### 4.2 Window Independence

Closing any window must not:

- Terminate other windows
- Terminate backend (unless shutdown rule triggered)

### 4.3 Multiprocess Discipline

- All cross-window operations must flow through backend IPC
- Never mutate shared state locally
- No direct figure moves across windows without backend command

---

## 5. Animation & Frame Loop Rules

Agents must ensure:

- Fixed timestep logic does not drift
- Frame scheduler does not block
- No per-frame heap allocations in hot paths
- No hidden synchronization inside animation
- Frame time logging exists in debug mode

If frame time > 2× target, log:

- Stage breakdown
- Fence wait duration
- Acquire/present result

---

## 6. UI / ImGui Rules

**During tab drag:**
- Render only tab + figure
- Hide menus/toolbars for dragged figure
- No chrome during drag

**Dock inside window:** Must preserve existing behavior.
**Drop outside:** Must create identical window — no broken docking state.

- ImGui context per window must be explicit
- No accidental shared context corruption

---

## 7. IPC Rules

Protocol must:

- Include versioning
- Include `request_id` correlation
- Enforce ordering via `seq`
- Support snapshot + diff
- Support reconnect/resync

**No silent protocol changes.**

---

## 8. Testing Requirements

Any structural change must include:

- Manual verification steps
- Resize torture test
- Multi-window close test
- Animation smoothness check

If rendering changes: **must not introduce validation layer errors**.

---

## 9. Session Summary (Required)

After every session, the agent must provide:

- What changed
- Why
- Files modified
- How to verify (exact steps)
- Expected result
- Known risks

---

## 10. Prohibited Actions

Agents must **never**:

- Rewrite large architecture blocks in one step
- Mix refactor + feature in one PR
- Change IPC protocol without version bump
- Introduce sleep-based frame pacing hacks
- Hide stalls instead of fixing root cause
- Assume Linux compositor behavior

---

## 11. Definition of Done

A feature is done only when:

- Acceptance criteria met
- No validation errors
- No frame hitches
- No resize regressions
- No memory leaks
- No deadlocks
- Works in multi-window scenario

---

## 12. Development Mode Strategy

Spectra must support both **inproc** and **multiprocess** modes. Feature flag required until parity is achieved.

---

## 13. Escalation Rule

If the agent is stuck, it must:

1. Identify the blocking assumption
2. Propose two solution paths
3. Choose the safest minimal-change option
4. Continue

**No endless retry loops.**

---

## 14. Advanced Mode (Optional)

Agent may add (behind debug flag only):

- Frame profiler overlay
- Fence wait histogram
- Swapchain recreate counter
- IPC latency log
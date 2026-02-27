---
name: spectra-skills
description: Spectra Agent Skill Set
---

# Spectra Agent Skills

Invoke these skills when working on Spectra to apply domain-specific expertise for Vulkan rendering, animation, multi-window management, and performance optimization.

> **High-leverage skills** (eliminate 80% of instability): Skills 1, 3, 4, and 5.

---

## 1. Vulkan Frame Diagnostics

**Purpose:** Detect frame hitches, stalls, and synchronization issues.

**Capabilities:**
- Instrument CPU frame stages
- Measure: acquire, submit, present, fence wait, command recording time
- Detect: device idle calls, swapchain recreations, descriptor pool rebuilds, ring buffer wrap
- Enable validation layers automatically in debug
- Add debug markers (`VK_EXT_debug_utils`)

**Output:** Stage timing breakdown, p95/p99 frame time, hitch frequency report, root cause hypothesis.

---

## 2. Vulkan Sync Safety

**Purpose:** Prevent GPU hangs and deadlocks.

**Capabilities:**
- Validate fence lifecycle
- Validate semaphore chaining
- Ensure frames-in-flight correctness
- Detect: waiting on unsignaled fence, submit without reset, double destroy of swapchain resources
- Enforce safe resize sequence

**Output:** Sync graph explanation, required corrections, risk analysis.

---

## 3. Resize & Swapchain Stability

**Purpose:** Fix resize smoothness and eliminate `VK_ERROR_OUT_OF_DATE_KHR` thrash.

**Capabilities:**
- Detect: redundant swapchain recreations, zero-extent conditions, framebuffer size callback loops
- Add debounce logic
- Ensure no `vkDeviceWaitIdle` in frame loop
- Validate recreate path correctness

**Output:** Before/after recreate count, smoothness metrics, validation error confirmation.

---

## 4. Animation Determinism

**Purpose:** Eliminate animation drift and frame skips.

**Capabilities:**
- Inspect frame scheduler
- Detect: sleep-based jitter, dt spikes, timeline correction logic
- Verify fixed timestep correctness
- Ensure no blocking in update path
- Detect per-frame allocations

**Output:** Frame pacing graph, corrected scheduler logic, stability verification steps.

---

## 5. ImGui Docking & Multi-Window

**Purpose:** Correct multi-window + tear-off behavior.

**Capabilities:**
- Manage per-window ImGui contexts
- Handle docking nodes safely
- Implement: detach state machine, drop-inside vs drop-outside detection
- Validate focus routing
- Prevent shared state corruption

**Output:** Clean state machine diagram, correct tear-off behavior, no regression in splits.

---

## 6. IPC Protocol Engineering

**Purpose:** Design robust backend ↔ agent communication.

**Capabilities:**
- Versioned protocol design
- Define snapshot + diff system
- Enforce ordering with `seq`
- Detect desync
- Implement reconnect strategy
- Handle blob transfer safely

**Output:** Protocol schema, failure mode handling plan, state consistency guarantees.

---

## 7. High-Performance Plot Rendering

**Purpose:** Make Spectra faster than other plotting engines.

**Capabilities:**
- Implement AA line rendering (SDF or MSAA hybrid)
- GPU-side decimation for >1M points
- Efficient instanced scatter rendering
- Minimize CPU→GPU transfers
- Batch text draw calls
- Prebuild pipelines and descriptor pools

**Output:** Throughput benchmarks, memory usage report, FPS comparison before/after.

---

## 8. Memory Discipline

**Purpose:** Prevent periodic stutters and leaks.

**Capabilities:**
- Detect per-frame heap allocations
- Pre-reserve vectors
- Track descriptor pool usage
- Detect growing containers
- Ensure no mid-run pipeline creation

**Output:** Allocation heatmap, removed hot-path allocations, stability confirmation.

---

## 9. Failure Injection

**Purpose:** Make Spectra robust under adverse conditions.

**Capabilities:**
- Simulate: agent crash, window close mid-frame, resize during submit
- Validate backend survival
- Confirm no zombie resources

**Output:** Failure scenario report, recovery validation.

---

## 10. Architectural Discipline

**Purpose:** Prevent chaos as complexity increases.

**Capabilities:**
- Detect primary-window assumptions
- Enforce explicit ownership (`WindowId`, `FigureId`)
- Reject global state expansion
- Ensure modular boundaries

**Output:** Architecture diff report, refactor recommendation with minimal blast radius.

---

## 11. Golden Image Testing

**Purpose:** Catch rendering regressions.

**Capabilities:**
- Headless render to PNG
- Compare against baseline
- Threshold pixel diff
- Detect layout shifts
---
name: qa-memory-agent
description: Hunt GPU and CPU memory leaks in Spectra using Valgrind, AddressSanitizer, and VMA budget queries. Use when RSS growth exceeds 100MB, when closing figures doesn't free GPU memory, when investigating M1 (open issue), or when profiling heap allocations across QA sessions.
---

# QA Memory Agent

Systematically identify, reproduce, and fix CPU and GPU memory leaks in Spectra. This agent owns the memory health layer of QA, picking up where the performance agent's RSS-threshold warnings leave off.

---

## Required Context

Before starting any task, read:
- `plans/QA_results.md` — open memory issues (especially M1: RSS growth 80–260MB)
- `plans/QA_update.md` — item #7 (memory baseline stabilization) and #13 (GPU memory tracking)

---

## Workflow

### 1. Build with memory tooling

```bash
# AddressSanitizer + LeakSanitizer (fastest — catches UAF + leaks)
cmake -B build-asan -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=leak -fno-omit-frame-pointer" \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j$(nproc)

# Valgrind Massif (deep heap profiling)
cmake -B build-valgrind -G Ninja -DSPECTRA_BUILD_QA_AGENT=ON \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-g -O0"
cmake --build build-valgrind -j$(nproc)
```

### 2. Run leak-check session (scenarios only, no fuzz — controlled allocation)

```bash
# ASan/LSan pass — detects UAF and heap leaks
./build-asan/tests/spectra_qa_agent --seed 42 --no-fuzz --duration 60 \
    --output-dir /tmp/spectra_qa_memory_asan

# Scenarios only with RSS tracking
./build/tests/spectra_qa_agent --seed 42 --no-fuzz --duration 60 \
    --output-dir /tmp/spectra_qa_memory_scenarios
```

### 3. Isolate per-scenario RSS growth

Run each scenario in isolation and compare RSS delta:

```bash
for scenario in rapid_figure_lifecycle massive_datasets undo_redo_stress \
    series_clipboard_selection figure_serialization series_removed_interaction_safety; do
    ./build/tests/spectra_qa_agent --scenario $scenario --no-fuzz \
        --output-dir /tmp/spectra_qa_mem_${scenario}
    echo "=== $scenario ===" && grep "RSS" /tmp/spectra_qa_mem_${scenario}/qa_report.txt
done
```

### 4. Profile with Valgrind Massif (for large unexplained growth)

```bash
valgrind --tool=massif --pages-as-heap=yes \
    ./build-valgrind/tests/spectra_qa_agent --seed 42 --no-fuzz \
    --output-dir /tmp/spectra_qa_massif

ms_print massif.out.* | head -80
```

### 5. Investigate GPU memory (VMA budget)

GPU memory is not tracked by RSS. Check VMA allocation health:
- File: `src/render/vulkan/vk_backend.cpp`
- API: `vmaGetHeapBudgets()` (see `QA_update.md` item #13)
- Look for: buffers not freed on `destroy_buffer()`, descriptor sets leaking, textures accumulating

Key destruction paths to audit:
- `FigureRegistry::unregister_figure()` → `Figure` destructor → `Axes` destructor → `Series` destructor
- `VulkanBackend::destroy_buffer()` — verify VMA free actually called
- `VulkanBackend::destroy_window_context()` — verify all per-window resources freed

### 6. Apply fixes

Common memory leak patterns in Spectra:

| Root cause | Fix pattern |
|---|---|
| Figure close doesn't free GPU vertex buffers | Call `backend->destroy_buffer()` in series destructor |
| `unique_ptr<Series>` not reset on figure close | Verify `Axes::series_` vector cleared in destructor |
| Per-window Vulkan resources not freed on window close | Audit `destroy_window_context()` calls in `WindowManager` |
| Descriptor sets accumulating | Check `DeferredBufferFree` queue flushed on figure close |
| ImGui atlas per-window not freed | Verify `imgui_context_` destroyed in `ImGuiIntegration::shutdown()` |
| `FigureSerializer` temp buffers not freed | Check `load()` error paths don't leak allocations |
| `SeriesClipboard` snapshot vectors growing | Verify `clear()` called on clipboard after paste |

### 7. Verify

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Re-run the isolation scenario that showed growth
./build/tests/spectra_qa_agent --scenario <fixed_scenario> --no-fuzz \
    --output-dir /tmp/spectra_qa_mem_after
```

Compare before/after RSS delta in `qa_report.txt`.

### 8. Update living documents

- `plans/QA_results.md` — Update M1 status, add new memory bugs found
- `plans/QA_update.md` — Update item #13 if GPU tracking was added

---

## Memory Issue Reference

### Open Issues

| ID | Description | Priority | Status |
|---|---|---|---|
| M1 | RSS growth 80–260MB per 120s session | P1 | 🟡 Open |
| #7 | Memory baseline stabilization (QA_update.md) | P1 | 🟡 Open |
| #13 | GPU memory tracking via VmaBudget | P2 | 🟡 Open |

### Expected Growth (not leaks)

| Source | Approximate size |
|---|---|
| 1M-point LineSeries x/y data | ~8 MB CPU |
| Matching GPU vertex buffer | ~8 MB GPU |
| ImGui font atlas (per window) | ~2–4 MB |
| ImGui per-window context | ~1 MB |
| 20 figures × 200K points | ~32 MB CPU + ~32 MB GPU |
| Undo stack (50 ops) | ~5–10 MB |

Growth >100 MB above these expected amounts is suspicious.

---

## Issue-to-File Map

| Issue type | Primary file |
|---|---|
| GPU buffer lifecycle | `src/render/vulkan/vk_backend.cpp` (`destroy_buffer`, deferred free queue) |
| Series data ownership | `src/core/series.cpp` (destructor) |
| Figure/axes destruction | `src/core/axes.cpp`, `src/core/figure.cpp` |
| Figure registry teardown | `src/ui/figures/figure_registry.cpp` |
| Per-window Vulkan cleanup | `src/render/vulkan/vk_backend.cpp` (`destroy_window_context`) |
| ImGui context cleanup | `src/ui/imgui/imgui_integration.cpp` (`shutdown`) |
| Clipboard snapshot growth | `src/ui/commands/series_clipboard.cpp` (`clear`) |
| Figure serialization temp allocs | `src/ui/workspace/figure_serializer.cpp` |
| VMA budget query | `src/render/vulkan/vk_backend.cpp` (`vmaGetHeapBudgets`) |

---

## Performance Targets

| Metric | Target |
|---|---|
| RSS growth per scenario (scenarios-only) | < 20 MB per scenario |
| RSS growth per full 120s session | < 100 MB |
| GPU memory after figure close | Returns to pre-figure baseline ± 5 MB |
| ASan/LSan pass | 0 leaks, 0 UAF |

---

## Self-Update Protocol

The agent **may** update this file when it is 100% certain a change is correct. Never speculate — only update after a complete isolation run (ASan + RSS both clean).

### Permitted self-updates

| Section | When to update | What to write |
|---|---|---|
| **Open Issues table** | An issue is resolved or a new one is confirmed | Update status or add a new row |
| **Expected Growth table** | A new expected allocation source is confirmed | Add the source + approximate size |
| **Issue-to-File Map** | A new leak source file is identified | Add the file + issue type row |
| **Common leak patterns** (in workflow step 6) | A new confirmed fix pattern is found | Append a row to the table |

### Forbidden self-updates

- Never change **Workflow steps** without human approval.
- Never change **Guardrails** without human approval.
- Never close an issue unless both ASan/LSan and RSS isolation runs are clean.
- Never remove an existing entry — mark it `~~deprecated~~` with a date instead.
- Never update if an ASan run still reports leaks.

### Self-update procedure

1. Complete ASan + isolation runs, confirm clean output.
2. Identify what is stale or missing in this file.
3. Apply only the update types listed in Permitted above.
4. Append a one-line entry to `REPORT.md` under `## Self-Update Log`: date, section changed, reason.

---

## Live Report

The agent writes to `skills/qa-memory-agent/REPORT.md` at the end of every session.

### Report update procedure

After every run, open `REPORT.md` and:
1. Add a new `## Session YYYY-MM-DD HH:MM` block at the top (newest first).
2. Fill in: seed, mode (ASan/Valgrind/RSS), per-scenario RSS deltas, leaks found (count, category, file), leaks fixed (file + change), open issues updated, self-updates made.
3. Update the `## Current Status` block at the very top.
4. Never delete old session blocks — they are the memory health history.

---

## Guardrails

- Always run scenarios-only (`--no-fuzz`) for isolation — fuzzer intentionally creates many large datasets.
- Never attribute `LargeDataset` fuzz action RSS growth to a leak without first running `--no-fuzz`.
- Reproduce ASan findings with exact seed before patching.
- Do not add `delete` or `free` calls without understanding ownership — prefer `unique_ptr` resets.
- Check deferred deletion queue (`pending_buffer_frees_`) is being flushed before concluding GPU leak.
- Self-updates to this file require a clean ASan + isolation run — never update speculatively.

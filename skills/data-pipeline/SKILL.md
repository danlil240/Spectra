---
name: data-pipeline
description: Develop, extend, or debug the Spectra data pipeline — decimation, filtering, transforms, live streaming, CSV loading, and real-time data updates. Use when adding new data sources, optimizing large dataset handling, implementing new filters or transforms (log, FFT, normalization), fixing streaming latency, or extending the decimation engine. Covers the full data path from input through processing to GPU buffer upload.
---

# Data Pipeline Development

Add or modify data processing capabilities: decimation, filters, transforms, streaming updates, CSV import, and efficient GPU data upload.

---

## Required Context

Before starting any task, read:
- `CLAUDE.md` — architecture overview, data conventions
- `src/data/decimation.cpp` / `decimation.hpp` — level-of-detail decimation engine
- `src/data/filters.cpp` / `filters.hpp` — data filters (smoothing, rolling average)
- `src/math/data_transform.cpp` / `data_transform.hpp` — mathematical transforms (log, FFT, normalize)
- `src/core/series.cpp` — series data model (point storage, append, update)
- `src/ui/data/csv_loader.cpp` — CSV file import
- `src/ui/data/axis_link.cpp` — linked axis synchronization
- `src/ui/data/clipboard_export.cpp` — data export to clipboard
- `include/spectra/series.hpp` — public series API (`std::span<const float>`)

---

## Data Flow Architecture

```
Input sources:
  ├─ Static arrays (C++ std::span / Python list/ndarray)
  ├─ CSV file (csv_loader.cpp)
  ├─ Live stream (append_data / IPC REQ_APPEND_DATA)
  └─ ROS2 topics / PX4 ULog (adapter layer)
       │
       ▼
  Series data model (src/core/series.cpp)
  - Stores raw (x, y) or (x, y, z) point arrays
  - Supports append for streaming
  - Notifies renderer on data change
       │
       ▼
  Processing pipeline (optional, per-series):
  ├─ Decimation (src/data/decimation.cpp) — LOD for large datasets
  ├─ Filters (src/data/filters.cpp) — smoothing, moving average
  └─ Transforms (src/math/data_transform.cpp) — log, FFT, normalize
       │
       ▼
  GPU buffer upload (src/render/renderer.cpp)
  - Vertex buffer / SSBO upload
  - Only re-uploads changed data (dirty flag)
       │
       ▼
  Rendering (shaders consume the buffers)
```

---

## Workflow

### 1. Adding a new data transform

In `src/math/data_transform.cpp`:

```cpp
std::vector<float> apply_new_transform(std::span<const float> input, const TransformParams& params)
{
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output[i] = /* transform logic */;
    }
    return output;
}
```

**Rules:**
- Take `std::span<const float>` for input — zero-copy from series data.
- Return `std::vector<float>` — caller owns the result.
- Never modify the input data in place.
- Handle edge cases: empty input, NaN/Inf values, single-point series.

Add the corresponding header declaration in `data_transform.hpp` and expose through the UI (data transform dropdown in axes menu).

### 2. Adding a new data filter

In `src/data/filters.cpp`:

```cpp
std::vector<float> new_filter(std::span<const float> data, int window_size)
{
    // Implement filter algorithm
    // Common patterns: sliding window, IIR, weighted average
}
```

Follow existing patterns — filters take raw data and return processed data without modifying the original series.

### 3. Improving decimation

The decimation engine (`src/data/decimation.cpp`) provides level-of-detail for large datasets:

- **Purpose:** Reduce point count for rendering when zoomed out, show full detail when zoomed in
- **Algorithm:** Largest-triangle-three-buckets (LTTB) or similar perceptual decimation
- **Key invariant:** Decimation must preserve visual shape — peaks, valleys, and trends must remain visible

When modifying decimation:
- Preserve the API: `decimate(data, target_count)` → `std::vector<float>`
- Run `bench_decimation` to verify no performance regression
- Test with 1M+ point series for visual correctness

### 4. Adding live streaming support

For real-time data updates, the flow is:

**C++ (in-process mode):**
```cpp
series->append_data(new_x, new_y);  // Thread-safe via lock
// Renderer picks up changes on next frame via dirty flag
```

**Python (multi-process mode):**
```python
series.append(new_x, new_y)  # Sends REQ_APPEND_DATA via IPC
```

**Key concerns for streaming:**
- Use ring buffer semantics for bounded memory (configurable max points)
- Batch small updates to reduce IPC overhead (REQ_UPDATE_BATCH)
- GPU upload only on frame boundaries, not per-append
- Decimation applies after append for the visible range

### 5. Adding new data import formats

For new file format import, add to `src/ui/data/`:

```cpp
// src/ui/data/new_format_loader.cpp
std::vector<Series> load_new_format(const std::filesystem::path& path)
{
    // Parse file, return series data
    // Validate file exists and is readable
    // Handle malformed data gracefully (return empty, log error)
}
```

**Input validation rules:**
- Validate file path exists before opening
- Check file size against reasonable limits before loading entirely into memory
- Handle encoding issues (UTF-8 BOM, line endings)
- Never trust external file content — validate numeric data

### 6. Build and validate

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run data-specific tests
ctest --test-dir build -R "decimation|filter|transform|series_data|csv" --output-on-failure

# Run benchmarks for performance validation
./build/tests/bench_decimation

# Run all non-GPU tests
ctest --test-dir build -LE gpu --output-on-failure
```

---

## Issue-to-File Map

| Issue type | Primary file(s) |
|---|---|
| Decimation algorithm / LOD | `src/data/decimation.cpp`, `decimation.hpp` |
| Smoothing / rolling filters | `src/data/filters.cpp`, `filters.hpp` |
| Math transforms (log, FFT, normalize) | `src/math/data_transform.cpp`, `data_transform.hpp` |
| Series data storage / append | `src/core/series.cpp`, `include/spectra/series.hpp` |
| 3D series data | `src/core/series3d.cpp`, `include/spectra/series3d.hpp` |
| CSV import | `src/ui/data/csv_loader.cpp` |
| Clipboard export | `src/ui/data/clipboard_export.cpp` |
| Axis linking / synchronization | `src/ui/data/axis_link.cpp` |
| Series statistics (mean, std, etc.) | `src/core/series_stats.cpp`, `include/spectra/series_stats.hpp` |
| GPU buffer upload (data → GPU) | `src/render/renderer.cpp`, `src/render/vulkan/vk_buffer.cpp` |
| Streaming IPC (append_data) | `src/ipc/codec.cpp` (REQ_APPEND_DATA) |
| Data transform UI | `src/ui/data/data_transform.hpp` |
| Decimation performance | `tests/bench/bench_decimation.cpp` |
| Data unit tests | `tests/unit/test_decimation.cpp`, `test_filters.cpp`, `test_data_transform.cpp` |

---

## Performance Guidelines

| Dataset size | Expected behavior |
|---|---|
| < 10K points | No decimation, direct render |
| 10K – 100K points | Optional decimation, still interactive |
| 100K – 1M points | Decimation required for 60fps, LOD by zoom level |
| > 1M points | Aggressive decimation, streaming append, bounded ring buffer |

### Benchmarks to track

- `bench_decimation`: LTTB decimation throughput (points/sec)
- GPU upload time: buffer staging + transfer (measured in renderer)
- Streaming throughput: appends/sec at various batch sizes

---

## Common Pitfalls

1. **Modifying series data on the render thread** — Series data is owned by the app thread. The renderer reads it under a lock. Never mutate series data from the render thread.
2. **Unbounded append** — Streaming without a max-points cap causes OOM. Always enforce ring buffer bounds for live data.
3. **NaN propagation** — A single NaN in the data breaks min/max calculations, decimation, and rendering. Sanitize input data at the boundary.
4. **Redundant GPU uploads** — Only upload data when the dirty flag is set. Re-uploading unchanged data wastes bandwidth and causes frame drops.
5. **Decimation on every frame** — Decimate only when the view range or data changes, not every frame. Cache the decimated result.
6. **CSV parsing with locale** — Decimal separators differ by locale (`.` vs `,`). Use locale-independent parsing (`std::from_chars` or equivalent).

---

## Guardrails

- Never modify series data on the render thread — only the app thread mutates, renderer reads under lock.
- Never load unbounded external data into memory without size validation.
- Always handle NaN/Inf in data processing functions — either filter or propagate explicitly.
- Never decimate to fewer than 2 points — it breaks the rendering assumption of drawable data.
- Always run `bench_decimation` after modifying the decimation algorithm to catch regressions.
- Keep the zero-copy contract: public APIs take `std::span<const float>`, never copy unless processing requires it.
- Validate all external file input (CSV, etc.) before processing.

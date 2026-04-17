# LT-8: Data Virtualization / Out-of-Core Datasets

## Problem

Current `LineSeries` stores all data in contiguous `std::vector<float>` in RAM.
For datasets exceeding available memory (multi-GB flight logs, week-long ROS2 bag
recordings), this fails. A 1-billion-point dataset requires ~8 GB just for x/y
coordinates.

## Goal

Support datasets larger than RAM via chunked, demand-loaded data access, while
preserving full backward compatibility with existing `LineSeries`/`ScatterSeries`.

## Architecture

### New Components

```
src/data/
  chunked_array.hpp/cpp    — ChunkedArray<T>: fixed-size-chunk container
  mapped_file.hpp/cpp      — MappedFile: cross-platform mmap wrapper
  lod_cache.hpp/cpp        — LodCache: multi-level decimation pyramid

include/spectra/
  chunked_series.hpp       — ChunkedLineSeries public API

src/core/
  chunked_series.cpp       — ChunkedLineSeries implementation
```

### Component Details

#### 1. ChunkedArray (`src/data/chunked_array.hpp`)

A segmented container that stores data in fixed-size chunks instead of one
contiguous vector. Each chunk is a `std::vector<float>` of up to `CHUNK_SIZE`
elements (default 1M floats = 4 MB per chunk).

**Key properties:**
- Random access via `operator[]` with chunk index + offset calculation
- Range access via `read(offset, count, dest)` that copies across chunk boundaries
- `append()` for streaming data
- `size()`, `capacity()`, `chunk_count()`
- Memory accounting via `memory_bytes()`
- Support for external chunk sources (memory-mapped regions)

#### 2. MappedFile (`src/data/mapped_file.hpp`)

Cross-platform memory-mapped file wrapper for read-only access to large binary
data files.

**Key properties:**
- RAII: opens file in constructor, unmaps in destructor
- Move-only (no copy)
- `data()` returns `const void*` to mapped region
- `size()` returns file size in bytes
- `subspan(offset, length)` returns `std::span<const float>` for typed access
- Uses `mmap()` on POSIX, `CreateFileMapping()` on Windows

#### 3. LodCache (`src/data/lod_cache.hpp`)

Pre-computed decimation pyramid for multi-resolution rendering.

**Levels:**
- Level 0: full resolution (source data)
- Level 1: 1:4 decimation (min-max)
- Level 2: 1:16 decimation
- Level N: 1:(4^N) decimation

**Key properties:**
- `build(x_span, y_span)` — computes all levels from source data
- `query(x_min, x_max, max_points)` — returns the coarsest level that
  covers the visible range with at most `max_points` output points
- Uses existing `data::min_max_decimate()` for level construction
- Invalidation on data change via generation counter

#### 4. ChunkedLineSeries (`include/spectra/chunked_series.hpp`)

New Series subclass that uses ChunkedArray storage and optional LodCache.

**Key properties:**
- Extends `Series` base class (same as `LineSeries`)
- `set_data(ChunkedArray x, ChunkedArray y)` — move chunked arrays in
- `load_binary(path, x_offset, y_offset, count)` — load from binary file
- `load_mapped(MappedFile, x_col, y_col)` — zero-copy from mapped file
- `append(float x, float y)` — streaming append
- `enable_lod(bool)` — toggle level-of-detail cache
- `set_memory_budget(size_t bytes)` — rolling window for streaming
- `x_data_range(offset, count)` / `y_data_range(offset, count)` — chunked reads
- `visible_data(x_min, x_max, max_points)` — LoD-aware range query

**Renderer integration:**
- The upload path in `render_upload.cpp` detects `ChunkedLineSeries` and:
  1. Queries `visible_data()` for the current viewport range
  2. Uploads only the visible (decimated) subset to GPU
  3. Avoids uploading the entire dataset

## Integration Points

1. **Forward declarations**: Add `ChunkedLineSeries` to `include/spectra/fwd.hpp`
2. **Renderer**: Add `ChunkedLineSeries` branch in `upload_series_data()`
3. **Decimation**: Reuse `data::min_max_decimate()` for LoD pyramid construction
4. **CMake**: Files auto-discovered via `GLOB_RECURSE` — just add source files
5. **Tests**: Add to `SPECTRA_UNIT_TESTS` list in `tests/CMakeLists.txt`

## Implementation Order

1. `ChunkedArray` — standalone data structure, no dependencies
2. `MappedFile` — standalone I/O utility
3. `LodCache` — depends on decimation.hpp
4. `ChunkedLineSeries` — depends on all above
5. Renderer integration — depends on ChunkedLineSeries
6. Tests for each component

## Backward Compatibility

- `LineSeries` and `ScatterSeries` are **unchanged**
- `ChunkedLineSeries` is a new, opt-in class
- Existing user code continues to work without modification
- The renderer handles both old and new series types transparently

## Impact

- Enables flight log analysis (PX4 ULog files: 2-10 GB)
- Enables long-duration ROS2 bag replay
- Enables batch processing of scientific datasets
- Memory-mapped access lets the OS manage page eviction
- LoD cache enables smooth zooming on billion-point datasets

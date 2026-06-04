---
name: add-test
description: >-
  Adds Spectra unit tests, golden image tests, or Google Benchmark cases with
  correct CMake labels (gpu, golden). Use when adding test coverage, fixing
  regressions, or creating visual baselines in tests/.
---

# Add Tests

## Unit tests

1. `tests/unit/test_<feature>.cpp` — Google Test `TEST(Suite, Name)`
2. Register in `tests/CMakeLists.txt` (`add_spectra_test` or equivalent)
3. GPU needs Vulkan: `set_tests_properties(... PROPERTIES LABELS "gpu")`
4. Run: `ctest --test-dir build -R test_name --output-on-failure`

## Golden tests

1. Case in `tests/golden/golden_test*.cpp` — headless PNG
2. Baseline: `tests/golden/baseline/`
3. Run: `ctest --test-dir build -L golden -j1`

## Benchmarks

1. `tests/bench/bench_<feature>.cpp` with `BENCHMARK(...)`
2. Run: `./build/tests/bench_<feature>`

## UI smoke

Optional: [spectra-mcp](../spectra-mcp/SKILL.md) instead of a C++ harness for interaction.

## Conventions

- One behavior per `TEST`
- Descriptive names: `TEST(Layout, SubplotGridSpacing)`
- GPU tests excluded from sanitizer CI (`-LE gpu`)

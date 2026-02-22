# Add Tests

Add unit tests, golden tests, or benchmarks for Spectra features.

## Unit Tests

1. Create or edit a test file in `tests/unit/test_<feature>.cpp`
2. Use Google Test: `TEST(SuiteName, TestName) { ... }`
3. Add the test target in `tests/CMakeLists.txt` using `add_spectra_test(unit test_name)`
4. If the test needs Vulkan/GLFW, label it as GPU: `set_tests_properties(unit_test_name PROPERTIES LABELS "gpu")`
5. Run: `ctest --test-dir build -R test_name --output-on-failure`

## Golden Image Tests

1. Add a test case in `tests/golden/golden_test*.cpp`
2. Render headless to a PNG
3. Compare against baseline in `tests/golden/baseline/`
4. Run: `ctest --test-dir build -L golden -j1`

## Benchmarks

1. Create `tests/bench/bench_<feature>.cpp`
2. Use Google Benchmark: `BENCHMARK(BM_Function)`
3. Add build target in `tests/CMakeLists.txt`
4. Run: `./build/tests/bench_<feature>`

## Conventions

- Test one behavior per TEST case
- Use descriptive test names: `TEST(Layout, SubplotGridSpacing)`
- GPU tests are excluded from sanitizer CI runs
- Golden tests run single-threaded to avoid GPU resource contention

# Spectra — Task Completion Checklist

Run from repo root after code changes. Scope checks to what you touched.

## 1. Format

```bash
make check-format
# or format touched files: clang-format -i path/to/file.cpp
```

Fail → `make format` or manual `clang-format -i`.

## 2. Build

```bash
cmake --build build -j$(nproc)
```

If CMake/options changed, reconfigure per `mem:suggested_commands` or `BUILD_ENVIRONMENT.md`.

## 3. Tests (default for C++ changes)

```bash
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)
```

Add when relevant:
- **GPU/render path:** `ctest --test-dir build -L gpu --output-on-failure`
- **Visual/golden:** lavapipe + `xvfb-run` golden ctest (see `mem:suggested_commands`)
- **Single focused test:** `./build/tests/unit_<name>`

## 4. Sanitizer-sensitive changes

Match CI (`BUILD_ENVIRONMENT.md` §8): separate `build-asan` / `build-ubsan`, always `-LE gpu`.

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  ctest --test-dir build-asan -LE gpu --output-on-failure -j$(nproc)
```

Never run GPU-labeled tests under ASan/UBSan.

## 5. Python-only changes

```bash
cd python && pytest tests/ -v
```

## 6. Optional integration builds

Only if you edited that adapter: ROS2 (`source` humble + `SPECTRA_USE_ROS2=ON`), Qt, WebGPU — see `BUILD_ENVIRONMENT.md` §4–6.

## 7. Memory maintenance (if Serena memories changed)

```bash
serena memories check
```

## Skip unless asked

- Full benchmark suite (`SPECTRA_BUILD_BENCHMARKS`)
- `SPECTRA_BUILD_QA_AGENT` stress runs
- Release packaging (`make package`)
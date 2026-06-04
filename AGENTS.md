# AGENTS.md

Agent-oriented notes for working in this repository. See also [`CLAUDE.md`](CLAUDE.md) and [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md).

## Cursor Cloud specific instructions

### Compiler

Cloud VMs may default `c++` to Clang without a usable `libstdc++` link path. Configure with GCC explicitly:

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_BUILD_TYPE=Release -DSPECTRA_USE_ROS2=OFF \
  -DSPECTRA_BUILD_TESTS=ON -DSPECTRA_BUILD_EXAMPLES=ON -DSPECTRA_BUILD_GOLDEN_TESTS=ON
```

Canonical flags and package list: [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md).

### Runtime environment

- Set **`XDG_RUNTIME_DIR`** before running GUI or IPC binaries (e.g. `export XDG_RUNTIME_DIR=/tmp/runtime-$(id -u)` and `mkdir -p "$XDG_RUNTIME_DIR"`).
- **Headless Vulkan** (CI-style): point at lavapipe — `export VK_ICD_FILENAMES=$(find /usr/share/vulkan/icd.d -name '*lvp*' | head -1)` and optionally `export LIBGL_ALWAYS_SOFTWARE=1`.
- **Interactive window demos**: run under **Xvfb** (`Xvfb :99 -screen 0 1920x1080x24` + `export DISPLAY=:99`) or `xvfb-run -a ./build/examples/basic_line`.

### Verify (no code changes)

| Check | Command |
|-------|---------|
| Build | `cmake --build build -j$(nproc)` after configure above |
| Unit tests (fast) | `ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)` |
| Format | `make check-format` |
| Headless plot export | `cd build/examples && ./offscreen_export` → `output.png` (1920×1080) |
| GUI example | `DISPLAY=:99 ./build/examples/basic_line` (with Xvfb + lavapipe env) |
| Main app | `./build/spectra` (same display/Vulkan requirements as examples) |

### Python (optional)

From `python/`: `pip install -e ".[dev]"` and `pip install flatbuffers` if collection fails. Run `python3 -m pytest tests/ -q` (Qt-related tests skip/fail without PyQt6).

### Services

No docker-compose or database. Default agent verification is **in-process `ctest`**; GPU/golden tiers need Vulkan (+ Xvfb for golden). Multiproc live tests need `spectra-backend` / display only when exercising Python `show()` or multiproc IPC end-to-end.

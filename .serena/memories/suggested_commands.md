# Spectra — Suggested Commands

All from repo root unless noted. Prefer exact flags in `BUILD_ENVIRONMENT.md` over ad-hoc CMake.

## Canonical configure + build (core features, ROS2 off)

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPECTRA_USE_GLFW=ON \
  -DSPECTRA_USE_IMGUI=ON \
  -DSPECTRA_USE_FFMPEG=ON \
  -DSPECTRA_USE_EIGEN=ON \
  -DSPECTRA_USE_PX4=ON \
  -DSPECTRA_USE_ROS2=OFF \
  -DSPECTRA_BUILD_EXAMPLES=ON \
  -DSPECTRA_BUILD_TESTS=ON \
  -DSPECTRA_BUILD_GOLDEN_TESTS=ON

cmake --build build -j$(nproc)
```

Shorthand: `make build` (uses `BUILD_DIR=build`, enables tests+examples).

## Tests

```bash
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)   # default CI-style
ctest --test-dir build -L gpu --output-on-failure
./build/tests/unit_<name>                                        # single binary
```

Golden (lavapipe + xvfb):

```bash
export VK_ICD_FILENAMES=$(find /usr/share/vulkan/icd.d -name '*lvp*' | head -1)
export LIBGL_ALWAYS_SOFTWARE=1
xvfb-run -a ctest --test-dir build -L golden -j1 --output-on-failure
```

## Run app

```bash
./build/spectra
./build/examples/<demo_name>
```

## Format

```bash
make format              # ./format_project.sh
make check-format        # --check only
clang-format -i <file>   # single file
```

## Debug / sanitizers (separate build dirs)

```bash
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug ...
# ASan/UBSan: see BUILD_ENVIRONMENT.md §8 → build-asan / build-ubsan
```

## Python (optional)

```bash
cd python && pip install -e ".[dev]"
cd python && pytest tests/ -v
# or: make pip-dev && make pip-test
```

## ROS2 build (only when needed)

```bash
source /opt/ros/humble/setup.bash
cmake -B build -G Ninja -DSPECTRA_USE_ROS2=ON -DSPECTRA_ROS2_BAG=ON \
  -DSPECTRA_BUILD_TESTS=OFF -DSPECTRA_BUILD_EXAMPLES=OFF
```

## Git / search

Standard Linux `git`, `rg`, `find`, `nproc` — no project-specific wrappers.

## Memory hygiene

```bash
serena memories check   # from project root after memory edits
```
# Spectra — Build Environment Reference

> **Purpose:** This document is the authoritative reference for all agents (spectra-builder,
> spectra-coder, spectra-dev, QA_*, github-ci, etc.) on how to reproduce a complete, working
> Spectra build environment. Follow these instructions exactly — do **not** iterate or guess.
>
> **Verified on:** Ubuntu 24.04 LTS (Noble) · GCC 13.3 · CMake 3.31 · Ninja 1.13

---

## Quick Reference

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 LTS (recommended) |
| Compiler | GCC 13+ **or** Clang 17+ |
| C++ Standard | C++20 |
| Build System | CMake 3.21+ with Ninja (preferred) |
| Vulkan SDK | `libvulkan-dev` ≥ 1.2 (APT package, no LunarG SDK needed) |
| Shader compiler | `glslang-tools` (provides `glslangValidator`) |
| Key fetched deps | GLFW 3.4 (or system >= 3.3), ImGui v1.92.6-docking, FlatBuffers v25.2.10, GoogleTest v1.14.0 (see §11) |

---

## 1. System Package Installation

Run this **once** on a fresh Ubuntu 24.04 machine or container:

```bash
sudo apt-get update
sudo apt-get install -y \
    # Build toolchain
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    \
    # Vulkan (headers + loader + validation layers + Mesa Vulkan drivers)
    vulkan-tools \
    libvulkan-dev \
    vulkan-validationlayers \
    mesa-vulkan-drivers \
    \
    # Shader compiler (GLSL → SPIR-V)
    glslang-tools \
    \
    # Window system dev headers (GLFW builds against these)
    libwayland-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev \
    \
    # Python (icon generation + Python bindings)
    python3 \
    python3-pillow \
    \
    # Optional but recommended: Eigen3 math library
    libeigen3-dev \
    \
    # Code formatting (CI and `make check-format` use clang-format 18)
    clang-format-18
```

> **Note:** Dear ImGui, FlatBuffers and GoogleTest are downloaded by CMake's `FetchContent`
> on the first configure (pinned tags, fetched with `git`, so it works behind proxies that
> block GitHub archive downloads). GLFW uses a system `libglfw3-dev` (>= 3.3) when present
> and is fetched (3.4) otherwise. Python 3 + Pillow are optional (resized desktop icons).
> For distro/offline builds see [§11](#11-dependencies-system-vs-fetched-offline-builds).

> **Fonts (runtime, optional):** only Inter is embedded. Glyphs Inter lacks (most maths
> operators, CJK, ...) are loaded on demand from installed system fonts, in both the plot-text
> atlas and the ImGui UI: DejaVu Sans / Noto Sans Math / FreeSans for symbols and Noto Sans CJK /
> WenQuanYi / Droid Sans Fallback / IPA Gothic for CJK (e.g. `fonts-dejavu-core`,
> `fonts-noto-cjk`). `SPECTRA_FALLBACK_FONTS=/path/a.ttf:/path/b.otf` adds fonts ahead of
> those. Without any fallback font, missing glyphs render as a hollow box. See
> `src/render/font_fallback.hpp`.

---

## 2. Standard Build (All Core Features)

This is the canonical build used by all agents. It enables every core feature, examples, and
tests. ROS2 and WebGPU are excluded (they require extra setup — see sections 4 and 5).

```bash
# From the repository root
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

**Expected outputs:**

| Path | Description |
|------|-------------|
| `build/libspectra.a` | Full GPU library (Vulkan + UI + IPC + PX4) |
| `build/libspectra-core.a` | Headless core (no GPU, no ImGui) |
| `build/libspectra_px4_adapter.a` | PX4/ULog adapter |
| `build/spectra` | Main application executable |
| `build/spectra-backend` | Multi-process daemon |
| `build/spectra-window` | Multi-process window agent |
| `build/spectra-px4` | Standalone PX4 flight-log viewer |
| `build/examples/*` | ~47 runnable demo programs |
| `build/tests/*` | 110+ unit test binaries + golden image tests |

---

## 3. Running Tests

```bash
# All non-GPU tests (fast, no display needed)
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)

# GPU tests only (require a Vulkan device or lavapipe software renderer)
ctest --test-dir build -L gpu --output-on-failure

# Golden image tests (software Vulkan via lavapipe — need xvfb + mesa)
sudo apt-get install -y xvfb
export VK_ICD_FILENAMES=$(find /usr/share/vulkan/icd.d -name '*lvp*' | head -1)
export LIBGL_ALWAYS_SOFTWARE=1
xvfb-run -a ctest --test-dir build -L golden -j1 --output-on-failure

# Run a single test binary directly
./build/tests/unit_test_easy_api
./build/tests/unit_test_ipc
```

### Formatting

Spectra is formatted with **clang-format 18** (`.clang-format`; CI installs `clang-format-18`).
Other major versions format differently, so `format_project.sh` refuses them.

```bash
git clang-format-18 origin/main   # format only the lines your branch changed (what CI checks)
make check-format                 # check every git-tracked C/C++ file; exits 1 if any needs formatting
make format                       # format every git-tracked C/C++ file in place
./format_project.sh --check path/to/file.cpp   # check specific files
```

`format_project.sh` processes only git-tracked `*.c/*.cc/*.cpp/*.cxx/*.h/*.hh/*.hpp/*.hxx/*.inl/*.m/*.mm`
files and skips `third_party/`, `_deps/`, `build*/`, `src/gpu/shaders/` and golden output, so
untracked local files (QA probes, scratch code) are never touched. It looks for `clang-format-18`,
then Homebrew `llvm@18` (`brew install llvm@18`), then `clang-format`. Set
`CLANG_FORMAT=/path/to/clang-format` to choose the binary (Windows: the LLVM 18 `clang-format.exe`,
run from Git Bash or WSL), or `SPECTRA_FORMAT_ALLOW_ANY_VERSION=1` to only warn on another
version. Exit codes: `0` clean, `1` files need formatting / failed, `2` tool missing or wrong
version. The `format_gate` ctest (label `format`, CPU only) covers the script and skips without
clang-format 18.

---

## 4. ROS2 Adapter Build

The ROS2 adapter is tested on **Humble** (Ubuntu 22.04) and **Jazzy** (Ubuntu 24.04).
Use whichever distro matches your machine; CMake auto-detects rosbag2 API differences at
configure time (see `bag_message_compat.hpp`).

| Distro | Ubuntu | Docker image | Setup |
|--------|--------|--------------|-------|
| Humble | 22.04 Jammy | `ros:humble-ros-base` | `source /opt/ros/humble/setup.bash` |
| Jazzy | 24.04 Noble | `ros:jazzy-ros-base` | `source /opt/ros/jazzy/setup.bash` |

```bash
# Pick one distro (example: Jazzy on Noble)
export ROS_DISTRO=jazzy   # or humble
docker run -it --rm ros:${ROS_DISTRO}-ros-base bash

# Inside the container, install additional deps:
apt-get update && apt-get install -y \
    libvulkan-dev \
    libglfw3-dev \
    glslang-tools \
    nlohmann-json3-dev \
    libtinyxml2-dev \
    python3-pillow

source /opt/ros/${ROS_DISTRO}/setup.bash

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPECTRA_USE_ROS2=ON \
    -DSPECTRA_ROS2_BAG=ON \
    -DSPECTRA_BUILD_TESTS=OFF \
    -DSPECTRA_BUILD_EXAMPLES=OFF

cmake --build build -j$(nproc)
```

On configure, CMake prints which rosbag2 timestamp API was detected, e.g.
`timestamp API: time_stamp (Humble)` or `recv_timestamp/send_timestamp (Jazzy+)`.

**Bag file compatibility:** Bags recorded on Jazzy may not open on Humble (rosbag2
`metadata.yaml` format differs). Bags recorded on Humble generally play on Jazzy.
Spectra reads/writes bags through the rosbag2 version installed on the build host.

**Additional APT packages required for ROS2:**

| Package | Purpose |
|---------|---------|
| `nlohmann-json3-dev` | JSON parsing in `ros_session.cpp` |
| `libtinyxml2-dev` | URDF parsing |
| ROS packages via `rosdep` | `rclcpp`, `std_msgs`, `geometry_msgs`, `nav_msgs`, `sensor_msgs`, `diagnostic_msgs`, `tf2_msgs`, `visualization_msgs`, `rosbag2_cpp`, `rosbag2_storage` |

---

## 5. Qt Adapter Build

Qt is **OFF by default**. To enable Qt6 embedding:

```bash
sudo apt-get install -y qt6-base-dev

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPECTRA_USE_QT=ON \
    -DSPECTRA_BUILD_QT_EXAMPLE=ON

cmake --build build -j$(nproc)
```

---

## 6. WebGPU Backend (Experimental)

WebGPU is **OFF by default** and requires Google Dawn or Emscripten:

```bash
# Native build with Dawn (must be pre-built and installed)
cmake -B build -G Ninja \
    -DSPECTRA_USE_WEBGPU=ON \
    -Ddawn_DIR=<dawn_install>/lib/cmake/dawn

# Emscripten/WASM build (emsdk 4.0.10+; uses the emdawnwebgpu port)
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPECTRA_USE_WEBGPU=ON \
    -DSPECTRA_RUNTIME_MODE=inproc \
    -DSPECTRA_USE_ROS2=OFF -DSPECTRA_USE_PX4=OFF \
    -DSPECTRA_USE_FFMPEG=OFF -DSPECTRA_USE_EIGEN=OFF \
    -DSPECTRA_BUILD_TESTS=OFF
cmake --build build-wasm --target webgpu_demo
# Serve build-wasm/examples/ over HTTP and open webgpu_demo.html in a WebGPU browser
```

Notes for the wasm build:

- No Vulkan SDK is needed. CMake fetches Vulkan-Headers and links generated stubs
  (`cmake/generate_vulkan_stubs.py`) so App code that references the Vulkan backend
  links; only the WebGPU backend runs in the browser.
- FlatBuffers' `flatc` must run on the host. CMake uses `SPECTRA_HOST_FLATC`, a `flatc`
  on `PATH`, or builds one from the fetched sources with the host compiler.
- The backend blocks on adapter/device/readback callbacks, so it links with `-sASYNCIFY`.
- Headless Chromium can render WebGPU offscreen (`AppConfig::headless = true` +
  `save_png`) but cannot present to a canvas, so use a real browser for the windowed demo.

Supported series (the WebGPU backend is an experimental **2D subset**; the list
lives in `src/render/webgpu/wgpu_pipeline_support.hpp`):

| Works | Not yet ported (no WGSL pipeline) |
|-------|-----------------------------------|
| Line, scatter (incl. colormapped scatter and the default-dot rim), bar, histogram, box plot, violin, band, stem, 2D shapes; grid, overlay fills, 2D/3D text; `PipelineType::Heatmap` (same SSBO layout as the example heatmap plug-in) | Every 3D type (3D axes, line, scatter, surface, mesh, shapes); plug-in series that ship SPIR-V shaders |

When a figure uses an unsupported type, the Renderer skips that series, logs a
one-time warning per kind, and draws a notice at the bottom-left of the canvas
("Not supported by this render backend: ...").

---

## 7. Debug Build

```bash
cmake -B build-debug -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSPECTRA_BUILD_TESTS=ON \
    -DSPECTRA_BUILD_EXAMPLES=ON

cmake --build build-debug -j$(nproc)
```

---

## 8. Sanitizer Builds

```bash
# AddressSanitizer
cmake -B build-asan -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
    -DSPECTRA_BUILD_TESTS=ON \
    -DSPECTRA_BUILD_GOLDEN_TESTS=OFF \
    -DSPECTRA_BUILD_EXAMPLES=OFF

cmake --build build-asan -j$(nproc)

# Run with leak detection (suppress known third-party leaks from GLFW/libdecor/GTK)
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
LSAN_OPTIONS=suppressions=tools/sanitizers/lsan.supp \
    ctest --test-dir build-asan -LE gpu --output-on-failure -j$(nproc)

# UndefinedBehaviorSanitizer
cmake -B build-ubsan -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined" \
    -DSPECTRA_BUILD_TESTS=ON \
    -DSPECTRA_BUILD_GOLDEN_TESTS=OFF \
    -DSPECTRA_BUILD_EXAMPLES=OFF

cmake --build build-ubsan -j$(nproc)
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    ctest --test-dir build-ubsan -LE gpu --output-on-failure -j$(nproc)
```

---

## 9. Python Bindings

The only Python packaging definition is the root `pyproject.toml`
(scikit-build-core); the version comes from `version.txt`.

```bash
# Development install from the repo root, pure Python (skips the C++ build;
# the launcher uses ./build/spectra-backend or $SPECTRA_BACKEND_PATH)
pip install -e ".[dev]" -Cwheel.cmake=false

# Run Python tests (Qt / libspectra_embed tests skip when unavailable)
cd python && pytest tests/ -v
```

Platform wheel (compiles the C++ tree and bundles `spectra-backend` +
`spectra-window` in `spectra/_bin/`; only the `python_wheel` CMake install
component goes into the wheel):

```bash
CMAKE_BUILD_PARALLEL_LEVEL=$(nproc) pip wheel . -w dist --no-deps
```

Without bundled binaries, `spectra-backend --download` (or
`SPECTRA_AUTO_DOWNLOAD=1`) fetches the release matching the package version
and verifies it against the release `SHA256SUMS`; it never runs unverified
binaries.

---

## 10. Complete CMake Feature Flag Reference

| Flag | Default | Requires | Description |
|------|---------|----------|-------------|
| `SPECTRA_USE_GLFW` | ON | `libwayland-dev`, X11 headers | GLFW windowing (fetched automatically) |
| `SPECTRA_USE_SDL3` | OFF | — | SDL3 windowing (mutually exclusive with GLFW) |
| `SPECTRA_USE_IMGUI` | ON | GLFW or SDL3 | Dear ImGui UI (fetched automatically) |
| `SPECTRA_USE_FFMPEG` | ON | `ffmpeg` in PATH at runtime | Video export via ffmpeg pipe |
| `SPECTRA_USE_EIGEN` | ON | `libeigen3-dev` | Eigen3 vector adapters |
| `SPECTRA_USE_QT` | OFF | `qt6-base-dev` | Qt6 adapter for embedding |
| `SPECTRA_USE_ROS2` | ON* | Sourced ROS2 workspace | ROS2 adapter (auto-disables if not found) |
| `SPECTRA_ROS2_BAG` | ON* | `rosbag2_cpp`, `rosbag2_storage` | rosbag2 playback/recording |
| `SPECTRA_USE_PX4` | ON | None (POSIX only) | PX4 ULog adapter + MAVLink |
| `SPECTRA_USE_WEBGPU` | OFF | Dawn or Emscripten | Experimental WebGPU backend |
| `SPECTRA_BUILD_EXAMPLES` | ON | — | ~47 runnable example programs |
| `SPECTRA_BUILD_QT_EXAMPLE` | OFF | `SPECTRA_USE_QT=ON` | Qt6 embed example |
| `SPECTRA_BUILD_TESTS` | ON | — | 110+ unit test binaries |
| `SPECTRA_BUILD_BENCHMARKS` | OFF | — | Google Benchmark suite |
| `SPECTRA_BUILD_GOLDEN_TESTS` | ON | `python3-pillow` | Visual regression tests |
| `SPECTRA_BUILD_EMBED_SHARED` | OFF | — | `libspectra_embed.so` for C FFI |
| `SPECTRA_BUILD_QA_AGENT` | OFF | — | QA stress-testing automation agent |
| `SPECTRA_PYTHON_WHEEL` | OFF | — | Bundle backend into Python package |
| `SPECTRA_BUILD_APPS` | ON | — | Build `spectra`, `spectra-backend`, `spectra-window`, `spectra-px4` |
| `SPECTRA_INSTALL` | ON | — | Generate install rules + the `find_package(spectra)` package |
| `SPECTRA_LINK_COMPILE_COMMANDS` | OFF | — | Symlink `build/compile_commands.json` into the source tree (clangd/ccls) |
| `SPECTRA_GENERATE_ICONS` | `AUTO` | `python3-pillow` | Resized hicolor icons; without Pillow the 1024px icon is installed to `share/pixmaps` |
| `SPECTRA_USE_SYSTEM_GLFW` | `AUTO` | `libglfw3-dev` >= 3.3 | `AUTO`/`ON`/`OFF`, see §11 |
| `SPECTRA_USE_SYSTEM_SDL3` | `AUTO` | SDL3 >= 3.2 | `AUTO`/`ON`/`OFF` |
| `SPECTRA_USE_SYSTEM_FLATBUFFERS` | OFF | `libflatbuffers-dev` + `flatbuffers-compiler` | `AUTO`/`ON`/`OFF` |
| `SPECTRA_USE_SYSTEM_GTEST` | OFF | `libgtest-dev` >= 1.11 | `AUTO`/`ON`/`OFF` |
| `SPECTRA_USE_SYSTEM_BENCHMARK` | OFF | `libbenchmark-dev` | `AUTO`/`ON`/`OFF` |
| `SPECTRA_RUNTIME_MODE` | `multiproc` | — | `inproc` or `multiproc` |
| `SPECTRA_PRESENT_MODE` | `FIFO` | — | Vulkan present mode: `FIFO`, `MAILBOX`, `IMMEDIATE` |

> *`SPECTRA_USE_ROS2` and `SPECTRA_ROS2_BAG` default to ON in CMakeLists.txt but
> **auto-disable** gracefully when `ament_cmake` is not found. They do **not** fail the build.
>
> When Spectra is a **subproject** (`add_subdirectory()` / `FetchContent`),
> `SPECTRA_BUILD_EXAMPLES`, `SPECTRA_BUILD_TESTS`, `SPECTRA_USE_ROS2` and `SPECTRA_USE_PX4`
> default to OFF (they follow `PROJECT_IS_TOP_LEVEL`), CPack is not included, and nothing is
> written into the source tree.

---

## 11. Dependencies: System vs. Fetched, Offline Builds

Every fetched dependency is pinned to a release tag (the commit is noted next to it in CMake):

| Library | Pinned tag | Fetched when | System switch (default) |
|---------|-----------|--------------|-------------------------|
| GLFW | `3.4` | no system GLFW >= 3.3 found | `SPECTRA_USE_SYSTEM_GLFW` (`AUTO`) |
| Dear ImGui | `v1.92.6-docking` | always (no usable system package) | none; use `FETCHCONTENT_SOURCE_DIR_IMGUI` |
| FlatBuffers | `v25.2.10` | by default | `SPECTRA_USE_SYSTEM_FLATBUFFERS` (`OFF`) |
| SDL3 | `release-3.2.0` | `SPECTRA_USE_SDL3=ON` and no system SDL3 >= 3.2 | `SPECTRA_USE_SYSTEM_SDL3` (`AUTO`) |
| GoogleTest | `v1.14.0` | `SPECTRA_BUILD_TESTS=ON` | `SPECTRA_USE_SYSTEM_GTEST` (`OFF`) |
| Google Benchmark | `v1.8.3` | `SPECTRA_BUILD_BENCHMARKS=ON` | `SPECTRA_USE_SYSTEM_BENCHMARK` (`OFF`) |
| Vulkan-Headers | `v1.3.275` | Emscripten builds only | none |

`SPECTRA_USE_SYSTEM_<DEP>` takes `AUTO` (try `find_package`, otherwise fetch), `ON` (require
the system package) or `OFF` (always fetch). With `SPECTRA_PYTHON_WHEEL=ON` the `AUTO`
defaults become `OFF`, so wheels stay self-contained.

**Self-contained release packages** (`.deb`/`.rpm`/AppImage/wheels) should configure with
`-DSPECTRA_USE_SYSTEM_GLFW=OFF`. The bundled GLFW is then installed as
`lib/spectra/libglfw3.a` and exported as `spectra::glfw`, so consumers of the package do not
need `libglfw3-dev`. With a system GLFW, `spectraConfig.cmake` finds `glfw3` again instead.

**Distro or fully offline build** (no network at configure time):

```bash
sudo apt install libglfw3-dev libgtest-dev libflatbuffers-dev flatbuffers-compiler
git clone --depth 1 --branch v1.92.6-docking https://github.com/ocornut/imgui.git /src/imgui
cmake -B build -G Ninja -DSPECTRA_USE_ROS2=OFF \
  -DSPECTRA_USE_SYSTEM_GLFW=ON -DSPECTRA_USE_SYSTEM_FLATBUFFERS=ON -DSPECTRA_USE_SYSTEM_GTEST=ON \
  -DFETCHCONTENT_SOURCE_DIR_IMGUI=/src/imgui -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

You can point any fetched dependency at a local checkout of its pinned tag with
`-DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>`, where `<NAME>` is `GLFW`, `IMGUI`,
`FLATBUFFERS`, `SDL3`, `GOOGLETEST` or `GOOGLEBENCHMARK`. A system FlatBuffers supplies both
the headers and `flatc`, so the generated code always matches the runtime headers.

**FlatBuffers Python bindings** in `python/spectra/_fb_generated/` are committed and are the
source of truth for the Python package. A normal build only generates C++ headers, in
`build/generated/flatbuffers/`. After changing `src/ipc/schemas/*.fbs`, run
`cmake --build build --target spectra_flatbuffers_python` and commit the result.

### Consuming Spectra from CMake

```cmake
# (a) Installed package: -Dspectra_DIR=<prefix>/lib/cmake/spectra (or CMAKE_PREFIX_PATH=<prefix>)
# (b) Build tree, without installing: -Dspectra_DIR=<spectra>/build
find_package(spectra 0.3 CONFIG REQUIRED)   # SameMinorVersion while the major version is 0
target_link_libraries(app PRIVATE spectra::spectra)        # C++ API
target_link_libraries(capp PRIVATE spectra::spectra_embed) # C FFI (SPECTRA_BUILD_EMBED_SHARED=ON)

# (c) Subproject: builds the library and apps only (no tests, examples, ROS2 or PX4)
add_subdirectory(third_party/spectra)        # or FetchContent_MakeAvailable(spectra)
target_link_libraries(app PRIVATE spectra::spectra)
```

The package looks only for the dependencies the build actually used: `Vulkan` and `Threads`,
plus `Eigen3` if built with `SPECTRA_USE_EIGEN`, and `glfw3`/`SDL3` if system copies were used.

### Packaging manifests and versioning

`version.txt` is the single source of the version. `tools/update_packaging.sh` (or
`cmake --build build --target spectra_update_packaging`) generates
`packaging/homebrew/spectra.rb`, `packaging/scoop/spectra.json` and `packaging/aur/PKGBUILD`
from the `*.in` templates next to them, and rewrites `PROJECT_NUMBER` in `docs/Doxyfile`.

Hashes:

- `--fetch` downloads the published `v<version>` source tarball and
  `spectra-<version>-win64.zip` and records their sha256.
- `--source-sha256` and `--win64-sha256` pass the hashes explicitly.
- Otherwise, hashes already recorded for the same version are kept, and a new version gets
  `SPECTRA_HASH_PENDING` until the release job fills it in.

The ctest `packaging_manifests_match_version` (label `packaging`) fails when a manifest is
behind `version.txt`.

### Licensing

`THIRD_PARTY_NOTICES.md` lists the third-party components, their versions and licenses, and
`licenses/` holds the full license texts. Both are installed with `LICENSE` to
`share/doc/spectra/`, so they ship in every CPack package. When you bump a dependency,
update all three.

---

## 12. macOS Build

```bash
brew install vulkan-headers vulkan-loader glslang molten-vk

python3 -m venv .venv && source .venv/bin/activate
pip install Pillow

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$(which python3)" \
    -DSPECTRA_BUILD_GOLDEN_TESTS=OFF  # Golden tests require lavapipe (Linux only)

cmake --build build -j$(sysctl -n hw.ncpu)
ctest --test-dir build -LE gpu --output-on-failure -j$(sysctl -n hw.ncpu)
```

---

## 13. Windows Build

```powershell
# 1. Install Vulkan SDK from https://vulkan.lunarg.com/sdk/home
#    (or use the CI version: 1.4.304.1)
$ver = "1.4.304.1"
$url = "https://sdk.lunarg.com/sdk/download/$ver/windows/VulkanSDK-$ver-Installer.exe"
Invoke-WebRequest -Uri $url -OutFile VulkanSDK.exe
Start-Process -FilePath .\VulkanSDK.exe -Args "--accept-licenses --default-answer --confirm-command install" -Wait
$env:VULKAN_SDK = "C:\VulkanSDK\$ver"
$env:PATH += ";$env:VULKAN_SDK\Bin"

# 2. Install Python + Pillow
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install Pillow

# 3. Configure and build (Visual Studio or Ninja)
$pythonExe = (Resolve-Path ".venv\Scripts\python.exe").Path
cmake -B build `
    -DCMAKE_BUILD_TYPE=Release `
    -DPython3_EXECUTABLE="$pythonExe" `
    -DSPECTRA_BUILD_TESTS=ON `
    -DSPECTRA_BUILD_EXAMPLES=ON `
    -DSPECTRA_BUILD_GOLDEN_TESTS=OFF

cmake --build build --config Release -j $env:NUMBER_OF_PROCESSORS
ctest --test-dir build -LE gpu -C Release --output-on-failure -j $env:NUMBER_OF_PROCESSORS
```

---

## 14. Docker — Lavapipe (Software Vulkan for CI)

For environments without a physical GPU (CI runners, Docker containers), use Mesa's lavapipe
software Vulkan renderer:

```bash
sudo apt-get install -y mesa-vulkan-drivers xvfb

# Discover lavapipe ICD
LVP_ICD=$(find /usr/share/vulkan/icd.d /etc/vulkan/icd.d 2>/dev/null \
    -name 'lvp_icd*.json' -o -name '*lavapipe*.json' 2>/dev/null | head -1)

# Fall back to searching by content
if [ -z "$LVP_ICD" ]; then
    LVP_ICD=$(grep -rl 'libvulkan_lvp' /usr/share/vulkan/icd.d 2>/dev/null | head -1)
fi

export VK_ICD_FILENAMES="$LVP_ICD"
export VK_DRIVER_FILES="$LVP_ICD"
export LIBGL_ALWAYS_SOFTWARE=1

# Run GPU/golden tests inside a virtual framebuffer
xvfb-run -a ctest --test-dir build -L golden -j1 --output-on-failure
```

---

## 15. Build Verification Checklist

After a successful build, verify the following outputs exist:

```bash
# Core libraries
ls build/libspectra.a          # Full GPU library
ls build/libspectra-core.a     # Headless core library
ls build/libspectra_px4_adapter.a  # PX4 adapter

# Executables
ls build/spectra               # Main app (when GLFW/SDL3 enabled)
ls build/spectra-backend       # Multi-process daemon
ls build/spectra-window        # Multi-process window agent
ls build/spectra-px4           # PX4 standalone app

# Verify unit tests run (no GPU needed)
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)
```

Expected: **0 tests failed** on `ctest -LE gpu`.

---

## 16. Common Issues and Fixes

| Symptom | Fix |
|---------|-----|
| `Could not find Vulkan` | Install `libvulkan-dev` |
| `glslangValidator: command not found` | Install `glslang-tools` |
| `Python3 not found` | Install `python3`; set `-DPython3_EXECUTABLE=...` |
| `Pillow not found` (status message) | Optional: install `python3-pillow` for resized hicolor icons; without it the source icon is installed to `share/pixmaps` |
| FetchContent download fails / offline | Use system packages or `FETCHCONTENT_SOURCE_DIR_<NAME>` (§11) |
| `packaging_manifests_match_version` fails | Run `tools/update_packaging.sh` after bumping `version.txt` and commit |
| `ament_cmake not found` | Normal — ROS2 auto-disables. Set `-DSPECTRA_USE_ROS2=OFF` to silence |
| Golden tests fail — no Vulkan device | Use lavapipe (see Section 14) |
| `GLFW_BUILD_TESTS` conflict | GLFW builds from source; conflict is handled by CMakeLists.txt |
| Clang build: `-Werror` fails | Ensure Clang 17+; use `-DCMAKE_CXX_COMPILER=clang++-17` |

---

## 17. Agent-Specific Notes

### spectra-builder
Use the **Standard Build** (Section 2) for all compile validation tasks. Always pass
`-DSPECTRA_BUILD_TESTS=ON` and check for zero errors. The build takes ~5–8 minutes on a
4-core machine (first run downloads FetchContent deps; subsequent runs are ~1–2 minutes).

### spectra-coder / spectra-dev
After editing source files, rebuild with:
```bash
cmake --build build -j$(nproc)
```
CMake's incremental build only recompiles changed files. No need to reconfigure unless
CMakeLists.txt or feature flags change.

### QA_Regression / QA_Design
Golden image tests require lavapipe and xvfb (Section 14). Set:
```bash
export SPECTRA_GOLDEN_BASELINE_DIR=$PWD/tests/golden/baseline
export SPECTRA_GOLDEN_OUTPUT_DIR=$PWD/tests/golden/output
```
To update baselines: `export SPECTRA_UPDATE_BASELINES=1` before running golden tests.

### QA_Performance
Use Release builds only. Enable benchmarks with:
```bash
cmake -B build -DSPECTRA_BUILD_BENCHMARKS=ON
cmake --build build -j$(nproc)
./build/tests/bench_*
```

### QA_Memory
Build with AddressSanitizer (Section 8). Run non-GPU tests only (`-LE gpu`).
For Valgrind Massif: `valgrind --tool=massif ./build/tests/unit_test_<name>`.

### github-ci
The CI workflow (`.github/workflows/ci.yml`) mirrors this document exactly. The `build-linux`
job uses GCC 13 and Clang 17 on Ubuntu 24.04. The `build-ros2` job uses `ros:humble-ros-base`.
The `golden-tests` job uses lavapipe + xvfb.

---

*Last verified: 2026-06-01 on Ubuntu 24.04, GCC 13.3, CMake 3.31, Ninja 1.13*
*Build result: 683/683 targets built, 0 errors*

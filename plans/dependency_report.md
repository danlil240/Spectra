# Spectra Dependency Analysis Report

## Summary

The **default build** (GLFW + ImGui + Vulkan, multiproc mode) requires the user to install only **2 system packages** beyond a C++20 toolchain: `libvulkan-dev` and `libglfw3-dev`. Everything else is either bundled or fetched at build time.

---

## 1. Mandatory Dependencies (always required)

| Dependency | Type | What User Installs | Notes |
|---|---|---|---|
| **Vulkan SDK / loader** | System `find_package` | `libvulkan-dev` (or Vulkan SDK) | `libvulkan1` ships with NVIDIA drivers. Dev headers are in `libvulkan-dev` or LunarG SDK. Users with NVIDIA already have the runtime ICD. |
| **C++20 compiler** | Toolchain | GCC 12+ / Clang 15+ | Standard package managers |
| **CMake 3.20+** | Toolchain | `cmake` | Standard |

## 2. Default-ON External Dependencies

| Dependency | Type | User Installs | Removable? | Impact |
|---|---|---|---|---|
| **GLFW 3.4** | System `find_package` → fallback `FetchContent` | `libglfw3-dev` or **auto-fetched** | No (core windowing) | If system GLFW is found, uses it. Otherwise downloads + builds from source (~1.5 MB). Transitively pulls X11/Wayland libs at runtime. |
| **Dear ImGui 1.91.8-docking** | `FetchContent` (always fetched) | **Nothing** — auto-downloaded | Could be made optional (see §5) | 8 MB source, 122 MB git history (full clone needed for correct docking tag). UI-only — core plotting works without it. |

## 3. Bundled Third-Party (zero user action needed)

| Library | Files | Size | Purpose | Removable? |
|---|---|---|---|---|
| **stb_image / stb_image_write / stb_truetype** | `third_party/stb/` (3 header-only files) | 552 KB | PNG export, image loading, font rasterization | No — core functionality (headless export, text rendering) |
| **Vulkan Memory Allocator (VMA)** | `third_party/vma/vk_mem_alloc.h` | 752 KB | GPU memory management | No — essential for Vulkan backend |
| **tinyfiledialogs** | `third_party/tinyfiledialogs.{cpp,h}` | 248 KB | Native file open/save dialogs | **Yes** — used only in 2 files for Save/Open dialogs |
| **Inter-Regular.ttf** | `third_party/Inter-Regular.ttf` | 404 KB | Default UI font | No (embedded at build time) |
| **SpectraIcons.ttf/.otf** | `third_party/SpectraIcons.*` | 24 KB | Custom icon font | No |
| **Font/icon header data** | `third_party/*.hpp` | ~few KB | Pre-generated font data | No |

## 4. Optional Dependencies (OFF by default, zero impact when disabled)

| Dependency | Flag | User Must Install | Current Default |
|---|---|---|---|
| **Qt6** | `SPECTRA_USE_QT=OFF` | `qt6-base-dev` | OFF |
| **Eigen3** | `SPECTRA_USE_EIGEN=OFF` | `libeigen3-dev` | OFF |
| **FFmpeg** | `SPECTRA_USE_FFMPEG=OFF` | `ffmpeg` (runtime, via `popen`) | OFF |
| **ROS2 Humble+** | `SPECTRA_USE_ROS2=OFF` | Full ROS2 workspace + ~12 packages (rclcpp, msgs, tinyxml2, nlohmann-json, rosbag2, etc.) | OFF |
| **Google Test** | `SPECTRA_BUILD_TESTS=ON` | **Nothing** — auto-fetched | ON (test-only) |
| **Google Benchmark** | `SPECTRA_BUILD_BENCHMARKS=OFF` | **Nothing** — auto-fetched | OFF |

## 5. Cutoff Candidates — Ranked by Ease

### 5a. Easy Cuts (low risk, minimal effort)

| Candidate | Effort | Savings | How |
|---|---|---|---|
| **tinyfiledialogs** | Low | 248 KB, remove 1 C source from build | Only used for file open/save dialogs in `figure_serializer.cpp` (2 calls) and `imgui_integration.cpp` (2 calls). Replace with: (a) ImGui's own file dialog, (b) a simple path text input, or (c) `#ifdef SPECTRA_USE_TINYFD` guard. The library itself has zero external deps, though — it's just extra code weight. |
| **Google Test (tests)** | Low | 7.6 MB fetched source | Already gated behind `SPECTRA_BUILD_TESTS=ON`. Set to OFF for end-user builds. No change needed — just document `cmake -DSPECTRA_BUILD_TESTS=OFF`. |

### 5b. Medium Cuts (more impactful, need design decisions)

| Candidate | Effort | Savings | How |
|---|---|---|---|
| **Dear ImGui** | Medium | 8 MB source + 122 MB git history (full clone) | Already gated behind `SPECTRA_USE_IMGUI=ON`. The core plotting, headless export, and programmatic API work without it. **Problem:** the full git clone (not shallow) is needed for the docking branch tag. Options: (1) pin to a commit hash and use `GIT_SHALLOW TRUE`, (2) vendor a tarball/submodule of just the needed files (~600 KB), (3) switch to a GitHub release archive URL. This would cut first-build time dramatically. |
| **GLFW** | Medium-Low | N/A (already optional fallback) | System GLFW is tried first. FetchContent fallback only fires if missing. For headless-only / embedded scenarios, `SPECTRA_USE_GLFW=OFF` already works. No further reduction possible without removing windowing. |

### 5c. Hard Cuts (architectural changes needed)

| Candidate | Effort | Savings | Why Hard |
|---|---|---|---|
| **stb** | High | 552 KB | `stb_image_write` is used for PNG export (core feature), `stb_truetype` for text rendering. Would need alternative implementations. Not worth it — stb is header-only, zero deps, and industry standard. |
| **VMA** | Very High | 752 KB | Deeply integrated into Vulkan backend memory management. Removing means reimplementing GPU allocation. Not advisable. |

## 6. Runtime Dependencies (what the user's system needs at runtime)

### spectra-backend (daemon, no window)
```
libc, libstdc++, libm, libgcc_s   (standard C++ runtime — always present)
```
Zero external runtime deps. Fully self-contained.

### spectra-window (GUI window)
```
libvulkan.so.1    — Vulkan loader (from NVIDIA driver or mesa-vulkan-drivers)
libglfw.so.3      — GLFW (or statically linked if built from FetchContent)
libX11.so.6       — X11 (transitive via GLFW, always present on Linux desktops)
libxcb, libXau, libXdmcp, libbsd, libmd   — X11 transitive deps (always present)
```

### spectra-ros (ROS2 mode, optional)
```
Everything above PLUS ~30 ROS2 shared libraries (rclcpp, rmw, rosidl, message types, rosbag2, etc.)
141 MB binary size (debug build)
```

## 7. User-Facing Install Requirements Summary

### Minimum (headless/embedded, no UI)
```bash
sudo apt install libvulkan-dev cmake g++
```
Build: `cmake -B build -DSPECTRA_USE_GLFW=OFF -DSPECTRA_USE_IMGUI=OFF -DSPECTRA_BUILD_TESTS=OFF`

### Default (full GUI)
```bash
sudo apt install libvulkan-dev libglfw3-dev cmake g++
# ImGui and Google Test are auto-fetched, no install needed
```
Build: `cmake -B build -G Ninja && cmake --build build`

### With ROS2
```bash
# Source ROS2 Humble workspace first, then:
sudo apt install nlohmann-json3-dev
```
Build: `cmake -B build -DSPECTRA_USE_ROS2=ON -DSPECTRA_ROS2_BAG=ON`

## 8. Recommendations

1. **ImGui git clone bloat** — The biggest pain point. The full (non-shallow) clone is 122 MB git history for 8 MB of source. Consider vendoring a source tarball or switching to `URL` mode with a GitHub release zip (~2 MB download).

2. **tinyfiledialogs** — Easy to guard behind a feature flag or replace with ImGui-native file dialog. Low priority since it has zero external deps itself.

3. **Document minimal install** — The current `README.md` / `getting_started.md` should clearly state only 2 system packages are needed: `libvulkan-dev` + `libglfw3-dev`.

4. **GLFW static linking for wheels** — Already handled (FetchContent fallback for wheels). Good.

5. **Default `SPECTRA_BUILD_TESTS=OFF` for end users** — Consider making this OFF by default and ON only in CI/dev presets, to avoid downloading Google Test for end users who just want to build the library.

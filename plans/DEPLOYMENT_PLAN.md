# Spectra — Deployment & Packaging Plan

> Goal: Make `spectra` installable via `pip`, `apt`, `brew`, `pacman`, or a single binary download on Linux/macOS/Windows.

---

## Phase 0: Foundation (Do First)

### 0.1 Single Version Source-of-Truth
- `version.txt` at project root: `0.1.0`
- CMake reads it: `file(READ version.txt SPECTRA_VERSION)` → `project(spectra VERSION ${SPECTRA_VERSION})`
- Python reads it: `pyproject.toml` uses `dynamic = ["version"]` + `setuptools.dynamic.version.file`
- CI reads it: `cat version.txt`
- Generates `spectra/version.hpp` at configure time with `SPECTRA_VERSION_MAJOR/MINOR/PATCH` + string

### 0.2 CMake Install Targets
```cmake
include(GNUInstallDirs)

# Library
install(TARGETS spectra EXPORT spectraTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Headers
install(DIRECTORY include/spectra/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spectra)

# Binaries (multiproc mode)
install(TARGETS spectra-backend spectra-window
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Assets
install(FILES third_party/Inter-Regular.ttf third_party/SpectraIcons.ttf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/spectra/fonts
)
install(FILES icons/spectra_icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME spectra.png
)
install(FILES icons/spectra.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
```

### 0.3 CMake Package Config (find_package support)
```cmake
install(EXPORT spectraTargets
    FILE spectraTargets.cmake
    NAMESPACE spectra::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/spectraConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/spectraConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spectra
)
```

Downstream usage:
```cmake
find_package(spectra 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE spectra::spectra)
```

### 0.4 Developer Makefile
Replace `Makefile.format` with a full `Makefile`:
```makefile
.PHONY: build test install package clean format

BUILD_TYPE ?= Release
BUILD_DIR  ?= build

configure:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSPECTRA_BUILD_TESTS=ON -DSPECTRA_BUILD_EXAMPLES=ON

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure -j$$(nproc)

install: build
	cmake --install $(BUILD_DIR) --prefix /usr/local

package: build
	cd $(BUILD_DIR) && cpack

clean:
	rm -rf $(BUILD_DIR)

format:
	./format_project.sh

# Python
pip-dev:
	cd python && pip install -e ".[dev]"

pip-test:
	cd python && pytest tests/ -v

# All
all: build test
```

---

## Phase 1: C++ Packaging (CPack)

### 1.1 CPack Configuration
Add to root `CMakeLists.txt`:
```cmake
set(CPACK_PACKAGE_NAME "spectra")
set(CPACK_PACKAGE_VENDOR "spectra-plot")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GPU-accelerated scientific plotting")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "danlil240")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README  ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

# Debian
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libvulkan1 (>= 1.2), libglfw3 (>= 3.3)")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")

# RPM
set(CPACK_RPM_PACKAGE_REQUIRES "vulkan-loader >= 1.2, glfw >= 3.3")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Science")

include(CPack)
```

This gives:
- `cpack -G DEB` → `spectra_0.1.0_amd64.deb`
- `cpack -G RPM` → `spectra-0.1.0-1.x86_64.rpm`
- `cpack -G TGZ` → `spectra-0.1.0-Linux.tar.gz`
- `cpack -G NSIS` → `spectra-0.1.0-win64.exe` (Windows)
- `cpack -G DragNDrop` → `spectra-0.1.0.dmg` (macOS)

### 1.2 AppImage (Portable Linux)
Create `packaging/AppImage/AppImageBuilder.yml`:
- Bundle `spectra-backend`, `spectra-window`, fonts, icons
- Use `linuxdeploy` to auto-bundle shared libs (libvulkan, libglfw)
- Single file: `Spectra-0.1.0-x86_64.AppImage`

### 1.3 Shared Library Option
```cmake
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
```
When ON, produce `libspectra.so` / `libspectra.dylib` / `spectra.dll` with proper SOVERSION.

---

## Phase 2: Python Package with Bundled Backend

### 2.1 Architecture
The Python package (`spectra-plot`) must ship the compiled `spectra-backend` binary so `pip install spectra-plot` is self-contained.

Strategy: **platform wheels** using `scikit-build-core` (modern CMake-aware Python build backend).

### 2.2 pyproject.toml Rewrite
```toml
[build-system]
requires = ["scikit-build-core>=0.8", "setuptools>=68"]
build-backend = "scikit_build_core.build"

[project]
name = "spectra-plot"
dynamic = ["version"]
description = "GPU-accelerated scientific plotting"
readme = "README.md"
license = {text = "MIT"}
requires-python = ">=3.9"
classifiers = [...]

[project.scripts]
spectra-backend = "spectra._backend:main"

[tool.scikit-build]
cmake.args = [
    "-DSPECTRA_BUILD_EXAMPLES=OFF",
    "-DSPECTRA_BUILD_TESTS=OFF",
    "-DSPECTRA_RUNTIME_MODE=multiproc",
]
wheel.packages = ["python/spectra"]
cmake.build-type = "Release"

[tool.scikit-build.cmake.define]
SPECTRA_PYTHON_WHEEL = "ON"
```

### 2.3 Backend Binary Bundling
When `SPECTRA_PYTHON_WHEEL=ON`, install `spectra-backend` into the Python package:
```cmake
if(SPECTRA_PYTHON_WHEEL)
    install(TARGETS spectra-backend
        RUNTIME DESTINATION spectra/_bin
    )
endif()
```

Python launcher updated to find `_bin/spectra-backend` inside the package:
```python
# _launcher.py addition
pkg_dir = os.path.dirname(os.path.abspath(__file__))
bundled = os.path.join(pkg_dir, "_bin", "spectra-backend")
if os.path.isfile(bundled):
    return bundled
```

### 2.4 Platform Wheels via cibuildwheel
- `cibuildwheel` builds wheels for: `manylinux_2_28_x86_64`, `macosx_12_0_x86_64`, `macosx_12_0_arm64`, `win_amd64`
- Vulkan SDK installed in CI for each platform
- Result: `pip install spectra-plot` works on all platforms

### 2.5 Entry Point
```toml
[project.scripts]
spectra = "spectra._cli:main"
```
Enables `spectra --help` from terminal (launch backend, open examples, etc.)

---

## Phase 3: Cross-Platform CI/CD

### 3.1 Expand ci.yml Matrix
```yaml
jobs:
  build-and-test:
    strategy:
      matrix:
        include:
          # Linux
          - os: ubuntu-24.04
            compiler: gcc
          - os: ubuntu-24.04
            compiler: clang
          # macOS (Intel + ARM)
          - os: macos-13        # Intel
            compiler: clang
          - os: macos-14        # ARM (M1)
            compiler: clang
          # Windows
          - os: windows-2022
            compiler: msvc
```

### 3.2 New CI Jobs

**`package-linux`** — Produces .deb, .rpm, .tar.gz, AppImage
**`package-macos`** — Produces .dmg
**`package-windows`** — Produces .msi/.exe installer
**`python-wheels`** — Produces platform wheels via cibuildwheel
**`python-sdist`** — Produces source distribution

### 3.3 Release Workflow (`.github/workflows/release.yml`)
Triggered on `v*` tag push:
1. Build + test on all 3 platforms
2. Package (deb, rpm, AppImage, dmg, msi)
3. Build Python wheels
4. Create GitHub Release with all artifacts
5. Publish to PyPI (`twine upload`)

```yaml
on:
  push:
    tags: ['v*']

jobs:
  release:
    ...
    steps:
      - uses: softprops/action-gh-release@v1
        with:
          files: |
            dist/*.deb
            dist/*.rpm
            dist/*.AppImage
            dist/*.dmg
            dist/*.exe
            dist/*.whl
```

---

## Phase 4: Platform-Specific Installers

### 4.1 Linux
| Format | Tool | Status |
|--------|------|--------|
| `.deb` | CPack DEB generator | Phase 1 |
| `.rpm` | CPack RPM generator | Phase 1 |
| AppImage | linuxdeploy | Phase 1 |
| Flatpak | `packaging/flatpak/org.spectra.Spectra.yml` | Phase 4 |
| Snap | `packaging/snap/snapcraft.yaml` | Phase 4 |
| AUR | `packaging/aur/PKGBUILD` | Phase 4 |

### 4.2 macOS
| Format | Tool | Status |
|--------|------|--------|
| `.dmg` | CPack DragNDrop | Phase 1 |
| Homebrew | `packaging/homebrew/spectra.rb` | Phase 4 |
| `.pkg` | CPack productbuild | Phase 4 |

Homebrew formula:
```ruby
class Spectra < Formula
  desc "GPU-accelerated scientific plotting library"
  homepage "https://github.com/danlil240/spectra"
  url "https://github.com/danlil240/spectra/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "..."
  license "MIT"

  depends_on "cmake" => :build
  depends_on "vulkan-headers" => :build
  depends_on "glfw"

  def install
    system "cmake", "-B", "build", *std_cmake_args,
           "-DSPECTRA_BUILD_EXAMPLES=OFF", "-DSPECTRA_BUILD_TESTS=OFF"
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end
end
```

### 4.3 Windows
| Format | Tool | Status |
|--------|------|--------|
| NSIS `.exe` | CPack NSIS | Phase 1 |
| WiX `.msi` | CPack WIX | Phase 4 |
| winget | `packaging/winget/spectra.yaml` | Phase 4 |
| Scoop | `packaging/scoop/spectra.json` | Phase 4 |

---

## Phase 5: Polish

### 5.1 Runtime Asset Resolution
Currently font files loaded relative to binary. Need proper search order:
1. `$SPECTRA_DATA_DIR` env var
2. `<install_prefix>/share/spectra/` (system install)
3. `<executable_dir>/../share/spectra/` (relocatable install)
4. `<package_dir>/_data/` (Python wheel)
5. Embedded fallback (compile fonts into binary via `xxd` or CMake `file(READ)`)

### 5.2 Embedded Fonts (Zero External Files)
Compile `Inter-Regular.ttf` and `SpectraIcons.ttf` into the library as C arrays:
```cmake
# cmake/EmbedAssets.cmake
function(embed_binary_file INPUT_FILE VAR_NAME OUTPUT_FILE)
    file(READ ${INPUT_FILE} HEX_CONTENT HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," HEX_CONTENT ${HEX_CONTENT})
    file(WRITE ${OUTPUT_FILE}
        "// Auto-generated\n"
        "static const unsigned char ${VAR_NAME}[] = {${HEX_CONTENT}};\n"
        "static const unsigned int ${VAR_NAME}_len = sizeof(${VAR_NAME});\n"
    )
endfunction()
```
This eliminates the #1 runtime failure mode ("font not found").

### 5.3 Shader Embedding
Already have `cmake/EmbedShaders.cmake` — verify SPIR-V blobs are compiled into the library (not loaded from disk at runtime). This is critical for packaging.

### 5.4 `spectra --version`
Backend and agent binaries print version and exit:
```
$ spectra-backend --version
spectra-backend 0.1.0 (Vulkan 1.2, C++20)
```

### 5.5 Man Pages
```
packaging/man/spectra-backend.1
packaging/man/spectra.1
```
Installed to `${CMAKE_INSTALL_MANDIR}/man1/`.

### 5.6 Shell Completions
```
packaging/completions/spectra.bash
packaging/completions/spectra.zsh
packaging/completions/spectra.fish
```

---

## File Tree (New Files to Create)

```
Spectra/
├── version.txt                              # "0.1.0"
├── Makefile                                 # Developer workflow
├── cmake/
│   ├── spectraConfig.cmake.in               # find_package() support
│   ├── EmbedAssets.cmake                    # Font/asset embedding
│   └── Install.cmake                        # Install rules (included by root CMakeLists)
├── packaging/
│   ├── AppImage/
│   │   └── AppImageBuilder.yml
│   ├── flatpak/
│   │   └── org.spectra.Spectra.yml
│   ├── snap/
│   │   └── snapcraft.yaml
│   ├── aur/
│   │   └── PKGBUILD
│   ├── homebrew/
│   │   └── spectra.rb
│   ├── winget/
│   │   └── spectra.yaml
│   ├── man/
│   │   ├── spectra-backend.1
│   │   └── spectra.1
│   └── completions/
│       ├── spectra.bash
│       ├── spectra.zsh
│       └── spectra.fish
├── .github/workflows/
│   ├── ci.yml                               # Updated: add macOS + Windows
│   ├── release.yml                          # New: tag-triggered release
│   └── python-wheels.yml                    # New: cibuildwheel
└── python/
    └── pyproject.toml                       # Rewritten for scikit-build-core
```

---

## Implementation Priority

| # | Task | Effort | Impact | Dep |
|---|------|--------|--------|-----|
| 1 | `version.txt` + version.hpp generation | 1h | High | — |
| 2 | CMake install targets + GNUInstallDirs | 2h | Critical | 1 |
| 3 | `cmake/spectraConfig.cmake.in` (find_package) | 1h | High | 2 |
| 4 | Developer `Makefile` | 1h | Medium | — |
| 5 | CPack config (deb/rpm/tgz) | 2h | High | 2 |
| 6 | Embedded fonts (eliminate runtime file deps) | 2h | Critical | — |
| 7 | Asset search path resolution | 1h | High | 6 |
| 8 | Cross-platform CI (macOS + Windows) | 4h | Critical | 2 |
| 9 | Python wheel with bundled backend | 4h | Critical | 2 |
| 10 | Release workflow (GitHub Releases + PyPI) | 3h | High | 8,9 |
| 11 | AppImage | 2h | Medium | 2 |
| 12 | Homebrew formula | 1h | Medium | 2 |
| 13 | Man pages + completions | 1h | Low | — |
| 14 | Flatpak/Snap | 3h | Low | 2 |

**Recommended start order:** 1 → 2 → 3 → 4 → 6 → 5 → 8 → 9 → 10 → 11 → 12

---

## Success Criteria

- [ ] `cmake --build build && cmake --install build --prefix /tmp/spectra` produces working install
- [ ] `find_package(spectra)` works from external CMake project
- [ ] `pip install spectra-plot` on fresh Linux/macOS/Windows machine → `import spectra; spectra.plot([1,4,9])` works
- [ ] GitHub Release on tag push produces .deb, .rpm, AppImage, .dmg, .exe, .whl artifacts
- [ ] `make build test package` works as one-liner for developers
- [ ] No runtime file dependencies (fonts/shaders embedded in binary)
- [ ] `spectra-backend --version` prints version

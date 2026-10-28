# Contributing to Spectra

Thanks for your interest in Spectra! Bug reports, docs fixes, examples and code are all welcome.
Spectra is a 0.x public preview, so APIs may still change before 1.0. If you plan a large change,
open an issue first so we can agree on the approach.

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
Security issues should be reported privately as described in [SECURITY.md](SECURITY.md), not in
public issues.

## 1. Get a working build

Spectra targets Linux x86_64 first (Ubuntu 22.04 / 24.04, GCC 13+ or Clang 17+, Vulkan 1.2+).
The full, authoritative list of packages and CMake flags is in
[`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md). Please use it rather than guessing dependencies.

```bash
git clone https://github.com/danlil240/Spectra.git
cd Spectra

# Standard developer configure (ROS2 off unless you work on the ROS2 adapter)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSPECTRA_USE_ROS2=OFF \
      -DSPECTRA_BUILD_EXAMPLES=ON \
      -DSPECTRA_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

Optional components (ROS2, PX4/ULog, Qt, WebGPU, Eigen, FFmpeg) each have a section in
`BUILD_ENVIRONMENT.md`.

## 2. Run the tests

```bash
# Default verification: every test that does not need a GPU
ctest --test-dir build -LE gpu --output-on-failure -j$(nproc)

# One test binary
./build/tests/unit_test_easy_api

# Python package tests
cd python && pip install -e ".[dev]" && pytest tests/ -v
```

### GPU and golden-image tests with lavapipe

Tests labelled `gpu` need a Vulkan device. Without one, use Mesa's software renderer (lavapipe)
under a virtual X server:

```bash
sudo apt-get install -y mesa-vulkan-drivers xvfb
export VK_ICD_FILENAMES=$(find /usr/share/vulkan/icd.d -name '*lvp*' | head -1)
export LIBGL_ALWAYS_SOFTWARE=1

xvfb-run -a ctest --test-dir build -L gpu --output-on-failure -j1      # all GPU tests
xvfb-run -a ctest --test-dir build -L golden --output-on-failure -j1   # golden images only
```

Run GPU tests serially (`-j1`); lavapipe is not reliable with many concurrent devices.

### Updating golden baselines

Golden tests render scenes headlessly and compare them with the images in
`tests/golden/baseline/`. When a change *intentionally* alters rendering:

1. Build with `-DSPECTRA_BUILD_GOLDEN_TESTS=ON` (the default when tests are on).
2. Regenerate the baselines on lavapipe, so they match CI:

   ```bash
   SPECTRA_UPDATE_BASELINES=1 xvfb-run -a ctest --test-dir build -L golden -j1
   ```

   Maintainers can instead run the **Update Golden Baselines** workflow from the GitHub Actions
   tab, which does the same on a clean CI runner and commits the result.
3. Run the golden tests again without `SPECTRA_UPDATE_BASELINES` and check that they pass.
4. Look at the changed images before committing and explain in the PR why they changed.
   Commit baseline updates separately (for example `test: update golden baselines for ...`).

On failure, the actual and diff images are written to `tests/golden/output/` (or
`$SPECTRA_GOLDEN_OUTPUT_DIR`), which is git-ignored.

## 3. Code style

- C++20, formatted with the repository's `.clang-format` (Google base, Allman braces,
  4-space indent, 100 columns) using **clang-format 18**, the version CI uses (other
  versions format differently). Format changed code before committing:

  ```bash
  git clang-format-18 origin/main      # only the lines your branch changed
  clang-format-18 -i path/to/file.cpp  # single file
  make format                          # every git-tracked C/C++ file (format_project.sh)
  make check-format                    # check only; exits non-zero if any file needs formatting
  ```

  `make format` / `make check-format` need clang-format 18 (`CLANG_FORMAT=/path/to/clang-format`
  overrides the binary) and skip `third_party/` and untracked files. See
  [BUILD_ENVIRONMENT.md](BUILD_ENVIRONMENT.md#formatting).

- Naming: `PascalCase` types, `snake_case` functions and variables, trailing `_` for members,
  `UPPER_SNAKE` macros.
- Public API lives in `include/spectra/` and should stay small; implementation goes in `src/`.
  Public headers use zero-copy parameter types (`std::span<const float>`, `std::string_view`).
- No global singletons: managers live on `App` and are passed by pointer. Use RAII and
  `std::unique_ptr` for ownership.
- The render backend does not throw; it returns error codes. GPU resources are destroyed on the
  render thread via deferred cleanup, never directly from the app thread.
- Python code follows PEP 8.

## 4. Tests are required

New behaviour needs Google Test coverage in `tests/unit/` (register the binary in
`tests/CMakeLists.txt`). Tests that need a Vulkan device must get the `gpu` label so they are
skipped by the default `ctest -LE gpu` run and by sanitizer jobs. Bug fixes should come with a
test that fails before the fix.

Interactive programs that need a human to look at a window go in `tests/manual/`; they are built
but not run by CTest.

## 5. Commits and pull requests

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add log-scale support to histogram bins
fix: keep legend inside axes after window resize
docs: document SPECTRA_SOCKET
```

Allowed types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `ci`, `chore`.

Before opening a PR:

1. Rebase on the latest `main`.
2. Build and run `ctest --test-dir build -LE gpu` (plus the GPU/golden tests if you touched
   rendering).
3. Format your changes with `clang-format`.
4. Add an entry under **Unreleased** in [`CHANGELOG.md`](CHANGELOG.md) for user-visible changes.
5. Fill in the pull request template.

Keep PRs focused: one logical change per PR is much easier to review.

## 6. Where things live

| Area | Location |
|---|---|
| Public API | `include/spectra/` |
| Data model (Figure, Axes, Series) | `src/core/` |
| Rendering / Vulkan | `src/render/`, `src/render/vulkan/` |
| Shaders (GLSL, compiled to SPIR-V) | `src/gpu/shaders/` |
| UI (ImGui, commands, workspace) | `src/ui/` |
| IPC, backend daemon, window agent | `src/ipc/`, `src/daemon/`, `src/agent/` |
| ROS2 / PX4 / Qt adapters | `src/adapters/` |
| Python package | `python/spectra/` |
| Tests and golden baselines | `tests/unit/`, `tests/golden/baseline/` |
| Examples | `examples/`, `python/examples/` |
| Docs site | `docs/` |

## License

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE).

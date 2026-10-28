# Changelog

All notable changes to Spectra are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Spectra is in a 0.x public preview: minor releases may contain breaking API changes.

Entries for 0.3.0 and earlier were reconstructed from the git history and GitHub releases and
summarise the main changes rather than listing every commit.

## [Unreleased]

Release-readiness work for the first public preview.

### Added
- Date-time axes (`xaxis_datetime(true)`), named category bars (`bar(names, heights)`) and an
  outside-right legend position (`LegendPosition::OutsideRight`).
- `append(double, double)` and `erase_before/after(double)` for full-precision streaming.
- `save_png`/`save_svg` overloads taking `ExportOptions{width, height, theme}`.
- 3D axes page in the inspector; UI scale setting (75–200%); PX4 Open Recent; File > Exit.
- Text annotations in the multi-process window, Python, workspace files and the C FFI.
- fontconfig font fallback, glyphs above U+FFFF in the UI, monochrome emoji.
- WebGPU ScatterColormap and Heatmap pipelines; Qt and wasm CI jobs.
- `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `CITATION.cff`, this changelog, pull-request and
  issue templates.
- `examples/README.md` and `python/examples/README.md` indexing every example.
- Doxygen `///` documentation for the main public headers (`figure.hpp`, `axes.hpp`,
  `series.hpp`, `app.hpp`, `easy.hpp`).
- `ScaleType::Sqrt` is a real (signed) square-root scale with sqrt-aware ticks.
- SVG `textLength` on Inter text (`SvgStyle::fit_text_length`) and an opt-in embedded Inter
  subset (`SvgStyle::embed_font`); plugin series export to SVG through the optional plugin
  vector-export hook (plugin API 2.2).
- Crowded legends: "+N more" lists hidden entries on hover and opens a full legend on click;
  crowded `Best` legends move outside the plot.
- Design-review QA harness asserts every captured state; plateau-aware memory warnings.

### Changed
- README rewritten for newcomers: screenshots, comparison with other plotting tools, supported
  platforms and project status.
- Runtime-mode documentation now explains that the CMake option `SPECTRA_RUNTIME_MODE` only
  selects which binaries are built, while each app runs in-process unless `SPECTRA_SOCKET`
  (or `AppConfig::socket_path`) is set.
- Interactive UI check programs moved from `examples/` to `tests/manual/`.
- Maintainer-only documentation moved to `docs/dev/`.

### Fixed
- WebGPU/Emscripten build with current emsdk.
- Dear ImGui is fetched via git instead of a release archive.
- Stem series are included in autoscale; histograms ignore non-finite samples.
- IPC preserves non-socket paths.
- Broken links in the documentation site.
- Multi-process window: stats series, subplot placement, mixed 2D/3D series, zoom %, titles.
- Log axes: markers, tooltips, selection, annotations, pan and linked zoom use the log mapping.
- Series colours keep following the palette after save, paste and duplicate.
- 3D: lighting toggle, box size, shared ticks, title row. SVG: 3D shading, ShapeSeries3D,
  measured legend width.
- Many UI shell, contrast and Qt theming fixes (issues #102–#112).
- Shutdown with plugins loaded no longer crashes; retained plugin transforms and mixed-owner
  undo groups no longer outlive a disabled or unloaded plugin.
- Translucent 3D shapes, surfaces and meshes blend in correct back-to-front order.
- Early windowed PNG exports are no longer cropped after the first resize; the multi-process
  window exports PNG/SVG/clipboard requests.
- Settings and colour-picker swatches match the plotted ink colour.
- Timeline playback without an animation callback; detaching a figure to another window; ghost
  legends in split view.
- `make check-format` fails on violations and requires clang-format 18.

### Removed
- Personal and generated files from the repository (vendored `.deb`, CTest output, local
  editor and Docker settings, unused icons).


## [0.3.0] - 2026-07-20

### Added
- Glass-style UI theme with a redesigned shell: registry-driven menu bar, navigation rail with
  search, panel registry, status bar pills and a shared `AppShell` used by the main app, the
  ROS2 app and the PX4 app.
- Interactive reference lines, function plotting and parameter knobs in the Easy API.
- Scientific rendering additions (colormapped scatter, uncertainty bands) and indexed
  interaction.
- Plot annotations overlay; box-select and shortcuts in the animation curve editor.
- Graceful SIGINT/SIGTERM shutdown.
- ROS2 Jazzy support for bags and subscriptions; plot-first `spectra-ros` dock layout and
  reworked ROS2 UI.
- Docker Compose environment and an in-process Docker image.
- UI automation action log and MCP fuzzing tools.

### Fixed
- Data lifetime and bounds hardening for series, Eigen inputs and C API handles.
- GLFW bundled in the install tree and CMake targets exported for consumers.
- Rendering during Win32 window move/resize; swapchain format selection.
- Figure lifecycle and asynchronous clipboard capture.

## [0.2.7] - 2026-06-05

### Added
- 3D axis picking, aligned views and hover tooltips.
- Topic publisher IPC; frame recording for text rendering.
- Embed surface configuration and axes border control.

### Fixed
- macOS/Windows portability (ResourceMonitor, `posix_spawn` environment, `MappedFile`).
- Sanitizer leaks and lavapipe golden-test flakes in CI.

## [0.2.6] - 2026-05-29

### Fixed
- Packaging: generated hicolor icons are installed, GLFW headers no longer land in the `.deb`,
  install export set fixed.
- Python wheel build dependencies and CI Python setup.

## [0.2.5] - 2026-05-25

### Added
- Docker publisher-first workflow for topics.
- `--idle-exit` flag for the backend daemon; `SPECTRA_SOCKET` honoured when auto-spawning.
- Neon default colour cycle for dark themes.

### Fixed
- Snap/GTK environment variables are sanitised before launching `xdg-open`.

## [0.2.4] - 2026-05-24

### Changed
- Performance: faster nearest-point hover scan, no RTTI in 3D series sorting, less CPU spin
  when limiting FPS.

### Fixed
- Topic publisher returns a disconnected publisher instead of `nullptr` when no daemon runs.

## [0.2.3] - 2026-05-19

### Added
- Optional SDL3 windowing backend.
- Copying figures to the clipboard; welcome-screen drop target that creates figures from topics.

## [0.2.2] - 2026-04-13

### Added
- Publish/subscribe topics with continuous auto-zoom for subscribed axes.
- WebGPU/WebAssembly backend with WGSL shaders (experimental, 2D).
- Plugin API expansion: overlays, exporters, data sources, custom series, manifests,
  crash isolation and lifecycle diagnostics.
- Data virtualisation for large datasets (`ChunkedLineSeries`, memory-mapped files, LOD cache).
- IPC protocol v2 based on FlatBuffers; ViewModel layer; thread-safe series.
- Accessibility: keyboard navigation, HTML table export and sonification.
- Middle-click tab closing, user-defined window/tab titles, custom ROS2 plot axes.

### Changed
- Library split into `spectra-core` (headless) and `spectra` (full GPU stack).
- Dear ImGui upgraded to 1.92.6.

## [0.2.1] - 2026-03-22

### Added
- Node graph panel layout and display modes.

### Fixed
- PyPI publishing switched to Trusted Publishing; sdist output path.

## [0.2.0] - 2026-03-21

### Added
- GPG-signed release artifacts and an APT repository for Ubuntu 22.04 / 24.04.
- 2D and 3D shape annotation series with a per-shape Easy API.
- Event-driven rendering and resource monitoring.
- `--version` and `--help` for the main binary.

## [0.1.2] - 2026-03-20

### Added
- PX4 ULog adapter: ULog reader, MAVLink bridge, plot manager and UI panels.
- Detachable panels with OS-level windows; "New Tab" for split panes.
- MCP-compatible automation server embedded in the app.
- Rectangle selection, derivative display and measurement tools; custom transform dialog.

### Fixed
- macOS 14 SDK compatibility (GLFW update).

## [0.1.1] - 2026-03-05

First tagged release.

### Added
- Vulkan 2D/3D plotting core with SDF lines and markers, MSDF text and MATLAB-style format
  strings.
- Easy API, Python package (`spectra-plot`), multi-window and multi-tab UI, animation timeline.
- Headless export to PNG, SVG, GIF and MP4.
- Qt adapter with multi-canvas support; C embedding API and shared library.
- Camera-relative rendering for deep zoom; statistical plots; CSV import.
- Packaging: `.deb`, `.rpm`, AppImage, macOS and Windows installers, man page and shell
  completions.

[Unreleased]: https://github.com/danlil240/Spectra/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/danlil240/Spectra/compare/v0.2.7...v0.3.0
[0.2.7]: https://github.com/danlil240/Spectra/compare/v0.2.6...v0.2.7
[0.2.6]: https://github.com/danlil240/Spectra/compare/v0.2.5...v0.2.6
[0.2.5]: https://github.com/danlil240/Spectra/compare/v0.2.4...v0.2.5
[0.2.4]: https://github.com/danlil240/Spectra/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/danlil240/Spectra/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/danlil240/Spectra/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/danlil240/Spectra/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/danlil240/Spectra/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/danlil240/Spectra/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/danlil240/Spectra/releases/tag/v0.1.1

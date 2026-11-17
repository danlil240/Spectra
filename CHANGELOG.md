# Changelog

All notable changes to Spectra are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Spectra is in a 0.x public preview: minor releases may contain breaking API changes.

Entries for 0.3.0 and earlier were reconstructed from the git history and GitHub releases and
summarise the main changes rather than listing every commit.

## [Unreleased]

### Added

- Plot style presets ("default", "paper", "talk", "minimal", "dark-paper") via `Figure::apply_style`, `spectra::style::use`/`style::context`, Python `spectra.style.use()`/`with spectra.style.context()`, an Inspector Style picker and `style.<name>` commands; per-axes minor grid, minor ticks, tick direction and despine options; and an Export options dialog (`file.export_dialog`: format PNG/SVG/PDF, theme, size in px or inches + DPI, scale, transparent background, live preview) whose choices are saved in settings.
- Matplotlib-style mathtext in plot text (titles, axis labels, legend entries, annotations): `$\alpha_1 = 10^{-3}$ s` renders Greek letters, common symbols, function names and sub/superscripts (70% size, baseline-shifted, stacked as in `x_i^2`) on screen and in SVG/PDF export, and text measurement uses the same layout; `\$` is a literal dollar and strings with an odd number of `$` stay plain.

- 3D camera views: commands and shortcuts for front / back / left / right / top / bottom / isometric (Alt+1..7) and a perspective/orthographic toggle (Alt+0), plus a clickable view cube in the corner of every 3D axes; axis views switch to orthographic so ticks stay evenly spaced, and all camera moves are undoable. `Axes3D::equal_aspect()` ("axis equal", Inspector checkbox, saved in workspaces), and 3D scatter per-point colours (`ScatterSeries3D::color_values()` + `colormap()`, with colorbar) and sizes (`sizes()`).

### Changed
- App polish: one Appearance screen in Settings (Tools > Appearance...) with live theme previews, palette swatch rows, a settings search box and SemiBold headings (Inter SemiBold, typography tokens); user themes and palettes load from `<config>/themes` and `<config>/palettes` and can be imported/exported from Settings (theme import now keeps its data palette); View > Color Vision Preview simulates protanopia/deuteranopia/tritanopia/achromatopsia on screen only; the nav rail groups tool modes on a tray, pins Settings/Help to the bottom and shows shortcut keys in every tooltip; View > Remove All Data Tips is undoable.
- Notebook and web polish: the Jupyter canvas renders at devicePixelRatio (`EmbedSurface.set_dpi_scale()` now lays the plot out in logical pixels, so it is sharp on Retina without shrinking the text), gets Home / Save PNG buttons, a cursor readout (new `EmbedSurface.cursor_data()` / `spectra_embed_get_cursor_data()`), drag-to-resize, a theme that follows the notebook (light by default instead of night) and re-renders when the plot changes from Python; the browser demo shows a poster and download progress while the wasm loads, keeps the poster with a message when WebGPU is missing, supports pinch zoom, shows both plots and is linked from the docs; new windows are scaled down to fit the screen's work area.
- Live, measure and legend interaction: every live axes gets a pill showing "Following" or "Paused · N s behind" (click or End resumes; a plain click no longer pauses a live plot) plus a status-bar Live pill; the Measure tool snaps to the nearest sample (ring indicator, Alt for a free point), measures in data space on log axes (with ×ratio and decades), reports Δx / Δy / slope with each axis' unit and only shows a distance when both axes share a unit; data tips can be dragged along their series, the selected tip steps with Left / Right, Shift+click selects a second tip for a Δx / Δy / slope readout, Esc deselects; hovering a legend entry fades the other series and double-click solos it (again: restore); the status-bar tool pill opens a tool chooser.
- Small UX fixes: the nav rail "Markers" button opens a data-tip list (go to / remove / clear all, undoable) instead of deleting every tip; "Toggle Dark/Light Theme" switches between the last-used dark and light themes (remembered in settings) and the command palette lists every theme and data palette; Python colours accept `C0`-`C9`, `tab:*`, short hex and CSS4 names and raise `ValueError` with a suggestion for unknown names; sequential palettes (viridis, monochrome) spread a few auto-coloured series across the ramp.
- Subplot spacing: `FigureStyle` margins now surround the whole grid and `subplot_hgap` / `subplot_vgap` (Inspector "H Gap" / "V Gap") separate the plots, and the new constrained layout (`FigureStyle::constrained_layout`, on by default, Inspector "Fit Labels", `Figure::tight_layout()`, pyplot `tight_layout()`) grows them from the measured tick labels, axis labels and titles so labels never overlap a neighbouring subplot or get clipped. New `Figure::share_x()` / `share_y()` (`ShareAxes::All|Row|Col`), `Axes::sharex()` / `sharey()` and pyplot / `sp.subplots(sharex=..., sharey=...)` link axis limits (pan/zoom one, all follow) and hide inner tick labels; `Figure::suptitle()` / `fig.suptitle()` / `plt.suptitle()` draws a title above the grid. All of it is drawn in PNG/SVG/PDF export and saved in workspaces and figure files.

- Axis text follows `AxisStyle`: `tick_size` (new), `label_size`, `title_size`, `tick_color` and `label_color` now change the drawn tick labels, axis labels and titles on screen and in PNG/SVG/PDF exports, margins grow to fit larger text, the Inspector edits them (with undo), and the UI scale setting also scales plot text, tick marks and margins. Unset colours (new default, alpha 0) and sizes (`tick_size` 0) follow the theme; the `title_size` default is now 18 px, the size titles were always drawn at.
- File menu: one save concept. Open... (Ctrl+O) opens .spectra workspaces, figures and layouts and data files (CSV/TSV, ULog, MCAP); Open Recent; Save (Ctrl+S) writes the whole window with its data to a .spectra workspace (Save As... is Ctrl+Shift+S); an Export submenu holds PNG (now Ctrl+E), SVG (Ctrl+Shift+E), PDF, the new CSV Data export and Layout. Dropping a .spectra file on the window opens it. Unsaved changes show a dot in the window title and on the tab, are autosaved to the config directory and offered back after a crash, and closing the window, quitting or closing a modified tab (Ctrl+W, the tab close button, middle click) asks Save / Don't Save / Cancel; Ctrl+W / Q / the close button on the last figure return to the start page instead of quitting. Save, export and open results (and failures) show as toasts in the bottom-right corner.
- Crosshair "compare at X" readout: with the crosshair on (C, or Shift+C / View > Compare at X), one compact tooltip lists every visible series of the hovered plot with its colour, name and value at the cursor X (interpolated, or the nearest sample via View > Compare: Nearest Sample; sorted by value or legend order) and marks each series' point; X-linked plots show the same X. Hover tooltips and data tips now also hit anywhere along a line segment, bars, histogram bins (range and count), image/heatmap cells (value), contour lines (level) and chunked series. Tooltips, data tips, crosshair labels and the status bar format date-time axes as dates and times, and the status bar shows X/Y with the digits the view resolves; its zoom pill only appears when zoomed (click to fit).
- Docked **Data** sidebar (View > Panels > Data, nav rail "Data", `panel.toggle_data` / Ctrl+B): open log files (ULog, MCAP), loaded CSV files, live topics and plugin data sources in one searchable tree with match counts and keyboard navigation (Up/Down/Left/Right, Enter to plot); plot a field by double-click, Enter, the "Plot" context-menu item or by dragging it onto a plot. The Log Browser and Topics windows stay available from each source's context menu.
- View navigation: every completed view change (drag pan, a wheel-zoom burst, box zoom, auto-fit / reset / home, arrow-key pan and +/- zoom, Inspector axis ranges) is one entry in a per-figure view history: Back / Forward with Alt+Left / Alt+Right, mouse buttons 4 / 5, View > Previous / Next View, the plot context menu and the command palette, and Ctrl+Z / Ctrl+Y undo / redo the same steps in order with other edits; linked axes move together. Shift+wheel zooms X only, Ctrl+wheel (or Alt) Y only, wheel or drag over the tick labels of one axis zooms / pans only that axis, horizontal wheel / trackpad swipes pan X, and a thin box-zoom band zooms one axis instead of being dropped (all in axis space on log / sqrt axes).
- The start page now offers New figure, Open file (CSV/TSV, ULog, MCAP, layouts), Open layout and Topics with their shortcuts, lists recent files and layouts, and hints that files can be dropped anywhere; empty axes show a "Drop a file here" hint (never in exports); `spectra data.csv flight.ulg` opens files given on the command line like a drop, unknown options print a warning, and dropped `.spectra` layouts open.
- Bars, histograms, box plots and violins now draw flat, nearly opaque fills (alpha 0.88) with a thin 1 px edge in a theme-aware shade of the fill instead of translucent gradients (opt back in with `gradient(true)`); new `fill_alpha()`, `edge_width()` and `edge_color()` setters (also in Python `bar`/`hist` and pyplot `alpha`/`edgecolor`/`linewidth`), box-plot medians in a contrasting ink, histogram bins separated by edges, and autoscale no longer pads below the baseline of bars, histograms, stems and bands filled to a constant (matplotlib-style sticky edges).
- Colormaps are one 256-entry lookup table per map generated from matplotlib, used by scatter, surfaces, imshow/heatmap, contours, the colorbar and SVG/PDF export, so a value gets the same colour everywhere (scatter viridis/coolwarm previously used a rough 3-stop ramp). New maps `Cividis`, `Turbo`, `Twilight`, `RdBu`, `PuOr`, `Hot`; reversed maps (`colormap_reversed(true)` or `"viridis_r"`), user maps (`Colormap::from_colors`), matplotlib names in `colormap("...")` and the Python embed API, and a Colormap picker with a Reversed toggle in the Inspector.
- 2D lines have round joins and round end caps (no V-notch at vertices), translucent lines are blended once where segments meet, and dash patterns stay continuous on lines of any length (the dash phase is computed once per vertex instead of from the previous 512 segments); dashes have round ends and dotted lines draw round dots, matched in SVG/PDF export.
- Several bar series on one axes are drawn side by side (grouped) instead of on top of each other; `Axes::bar_mode(BarMode::Grouped | Stacked | Overlay)` (Inspector "Bars" section, Python `bar(..., stacked=True)` / `Axes.set_bar_mode()`, matplotlib's `bottom=<array>` idiom) stacks them with negatives stacking downwards, or restores the old overlay; it is saved in workspaces and figure files and used by SVG/PDF export. A stack's origin is a sticky autoscale edge like a bar baseline.
- 3D surfaces map their colormap over the data z range (or `SurfaceSeries::colormap_range()` when set), so zooming or panning no longer recolours them, and `colormap_alpha()` now works; the colorbar and SVG/PDF export use the same range. 3D grid panes sit on the faces farthest from the camera and are depth-tested, and the new `Axes3D::box_style(BoxStyle::Back | Full | None)` (default Back) drops the box edges that crossed in front of the data.
- Transforms menu and Custom Formula dialog: a transform applies to the selected series, else to the visible series of the current (last clicked) plot instead of every plot; by default it adds a "<name> · <transform>" copy next to each source (dashed, same colour), with an "Overwrite current series" toggle (saved in settings) that renames the series instead; every apply is one undoable step (Ctrl+Z / Ctrl+Y), the view autoscales when the range changes a lot, and in overwrite mode a unit-changing transform (derivative, FFT, log) suffixes the y label when it covered the whole plot.
- Python in Jupyter: the Easy API and `spectra.pyplot` draw inline (each cell's figures render offscreen to PNG under it, `show()` no longer blocks the kernel, `Figure` has `_repr_png_`/`to_png()`), switchable with `sp.use_window()` / `sp.use_inline()` / `SPECTRA_DISPLAY`; format strings (`sp.plot(y, "r--o")`, `linestyle=`, `marker=`) now reach the window over IPC (new optional `line_style`/`marker_style` snapshot fields and diff ops); NumPy data is vectorised end to end (5M points: ~1 s instead of 7.6 s) and the IPC reply timeout grows with the payload, raising a friendly `spectra.TimeoutError` instead of a raw `ConnectionError`.
- The browser (WebGPU) demo fills its page, follows resizes and renders at the display's full resolution (devicePixelRatio), so it is no longer small and blurry on HiDPI screens. It also uses the theme's data palette like the native app.

### Fixed

- Golden image tests resolve their baselines through an absolute path set by CMake, so `ctest` compares images instead of silently skipping every scene (ccache made `__FILE__` relative and ctest runs from `build/tests`); a missing baseline now fails unless `SPECTRA_GOLDEN_STRICT=0`. 22 stale 3D baselines were regenerated for the Back box style.
- Browser builds embed a DejaVu Sans symbol subset as the text fallback font, so symbols Inter lacks (for example ∇, ∮, ⇌) no longer render as boxes.
- The WebGPU backend prefers a UNORM surface format, matching the Vulkan swapchain, so theme colours are not gamma-shifted on native Dawn.
- The browser (WebGPU) demo no longer shows a blank canvas on browsers whose preferred canvas format is RGBA8Unorm (Chrome on Android, Linux and ChromeOS): pipelines are rebuilt whenever the swapchain format changes. The demo page now shows GPU errors, device loss, a missing adapter and wasm aborts as a red status with the error text, and only says "Running" after the first frame rendered without errors.
- Tool shortcuts work again (P pan, Z zoom, S select, M measure, new N annotate and I ROI) and the nav rail follows them and shows each key in its tooltip; Esc cancels the gesture in progress before closing the palette; middle-button pan and arrow/+/- keys pan log axes by ratios and move linked axes (one undo step); the mouse cursor shows the active tool over the plot.

- WebGPU windows, including the browser demo, now respond to the mouse and keyboard (pan, scroll zoom, box zoom, reset). Only Vulkan windows had input wired before.

## [0.4.0] - 2026-11-06

First public preview: Linux is the supported platform; macOS and Windows are experimental.

### Added

- Shareable layout files: File > Save Layout / Open Layout / Recent Layouts and `spectra --layout FILE` save a dashboard with data-binding URIs (`topic://`, `csv://`, reserved `ulog://`) instead of data, and re-resolve them on open (pending topic subscriptions, CSV lookup, Data Bindings window to pick replacements).
- Log browser: open PX4 ULog (`.ulg`) and MCAP (`.mcap`, JSON and ROS 2 CDR messages, no ROS install needed) files in the main `spectra` app by drag-and-drop or File > Open Log..., search their fields in a tree and drag a field onto a plot (double-precision timestamps). New `LogReader`/`LogReaderRegistry` in `src/io/logs/`; the ULog parser moved there from the PX4 adapter. `SPECTRA_USE_MCAP` (default ON) fetches foxglove/mcap.
- Python: `sp.bar`/`barh`, `sp.hist`, `sp.stem` and new `sp.fill_between` draw the native C++ bar, histogram, stem and band series over IPC; `sp.savefig(path, width=, height=)` / `Figure.savefig` render PNG or SVG offscreen in the backend (new `REQ_EXPORT_FIGURE`); `sp.xscale`/`yscale`, `semilogx`/`semilogy`/`loglog`, date-time x axes switched on automatically for datetime x data; and a `spectra.pyplot` matplotlib-compatibility module.
- Topic bridges: `spectra-backend --udp-bridge [HOST:]PORT` (or `SPECTRA_UDP_BRIDGE`) publishes UDP JSON / CSV datagrams as topics, one topic per numeric field (`udp/imu/accel/x`); plugin data sources are published as topics `<source>/<label>` and can be started and stopped from the Topics panel.
- Python: `sp.errorbar(x, y, yerr=, xerr=, fmt=, capsize=)` with scalar, per-point or (lower, upper) errors.
- Python: `sp.plot(df)` draws one line per numeric DataFrame column against its index; a pandas Series plots against its index with its `name` as the label. Datetime/timedelta data (NumPy, pandas, `datetime`) converts to epoch seconds, and any `__array__` object (xarray, CPU tensors) is accepted.
- Image, heatmap and contour plots for 2D axes: `imshow`, `heatmap`, `contour` and `contourf` (GPU colormap lookup, nearest or bilinear sampling, cell value labels, colorbars and SVG export).
- Interactive Jupyter widget: `EmbedSurface` shows as a live, pannable, zoomable canvas in JupyterLab, Notebook 7, VS Code and Colab (`spectra.notebook`, `pip install "spectra-plot[notebook]"`); Linux wheels now bundle `libspectra_embed`.
- `SPECTRA_CONFIG_DIR` overrides the per-user config directory, and `XDG_CONFIG_HOME` is honoured on Linux/macOS, for settings, keybindings, workspaces, autosave, plugins and the default theme.
- Truncated dense legends scroll with the mouse wheel in interactive windows.
- QA: framebuffer contrast tests for plots (4 themes, 1x/2x, simulated colour-vision deficiency) and text-contrast checks in the design review; `--repeat` soak mode with an RSS/heap slope gate (`qa_soak` ctest).
- Date-time axes (`xaxis_datetime(true)`), named category bars (`bar(names, heights)`) and an
  outside-right legend position (`LegendPosition::OutsideRight`).
- `append(double, double)` and `erase_before/after(double)` for full-precision streaming.
- `save_png`/`save_svg` overloads taking `ExportOptions{width, height, theme}`.
- Vector PDF export (`save_pdf`, `PdfExporter`, File > Export PDF): the SVG drawing as PDF 1.4 with an embedded Inter subset (selectable text), one page per figure.
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
- Dense legends at explicit inside positions are capped to 35% of the plot area (and half its height once summarised), folding the rest into "+N more".
- High Contrast grid is quieter (major alpha 0.26 → 0.20, minor 0.08 → 0.065); Night axis spine and several small UI labels have stronger contrast; theme transitions keep text readable on every frame.
- `default_theme.json` moves from `~/.spectra/` to the config directory; the old location is still read.
- README rewritten for newcomers: screenshots, comparison with other plotting tools, supported
  platforms and project status.
- Runtime-mode documentation now explains that the CMake option `SPECTRA_RUNTIME_MODE` only
  selects which binaries are built, while each app runs in-process unless `SPECTRA_SOCKET`
  (or `AppConfig::socket_path`) is set.
- Interactive UI check programs moved from `examples/` to `tests/manual/`.
- Maintainer-only documentation moved to `docs/dev/`.

### Fixed
- Embed / Python / Qt rendering: unchanged series are no longer re-uploaded every frame (render cost no longer grows with point count when data is static), offscreen pixel readback uses host-cached memory (much faster on NVIDIA/AMD), and the Windows `spectra_embed.dll` now exports its C API so `spectra.embed` can load it.
- Autoscaled axes no longer rescan every series on each `x_limits()`/`y_limits()` call; the data extent is cached until a series changes, is added or removed (large autoscaled plots were spending most of each frame rescanning data).
- Use-after-free when playing the demo animation after closing its figure; an ImGui window leaked per figure or split pane ever shown.
- PX4 viewer ignored dropped `.ulg` files; Parameters sort went stale after re-opening a changed log.
- Dropping several CSV files opened only the last; unparsable CSV failed silently.
- Plugin series created before their plugin loaded were never drawn.
- Python: epoch timestamps lost precision (sent as float32); `Axes.remove_series(i)` removed from the wrong subplot.
- Design review: zoom scenes now target the measured axes, and animated states wait for settled geometry.
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

[Unreleased]: https://github.com/danlil240/Spectra/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/danlil240/Spectra/compare/v0.3.0...v0.4.0
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

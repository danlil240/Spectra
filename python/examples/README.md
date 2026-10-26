# Spectra Python Examples

Scripts that use the `spectra` Python package. C++ examples live in
[`examples/`](../../examples/README.md).

## Setup and run

```bash
pip install spectra-plot          # or, from a source checkout: pip install -e python/
python python/examples/easy_minimal.py
```

The package talks to the `spectra-backend` process over IPC and launches it automatically when it
is not already running. A Vulkan driver and a display are needed for windowed examples;
`easy_embed_demo.py` renders offscreen and works headless.

## Easy API

| Script | What it shows |
|---|---|
| [`easy_minimal.py`](easy_minimal.py) | The simplest possible example: one line to plot. |
| [`easy_one_liner.py`](easy_one_liner.py) | A gallery of one-line plots. |
| [`easy_subplots.py`](easy_subplots.py) | Several subplots in one window. |
| [`easy_multi_tab.py`](easy_multi_tab.py) | Multiple figures as tabs in one window (`sp.tab()`). |
| [`easy_3d.py`](easy_3d.py) | 3D one-liners: helix, scatter cloud, surface. |
| [`easy_showcase.py`](easy_showcase.py) | Every Easy API feature in one script. |
| [`basic_line.py`](basic_line.py) | Basic line plot using the object API. |

## Live data

| Script | What it shows |
|---|---|
| [`easy_live_dashboard.py`](easy_live_dashboard.py) | `sp.live()` callback-driven live dashboard. |
| [`easy_multi_live.py`](easy_multi_live.py) | Several series updating in real time. |
| [`streaming_update.py`](streaming_update.py) | `Series.append()` and `FramePacer` at 60 FPS. |
| [`topic_publisher.py`](topic_publisher.py) | Publish a named topic; drag it onto an axes from the Topics panel. |
| [`topic_subscriber.py`](topic_subscriber.py) | Subscribe to a topic and plot it live. |

## Embedding and Qt

| Script | What it shows | Extra requirement |
|---|---|---|
| [`easy_embed_demo.py`](easy_embed_demo.py) | `spectra.embed`: render to pixels or files without a window. | |
| [`easy_embed_pyqt.py`](easy_embed_pyqt.py) | `spectra.embed` frames shown in a Qt widget. | `PyQt5` |
| [`pyqt_embed.py`](pyqt_embed.py) | GPU plot inside a `QWidget` via the C embedding library. | `PyQt5`, `libspectra_embed` (`SPECTRA_BUILD_EMBED_SHARED=ON`) |
| [`qt_minimal.py`](qt_minimal.py) | Minimal Qt embed, side by side with the matplotlib equivalent. | PyQt5/6 or PySide2/6 |
| [`qt_backend_demo.py`](qt_backend_demo.py) | matplotlib-style `FigureCanvasSpectra` + navigation toolbar. | PyQt5/6 or PySide2/6 |
| [`qt_dynamic_demo.py`](qt_dynamic_demo.py) | Static and timer-driven dynamic canvases, like matplotlib's Qt example. | PyQt5/6 or PySide2/6 |

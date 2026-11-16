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

## Jupyter

Inside a Jupyter kernel (JupyterLab, Notebook, VS Code, Colab) the Easy API and
`spectra.pyplot` draw **inline**: when a cell finishes, every figure it drew into is rendered
offscreen by the backend and shown as a PNG under the cell, like matplotlib's inline backend.
`sp.show()` displays the pending figures and returns at once, and a `Figure` shows as an image
when it is the last expression of a cell. The next plot call after a cell starts a new figure.

```python
import numpy as np
import spectra as sp

x = np.linspace(0, 10, 200)
sp.plot(x, np.sin(x), "r--", label="sin")   # appears under the cell
sp.plot(x, np.cos(x), "b:o", label="cos")
sp.legend()
```

| Call / variable | Effect |
|---|---|
| `sp.use_inline(width=960, height=576, theme="light")` | Inline PNGs (default in Jupyter); size of default-sized figures and export theme. |
| `sp.use_window()` | Native Spectra windows (default outside Jupyter); needed for `sp.live()` and knobs. |
| `SPECTRA_DISPLAY=inline` / `window` | Same choice from the environment, before the first plot. |
| `sp.display_mode()` | `"inline"` or `"window"`. |
| `fig.to_png()` | PNG bytes of a figure, rendered offscreen. |

For an interactive, pannable canvas in the notebook use the
[`notebook_widget.ipynb`](notebook_widget.ipynb) widget (`spectra.notebook`).

## Large arrays

NumPy arrays go to the backend as one contiguous float32 block, so `sp.plot(x, y)` with 5
million points takes well under a second on the Python side. The reply timeout grows with the
amount of data sent (5 s for small requests, plus about 1 s per 10 MB, plus a minute for
exports); set `SPECTRA_IPC_TIMEOUT=<seconds>` to raise the base. A timeout raises
`spectra.TimeoutError` (a `spectra.ConnectionError`) with a hint instead of a bare socket error.

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
| [`notebook_widget.ipynb`](notebook_widget.ipynb) | Live, pannable, zoomable plots in Jupyter (`spectra.notebook`). | `spectra-plot[notebook]`, `libspectra_embed` |

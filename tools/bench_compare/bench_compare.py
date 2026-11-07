#!/usr/bin/env python3
"""Frame-time comparison: Spectra vs matplotlib vs pyqtgraph.

Each library renders the same N-point line plot into an offscreen RGBA buffer
of the same size, so no window manager, vsync or PNG encoding is measured.
Two scenarios:

  static  -- data unchanged, redraw the frame (pan/zoom/resize cost)
  stream  -- replace the Y data, then redraw (live-data cost)

Every number is the median of --frames timed frames after --warmup untimed
frames.  Results go to stdout as a Markdown table and to --csv.

Run it on a machine with a real GPU; numbers from a software Vulkan driver
(lavapipe/llvmpipe) say nothing about Spectra's GPU path.

Setup (Spectra's in-process renderer is not in the PyPI wheel yet):
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \\
          -DSPECTRA_BUILD_EMBED_SHARED=ON -DSPECTRA_USE_ROS2=OFF
    cmake --build build --target spectra_embed
    pip install numpy matplotlib pyqtgraph PySide6
    SPECTRA_EMBED_LIB=build/libspectra_embed.so \\
        python tools/bench_compare/bench_compare.py

    # Faster smoke run:
    python tools/bench_compare/bench_compare.py --sizes 1e4 1e5 --frames 10
"""

from __future__ import annotations

import argparse
import csv
import os
import platform
import statistics
import sys
import time
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "python"))


# ─── Backends ────────────────────────────────────────────────────────────────
# Each backend: setup(x, y, w, h) -> state; draw(state) -> None;
# update(state, y) -> None; close(state) -> None.


class MatplotlibBackend:
    name = "matplotlib (Agg)"

    def __init__(self) -> None:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        self.plt = plt

    def setup(self, x, y, w, h):
        dpi = 100
        fig, ax = self.plt.subplots(figsize=(w / dpi, h / dpi), dpi=dpi)
        (line,) = ax.plot(x, y, linewidth=1.0)
        ax.grid(True)
        fig.canvas.draw()
        return fig, line

    def draw(self, state) -> None:
        fig, _ = state
        fig.canvas.draw()
        fig.canvas.buffer_rgba()

    def update(self, state, y) -> None:
        _, line = state
        line.set_ydata(y)

    def close(self, state) -> None:
        self.plt.close(state[0])


class PyqtgraphBackend:
    name = "pyqtgraph (Qt raster)"

    def __init__(self) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        import pyqtgraph as pg

        self.pg = pg
        self.app = pg.mkQApp()

    def setup(self, x, y, w, h):
        pw = self.pg.PlotWidget()
        pw.resize(w, h)
        pw.showGrid(x=True, y=True)
        curve = pw.plot(x, y, pen=self.pg.mkPen(width=1))
        pw.grab()
        return pw, curve, x

    def draw(self, state) -> None:
        pw, _, _ = state
        self.app.processEvents()
        pw.grab().toImage()

    def update(self, state, y) -> None:
        _, curve, x = state
        curve.setData(x, y)

    def close(self, state) -> None:
        state[0].deleteLater()
        self.app.processEvents()


class SpectraBackend:
    name = "Spectra (Vulkan)"

    def __init__(self) -> None:
        from spectra._embed import EmbedSurface

        self.EmbedSurface = EmbedSurface

    def setup(self, x, y, w, h):
        surface = self.EmbedSurface(w, h)
        ax = surface.figure().subplot(1, 1, 1)
        series = ax.line(x, y)
        series.set_line_width(1.0)  # match the 1 px lines of the other two
        ax.set_grid(True)
        ax.auto_fit()
        surface.render()
        return surface, series

    def draw(self, state) -> None:
        state[0].render()

    def update(self, state, y) -> None:
        state[1].set_y(y)

    def close(self, state) -> None:
        state[0].close()


BACKENDS = {
    "spectra": SpectraBackend,
    "matplotlib": MatplotlibBackend,
    "pyqtgraph": PyqtgraphBackend,
}


# ─── Measurement ─────────────────────────────────────────────────────────────


def make_data(n: int, seed: int = 0):
    rng = np.random.default_rng(seed)
    x = np.arange(n, dtype=np.float32)
    y = np.cumsum(rng.standard_normal(n)).astype(np.float32)
    return x, y


def time_frames(backend, state, frames: int, warmup: int, ys=None) -> float:
    """Return the median frame time in milliseconds."""
    samples = []
    for i in range(warmup + frames):
        t0 = time.perf_counter()
        if ys is not None:
            backend.update(state, ys[i % len(ys)])
        backend.draw(state)
        dt = (time.perf_counter() - t0) * 1000.0
        if i >= warmup:
            samples.append(dt)
    return statistics.median(samples)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--sizes", nargs="+", type=float, default=[1e4, 1e5, 1e6, 1e7])
    ap.add_argument("--libs", nargs="+", default=list(BACKENDS), choices=list(BACKENDS))
    ap.add_argument("--width", type=int, default=1600)
    ap.add_argument("--height", type=int, default=900)
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--max-seconds", type=float, default=60.0,
                    help="skip larger sizes for a library once one run exceeds this")
    ap.add_argument("--csv", default="bench_compare.csv")
    args = ap.parse_args()

    sizes = [int(s) for s in args.sizes]
    rows = []
    for lib in args.libs:
        try:
            backend = BACKENDS[lib]()
        except Exception as exc:  # missing package or driver
            print(f"[skip] {lib}: {exc}", file=sys.stderr)
            continue
        too_slow = False
        for n in sizes:
            if too_slow:
                rows.append((backend.name, n, None, None))
                continue
            x, y = make_data(n)
            ys = [make_data(n, seed=s)[1] for s in (1, 2)]
            t0 = time.perf_counter()
            state = backend.setup(x, y, args.width, args.height)
            static_ms = time_frames(backend, state, args.frames, args.warmup)
            stream_ms = time_frames(backend, state, args.frames, args.warmup, ys)
            backend.close(state)
            elapsed = time.perf_counter() - t0
            rows.append((backend.name, n, static_ms, stream_ms))
            print(f"{backend.name:24s} n={n:>10,d}  static {static_ms:9.2f} ms  "
                  f"stream {stream_ms:9.2f} ms", file=sys.stderr)
            too_slow = elapsed > args.max_seconds

    with open(args.csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["library", "points", "static_ms", "stream_ms"])
        for r in rows:
            w.writerow(r)

    def fmt(ms):
        if ms is None:
            return "skipped"
        return f"{ms:.1f} ms ({1000.0 / ms:.0f} fps)" if ms > 0 else "-"

    print(f"\n{args.width}x{args.height}, median of {args.frames} frames, "
          f"{platform.system()} {platform.machine()}, Python {platform.python_version()}\n")
    print("| Library | Points | Static redraw | Stream update + redraw |")
    print("|---|---:|---:|---:|")
    for name, n, s, u in rows:
        print(f"| {name} | {n:,d} | {fmt(s)} | {fmt(u)} |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

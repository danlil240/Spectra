#!/usr/bin/env python3
"""3D plotting one-liners — helix, scatter cloud, surface.

Uses GPU-accelerated 3D rendering via the Spectra Axes3D backend.

Usage:
    python examples/easy_3d.py
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# ─── Example 1: 3D Helix ─────────────────────────────────────────────────────
t = [i * 0.05 for i in range(200)]
x = [math.cos(ti * 2) for ti in t]
y = [math.sin(ti * 2) for ti in t]
z = list(t)

sp.plot3(x, y, z, color="cyan", label="helix")
sp.title("3D Helix")
sp.grid()

# ─── Example 2: 3D Scatter Cloud (new window) ────────────────────────────────
sp.figure("3D Scatter Cloud")
random.seed(42)
n = 500
sx = [random.gauss(0, 1) for _ in range(n)]
sy = [random.gauss(0, 1) for _ in range(n)]
sz = [random.gauss(0, 1) for _ in range(n)]

sp.scatter3(sx, sy, sz, color="orange", size=3, label="random cloud")
sp.title("3D Scatter")

# ─── Example 3: Surface (new window, requires numpy) ─────────────────────────
try:
    import numpy as np

    sp.figure("Surface Plot")
    X, Y = np.meshgrid(np.linspace(-3, 3, 40), np.linspace(-3, 3, 40))
    Z = np.sin(X) * np.cos(Y)
    sp.surf(X.ravel().tolist(), Y.ravel().tolist(), Z.ravel().tolist(), color="purple")
    sp.title("sin(x) * cos(y)")
except ImportError:
    print("numpy not installed — skipping surface example")

# ─── Block ────────────────────────────────────────────────────────────────────
print("Close all windows to exit.")
sp.show()
print("Done.")

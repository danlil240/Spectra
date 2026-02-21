#!/usr/bin/env python3
"""One-liner plotting examples — the simplest possible API.

Each example is literally ONE LINE of code (after import).

Usage:
    python examples/easy_one_liner.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# ─── Example 1: Plot y-values (x auto-generated) ─────────────────────────────_
sp.plot([1, 4, 9, 16, 25, 36, 49, 64])

# ─── Example 2: Plot x, y with styling ───────────────────────────────────────
x = [i * 0.1 for i in range(100)]
y = [math.sin(xi) for xi in x]
sp.plot(x, y, color="red", label="sin(x)", width=2)

# ─── Example 3: Add another line to the same plot ────────────────────────────
y2 = [math.cos(xi) for xi in x]
sp.plot(x, y2, color="blue", label="cos(x)")

# ─── Example 4: Labels and title ─────────────────────────────────────────────
sp.title("Trigonometric Functions")
sp.xlabel("x (radians)")
sp.ylabel("amplitude")
sp.legend()
sp.grid()

# ─── Block until window closed ───────────────────────────────────────────────
print("Close the window to exit.")
sp.show()
print("Done.")

#!/usr/bin/env python3
"""Subplot examples — multiple plots in one window.

Usage:
    python examples/easy_subplots.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# Generate data
x = [i * 0.05 for i in range(200)]
sin_y = [math.sin(xi) for xi in x]
cos_y = [math.cos(xi) for xi in x]
tan_y = [max(-5, min(5, math.tan(xi))) for xi in x]
exp_y = [math.exp(-xi * 0.3) * math.sin(xi * 3) for xi in x]

# ─── 2x2 subplot grid ────────────────────────────────────────────────────────
sp.subplot(2, 2, 1)
sp.plot(x, sin_y, color="red")
sp.title("sin(x)")
sp.grid()

sp.subplot(2, 2, 2)
sp.plot(x, cos_y, color="blue")
sp.title("cos(x)")
sp.grid()

sp.subplot(2, 2, 3)
sp.plot(x, tan_y, color="green")
sp.title("tan(x) clamped")
sp.grid()

sp.subplot(2, 2, 4)
sp.plot(x, exp_y, color="purple")
sp.title("damped oscillation")
sp.grid()

# ─── Block ────────────────────────────────────────────────────────────────────
print("Close the window to exit.")
sp.show()
print("Done.")

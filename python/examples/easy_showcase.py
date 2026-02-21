#!/usr/bin/env python3
"""Full showcase of the Spectra easy API — every feature in one script.

Demonstrates:
  - One-liner plots (plot, scatter, hist, bar, stem)
  - Styling (color, width, labels)
  - Subplots
  - Multiple windows
  - Live streaming
  - Horizontal/vertical lines

Usage:
    python examples/easy_showcase.py
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

random.seed(42)

# ═══════════════════════════════════════════════════════════════════════════════
# Window 1: Classic plots
# ═══════════════════════════════════════════════════════════════════════════════

x = [i * 0.05 for i in range(200)]

# Top-left: Line plot with multiple series
sp.subplot(2, 2, 1)
sp.plot(x, [math.sin(xi) for xi in x], color="red", label="sin")
sp.plot(x, [math.cos(xi) for xi in x], color="blue", label="cos")
sp.hline(0.0)
sp.title("Line Plot")
sp.legend()
sp.grid()

# Top-right: Scatter plot
sp.subplot(2, 2, 2)
sx = [random.gauss(0, 1) for _ in range(200)]
sy = [random.gauss(0, 1) for _ in range(200)]
sp.scatter(sx, sy, color="orange", size=3, label="gaussian")
sp.title("Scatter Plot")
sp.grid()

# Bottom-left: Histogram
sp.subplot(2, 2, 3)
data = [random.gauss(5, 2) for _ in range(1000)]
sp.hist(data, bins=40, color="green")
sp.title("Histogram")
sp.xlabel("value")
sp.ylabel("count")
sp.grid()

# Bottom-right: Bar chart
sp.subplot(2, 2, 4)
categories = [1, 2, 3, 4, 5, 6]
values = [23, 45, 12, 67, 34, 56]
sp.bar(categories, values, color="purple")
sp.title("Bar Chart")
sp.xlabel("category")
sp.ylabel("value")
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
# Window 2: Live streaming
# ═══════════════════════════════════════════════════════════════════════════════

sp.figure("Live Signals")
sp.title("Real-Time Sensor Data")
sp.xlabel("Time (s)")
sp.ylabel("Amplitude")
sp.grid()

# Pre-create series
sig1 = sp.plot([], [], color="red", label="sensor A")
sig2 = sp.plot([], [], color="cyan", label="sensor B")
sp.legend()

t_buf = []
s1_buf = []
s2_buf = []


def stream(t, dt):
    t_buf.append(t)
    s1_buf.append(math.sin(t * 2) + random.gauss(0, 0.1))
    s2_buf.append(math.cos(t * 1.5) * 0.8 + random.gauss(0, 0.05))

    # Keep last 10 seconds
    while len(t_buf) > 300:
        del t_buf[0], s1_buf[0], s2_buf[0]

    sig1.set_data(t_buf, s1_buf)
    sig2.set_data(t_buf, s2_buf)

    if len(t_buf) > 1:
        sp.xlim(t_buf[0], t_buf[-1])
    sp.ylim(-2.0, 2.0)


sp.live(stream, fps=30)

# ═══════════════════════════════════════════════════════════════════════════════
# Window 3: Math functions
# ═══════════════════════════════════════════════════════════════════════════════

sp.figure("Math Functions")

sp.subplot(2, 1, 1)
x2 = [i * 0.02 for i in range(500)]
sp.plot(x2, [math.exp(-xi) * math.cos(xi * 10) for xi in x2], color="magenta", width=2)
sp.title("Damped Oscillation: e^(-x) * cos(10x)")
sp.grid()

sp.subplot(2, 1, 2)
sp.plot(x2, [xi ** 0.5 * math.sin(xi * 5) for xi in x2], color="orange", width=2)
sp.title("sqrt(x) * sin(5x)")
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
print("3 windows open. Close all to exit.")
sp.show()
print("Done.")

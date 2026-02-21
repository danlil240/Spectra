#!/usr/bin/env python3
"""Multi-tab window demo — multiple figures as tabs in one OS window.

Demonstrates:
  - sp.tab() to create new tabs in the same window
  - Named tabs with sp.tab("name")
  - Each tab has its own axes, subplots, and data
  - Mixing tabs with subplots
  - Advanced API: fig.show(window_id=other_fig.window_id)

Usage:
    python examples/easy_multi_tab.py
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

random.seed(42)

# ═══════════════════════════════════════════════════════════════════════════════
# Tab 1: Sine & Cosine (auto-created with first plot)
# ═══════════════════════════════════════════════════════════════════════════════

x = [i * 0.05 for i in range(200)]

sp.plot(x, [math.sin(xi) for xi in x], color="red", label="sin(x)")
sp.plot(x, [math.cos(xi) for xi in x], color="blue", label="cos(x)")
sp.title("Trigonometry")
sp.xlabel("x")
sp.ylabel("y")
sp.legend()
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
# Tab 2: Scatter plot (new tab in SAME window)
# ═══════════════════════════════════════════════════════════════════════════════

sp.tab("Scatter")

sx = [random.gauss(0, 1) for _ in range(300)]
sy = [0.7 * xi + random.gauss(0, 0.5) for xi in sx]
sp.scatter(sx, sy, color="orange", size=3, label="correlated")
sp.title("Scatter with Correlation")
sp.xlabel("x")
sp.ylabel("y")
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
# Tab 3: Histogram + statistics (new tab, same window)
# ═══════════════════════════════════════════════════════════════════════════════

sp.tab("Distribution")

data = [random.gauss(5, 2) for _ in range(2000)]
sp.hist(data, bins=50, color="green")
sp.title("Normal Distribution (μ=5, σ=2)")
sp.xlabel("value")
sp.ylabel("frequency")
sp.vline(5.0, color="red")  # mean
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
# Tab 4: Subplots inside a tab (new tab, same window)
# ═══════════════════════════════════════════════════════════════════════════════

sp.tab("Multi-Panel")

x2 = [i * 0.02 for i in range(500)]

sp.subplot(2, 1, 1)
sp.plot(x2, [math.exp(-xi) * math.cos(xi * 10) for xi in x2], color="magenta", width=2)
sp.title("Damped Oscillation")
sp.grid()

sp.subplot(2, 1, 2)
sp.plot(x2, [xi ** 0.5 * math.sin(xi * 5) for xi in x2], color="cyan", width=2)
sp.title("sqrt(x) · sin(5x)")
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
# Tab 5: Bar chart (new tab, same window)
# ═══════════════════════════════════════════════════════════════════════════════

sp.tab("Bar Chart")

categories = [1, 2, 3, 4, 5, 6, 7, 8]
values = [23, 45, 12, 67, 34, 56, 78, 41]
sp.bar(categories, values, color="purple")
sp.title("Monthly Sales")
sp.xlabel("Month")
sp.ylabel("Units Sold")
sp.grid()

# ═══════════════════════════════════════════════════════════════════════════════
print("1 window with 5 tabs. Close to exit.")
sp.show()
print("Done.")
